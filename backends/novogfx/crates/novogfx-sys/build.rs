// Links against the system libdrm. The vendored `third_party/libdrm`
// headers are the source of truth for the constants/structs in `src/`;
// at link time we use the system `-ldrm` (2.4.x ABI, backwards compatible).
fn main() {
    println!("cargo:rustc-link-lib=drm");
    println!("cargo:rerun-if-changed=build.rs");
}
