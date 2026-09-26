#include "joltscript/bindings.h"
#include "tilly/logger.h"

void jolt_register_core_bindings(void) {
    tilly_log_simple(TILLY_LOG_INFO, "Registering core JoltScript bindings");
    // TODO: Register buffer, texture, kernel bindings
}
