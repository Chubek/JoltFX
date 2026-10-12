#ifndef INGAGI_KEYSTONE_BRIDGE_H
#define INGAGI_KEYSTONE_BRIDGE_H
#include <stddef.h>
#include <keystone/keystone.h>

typedef struct {
    void *library;
    unsigned char *bytes;
    size_t size;
    void (*release)(unsigned char *);
    char error[512];
} ig_ks_result;

void ig_ks_assemble(const char *library, const char *source, ig_ks_result *result);
void ig_ks_dispose(ig_ks_result *result);
#endif
