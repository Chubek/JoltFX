#ifndef JFX_IAP_H
#define JFX_IAP_H
/* Ingagi Animation Pack (.iap) — API 1.0.
 *
 * A deterministic binary container for the 3D Modeling & Animation tab's
 * animation event stream plus packed asset bytes. It is written by Ingagi
 * driving the scene (keys/cloners) or by hand edits, and read back for
 * inspection. The player for this format is future work.
 *
 * Layout (all integers little-endian):
 *   0x00  char[4]  magic "IAP1"
 *   0x04  u32      version (1)
 *   0x08  u32      fps (1..240)
 *   0x0C  u32      frames (1..36000)
 *   0x10  u32      object_count
 *   then per object:
 *     u8       name_len, name bytes (0..127, no NUL/control)
 *     u32      cloner_mode (0..3), u32 instances (1..64), f32 spacing
 *     u32      key_count, then per key: u32 channel, u32 frame,
 *              f32 value, u32 interpolation (0..2), keys sorted by
 *              (channel, frame)
 *   then asset table:
 *     u32      asset_count (0..16)
 *     per asset: u8 path_len (1..127), path bytes, u64 size (<=16MiB),
 *                raw bytes. Total packed bytes <=64MiB.
 *   then u32 CRC32 (IEEE) over every preceding byte.
 *
 * Bounds mirror `jfx_modeling3d.h`; export validates the scene first and
 * leaves the destination untouched on error (temporary + atomic rename,
 * same as PLY/PNG export). All entry points validate pointers. */
#include <stddef.h>
#include <stdint.h>
#include "jfx/jfx_result.h"
#include "jfx/jfx_modeling3d.h"
#ifdef __cplusplus
extern "C" {
#endif

#define JFX_IAP_API_MAJOR 1
#define JFX_IAP_API_MINOR 0
#define JFX_IAP_MAGIC "IAP1"
#define JFX_IAP_VERSION 1u
#define JFX_IAP_MAX_ASSETS 16u
#define JFX_IAP_MAX_ASSET_BYTES (16u*1024u*1024u)
#define JFX_IAP_MAX_TOTAL_BYTES (64u*1024u*1024u)

/* Export the animation event stream of `scene` plus the files in
 * `asset_paths` (array of `asset_count` NUL-terminated paths) into `path`.
 * Asset bytes are packed verbatim; missing/unreadable assets fail without
 * touching `path`. */
jfx_result_t jfx_iap_export(const jfx_scene3d_t *scene,
    const char *const *asset_paths, size_t asset_count, const char *path);

/* Apply a pack's animation event stream to `scene` (player import path).
 * The pack must be well-formed (same checks as validate) and hold exactly
 * as many objects as the scene; keys/cloners/clock apply per object index
 * (names are informational). Meshes, transforms, materials and history are
 * otherwise untouched except for one undoable history step covering the
 * whole import. Fails without touching the scene or its history. */
jfx_result_t jfx_iap_import(jfx_scene3d_t *scene, const char *path);

/* Inspect a pack without loading a scene. On success writes fps/frames/
 * objects/assets and the byte size. Any out pointer may be NULL. */
jfx_result_t jfx_iap_info(const char *path,
    uint32_t *out_fps, uint32_t *out_frames,
    uint32_t *out_objects, uint32_t *out_assets, uint64_t *out_bytes);

/* Validate a pack's structure, bounds and CRC. Returns JFX_SUCCESS when
 * the file is a well-formed .iap, otherwise INVALID_ARGUMENT or NOT_FOUND. */
jfx_result_t jfx_iap_validate(const char *path);

#ifdef __cplusplus
}
#endif
#endif
