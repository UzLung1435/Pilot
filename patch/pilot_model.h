#ifndef PILOT_MODEL_H
#define PILOT_MODEL_H

/* One Pilot build targets exactly one camera body.
 *
 * Every model-visible name comes from this header: the three bootstrap
 * exports, the persistent DSO path, the boot trace file and the tag a module
 * must export before the framework will load it.  A DSO or module compiled for
 * another body therefore cannot satisfy this build's dlopen()/dlsym() checks.
 *
 * Values must stay in sync with models.json; tools/pilot_tool/models.py
 * --check verifies both branches.  Build with -DPILOT_MODEL_A7C2 for the
 * ILCE-7CM2, or use the default for the camera-verified ILCE-6700 build.
 */

#if defined(PILOT_MODEL_A7C2) && defined(PILOT_MODEL_A6700)
#error "define exactly one PILOT_MODEL_* macro"
#endif

#if defined(PILOT_MODEL_A7C2)

#define PILOT_MODEL_ID "a7c2"
#define PILOT_MODEL_NAME "Sony ILCE-7CM2 (A7C II)"
#define PILOT_DSO_SONAME "pilot_a7c2.so"
#define PILOT_DSO_PATH "/usr/lib/pilot_a7c2.so"
#define PILOT_BOOT_TRACE_PATH "/tmp_wbshd/pilot-a7c2-boot.bin"
#define PILOT_MARKER_FN pilot_a7c2_v1_marker
#define PILOT_ON_MOUNT_FN pilot_a7c2_v1_on_mount
#define PILOT_GET_API_FN pilot_a7c2_get_api
#define PILOT_MODULE_MODEL_FN pilot_module_model_a7c2
#define PILOT_MARKER_SYMBOL "pilot_a7c2_v1_marker"
#define PILOT_ON_MOUNT_SYMBOL "pilot_a7c2_v1_on_mount"
#define PILOT_GET_API_SYMBOL "pilot_a7c2_get_api"
#define PILOT_MODULE_MODEL_SYMBOL "pilot_module_model_a7c2"

#else

#define PILOT_MODEL_A6700 1
#define PILOT_MODEL_ID "a6700"
#define PILOT_MODEL_NAME "Sony ILCE-6700"
#define PILOT_DSO_SONAME "pilot.so"
#define PILOT_DSO_PATH "/usr/lib/pilot.so"
#define PILOT_BOOT_TRACE_PATH "/tmp_wbshd/pilot-boot.bin"
#define PILOT_MARKER_FN pilot_a6700_v1_marker
#define PILOT_ON_MOUNT_FN pilot_a6700_v1_on_mount
#define PILOT_GET_API_FN pilot_a6700_get_api
#define PILOT_MODULE_MODEL_FN pilot_module_model_a6700
#define PILOT_MARKER_SYMBOL "pilot_a6700_v1_marker"
#define PILOT_ON_MOUNT_SYMBOL "pilot_a6700_v1_on_mount"
#define PILOT_GET_API_SYMBOL "pilot_a6700_get_api"
#define PILOT_MODULE_MODEL_SYMBOL "pilot_module_model_a6700"

#endif

/* Known model ids, used to name the offending body in a mismatch diagnostic. */
#define PILOT_MODULE_MODEL_PREFIX "pilot_module_model_"
#define PILOT_MODEL_IDS { "a6700", "a7c2", 0 }

#endif
