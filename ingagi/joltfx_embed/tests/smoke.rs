#[path = "../../native_extension_api/Ingagi-Module.rs"]
#[allow(dead_code)]
mod ingagi_module;
use ingagi_module::{Session, Value};
fn main() {
    let mut vm=Session::new(1000).unwrap();
    vm.load("fn main(x: i64) { return x + 1; }").unwrap();
    assert_eq!(vm.call("main", &[Value::int(41)]).unwrap().integer,42);
    assert!(vm.load("fn {").is_err());
    assert_eq!(vm.call("main", &[Value::int(41)]).unwrap().integer,42);
    vm.close().unwrap();
}
