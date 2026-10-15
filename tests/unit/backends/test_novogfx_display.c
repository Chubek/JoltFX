/* Link the real display bridge against deterministic NovoGFX/DRM fakes.
 * No /dev/dri access or physical modesetting is possible in this test. */
#include "novogfx_display.h"
#include "novogfx.h"
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct novogfx_device_t { int unused; };
struct novogfx_connector_t { int unused; };
struct novogfx_crtc_t { int unused; };
struct novogfx_framebuffer_t { unsigned index; };
struct novogfx_dumb_buffer_t { unsigned index; };
struct novogfx_atomic_request_t { int unused; };
struct novogfx_event_reader_t { int unused; };
static struct novogfx_device_t device;
static struct novogfx_connector_t connector;
static struct novogfx_crtc_t crtc;
static struct novogfx_framebuffer_t fbs[2] = {{0},{1}};
static struct novogfx_dumb_buffer_t buffers[2] = {{0},{1}};
static struct novogfx_atomic_request_t request;
static struct novogfx_event_reader_t reader;
static novogfx_connector_t *connectors[] = { &connector };
static novogfx_resources_t resources;
static novogfx_mode_t mode;
static drmModeCrtc saved;
static uint32_t mapped[2][12]; /* 4x2 pixels, with 8 bytes of row padding. */
static int step, fail_step, created, destroyed, live_devices, live_crtcs, live_lists, live_readers, live_requests;
static int restored, commits, flips, master, flip_fail, master_fail;
static unsigned target;
static novogfx_event_t event;
static bool emit;
static unsigned next(void) { return ++step == fail_step ? NOVOGFX_ERR_IO : NOVOGFX_OK; }
const char *novogfx_status_string(unsigned status) { return status ? "injected failure" : "ok"; }
novogfx_status_t novogfx_device_open(const char *path, novogfx_device_t **out) {
    assert(strcmp(path, "/mock/card") == 0); unsigned s=next(); if (!s) { *out=&device; ++live_devices; } return s;
}
int novogfx_device_fd(const novogfx_device_t *d) { assert(d==&device); return 42; }
void novogfx_device_destroy(novogfx_device_t *d) { if (d) { assert(live_devices==1); --live_devices; } }
int drmIsMaster(int fd) { assert(fd==42); return master; }
int drmSetMaster(int fd) { assert(fd==42); if (master_fail) return -1; master=1; return 0; }
int drmDropMaster(int fd) { assert(fd==42); master=0; return 0; }
novogfx_status_t novogfx_device_enable_atomic(const novogfx_device_t *d) { assert(d==&device); return next(); }
novogfx_status_t novogfx_device_get_resources(const novogfx_device_t *d, novogfx_resources_t **out) {
    assert(d==&device); unsigned s=next(); if (!s) { resources.num_connectors=1; resources.connectors=connectors; *out=&resources; ++live_lists; } return s;
}
void novogfx_resources_destroy(novogfx_resources_t *r) { if (r) { assert(r==&resources && live_lists==1); --live_lists; } }
novogfx_status_t novogfx_connector_status(const novogfx_connector_t *c, novogfx_connector_status_t *out) {
    assert(c==&connector); unsigned s=next(); *out=NOVOGFX_CONNECTOR_CONNECTED; return s;
}
novogfx_crtc_t *novogfx_connector_current_crtc(const novogfx_connector_t *c) { assert(c==&connector); ++live_crtcs; return &crtc; }
void novogfx_crtc_destroy(novogfx_crtc_t *c) { if (c) { assert(c==&crtc && live_crtcs>0); --live_crtcs; } }
unsigned long novogfx_crtc_id(const novogfx_crtc_t *c) { assert(c==&crtc); return 7; }
unsigned long novogfx_connector_id(const novogfx_connector_t *c) { assert(c==&connector); return 9; }
drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t id) {
    assert(fd==42 && id==7); memset(&saved,0,sizeof(saved)); saved.crtc_id=7; saved.buffer_id=123;
    saved.mode_valid=1; saved.mode.hdisplay=4; saved.mode.vdisplay=2; return &saved;
}
void drmModeFreeCrtc(drmModeCrtcPtr c) { assert(c==&saved); }
int drmModeSetCrtc(int fd, uint32_t id, uint32_t fb, uint32_t x, uint32_t y,
    uint32_t *ids, int count, drmModeModeInfoPtr m) {
    assert(fd==42 && id==7 && fb==123 && x==0 && y==0 && count==1 && ids[0]==9 && m==&saved.mode);
    assert(created==2 && destroyed==0); ++restored; return 0;
}
const novogfx_mode_t *novogfx_crtc_current_mode(const novogfx_crtc_t *c) { assert(c==&crtc); return &mode; }
novogfx_status_t novogfx_dumb_buffer_create(novogfx_device_t *d, const novogfx_dumb_buffer_desc_t *desc,
    novogfx_dumb_buffer_t **out, novogfx_framebuffer_t **fb) {
    assert(d==&device && desc->width==4 && desc->height==2 && desc->bpp==32);
    unsigned s=next(); if (!s) { *out=&buffers[created]; *fb=&fbs[created]; ++created; } return s;
}
novogfx_status_t novogfx_dumb_buffer_get_info(const novogfx_dumb_buffer_t *b, novogfx_dumb_buffer_info_t *info) {
    assert(b==&buffers[b->index]); unsigned s=next(); info->width=4; info->height=2; info->pitch=24; info->size=48; return s;
}
novogfx_status_t novogfx_dumb_buffer_map(novogfx_dumb_buffer_t *b, uint8_t **out) {
    unsigned s=next(); if (!s) *out=(uint8_t *)mapped[b->index]; return s;
}
void novogfx_dumb_buffer_destroy(novogfx_dumb_buffer_t *b) { if (b) { assert(b==&buffers[b->index]); ++destroyed; } }
novogfx_status_t novogfx_event_reader_create(novogfx_device_t *d, novogfx_event_reader_t **out) {
    assert(d==&device); unsigned s=next(); if (!s) { *out=&reader; ++live_readers; } return s;
}
void novogfx_event_reader_destroy(novogfx_event_reader_t *r) { if (r) { assert(r==&reader); --live_readers; } }
novogfx_status_t novogfx_event_reader_poll(novogfx_event_reader_t *r, novogfx_event_t **events, unsigned long *count) {
    assert(r==&reader); unsigned s=next(); *events=&event; *count=emit?1:0; emit=false; return s;
}
novogfx_status_t novogfx_atomic_request_create(novogfx_device_t *d, novogfx_atomic_request_t **out) {
    assert(d==&device); unsigned s=next(); if (!s) { *out=&request; ++live_requests; } return s;
}
novogfx_status_t novogfx_atomic_request_set_crtc(novogfx_atomic_request_t *r, novogfx_crtc_t *c,
    novogfx_framebuffer_t *fb, const novogfx_mode_t *m, novogfx_connector_t *conn) {
    assert(r==&request && c==&crtc && fb==&fbs[0] && m==&mode && conn==&connector); return next();
}
novogfx_status_t novogfx_atomic_request_commit(novogfx_atomic_request_t *r, unsigned long flags) {
    assert(r==&request); assert(flags==NOVOGFX_COMMIT_ALLOW_MODESET || flags==(NOVOGFX_COMMIT_ALLOW_MODESET|NOVOGFX_COMMIT_TEST_ONLY));
    unsigned s=next(); if (!s && !(flags & NOVOGFX_COMMIT_TEST_ONLY)) ++commits; return s;
}
void novogfx_atomic_request_destroy(novogfx_atomic_request_t *r) { if (r) { assert(r==&request); --live_requests; } }
novogfx_status_t novogfx_crtc_page_flip(novogfx_crtc_t *c, novogfx_framebuffer_t *fb, unsigned long flags, unsigned long cookie) {
    assert(c==&crtc && flags==DRM_MODE_PAGE_FLIP_EVENT && cookie==0);
    if (flip_fail) return NOVOGFX_ERR_DEVICE_LOST;
    target=fb->index; ++flips; return NOVOGFX_OK;
}

static void reset(void) {
    step=created=destroyed=restored=commits=flips=master=flip_fail=master_fail=0; emit=false;
    memset(mapped,0xa5,sizeof(mapped));
}
static void clean(void) {
    assert(created==destroyed && !live_devices && !live_crtcs && !live_lists && !live_readers && !live_requests && !master);
    assert(restored==commits);
}
int main(void) {
    assert(jfx_novogfx_display_create("/mock/card",NULL,NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_novogfx_display_width(NULL)==0 && jfx_novogfx_display_height(NULL)==0);
    jfx_novogfx_display_destroy(NULL);
    reset(); fail_step=0;
    jfx_novogfx_display_t *d=NULL; char error[256];
    assert(jfx_novogfx_display_create("/mock/card",&d,error,sizeof(error))==JFX_SUCCESS);
    int setup_steps=step;
    assert(jfx_novogfx_display_width(d)==4 && jfx_novogfx_display_height(d)==2);
    const uint8_t pixels[40]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255,
        99,99,99,99, 1,2,3,255, 4,5,6,255, 7,8,9,255, 10,11,12,255};
    assert(jfx_novogfx_display_present(NULL,pixels,20)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_novogfx_display_present(d,pixels,15)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_novogfx_display_present(d,pixels,SIZE_MAX)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_SUCCESS && target==1 && flips==1);
    assert(mapped[1][0]==0xffff0000 && mapped[1][1]==0xff00ff00 && mapped[1][2]==0xff0000ff);
    assert(mapped[1][6]==0xff010203 && mapped[1][4]==0 && mapped[1][5]==0);
    uint32_t retained[2][12]; memcpy(retained,mapped,sizeof(mapped));
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_ERROR_BUSY && flips==1);
    assert(memcmp(retained,mapped,sizeof(mapped))==0);
    emit=true; event.type=NOVOGFX_EVENT_PAGE_FLIP; event.crtc_id=8;
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_ERROR_BUSY && flips==1);
    emit=true; event.crtc_id=7;
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_SUCCESS && target==0 && flips==2);
    emit=true; flip_fail=1;
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_ERROR_BACKEND_FAILURE && flips==2);
    flip_fail=0; fail_step=step+1;
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_ERROR_BACKEND_FAILURE && flips==2);
    fail_step=0; emit=true; event.flip_status=-1;
    assert(jfx_novogfx_display_present(d,pixels,20)==JFX_ERROR_BACKEND_FAILURE && flips==2);
    event.flip_status=0;
    jfx_novogfx_display_destroy(d); clean();
    /* Every setup failure must unwind without modesetting/restoring or leaks. */
    for (int i=1; i<=setup_steps; ++i) {
        reset(); fail_step=i; d=(jfx_novogfx_display_t *)1;
        assert(jfx_novogfx_display_create("/mock/card",&d,error,sizeof(error))!=JFX_SUCCESS && !d);
        clean();
    }
    reset(); fail_step=0; master_fail=1;
    assert(jfx_novogfx_display_create("/mock/card",&d,error,sizeof(error))==JFX_ERROR_BACKEND_FAILURE && !d);
    clean();
    puts("NovoGFX scanout ownership, pixels, flip completion and failures: OK");
}
