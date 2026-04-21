/**
 * widget_indicator.c — Turn signal indicator widget (max 2).
 * Displays a turn signal arrow that blinks when activated via signal.
 */
#include "widget_indicator.h"
#include "esp_stubs.h"
#include "signal.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "cJSON.h"
#include "widget_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "w_indicator";

#define INDICATOR_COLOR_ON    lv_color_hex(0x00FF00)
#define INDICATOR_COLOR_OFF   lv_color_hex(0x1A1A1A)
#define INDICATOR_OPA_ACTIVE  255
#define INDICATOR_OPA_BLINK   128
#define INDICATOR_OPA_INACTIVE 50

/* ── Animation timer callback ── */
static void _anim_timer_cb(lv_timer_t *timer) {
    widget_t *w = (widget_t *)timer->user_data;
    indicator_data_t *d = (indicator_data_t *)w->type_data;
    if (!d || !d->indicator_obj) return;

    if (!d->current_state) {
        /* Inactive — pause timer and set dim */
        lv_obj_set_style_opa(d->indicator_obj, INDICATOR_OPA_INACTIVE, 0);
        lv_timer_pause(d->anim_timer);
        return;
    }

    /* Toggle blink state */
    d->anim_on = !d->anim_on;
    uint8_t opa = d->anim_on ? INDICATOR_OPA_ACTIVE : INDICATOR_OPA_BLINK;
    lv_obj_set_style_opa(d->indicator_obj, opa, 0);
}

/* ── Signal callback ── */
static void _indicator_signal_cb(float value, bool is_stale, void *user_data) {
    widget_t *w = (widget_t *)user_data;
    indicator_data_t *d = (indicator_data_t *)w->type_data;
    if (!d || !d->indicator_obj) return;

    bool new_state = (!is_stale && value >= 0.5f);

    if (new_state == d->current_state) return; /* no change */
    d->current_state = new_state;

    if (new_state) {
        /* Activate: full opacity, start blink timer */
        lv_obj_set_style_opa(d->indicator_obj, INDICATOR_OPA_ACTIVE, 0);
        d->anim_on = true;
        if (d->animation_enabled && d->anim_timer) {
            lv_timer_resume(d->anim_timer);
        }
    } else {
        /* Deactivate: dim opacity, pause blink */
        lv_obj_set_style_opa(d->indicator_obj, INDICATOR_OPA_INACTIVE, 0);
        d->anim_on = false;
        if (d->anim_timer) {
            lv_timer_pause(d->anim_timer);
        }
    }
}

/* ── vtable: create ── */
static void _create(widget_t *w, lv_obj_t *parent) {
    indicator_data_t *d = (indicator_data_t *)w->type_data;

    /* Container */
    w->root = lv_obj_create(parent);
    lv_obj_set_size(w->root, w->w, w->h);
    lv_obj_set_align(w->root, LV_ALIGN_CENTER);
    lv_obj_set_pos(w->root, w->x, w->y);
    lv_obj_clear_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);

    /* Indicator visual — image-based arrow from compiled assets */
    d->indicator_obj = lv_img_create(w->root);
    const lv_img_dsc_t *src = (d->slot == 0)
        ? &ui_img_indicator_left_png
        : &ui_img_indicator_right_png;
    lv_img_set_src(d->indicator_obj, src);
    lv_obj_center(d->indicator_obj);

    /* Scale image to fill widget size */
    uint16_t zoom_x = (uint16_t)((uint32_t)w->w * 256 / src->header.w);
    uint16_t zoom_y = (uint16_t)((uint32_t)w->h * 256 / src->header.h);
    uint16_t zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;
    lv_img_set_zoom(d->indicator_obj, zoom);

    /* Recolor to green when active (start inactive/dim) */
    lv_obj_set_style_img_recolor(d->indicator_obj, INDICATOR_COLOR_ON, 0);
    lv_obj_set_style_img_recolor_opa(d->indicator_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(d->indicator_obj, INDICATOR_OPA_INACTIVE, 0);

    /* Create blink animation timer (starts paused) */
    d->anim_timer = lv_timer_create(_anim_timer_cb, 500, w);
    lv_timer_pause(d->anim_timer);
    d->anim_on = false;

    /* Signal subscription */
    if (d->signal_name[0] != '\0') {
        d->signal_index = signal_find_by_name(d->signal_name);
        if (d->signal_index >= 0)
            signal_subscribe(d->signal_index, _indicator_signal_cb, w);
    }
}

/* ── vtable: from_json ── */
static void _from_json(widget_t *w, cJSON *in) {
    indicator_data_t *d = (indicator_data_t *)w->type_data;
    widget_base_from_json(w, in);

    cJSON *cfg = cJSON_GetObjectItemCaseSensitive(in, "config");
    if (!cfg) return;

    cJSON *item;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "slot")) && cJSON_IsNumber(item)) {
        d->slot = (uint8_t)item->valueint;
        w->slot = d->slot;
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "input_source")) && cJSON_IsNumber(item))
        d->input_source = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "animation")) && cJSON_IsBool(item))
        d->animation_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "is_momentary")) && cJSON_IsBool(item))
        d->is_momentary = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "signal_name")) && cJSON_IsString(item))
        safe_strncpy(d->signal_name, item->valuestring, sizeof(d->signal_name));

    if (d->signal_name[0] != '\0')
        d->signal_index = signal_find_by_name(d->signal_name);
}

/* ── vtable: to_json ── */
static void _to_json(widget_t *w, cJSON *out) {
    indicator_data_t *d = (indicator_data_t *)w->type_data;
    widget_base_to_json(w, out);
    cJSON *cfg = cJSON_AddObjectToObject(out, "config");
    if (!cfg) return;

    cJSON_AddNumberToObject(cfg, "slot", d->slot);
    if (d->input_source != 0)
        cJSON_AddNumberToObject(cfg, "input_source", d->input_source);
    if (!d->animation_enabled)
        cJSON_AddBoolToObject(cfg, "animation", d->animation_enabled);
    if (!d->is_momentary)
        cJSON_AddBoolToObject(cfg, "is_momentary", d->is_momentary);
    if (d->signal_name[0])
        cJSON_AddStringToObject(cfg, "signal_name", d->signal_name);
}

/* ── vtable: resize ── */
static void _resize(widget_t *w, uint16_t nw, uint16_t nh) {
    w->w = nw; w->h = nh;
}

/* ── vtable: destroy ── */
static void _destroy(widget_t *w) {
    indicator_data_t *d = (indicator_data_t *)w->type_data;
    if (d) {
        if (d->signal_index >= 0)
            signal_unsubscribe(d->signal_index, _indicator_signal_cb, w);
        if (d->anim_timer)
            lv_timer_del(d->anim_timer);
    }
    widget_rules_free(w);
    if (w->root && lv_obj_is_valid(w->root))
        lv_obj_del(w->root);
    free(d);
    free(w);
}

/* ── Factory ── */
widget_t *widget_indicator_create_instance(uint8_t slot) {
    widget_t *w = calloc(1, sizeof(widget_t));
    indicator_data_t *d = calloc(1, sizeof(indicator_data_t));
    if (!w || !d) { free(w); free(d); return NULL; }

    uint8_t s = slot & 1;
    w->type = WIDGET_INDICATOR;
    w->type_data = d;
    w->slot = s;
    snprintf(w->id, sizeof(w->id), "indicator_%u", s);
    w->x = (s == 0) ? -95 : 95;
    w->y = -133;
    w->w = 50; w->h = 50;

    d->slot = s;
    d->input_source = 0;
    d->animation_enabled = true;
    d->is_momentary = true;
    d->current_state = false;
    d->signal_index = -1;

    w->create = _create;
    w->resize = _resize;
    w->from_json = _from_json;
    w->to_json = _to_json;
    w->destroy = _destroy;

    return w;
}
