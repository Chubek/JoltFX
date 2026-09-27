#include "commands.h"

#include "jfx/jfx_engine.h"
#include "tilly/logger.h"

#include <stdio.h>
#include <string.h>

#define JOLTFX_CLI_VERSION "0.2.0"

static int cmd_version(void) {
    printf("joltfx %s\n", JOLTFX_CLI_VERSION);
    return 0;
}

/* Legacy `run`: init the engine and tick once, preserving the old entry point. */
static int cmd_run(void) {
    jfx_engine_config_t config;
    memset(&config, 0, sizeof(config));
    config.max_buffers = 1024;
    config.max_textures = 256;
    config.max_kernels = 128;
    config.backend_name = NULL;

    jfx_engine_t *engine = NULL;
    jfx_result_t result = jfx_engine_init(&config, &engine);
    if (result != JFX_SUCCESS) {
        tilly_log_simple(TILLY_LOG_ERROR, "Failed to initialize engine");
        return 1;
    }
    tilly_log_simple(TILLY_LOG_INFO, "Engine initialized, running...");
    result = jfx_engine_tick(engine);
    jfx_engine_shutdown(engine);
    return result == JFX_SUCCESS ? 0 : 1;
}

static int is_help_flag(const char *arg) {
    return strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const char *command = argv[1];
    int rest_argc = argc - 2;
    char **rest_argv = argv + 2;

    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0 ||
        strcmp(command, "-V") == 0) {
        return cmd_version();
    }
    if (strcmp(command, "help") == 0 || is_help_flag(command)) {
        if (rest_argc == 1) {
            print_command_help(rest_argv[0]);
            return 0;
        }
        print_usage();
        return 0;
    }
    if (strcmp(command, "compile") == 0) {
        return cmd_compile(rest_argc, rest_argv);
    }
    if (strcmp(command, "verify") == 0) {
        return cmd_verify(rest_argc, rest_argv);
    }
    if (strcmp(command, "effects") == 0) {
        return cmd_effects(rest_argc, rest_argv);
    }
    if (strcmp(command, "info") == 0) {
        return cmd_info(rest_argc, rest_argv);
    }
    if (strcmp(command, "render") == 0) {
        return cmd_render(rest_argc, rest_argv);
    }
    if (strcmp(command, "run") == 0) {
        return cmd_run();
    }

    printf("Unknown command: %s\n", command);
    print_usage();
    return 1;
}
