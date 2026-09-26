#ifndef TILLY_MODULE_H
#define TILLY_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_module tilly_module_t;

typedef int (*tilly_module_init_fn)(void);
typedef void (*tilly_module_shutdown_fn)(void);

tilly_module_t *tilly_module_load(const char *name);
void tilly_module_unload(tilly_module_t *module);

#ifdef __cplusplus
}
#endif

#endif // TILLY_MODULE_H
