//go:build linux && cgo

#include "bridge.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Keystone owns the engine and output allocation; Go copies bytes before
 * disposal. This bridge does not allocate or retain any Go-owned pointer. */
void ig_ks_assemble(const char *library, const char *source, ig_ks_result *r) {
    memset(r, 0, sizeof(*r));
    r->library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!r->library) {
        snprintf(r->error, sizeof(r->error), "%s", dlerror());
        return;
    }
    unsigned int (*version)(unsigned int *, unsigned int *) = dlsym(r->library, "ks_version");
    ks_err (*open_engine)(ks_arch, int, ks_engine **) = dlsym(r->library, "ks_open");
    ks_err (*close_engine)(ks_engine *) = dlsym(r->library, "ks_close");
    ks_err (*option)(ks_engine *, ks_opt_type, size_t) = dlsym(r->library, "ks_option");
    int (*assemble)(ks_engine *, const char *, uint64_t, unsigned char **, size_t *, size_t *) = dlsym(r->library, "ks_asm");
    ks_err (*last_error)(ks_engine *) = dlsym(r->library, "ks_errno");
    const char *(*error_string)(ks_err) = dlsym(r->library, "ks_strerror");
    r->release = dlsym(r->library, "ks_free");
    if (!version || !open_engine || !close_engine || !option || !assemble || !last_error || !error_string || !r->release) {
        snprintf(r->error, sizeof(r->error), "library does not expose the Keystone C API");
        return;
    }
    unsigned int major = 0, minor = 0;
    version(&major, &minor);
    if (major != KS_API_MAJOR || minor != KS_API_MINOR) {
        snprintf(r->error, sizeof(r->error), "ABI %u.%u does not match vendored headers %d.%d", major, minor, KS_API_MAJOR, KS_API_MINOR);
        return;
    }
    ks_engine *engine = NULL;
    ks_err err = open_engine(KS_ARCH_X86, KS_MODE_64, &engine);
    if (err != KS_ERR_OK) {
        snprintf(r->error, sizeof(r->error), "%s", error_string(err));
        return;
    }
    err = option(engine, KS_OPT_SYNTAX, KS_OPT_SYNTAX_ATT);
    size_t statements = 0;
    if (err != KS_ERR_OK) {
        snprintf(r->error, sizeof(r->error), "%s", error_string(err));
    } else if (assemble(engine, source, 0, &r->bytes, &r->size, &statements) != 0) {
        snprintf(r->error, sizeof(r->error), "after %zu statements: %s", statements, error_string(last_error(engine)));
    }
    close_engine(engine);
}

void ig_ks_dispose(ig_ks_result *r) {
    if (r->bytes && r->release) r->release(r->bytes);
    if (r->library) dlclose(r->library);
    memset(r, 0, sizeof(*r));
}
