#include "menu_api.h"

#include <limits.h>
#include <string.h>

static menu_api_status context(const menu_api *api)
{
    if (!api)
        return MENU_API_E_ARGUMENT;
    return api->entered ? MENU_API_OK : MENU_API_E_CONTEXT;
}

static menu_api_status begin(menu_api *api)
{
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (api->busy)
        return MENU_API_E_BUSY;
    api->busy = true;
    return MENU_API_OK;
}

static menu_api_status finish(menu_api *api, menu_api_status status)
{
    api->busy = false;
    return status;
}

static bool container_kind(menu_api_kind kind)
{
    return kind == MENU_API_GROUP || kind == MENU_API_PAGE ||
           kind == MENU_API_SUBMENU;
}

static bool scalar_kind(menu_api_kind kind)
{
    return kind == MENU_API_BOOL || kind == MENU_API_ENUM ||
           kind == MENU_API_NUMERIC;
}

static menu_api_status lookup(const menu_api *api, menu_api_handle handle,
                              size_t *index)
{
    uint32_t low = (uint32_t)handle;
    uint32_t generation = (uint32_t)(handle >> 32);
    if (!low || low > MENU_API_NODE_LIMIT || !generation)
        return MENU_API_E_STALE;
    *index = (size_t)low - 1u;
    if (!api->slots[*index].used ||
        api->slots[*index].generation != generation)
        return MENU_API_E_STALE;
    return MENU_API_OK;
}

static menu_api_status container_check(const menu_api *api,
                                       menu_api_handle handle)
{
    size_t index;
    menu_api_status status;
    if (handle == MENU_API_ROOT)
        return MENU_API_OK;
    status = lookup(api, handle, &index);
    if (status != MENU_API_OK)
        return status;
    return container_kind(api->slots[index].info.kind) ? MENU_API_OK :
                                                        MENU_API_E_TYPE;
}

static menu_api_status text_copy(char *dest, const char *source, size_t maximum,
                                 bool allow_empty)
{
    size_t length = 0, i = 0;
    if (!source)
        return MENU_API_E_ARGUMENT;
    while (length <= maximum && source[length])
        ++length;
    if (length > maximum)
        return MENU_API_E_LIMIT;
    if (!allow_empty && !length)
        return MENU_API_E_ARGUMENT;
    while (i < length) {
        unsigned char first = (unsigned char)source[i++];
        uint32_t code, minimum;
        size_t extra;
        if (first < 0x80u)
            continue;
        if (first >= 0xc2u && first <= 0xdfu) {
            code = first & 0x1fu;
            minimum = 0x80u;
            extra = 1;
        } else if (first >= 0xe0u && first <= 0xefu) {
            code = first & 0x0fu;
            minimum = 0x800u;
            extra = 2;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            code = first & 0x07u;
            minimum = 0x10000u;
            extra = 3;
        } else {
            return MENU_API_E_UTF8;
        }
        if (extra > length - i)
            return MENU_API_E_UTF8;
        while (extra--) {
            unsigned char next = (unsigned char)source[i++];
            if ((next & 0xc0u) != 0x80u)
                return MENU_API_E_UTF8;
            code = (code << 6) | (next & 0x3fu);
        }
        if (code < minimum || code > 0x10ffffu ||
            (code >= 0xd800u && code <= 0xdfffu))
            return MENU_API_E_UTF8;
    }
    memcpy(dest, source, length + 1u);
    return MENU_API_OK;
}

static menu_api_status number_check(const menu_api_slot *slot, int64_t value,
                                    bool writing)
{
    size_t i;
    switch (slot->info.kind) {
    case MENU_API_BOOL:
        return value == 0 || value == 1 ? MENU_API_OK : MENU_API_E_RANGE;
    case MENU_API_ENUM:
        for (i = 0; i < slot->info.option_count; ++i) {
            if (slot->options[i].value == value)
                return writing && !slot->options[i].enabled ?
                       MENU_API_E_DISABLED : MENU_API_OK;
        }
        return MENU_API_E_RANGE;
    case MENU_API_NUMERIC:
        if (value < slot->info.minimum || value > slot->info.maximum)
            return MENU_API_E_RANGE;
        /* Unsigned subtraction also covers INT64_MIN..INT64_MAX safely. */
        return ((uint64_t)value - (uint64_t)slot->info.minimum) %
               (uint64_t)slot->info.step == 0 ? MENU_API_OK : MENU_API_E_RANGE;
    default:
        return MENU_API_E_TYPE;
    }
}

static void decimal(int64_t number, char *text)
{
    char reverse[20];
    size_t count = 0, index = 0;
    uint64_t magnitude = number < 0 ? UINT64_C(0) - (uint64_t)number :
                                      (uint64_t)number;
    if (number < 0)
        text[index++] = '-';
    do {
        reverse[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude);
    while (count)
        text[index++] = reverse[--count];
    text[index] = '\0';
}

static menu_api_status canonical(const menu_api_slot *slot,
                                 const menu_api_value *raw,
                                 menu_api_value *value, bool allow_format,
                                 int *callback_code)
{
    size_t i;
    menu_api_status status;
    memset(value, 0, sizeof(*value));
    if (slot->info.kind == MENU_API_READONLY)
        return text_copy(value->text, raw->text, MENU_API_VALUE_MAX, true);
    status = number_check(slot, raw->number, false);
    if (status != MENU_API_OK)
        return status;
    value->number = raw->number;
    if (allow_format && slot->format) {
        char formatted[MENU_API_VALUE_MAX + 1u];
        /* A callback must supply its own NUL within the provided capacity. */
        memset(formatted, 0xff, sizeof(formatted));
        *callback_code = slot->format(slot->userdata, raw->number, formatted,
                                     sizeof(formatted));
        if (*callback_code)
            return MENU_API_E_CALLBACK;
        return text_copy(value->text, formatted, MENU_API_VALUE_MAX, true);
    }
    if (slot->info.kind == MENU_API_BOOL) {
        strcpy(value->text, raw->number ? "On" : "Off");
    } else if (slot->info.kind == MENU_API_ENUM) {
        for (i = 0; i < slot->info.option_count; ++i) {
            if (slot->options[i].value == raw->number) {
                strcpy(value->text, slot->options[i].label);
                break;
            }
        }
    } else {
        decimal(raw->number, value->text);
    }
    return MENU_API_OK;
}

static menu_api_status raw_read(menu_api_slot *slot, menu_api_value *value,
                                int *callback_code)
{
    menu_api_value raw = {0};
    if (!slot->get) {
        if (!slot->format) {
            *value = slot->cached;
            return MENU_API_OK;
        }
        raw.number = slot->cached.number;
    } else {
        *callback_code = slot->get(slot->userdata, &raw);
        if (*callback_code)
            return MENU_API_E_CALLBACK;
    }
    return canonical(slot, &raw, value, true, callback_code);
}

static menu_api_status read_slot(menu_api_slot *slot, int *callback_code)
{
    menu_api_value value;
    menu_api_status status;
    if (!scalar_kind(slot->info.kind) && slot->info.kind != MENU_API_READONLY)
        return MENU_API_E_TYPE;
    status = raw_read(slot, &value, callback_code);
    if (status == MENU_API_OK) {
        slot->cached = value;
        slot->cached_valid = true;
    }
    return status;
}

static void result_copy(const menu_api_slot *slot, menu_api_result *result,
                        bool fresh)
{
    result->has_value = slot->cached_valid;
    result->fresh = fresh && slot->cached_valid;
    if (slot->cached_valid)
        result->value = slot->cached;
}

static menu_api_status conditions(const menu_api_slot *slot, bool *visible,
                                  bool *enabled, int *callback_code)
{
    bool value = false;
    if (slot->visible) {
        *callback_code = slot->visible(slot->userdata, &value);
        if (*callback_code)
            return MENU_API_E_CALLBACK;
        if (!value) {
            *visible = false;
            *enabled = false;
            return MENU_API_OK;
        }
    }
    if (*enabled && slot->enabled) {
        value = false;
        *callback_code = slot->enabled(slot->userdata, &value);
        if (*callback_code)
            return MENU_API_E_CALLBACK;
        *enabled = value;
    }
    return MENU_API_OK;
}

static menu_api_status effective(const menu_api *api, menu_api_handle handle,
                                 bool *visible, bool *enabled,
                                 int *callback_code)
{
    size_t chain[MENU_API_NODE_LIMIT], count = 0, index;
    menu_api_status status;
    *visible = true;
    *enabled = true;
    while (handle != MENU_API_ROOT) {
        if (count == MENU_API_NODE_LIMIT)
            return MENU_API_E_STATE;
        status = lookup(api, handle, &index);
        if (status != MENU_API_OK)
            return status;
        chain[count++] = index;
        handle = api->slots[index].info.parent;
    }
    while (count) {
        status = conditions(&api->slots[chain[--count]], visible, enabled,
                            callback_code);
        if (status != MENU_API_OK || !*visible)
            return status;
    }
    return MENU_API_OK;
}

static menu_api_status access_check(const menu_api *api, menu_api_handle handle,
                                    int *callback_code)
{
    bool visible, enabled;
    menu_api_status status = effective(api, handle, &visible, &enabled,
                                       callback_code);
    if (status != MENU_API_OK)
        return status;
    if (!visible)
        return MENU_API_E_HIDDEN;
    return enabled ? MENU_API_OK : MENU_API_E_DISABLED;
}

static size_t child_indices(const menu_api *api, menu_api_handle parent,
                            size_t indices[MENU_API_NODE_LIMIT])
{
    size_t count = 0, i, j;
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        if (!api->slots[i].used || api->slots[i].info.parent != parent)
            continue;
        j = count;
        while (j && api->slots[indices[j - 1u]].order > api->slots[i].order) {
            indices[j] = indices[j - 1u];
            --j;
        }
        indices[j] = i;
        ++count;
    }
    return count;
}

menu_api_status menu_api_enter(menu_api *api)
{
    if (!api)
        return MENU_API_E_ARGUMENT;
    if (api->entered || api->busy)
        return MENU_API_E_BUSY;
    api->entered = true;
    return MENU_API_OK;
}

menu_api_status menu_api_leave(menu_api *api)
{
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (api->busy)
        return MENU_API_E_BUSY;
    api->entered = false;
    return MENU_API_OK;
}

menu_api_status menu_api_register(menu_api *api, const menu_api_def *def,
                                  menu_api_handle *handle)
{
    menu_api_slot candidate = {0};
    menu_api_value initial = {0};
    size_t free_index = MENU_API_NODE_LIMIT, parent_index = 0, i, j;
    menu_api_kind parent_kind = 0;
    menu_api_status status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (!def || !handle || !def->id || def->kind < MENU_API_GROUP ||
        def->kind > MENU_API_READONLY)
        return finish(api, MENU_API_E_ARGUMENT);
    if (def->parent != MENU_API_ROOT) {
        status = lookup(api, def->parent, &parent_index);
        if (status != MENU_API_OK)
            return finish(api, status);
        parent_kind = api->slots[parent_index].info.kind;
    }
    if ((def->kind == MENU_API_GROUP && def->parent != MENU_API_ROOT &&
         parent_kind != MENU_API_GROUP) ||
        (def->kind == MENU_API_PAGE && parent_kind != MENU_API_GROUP) ||
        (def->kind >= MENU_API_SUBMENU && parent_kind != MENU_API_PAGE &&
         parent_kind != MENU_API_SUBMENU))
        return finish(api, MENU_API_E_TYPE);
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        if (api->slots[i].used && api->slots[i].info.id == def->id)
            return finish(api, MENU_API_E_DUPLICATE);
        if (!api->slots[i].used && api->slots[i].generation != UINT32_MAX &&
            free_index == MENU_API_NODE_LIMIT)
            free_index = i;
    }
    if (free_index == MENU_API_NODE_LIMIT || api->order == UINT64_MAX)
        return finish(api, MENU_API_E_LIMIT);
    if ((def->format && !scalar_kind(def->kind)) ||
        (scalar_kind(def->kind) && (!!def->get != !!def->set || def->action)) ||
        (container_kind(def->kind) && (def->get || def->set || def->action)) ||
        (def->kind == MENU_API_ACTION && (!def->action || def->get || def->set)) ||
        (def->kind == MENU_API_READONLY && (!def->get || def->set || def->action)))
        return finish(api, MENU_API_E_ARGUMENT);
    candidate.info.id = def->id;
    candidate.info.parent = def->parent;
    candidate.info.kind = def->kind;
    candidate.info.bound = def->get != NULL;
    status = text_copy(candidate.info.label, def->label, MENU_API_LABEL_MAX,
                       false);
    if (status != MENU_API_OK)
        return finish(api, status);
    if (def->kind == MENU_API_ENUM) {
        if (!def->options || !def->option_count)
            return finish(api, MENU_API_E_ARGUMENT);
        if (def->option_count > MENU_API_OPTION_LIMIT)
            return finish(api, MENU_API_E_LIMIT);
        candidate.info.option_count = def->option_count;
        for (i = 0; i < def->option_count; ++i) {
            for (j = 0; j < i; ++j) {
                if (def->options[i].value == candidate.options[j].value)
                    return finish(api, MENU_API_E_DUPLICATE);
            }
            candidate.options[i].value = def->options[i].value;
            candidate.options[i].enabled = def->options[i].enabled;
            status = text_copy(candidate.options[i].label, def->options[i].label,
                               MENU_API_LABEL_MAX, false);
            if (status != MENU_API_OK)
                return finish(api, status);
        }
    } else if (def->option_count || def->options) {
        return finish(api, MENU_API_E_TYPE);
    }
    if (def->kind == MENU_API_NUMERIC) {
        if (def->minimum > def->maximum || def->step <= 0)
            return finish(api, MENU_API_E_RANGE);
        candidate.info.minimum = def->minimum;
        candidate.info.maximum = def->maximum;
        candidate.info.step = def->step;
    }
    candidate.userdata = def->userdata;
    candidate.get = def->get;
    candidate.set = def->set;
    candidate.format = def->format;
    candidate.action = def->action;
    candidate.visible = def->visible;
    candidate.enabled = def->enabled;
    if (scalar_kind(def->kind) && !def->get) {
        status = number_check(&candidate, def->initial, true);
        if (status != MENU_API_OK)
            return finish(api, status);
        initial.number = def->initial;
        status = canonical(&candidate, &initial, &candidate.cached, false, NULL);
        if (status != MENU_API_OK)
            return finish(api, status);
        candidate.cached_valid = true;
    }
    candidate.generation = api->slots[free_index].generation;
    if (!candidate.generation)
        candidate.generation = 1;
    candidate.info.handle = ((uint64_t)candidate.generation << 32) |
                            (uint64_t)(free_index + 1u);
    candidate.order = ++api->order;
    candidate.used = true;
    api->slots[free_index] = candidate;
    *handle = candidate.info.handle;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_unregister(menu_api *api, menu_api_handle handle)
{
    size_t index, i;
    uint32_t generation;
    menu_api_status status = begin(api);
    if (status != MENU_API_OK)
        return status;
    status = lookup(api, handle, &index);
    if (status != MENU_API_OK)
        return finish(api, status);
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        if (api->slots[i].used && api->slots[i].info.parent == handle)
            return finish(api, MENU_API_E_BUSY);
    }
    for (i = 0; i < api->depth; ++i) {
        if (api->frames[i].container == handle ||
            api->frames[i].container == api->slots[index].info.parent)
            return finish(api, MENU_API_E_BUSY);
    }
    if (api->slots[index].consumer_pins)
        return finish(api,MENU_API_E_BUSY);
    generation = api->slots[index].generation;
    memset(&api->slots[index], 0, sizeof(api->slots[index]));
    /* Retire exhausted slots instead of ever reviving a stale generation. */
    api->slots[index].generation = generation == UINT32_MAX ? generation :
                                                                  generation + 1u;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_pin(menu_api *api,menu_api_handle handle)
{
    size_t index;
    menu_api_status status=begin(api);
    if (status!=MENU_API_OK) return status;
    status=lookup(api,handle,&index);
    if (status==MENU_API_OK) {
        if (api->slots[index].consumer_pins==UINT32_MAX) status=MENU_API_E_LIMIT;
        else ++api->slots[index].consumer_pins;
    }
    return finish(api,status);
}

menu_api_status menu_api_unpin(menu_api *api,menu_api_handle handle)
{
    size_t index;
    menu_api_status status=begin(api);
    if (status!=MENU_API_OK) return status;
    status=lookup(api,handle,&index);
    if (status==MENU_API_OK) {
        if (!api->slots[index].consumer_pins) status=MENU_API_E_STATE;
        else --api->slots[index].consumer_pins;
    }
    return finish(api,status);
}

menu_api_status menu_api_describe(const menu_api *api, menu_api_handle handle,
                                  menu_api_info *info)
{
    size_t index;
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!info)
        return MENU_API_E_ARGUMENT;
    status = lookup(api, handle, &index);
    if (status == MENU_API_OK)
        *info = api->slots[index].info;
    return status;
}

menu_api_status menu_api_find(const menu_api *api, uint32_t id,
                              menu_api_handle *handle)
{
    size_t i;
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!id || !handle)
        return MENU_API_E_ARGUMENT;
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        if (api->slots[i].used && api->slots[i].info.id == id) {
            *handle = api->slots[i].info.handle;
            return MENU_API_OK;
        }
    }
    return MENU_API_E_NOT_FOUND;
}

menu_api_status menu_api_children(const menu_api *api, menu_api_handle parent,
                                  menu_api_handle *handles, size_t capacity,
                                  size_t *count)
{
    size_t indices[MENU_API_NODE_LIMIT], i;
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!count || (!handles && capacity))
        return MENU_API_E_ARGUMENT;
    status = container_check(api, parent);
    if (status != MENU_API_OK)
        return status;
    *count = child_indices(api, parent, indices);
    if (!handles)
        return MENU_API_OK;
    if (capacity < *count)
        return MENU_API_E_LIMIT;
    for (i = 0; i < *count; ++i)
        handles[i] = api->slots[indices[i]].info.handle;
    return MENU_API_OK;
}

menu_api_status menu_api_option_at(const menu_api *api, menu_api_handle handle,
                                   size_t index, menu_api_option *option)
{
    size_t slot_index;
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!option)
        return MENU_API_E_ARGUMENT;
    status = lookup(api, handle, &slot_index);
    if (status != MENU_API_OK)
        return status;
    if (api->slots[slot_index].info.kind != MENU_API_ENUM)
        return MENU_API_E_TYPE;
    if (index >= api->slots[slot_index].info.option_count)
        return MENU_API_E_RANGE;
    *option = api->slots[slot_index].options[index];
    return MENU_API_OK;
}

menu_api_status menu_api_peek(const menu_api *api, menu_api_handle handle,
                              menu_api_result *result)
{
    size_t index;
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = lookup(api, handle, &index);
    if (status != MENU_API_OK)
        return status;
    if (!scalar_kind(api->slots[index].info.kind) &&
        api->slots[index].info.kind != MENU_API_READONLY)
        return MENU_API_E_TYPE;
    result_copy(&api->slots[index], result, false);
    return MENU_API_OK;
}

menu_api_status menu_api_read(menu_api *api, menu_api_handle handle,
                              menu_api_result *result)
{
    size_t index;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    status = lookup(api, handle, &index);
    if (status == MENU_API_OK) {
        status = read_slot(&api->slots[index], &result->callback_code);
        result_copy(&api->slots[index], result, status == MENU_API_OK);
    }
    return finish(api, status);
}

static menu_api_status write_slot(menu_api *api, menu_api_slot *slot,
                                  int64_t number, int *callback_code)
{
    menu_api_value raw = {0}, value;
    menu_api_status status = number_check(slot, number, true);
    if (status != MENU_API_OK)
        return status;
    status = access_check(api, slot->info.handle, callback_code);
    if (status != MENU_API_OK)
        return status;
    if (slot->set) {
        *callback_code = slot->set(slot->userdata, number);
        if (*callback_code)
            return MENU_API_E_CALLBACK;
        status = raw_read(slot, &value, callback_code);
        if (status != MENU_API_OK)
            return status;
        if (value.number != number)
            return MENU_API_E_READBACK;
    } else {
        raw.number = number;
        status = canonical(slot, &raw, &value, true, callback_code);
        if (status != MENU_API_OK)
            return status;
    }
    slot->cached = value;
    slot->cached_valid = true;
    return MENU_API_OK;
}

menu_api_status menu_api_set(menu_api *api, menu_api_handle handle, int64_t value,
                             menu_api_result *result)
{
    size_t index;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    status = lookup(api, handle, &index);
    if (status == MENU_API_OK) {
        status = write_slot(api, &api->slots[index], value,
                            &result->callback_code);
        result_copy(&api->slots[index], result, status == MENU_API_OK);
    }
    return finish(api, status);
}

menu_api_status menu_api_step(menu_api *api, menu_api_handle handle, int direction,
                              menu_api_result *result)
{
    size_t index;
    int64_t number, step;
    menu_api_slot *slot;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (direction != -1 && direction != 1)
        return finish(api, MENU_API_E_ARGUMENT);
    status = lookup(api, handle, &index);
    if (status != MENU_API_OK)
        return finish(api, status);
    slot = &api->slots[index];
    if (slot->info.kind != MENU_API_NUMERIC) {
        result_copy(slot, result, false);
        return finish(api, MENU_API_E_TYPE);
    }
    status = access_check(api, handle, &result->callback_code);
    if (status == MENU_API_OK)
        status = read_slot(slot, &result->callback_code);
    if (status == MENU_API_OK) {
        number = slot->cached.number;
        step = slot->info.step;
        if ((direction > 0 && (uint64_t)slot->info.maximum - (uint64_t)number <
                              (uint64_t)step) ||
            (direction < 0 && (uint64_t)number - (uint64_t)slot->info.minimum <
                              (uint64_t)step)) {
            status = MENU_API_E_RANGE;
        } else {
            number = direction > 0 ? number + step : number - step;
            status = write_slot(api, slot, number, &result->callback_code);
        }
    }
    result_copy(slot, result, status == MENU_API_OK);
    return finish(api, status);
}

menu_api_status menu_api_invoke(menu_api *api, menu_api_handle handle,
                                menu_api_result *result)
{
    size_t index;
    menu_api_slot *slot;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    status = lookup(api, handle, &index);
    if (status != MENU_API_OK)
        return finish(api, status);
    slot = &api->slots[index];
    if (slot->info.kind != MENU_API_ACTION) {
        result_copy(slot, result, false);
        return finish(api, MENU_API_E_TYPE);
    }
    status = access_check(api, handle, &result->callback_code);
    if (status == MENU_API_OK) {
        result->callback_code = slot->action(slot->userdata);
        if (result->callback_code)
            status = MENU_API_E_CALLBACK;
    }
    return finish(api, status);
}

static void snapshot_clear(menu_api_snapshot *snapshot,
                           menu_api_handle container)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->container = container;
    snapshot->selected_index = MENU_API_NO_SELECTION;
}

static menu_api_status snapshot_build(menu_api *api, menu_api_handle container,
                                      menu_api_snapshot *snapshot,
                                      bool *container_enabled)
{
    size_t indices[MENU_API_NODE_LIMIT], i;
    bool visible, enabled;
    menu_api_status status;
    menu_api_slot *slot;
    menu_api_row *row;
    snapshot_clear(snapshot, container);
    status = container_check(api, container);
    if (status != MENU_API_OK)
        return status;
    status = effective(api, container, &visible, &enabled,
                       &snapshot->callback_code);
    if (status != MENU_API_OK)
        return status;
    if (!visible)
        return MENU_API_E_HIDDEN;
    *container_enabled = enabled;
    snapshot->total_children = child_indices(api, container, indices);
    for (i = 0; i < snapshot->total_children; ++i) {
        bool child_visible = true, child_enabled = enabled;
        slot = &api->slots[indices[i]];
        status = conditions(slot, &child_visible, &child_enabled,
                            &snapshot->callback_code);
        if (status != MENU_API_OK)
            return status;
        if (!child_visible)
            continue;
        if (snapshot->count == MENU_API_VISIBLE_LIMIT)
            return MENU_API_E_LIMIT;
        row = &snapshot->rows[snapshot->count];
        row->info = slot->info;
        row->enabled = child_enabled;
        if (scalar_kind(slot->info.kind) || slot->info.kind == MENU_API_READONLY) {
            status = read_slot(slot, &snapshot->callback_code);
            if (status != MENU_API_OK)
                return status;
            row->value = slot->cached;
        }
        ++snapshot->count;
        if (child_enabled)
            ++snapshot->enabled_count;
    }
    return MENU_API_OK;
}

static void reconcile(menu_api_frame *frame, menu_api_snapshot *snapshot)
{
    size_t i, start;
    for (i = 0; i < snapshot->count; ++i) {
        if (snapshot->rows[i].enabled &&
            snapshot->rows[i].info.handle == frame->selected) {
            frame->selected_index = i;
            snapshot->selected_index = i;
            return;
        }
    }
    start = frame->selected_index;
    if (start == MENU_API_NO_SELECTION)
        start = 0;
    if (snapshot->count && start >= snapshot->count)
        start = snapshot->count - 1u;
    for (i = 0; i < snapshot->count; ++i) {
        size_t index = (start + i) % snapshot->count;
        if (snapshot->rows[index].enabled) {
            frame->selected = snapshot->rows[index].info.handle;
            frame->selected_index = index;
            snapshot->selected_index = index;
            return;
        }
    }
    frame->selected = MENU_API_ROOT;
    frame->selected_index = MENU_API_NO_SELECTION;
}

static menu_api_status snapshot_error(menu_api *api, menu_api_status status,
                                      const menu_api_snapshot *temporary,
                                      menu_api_snapshot *snapshot)
{
    snapshot->callback_code = temporary->callback_code;
    return finish(api, status);
}

menu_api_status menu_api_snapshot_get(menu_api *api, menu_api_handle container,
                                      menu_api_snapshot *snapshot)
{
    menu_api_snapshot temporary;
    bool enabled;
    menu_api_status status;
    if (!snapshot)
        return MENU_API_E_ARGUMENT;
    snapshot_clear(snapshot, container);
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    status = snapshot_build(api, container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    if (api->depth && api->frames[api->depth - 1u].container == container)
        reconcile(&api->frames[api->depth - 1u], &temporary);
    *snapshot = temporary;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_activate(menu_api *api, menu_api_handle container,
                                  menu_api_snapshot *snapshot)
{
    menu_api_snapshot temporary;
    menu_api_frame frame = {container, MENU_API_ROOT, MENU_API_NO_SELECTION};
    bool enabled;
    menu_api_status status;
    if (!snapshot)
        return MENU_API_E_ARGUMENT;
    snapshot_clear(snapshot, container);
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (api->depth)
        return finish(api, MENU_API_E_BUSY);
    status = snapshot_build(api, container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    if (!enabled)
        return finish(api, MENU_API_E_DISABLED);
    reconcile(&frame, &temporary);
    api->frames[0] = frame;
    api->depth = 1;
    *snapshot = temporary;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_move(menu_api *api, int direction,
                              menu_api_snapshot *snapshot)
{
    menu_api_snapshot temporary;
    menu_api_frame frame;
    size_t index;
    bool enabled;
    menu_api_status status;
    if (!snapshot)
        return MENU_API_E_ARGUMENT;
    snapshot_clear(snapshot, MENU_API_ROOT);
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (direction != -1 && direction != 1)
        return finish(api, MENU_API_E_ARGUMENT);
    if (!api->depth)
        return finish(api, MENU_API_E_STATE);
    frame = api->frames[api->depth - 1u];
    status = snapshot_build(api, frame.container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    reconcile(&frame, &temporary);
    api->frames[api->depth - 1u] = frame;
    *snapshot = temporary;
    if (frame.selected_index == MENU_API_NO_SELECTION)
        return finish(api, MENU_API_E_DISABLED);
    index = frame.selected_index;
    while ((direction > 0 && index + 1u < temporary.count) ||
           (direction < 0 && index > 0)) {
        index = direction > 0 ? index + 1u : index - 1u;
        if (temporary.rows[index].enabled) {
            frame.selected = temporary.rows[index].info.handle;
            frame.selected_index = index;
            api->frames[api->depth - 1u] = frame;
            snapshot->selected_index = index;
            return finish(api, MENU_API_OK);
        }
    }
    return finish(api, MENU_API_E_RANGE);
}

menu_api_status menu_api_open(menu_api *api, menu_api_snapshot *snapshot)
{
    menu_api_snapshot temporary;
    menu_api_frame parent, child;
    bool enabled;
    menu_api_status status;
    if (!snapshot)
        return MENU_API_E_ARGUMENT;
    snapshot_clear(snapshot, MENU_API_ROOT);
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (!api->depth)
        return finish(api, MENU_API_E_STATE);
    if (api->depth == MENU_API_NODE_LIMIT)
        return finish(api, MENU_API_E_LIMIT);
    parent = api->frames[api->depth - 1u];
    status = snapshot_build(api, parent.container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    reconcile(&parent, &temporary);
    if (parent.selected_index == MENU_API_NO_SELECTION)
        return finish(api, MENU_API_E_STATE);
    if (!container_kind(temporary.rows[parent.selected_index].info.kind))
        return finish(api, MENU_API_E_TYPE);
    child.container = parent.selected;
    child.selected = MENU_API_ROOT;
    child.selected_index = MENU_API_NO_SELECTION;
    status = snapshot_build(api, child.container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    if (!enabled)
        return finish(api, MENU_API_E_DISABLED);
    reconcile(&child, &temporary);
    api->frames[api->depth - 1u] = parent;
    api->frames[api->depth++] = child;
    *snapshot = temporary;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_back(menu_api *api, menu_api_snapshot *snapshot)
{
    menu_api_snapshot temporary;
    menu_api_frame parent;
    bool enabled;
    menu_api_status status;
    if (!snapshot)
        return MENU_API_E_ARGUMENT;
    snapshot_clear(snapshot, MENU_API_ROOT);
    status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (api->depth <= 1u)
        return finish(api, MENU_API_E_STATE);
    parent = api->frames[api->depth - 2u];
    status = snapshot_build(api, parent.container, &temporary, &enabled);
    if (status != MENU_API_OK)
        return snapshot_error(api, status, &temporary, snapshot);
    reconcile(&parent, &temporary);
    memset(&api->frames[--api->depth], 0, sizeof(api->frames[0]));
    api->frames[api->depth - 1u] = parent;
    *snapshot = temporary;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_deactivate(menu_api *api)
{
    menu_api_status status = begin(api);
    if (status != MENU_API_OK)
        return status;
    if (!api->depth)
        return finish(api, MENU_API_E_STATE);
    memset(api->frames, 0, sizeof(api->frames));
    api->depth = 0;
    return finish(api, MENU_API_OK);
}

menu_api_status menu_api_navigation_get(const menu_api *api,
                                        menu_api_navigation *navigation)
{
    menu_api_status status = context(api);
    if (status != MENU_API_OK)
        return status;
    if (!navigation)
        return MENU_API_E_ARGUMENT;
    memset(navigation, 0, sizeof(*navigation));
    navigation->selected_index = MENU_API_NO_SELECTION;
    navigation->depth = api->depth;
    navigation->active = api->depth != 0;
    if (api->depth) {
        const menu_api_frame *frame = &api->frames[api->depth - 1u];
        navigation->container = frame->container;
        navigation->selected = frame->selected;
        navigation->selected_index = frame->selected_index;
    }
    return MENU_API_OK;
}

static menu_api_status storage_check(const menu_api *api,
                                      const menu_api_storage_image *image,
                                      bool writing,
                                      size_t indices[MENU_API_NODE_LIMIT],
                                      uint32_t *failed_id)
{
    size_t i, j, count = 0;
    menu_api_status status;
    if (image->schema != MENU_API_STORAGE_SCHEMA)
        return MENU_API_E_ARGUMENT;
    if (image->count > MENU_API_NODE_LIMIT)
        return MENU_API_E_LIMIT;
    for (i = 0; i < image->count; ++i) {
        const menu_api_storage_record *record = &image->records[i];
        *failed_id = record->id;
        if (!record->id)
            return MENU_API_E_ARGUMENT;
        for (j = 0; j < i; ++j) {
            if (image->records[j].id == record->id)
                return MENU_API_E_DUPLICATE;
        }
        for (j = 0; j < MENU_API_NODE_LIMIT; ++j) {
            if (api->slots[j].used && api->slots[j].info.id == record->id)
                break;
        }
        if (j == MENU_API_NODE_LIMIT)
            return MENU_API_E_NOT_FOUND;
        if (!scalar_kind(api->slots[j].info.kind) ||
            record->kind != (uint32_t)api->slots[j].info.kind)
            return MENU_API_E_TYPE;
        status = number_check(&api->slots[j], record->number, writing);
        if (status != MENU_API_OK)
            return status;
        indices[i] = j;
    }
    *failed_id = 0;
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        if (api->slots[i].used && scalar_kind(api->slots[i].info.kind))
            ++count;
    }
    return count == image->count ? MENU_API_OK : MENU_API_E_STATE;
}

static menu_api_status storage_finish(menu_api *api, menu_api_status status,
                                      menu_api_storage_result *result)
{
    size_t i;
    result->primary_status = status;
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        const menu_api_slot *slot = &api->slots[i];
        if (!slot->used || !scalar_kind(slot->info.kind))
            continue;
        if (slot->info.id == result->failed_id)
            result_copy(slot, &result->last_accepted, false);
        if (slot->info.id == result->rollback_failed_id)
            result_copy(slot, &result->rollback_last_accepted, false);
    }
    result->last_accepted.callback_code = result->callback_code;
    result->rollback_last_accepted.callback_code = result->rollback_callback_code;
    return finish(api, result->rollback_status == MENU_API_OK ? status :
                                                               MENU_API_E_ROLLBACK);
}

menu_api_status menu_api_storage_save(menu_api *api,
                                      const menu_api_storage_backend *backend,
                                      menu_api_storage_image *image,
                                      menu_api_storage_result *result)
{
    menu_api_storage_image readback = {0};
    size_t indices[MENU_API_NODE_LIMIT], i, j;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK) {
        result->primary_status = status;
        return status;
    }
    if (!backend || !backend->save || !backend->load || !image)
        return storage_finish(api, MENU_API_E_ARGUMENT, result);
    memset(image, 0, sizeof(*image));
    image->schema = MENU_API_STORAGE_SCHEMA;
    for (i = 0; i < MENU_API_NODE_LIMIT; ++i) {
        menu_api_slot *slot = &api->slots[i];
        menu_api_storage_record *record;
        if (!slot->used || !scalar_kind(slot->info.kind))
            continue;
        status = read_slot(slot, &result->callback_code);
        if (status != MENU_API_OK) {
            result->failed_id = slot->info.id;
            return storage_finish(api, status, result);
        }
        record = &image->records[image->count++];
        record->id = slot->info.id;
        record->kind = (uint32_t)slot->info.kind;
        record->number = slot->cached.number;
    }
    result->callback_code = backend->save(backend->userdata, image);
    if (result->callback_code)
        return storage_finish(api, MENU_API_E_CALLBACK, result);
    result->callback_code = backend->load(backend->userdata, &readback);
    if (result->callback_code)
        return storage_finish(api, MENU_API_E_CALLBACK, result);
    status = storage_check(api, &readback, false, indices, &result->failed_id);
    if (status != MENU_API_OK)
        return storage_finish(api, status, result);
    for (i = 0; i < image->count; ++i) {
        for (j = 0; j < readback.count; ++j) {
            if (image->records[i].id == readback.records[j].id)
                break;
        }
        if (j == readback.count ||
            image->records[i].kind != readback.records[j].kind ||
            image->records[i].number != readback.records[j].number) {
            result->failed_id = image->records[i].id;
            return storage_finish(api, MENU_API_E_READBACK, result);
        }
    }
    return storage_finish(api, MENU_API_OK, result);
}

menu_api_status menu_api_storage_load(menu_api *api,
                                      const menu_api_storage_backend *backend,
                                      menu_api_storage_image *image,
                                      menu_api_storage_result *result)
{
    int64_t old_numbers[MENU_API_NODE_LIMIT];
    size_t indices[MENU_API_NODE_LIMIT], i, j;
    menu_api_status status;
    if (!result)
        return MENU_API_E_ARGUMENT;
    memset(result, 0, sizeof(*result));
    status = begin(api);
    if (status != MENU_API_OK) {
        result->primary_status = status;
        return status;
    }
    if (!backend || !backend->load || !image)
        return storage_finish(api, MENU_API_E_ARGUMENT, result);
    memset(image, 0, sizeof(*image));
    result->callback_code = backend->load(backend->userdata, image);
    if (result->callback_code)
        return storage_finish(api, MENU_API_E_CALLBACK, result);
    status = storage_check(api, image, true, indices, &result->failed_id);
    if (status != MENU_API_OK)
        return storage_finish(api, status, result);
    for (i = 0; i < image->count; ++i) {
        menu_api_slot *slot = &api->slots[indices[i]];
        status = access_check(api, slot->info.handle, &result->callback_code);
        if (status != MENU_API_OK) {
            result->failed_id = slot->info.id;
            return storage_finish(api, status, result);
        }
    }
    for (i = 0; i < image->count; ++i) {
        menu_api_slot *slot = &api->slots[indices[i]];
        status = read_slot(slot, &result->callback_code);
        if (status == MENU_API_OK)
            status = number_check(slot, slot->cached.number, true);
        if (status != MENU_API_OK) {
            result->failed_id = slot->info.id;
            return storage_finish(api, status, result);
        }
        old_numbers[i] = slot->cached.number;
    }
    for (i = 0; i < image->count; ++i) {
        status = write_slot(api, &api->slots[indices[i]], image->records[i].number,
                            &result->callback_code);
        if (status == MENU_API_OK) {
            ++result->applied_count;
            continue;
        }
        result->failed_id = image->records[i].id;
        /* The current setter may have succeeded before its readback failed. */
        for (j = i + 1u; j > 0; --j) {
            int callback_code = 0;
            menu_api_status rollback_status;
            menu_api_slot *slot = &api->slots[indices[j - 1u]];
            rollback_status = write_slot(api, slot, old_numbers[j - 1u],
                                         &callback_code);
            if (rollback_status == MENU_API_OK) {
                ++result->rolled_back_count;
            } else if (result->rollback_status == MENU_API_OK) {
                result->rollback_status = rollback_status;
                result->rollback_callback_code = callback_code;
                result->rollback_failed_id = slot->info.id;
            }
        }
        return storage_finish(api, status, result);
    }
    return storage_finish(api, MENU_API_OK, result);
}
