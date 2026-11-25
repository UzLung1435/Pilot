/* PILOT module: Pilot menu UI for ILCE-7CM2 (A7C II) firmware 2.01.
 *
 * The A6700 build of this module (../pilot_menu_ui.c) synthesises its own pages
 * and replaces two stock screens.  This body has neither the page table nor the
 * resource bank that build relies on, so the module instead inserts one row into
 * the stock "settings options" page and takes over the native list screen that
 * row opens.  See a7c2/README.md for the recovered addresses and the contracts
 * they are checked against.
 *
 * Layout of this translation unit, in include order:
 *
 *   pilot_a7c2_menu.h    addresses, ABIs and diagnostic records
 *   pilot_menu_model.inc the Pilot content tree, body independent, host testable
 *   pilot_menu_entry.inc patching glue and stock-menu hooks
 *   pilot_menu_list.inc  native list transport (vendored reference code)
 */

#define _GNU_SOURCE
#include "../../../patch/pilot.h"
#include "pilot_a7c2_menu.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

PILOT_DECLARE_MODULE_MODEL();

static const PilotApi *g_api;
static uintptr_t g_appfw_base;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_installed;

static int find_appfw_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    if (!info->dlpi_name || !strstr(info->dlpi_name, "appFw.so"))
        return 0;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;
}

#include "pilot_menu_model.inc"
#include "pilot_menu_entry.inc"
#include "pilot_menu_list.inc"

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api) {
    if (!api || api->abi_version < PILOT_ABI_VERSION || api->struct_size < sizeof(PilotApi))
        return PILOT_STATUS_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_lock);
    PilotStatus status = PILOT_STATUS_OK;
    if (!g_installed) {
        g_api = api;
        int result = pilot_install();
        if (result == 0) {
            g_installed = 1;
        } else {
            pilot_menu_observation.install_status = (uint32_t)result;
            status = result == -1 || result == -2 ? PILOT_STATUS_CONFLICT
                                                  : PILOT_STATUS_UNSUPPORTED;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return status;
}
