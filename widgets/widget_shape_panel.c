/**
 * widget_shape_panel.c — Decorative rectangle/shape widget.
 * Ported from firmware with proper RGB565 color handling and defaults-only JSON.
 */
#include "widget_shape_panel.h"
#include "esp_stubs.h"
#include "ui/theme.h"
#include "cJSON.h"
#include "widget_rules.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "w_shape_panel";

/* ── Default values (matching firmware) ── */
#define DEF_BG_COLOR      0x1A1A1A
#define DEF_BG_OPA        255
#define DEF_BORDER_COLOR  0x2E2F2E
#define DEF_BORDER_WIDTH  0
#define DEF_BORDER_RADIUS 10
#define DEF_SHADOW_WIDTH  0
#define DEF_SHADOW_COLOR  0x000000
#define DEF_SHADOW_OPA    128
#define DEF_SHADOW_OFS_X  0
#define DEF_SHADOW_OFS_Y  0

/* ── RGB565 color helpers (match firmware exactly) ── */
static inline uint32_t _color_to_u32(lv_color_t c) {
    return (uint32_t)c.full;
}

static inline lv_color_t _u32_to_color(uint32_t v) {
    lv_color_t c;
    c.full = (uint16_t)v;
    return c;
}

/* ── vtable: create ── */
static void _create(widget_t *w, lv_obj_t *parent) {
    shape_panel_data_t *d = (shape_panel_data_t *)w->type_data;
    if (!d) return;

    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w->w, w->h);
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_set_pos(obj, w->x, w->y);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    /* Background */
    lv_obj_set_style_bg_color(obj, d->bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, d->bg_opa, LV_PART_MAIN);

    /* Border */
    lv_obj_set_style_border_color(obj, d->border_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, d->border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, d->border_radius, LV_PART_MAIN);

    /* Shadow */
    lv_obj_set_style_shadow_width(obj, d->shadow_width, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(obj, d->shadow_color, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, d->shadow_opa, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_x(obj, d->shadow_ofs_x, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(obj, d->shadow_ofs_y, LV_PART_MAIN);

    w->root = obj;
}

/* ── vtable: resize ── */
static void _resize(widget_t *w, uint16_t nw, uint16_t nh) {
    if (w->root && lv_obj_is_valid(w->root))
        lv_obj_set_size(w->root, nw, nh);
    w->w = nw;
    w->h = nh;
}

/* ── vtable: from_json (RGB565 values via _u32_to_color) ── */
static void _from_json(widget_t *w, cJSON *in) {
    shape_panel_data_t *d = (shape_panel_data_t *)w->type_data;
    widget_base_from_json(w, in);
    if (!d) return;

    cJSON *cfg = cJSON_GetObjectItemCaseSensitive(in, "config");
    if (!cfg) return;

    cJSON *item;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "bg_color")) && cJSON_IsNumber(item))
        d->bg_color = _u32_to_color((uint32_t)item->valueint);
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "bg_opa")) && cJSON_IsNumber(item))
        d->bg_opa = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "border_color")) && cJSON_IsNumber(item))
        d->border_color = _u32_to_color((uint32_t)item->valueint);
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "border_width")) && cJSON_IsNumber(item))
        d->border_width = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "border_radius")) && cJSON_IsNumber(item))
        d->border_radius = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "shadow_width")) && cJSON_IsNumber(item))
        d->shadow_width = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "shadow_color")) && cJSON_IsNumber(item))
        d->shadow_color = _u32_to_color((uint32_t)item->valueint);
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "shadow_opa")) && cJSON_IsNumber(item))
        d->shadow_opa = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "shadow_ofs_x")) && cJSON_IsNumber(item))
        d->shadow_ofs_x = (int8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "shadow_ofs_y")) && cJSON_IsNumber(item))
        d->shadow_ofs_y = (int8_t)item->valueint;
}

/* ── vtable: to_json (defaults-only serialization) ── */
static void _to_json(widget_t *w, cJSON *out) {
    shape_panel_data_t *d = (shape_panel_data_t *)w->type_data;
    widget_base_to_json(w, out);
    if (!d) return;

    cJSON *cfg = cJSON_AddObjectToObject(out, "config");
    if (!cfg) return;

    uint32_t col;

    col = _color_to_u32(d->bg_color);
    if (col != DEF_BG_COLOR)
        cJSON_AddNumberToObject(cfg, "bg_color", col);
    if (d->bg_opa != DEF_BG_OPA)
        cJSON_AddNumberToObject(cfg, "bg_opa", d->bg_opa);

    col = _color_to_u32(d->border_color);
    if (col != DEF_BORDER_COLOR)
        cJSON_AddNumberToObject(cfg, "border_color", col);
    if (d->border_width != DEF_BORDER_WIDTH)
        cJSON_AddNumberToObject(cfg, "border_width", d->border_width);
    if (d->border_radius != DEF_BORDER_RADIUS)
        cJSON_AddNumberToObject(cfg, "border_radius", d->border_radius);

    if (d->shadow_width != DEF_SHADOW_WIDTH)
        cJSON_AddNumberToObject(cfg, "shadow_width", d->shadow_width);
    col = _color_to_u32(d->shadow_color);
    if (col != DEF_SHADOW_COLOR)
        cJSON_AddNumberToObject(cfg, "shadow_color", col);
    if (d->shadow_opa != DEF_SHADOW_OPA)
        cJSON_AddNumberToObject(cfg, "shadow_opa", d->shadow_opa);
    if (d->shadow_ofs_x != DEF_SHADOW_OFS_X)
        cJSON_AddNumberToObject(cfg, "shadow_ofs_x", d->shadow_ofs_x);
    if (d->shadow_ofs_y != DEF_SHADOW_OFS_Y)
        cJSON_AddNumberToObject(cfg, "shadow_ofs_y", d->shadow_ofs_y);
}

/* ── vtable: apply_overrides ── */
static void _apply_overrides(widget_t *w, const rule_override_t *ov, uint8_t count) {
    if (!w || !w->root || !lv_obj_is_valid(w->root)) return;
    shape_panel_data_t *d = (shape_panel_data_t *)w->type_data;
    if (!d) return;

    lv_color_t bg = d->bg_color;
    uint8_t bg_opa = d->bg_opa;
    lv_color_t bdr = d->border_color;
    uint8_t bdr_w = d->border_width;
    uint8_t radius = d->border_radius;
    lv_color_t shd = d->shadow_color;
    uint8_t shd_w = d->shadow_width;
    uint8_t shd_opa = d->shadow_opa;

    for (uint8_t i = 0; i < count; i++) {
        const rule_override_t *o = &ov[i];
        if (strcmp(o->field_name, "bg_color") == 0 && o->value_type == RULE_VAL_COLOR) {
            bg.full = (uint16_t)o->value.color;
        } else if (strcmp(o->field_name, "bg_opa") == 0 && o->value_type == RULE_VAL_NUMBER) {
            bg_opa = (uint8_t)o->value.num;
        } else if (strcmp(o->field_name, "border_color") == 0 && o->value_type == RULE_VAL_COLOR) {
            bdr.full = (uint16_t)o->value.color;
        } else if (strcmp(o->field_name, "border_width") == 0 && o->value_type == RULE_VAL_NUMBER) {
            bdr_w = (uint8_t)o->value.num;
        } else if (strcmp(o->field_name, "border_radius") == 0 && o->value_type == RULE_VAL_NUMBER) {
            radius = (uint8_t)o->value.num;
        } else if (strcmp(o->field_name, "shadow_color") == 0 && o->value_type == RULE_VAL_COLOR) {
            shd.full = (uint16_t)o->value.color;
        } else if (strcmp(o->field_name, "shadow_width") == 0 && o->value_type == RULE_VAL_NUMBER) {
            shd_w = (uint8_t)o->value.num;
        } else if (strcmp(o->field_name, "shadow_opa") == 0 && o->value_type == RULE_VAL_NUMBER) {
            shd_opa = (uint8_t)o->value.num;
        }
    }

    lv_obj_set_style_bg_color(w->root, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->root, bg_opa, LV_PART_MAIN);
    lv_obj_set_style_border_color(w->root, bdr, LV_PART_MAIN);
    lv_obj_set_style_border_width(w->root, bdr_w, LV_PART_MAIN);
    lv_obj_set_style_radius(w->root, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(w->root, shd, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(w->root, shd_w, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(w->root, shd_opa, LV_PART_MAIN);
}

/* ── vtable: destroy ── */
static void _destroy(widget_t *w) {
    if (!w) return;
    widget_rules_free(w);
    if (w->root && lv_obj_is_valid(w->root))
        lv_obj_del(w->root);
    w->root = NULL;
    if (w->type_data) free(w->type_data);
    free(w);
}

/* ── Factory ── */
widget_t *widget_shape_panel_create_instance(uint8_t slot) {
    widget_t *w = calloc(1, sizeof(widget_t));
    shape_panel_data_t *d = calloc(1, sizeof(shape_panel_data_t));
    if (!w || !d) { free(w); free(d); return NULL; }

    /* Set defaults using lv_color_hex (RGB888 constants -> RGB565 internal) */
    d->bg_color      = lv_color_hex(DEF_BG_COLOR);
    d->bg_opa        = DEF_BG_OPA;
    d->border_color  = lv_color_hex(DEF_BORDER_COLOR);
    d->border_width  = DEF_BORDER_WIDTH;
    d->border_radius = DEF_BORDER_RADIUS;
    d->shadow_width  = DEF_SHADOW_WIDTH;
    d->shadow_color  = lv_color_hex(DEF_SHADOW_COLOR);
    d->shadow_opa    = DEF_SHADOW_OPA;
    d->shadow_ofs_x  = DEF_SHADOW_OFS_X;
    d->shadow_ofs_y  = DEF_SHADOW_OFS_Y;

    w->type      = WIDGET_SHAPE_PANEL;
    w->slot      = slot;
    w->x         = 0;
    w->y         = 0;
    w->w         = 150;
    w->h         = 80;
    w->type_data = d;
    snprintf(w->id, sizeof(w->id), "shape_panel_%u", slot);

    w->create        = _create;
    w->resize        = _resize;
    w->from_json     = _from_json;
    w->to_json       = _to_json;
    w->destroy       = _destroy;
    w->apply_overrides = _apply_overrides;

    return w;
}
