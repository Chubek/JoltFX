package compiler

import "encoding/binary"

// elfObject writes an ELF64 little-endian AMD64 relocatable containing one
// position-independent text blob. Calls/branches within it are already resolved
// by Keystone. The only global symbol is a jump stub at offset zero, retaining
// the selected function's SysV ABI. GNU-stack explicitly requests a non-executable
// stack when this object is linked.
func elfObject(code []byte) []byte {
	const sectionCount = 6
	names := []byte("\x00.text\x00.symtab\x00.strtab\x00.shstrtab\x00.note.GNU-stack\x00")
	symbols := []byte("\x00ingagi_entry\x00")
	align := func(n int) int { return (n + 7) &^ 7 }
	textOff := 64
	symOff := align(textOff + len(code))
	strOff := symOff + 48 // null symbol + global entry
	nameOff := strOff + len(symbols)
	sectionOff := align(nameOff + len(names))
	out := make([]byte, sectionOff+sectionCount*64)
	le := binary.LittleEndian
	copy(out, []byte{0x7f, 'E', 'L', 'F', 2, 1, 1})
	le.PutUint16(out[16:], 1)  // ET_REL
	le.PutUint16(out[18:], 62) // EM_X86_64
	le.PutUint32(out[20:], 1)
	le.PutUint64(out[40:], uint64(sectionOff))
	le.PutUint16(out[52:], 64)
	le.PutUint16(out[58:], 64)
	le.PutUint16(out[60:], sectionCount)
	le.PutUint16(out[62:], 4)
	copy(out[textOff:], code)
	copy(out[strOff:], symbols)
	copy(out[nameOff:], names)
	sym := out[symOff+24:]
	le.PutUint32(sym, 1)     // name: ingagi_entry
	sym[4] = 0x12            // STB_GLOBAL | STT_FUNC
	le.PutUint16(sym[6:], 1) // .text
	le.PutUint64(sym[16:], uint64(len(code)))
	section := func(index int, name, typ uint32, flags uint64, off, size int, link, info uint32, alignment, entsize uint64) {
		h := out[sectionOff+index*64:]
		le.PutUint32(h, name)
		le.PutUint32(h[4:], typ)
		le.PutUint64(h[8:], flags)
		le.PutUint64(h[24:], uint64(off))
		le.PutUint64(h[32:], uint64(size))
		le.PutUint32(h[40:], link)
		le.PutUint32(h[44:], info)
		le.PutUint64(h[48:], alignment)
		le.PutUint64(h[56:], entsize)
	}
	section(1, 1, 1, 6, textOff, len(code), 0, 0, 16, 0)
	section(2, 7, 2, 0, symOff, 48, 3, 1, 8, 24)
	section(3, 15, 3, 0, strOff, len(symbols), 0, 0, 1, 0)
	section(4, 23, 3, 0, nameOff, len(names), 0, 0, 1, 0)
	section(5, 33, 1, 0, nameOff, 0, 0, 0, 1, 0)
	return out
}
