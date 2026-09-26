#include "jfx/jfx_engine.h"
#include "tilly/logger.h"
#include <stdio.h>
#include <string.h>

static void print_usage(void) {
    printf("JoltFX CLI - High-performance effects engine\n\n");
    printf("Usage: joltfx <command> [options]\n\n");
    printf("Commands:\n");
    printf("  version    Show version information\n");
    printf("  help       Show this help message\n");
    printf("  run        Execute an effect pipeline\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "version") == 0) {
        printf("JoltFX version 0.1.0\n");
        return 0;
    }

    if (strcmp(command, "help") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        jfx_engine_config_t config = {
            .max_buffers = 1024,
            .max_textures = 256,
            .max_kernels = 128,
            .backend_name = NULL
        };

        jfx_engine_t *engine = NULL;
        jfx_result_t result = jfx_engine_init(&config, &engine);

        if (result != JFX_SUCCESS) {
            tilly_log_simple(TILLY_LOG_ERROR, "Failed to initialize engine");
            return 1;
        }

        tilly_log_simple(TILLY_LOG_INFO, "Engine initialized, running...");

        // TODO: Load and execute effect pipeline

        jfx_engine_shutdown(engine);
        return 0;
    }

    printf("Unknown command: %s\n", command);
    print_usage();
    return 1;
}
