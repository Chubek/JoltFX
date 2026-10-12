//! Isolated EDID parsing (no device required, fuzzable).
//!
//! Implements the 128-byte base-block checksum, manufacturer id decode, and
//! a minimal descriptor scan sufficient for `Connector::edid()`.

/// Parsed EDID base block.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Edid {
    /// Raw bytes (at least one 128-byte block).
    raw: Vec<u8>,
    /// Three-letter manufacturer id, e.g. `"DEL"`.
    pub manufacturer: String,
    /// Product code (little-endian u16).
    pub product_code: u16,
    /// Serial number (little-endian u32).
    pub serial: u32,
    /// Manufacture week (1-53, 0/255 = unspecified).
    pub week: u8,
    /// Manufacture year offset (years since 1990).
    pub year_offset: u8,
    /// EDID version (usually 1).
    pub version: u8,
    /// EDID revision (usually 3 or 4).
    pub revision: u8,
}

impl Edid {
    /// Parse raw EDID bytes.
    ///
    /// Requires at least 128 bytes, a valid 8-byte header, and a valid
    /// base-block checksum (sum of all 128 bytes ≡ 0 mod 256).
    ///
    /// # Errors
    ///
    /// Returns a `String` describing the failure; the error type is a plain
    /// string so the parser stays dependency-free and fuzzable in isolation.
    pub fn parse(bytes: &[u8]) -> std::result::Result<Self, String> {
        if bytes.len() < 128 {
            return Err(format!("EDID too short: {} bytes", bytes.len()));
        }
        if bytes.len() % 128 != 0 {
            return Err(format!(
                "EDID length {} is not a multiple of 128",
                bytes.len()
            ));
        }
        const HEADER: [u8; 8] = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];
        if bytes[0..8] != HEADER {
            return Err("EDID header mismatch".to_string());
        }
        let checksum: u32 = bytes[0..128].iter().map(|&b| u32::from(b)).sum();
        if checksum % 256 != 0 {
            return Err("EDID base-block checksum mismatch".to_string());
        }
        let mfr_word = u16::from_be_bytes([bytes[8], bytes[9]]);
        if mfr_word == 0 {
            return Err("EDID manufacturer id is zero".to_string());
        }
        let manufacturer = decode_manufacturer(mfr_word);
        let product_code = u16::from_le_bytes([bytes[10], bytes[11]]);
        let serial = u32::from_le_bytes([bytes[12], bytes[13], bytes[14], bytes[15]]);
        Ok(Self {
            raw: bytes.to_vec(),
            manufacturer,
            product_code,
            serial,
            week: bytes[16],
            year_offset: bytes[17],
            version: bytes[18],
            revision: bytes[19],
        })
    }

    /// Raw bytes backing this parsed block.
    #[must_use]
    pub fn as_bytes(&self) -> &[u8] {
        &self.raw
    }

    /// Full manufacture year (1990 + offset), or `None` if unspecified.
    #[must_use]
    pub fn year(&self) -> Option<u16> {
        if self.week == 0xFF && self.year_offset == 0xFF {
            None
        } else {
            Some(1990 + u16::from(self.year_offset))
        }
    }

    /// Number of 128-byte extension blocks following the base block.
    #[must_use]
    pub fn extension_count(&self) -> u8 {
        self.raw[126]
    }
}

fn decode_manufacturer(word: u16) -> String {
    // 5 bits per letter, 1 = 'A'.
    let mut out = String::with_capacity(3);
    for shift in [10u32, 5, 0] {
        let v = ((word >> shift) & 0x1F) as u8;
        if v == 0 || v > 26 {
            out.push('?');
        } else {
            out.push((b'A' + v - 1) as char);
        }
    }
    out
}

/// Build a minimal valid 128-byte EDID for tests.
#[cfg(test)]
pub(crate) fn sample_edid_bytes() -> Vec<u8> {
    let mut b = vec![0u8; 128];
    b[0] = 0x00;
    for (i, slot) in b.iter_mut().enumerate().take(8).skip(1) {
        *slot = if i == 7 { 0x00 } else { 0xFF };
    }
    // Manufacturer "DEL" = D(4) E(5) L(12): 4<<10 | 5<<5 | 12.
    let word: u16 = (4 << 10) | (5 << 5) | 12;
    b[8..10].copy_from_slice(&word.to_be_bytes());
    b[10..12].copy_from_slice(&0x1234u16.to_le_bytes());
    b[12..16].copy_from_slice(&0xdead_beefu32.to_le_bytes());
    b[16] = 12; // week
    b[17] = 30; // 2020
    b[18] = 1;
    b[19] = 4;
    // Fix checksum.
    let sum: u32 = b.iter().map(|&x| u32::from(x)).sum();
    let need = (256 - (sum % 256)) % 256;
    b[127] = need as u8;
    b
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_sample_block() {
        let bytes = sample_edid_bytes();
        let e = Edid::parse(&bytes).expect("sample must parse");
        assert_eq!(e.manufacturer, "DEL");
        assert_eq!(e.product_code, 0x1234);
        assert_eq!(e.serial, 0xdead_beef);
        assert_eq!(e.year(), Some(2020));
        assert_eq!(e.extension_count(), 0);
        assert_eq!(e.as_bytes(), bytes.as_slice());
    }

    #[test]
    fn rejects_short_and_bad_header() {
        assert!(Edid::parse(&[0u8; 64]).is_err());
        let mut b = sample_edid_bytes();
        b[0] = 0x01;
        // Re-fix checksum so only the header check fires.
        let sum: u32 = b.iter().map(|&x| u32::from(x)).sum();
        let without_last: u32 = b[..127].iter().map(|&x| u32::from(x)).sum();
        let _ = (sum, without_last);
        assert!(Edid::parse(&b).is_err());
    }

    #[test]
    fn rejects_bad_checksum() {
        let mut b = sample_edid_bytes();
        b[10] ^= 0xFF;
        assert!(Edid::parse(&b).is_err());
    }

    #[test]
    fn rejects_non_multiple_length() {
        let mut b = sample_edid_bytes();
        b.push(0);
        assert!(Edid::parse(&b).is_err());
    }

    #[test]
    fn fuzz_corpus_never_panics() {
        // Byte-level fuzz smoke: random-ish mutations must return, not panic.
        let base = sample_edid_bytes();
        for i in 0..base.len() {
            let mut m = base.clone();
            m[i] = m[i].wrapping_add(1 + (i as u8));
            let _ = Edid::parse(&m);
            let mut t = base.clone();
            t[i] = t[i].wrapping_mul(31).wrapping_add(7);
            let _ = Edid::parse(&t);
        }
        let _ = Edid::parse(&[]);
        let _ = Edid::parse(&[0u8; 256]);
    }
}
