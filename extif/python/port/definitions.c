#include "py/obj.h"
MP_REGISTER_ROOT_POINTER(mp_obj_t jfx_refs[128]);
MP_REGISTER_MODULE(MP_QSTR_jfx, jfx_python_module);
MP_REGISTER_MODULE(MP_QSTR_pyjoltfx, jfx_python_module);
/* These names are used by the adapter's static binding table. */
static const unsigned names[] = {
    MP_QSTR_Value, MP_QSTR_clamp, MP_QSTR_log, MP_QSTR_command, MP_QSTR_state,
    MP_QSTR_on, MP_QSTR_off, MP_QSTR_register_kernel, MP_QSTR_size, MP_QSTR_read,
    MP_QSTR_write, MP_QSTR_dimensions, MP_QSTR_sample, MP_QSTR_write_pixel, MP_QSTR_color
};
