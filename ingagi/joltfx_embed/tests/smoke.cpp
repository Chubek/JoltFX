#include "Ingagi-Module.hpp"
#include <cassert>
int main() {
    ingagi::session original;
    original.load("fn main(a: i64) { return a + 1; }");
    ingagi::session vm(std::move(original));
    assert(vm.call("main",{ingagi::integer(41)}).integer==42);
    try { vm.load("fn {"); assert(false); }
    catch (const ingagi::error& e) { assert(e.status==INGAGI_COMPILE_ERROR); }
    vm.close();
}
