/**
 * widget_rpm_bar.c — RPM bar widget with redline, tick marks, and limiter effects.
 * Ported from firmware rendering to WASM preview.
 */
#include "widget_rpm_bar.h"
#include "esp_stubs.h"
#include "signal.h"
#include "font_manager.h"
#include "ui/theme.h"
#include "cJSON.h"
#include "widget_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "w_rpm_bar";

/* ── Limiter flash timer callback ── */
static void _limiter_timer_cb(lv_timer_t *timer) {
    widget_t *w = (widget_t *)timer->user_data;
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    if (!d || !d->bar_obj) return;

    d->limiter_flash_state = !d->limiter_flash_state;
    uint8_t effect = d->limiter_effect;
    bool bar_flash = (effect == 1 || effect == 2 || effect == 4 || effect == 5);
    // bool circle_flash = (effect == 2 || effect == 3 || effect == 5 || effect == 6);
    // Note: effects 4-6 are "solid" variants - use solid instead of toggle

    if (bar_flash) {
        bool is_solid = (effect >= 4);
        bool show_limiter = is_solid || d->limiter_flash_state;
        lv_color_t col = show_limiter ? d->limiter_color : d->bar_color;
        lv_obj_set_style_bg_color(d->bar_obj, col, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    }

    /* RPM lights circles flash - not implemented yet (no lights_circles array) */
}

/* ── Build tick marks on container ── */
static void _build_tick_marks(rpm_bar_data_t *d, lv_obj_t *container,
                              lv_coord_t bar_x_abs, lv_coord_t bar_w_abs,
                              lv_coord_t bar_h_abs) {
    /* Clean up old ticks */
    for (int i = 0; i < d->num_ticks; i++) {
        if (d->tick_lines[i]) { lv_obj_del(d->tick_lines[i]); d->tick_lines[i] = NULL; }
    }
    for (int i = 0; i < d->num_labels; i++) {
        if (d->tick_labels[i]) { lv_obj_del(d->tick_labels[i]); d->tick_labels[i] = NULL; }
    }
    d->num_ticks = 0;
    d->num_labels = 0;

    int increments = 500;
    int num_lines = (d->gauge_max / increments) + 1;
    if (num_lines > MAX_RPM_LINES) num_lines = MAX_RPM_LINES;

    lv_coord_t bar_y_top = 0;
    lv_coord_t bar_y_bot = bar_h_abs - 13;

    /* Use 765/783 logical width ratio from firmware so ticks align correctly
     * and the last number stays visible (bar extends 2.3% beyond ticks) */
    lv_coord_t tick_w = (lv_coord_t)((int32_t)bar_w_abs * 765 / 783);

    for (int i = 0; i < num_lines; i++) {
        int rpm_value = i * increments;
        lv_coord_t x_pos = bar_x_abs + (lv_coord_t)((int64_t)rpm_value * tick_w / d->gauge_max);

        lv_coord_t line_w, line_h;
        bool add_label = false;
        if ((rpm_value % 1000) == 0) {
            line_w = 3; line_h = 12; add_label = true;
        } else {
            line_w = 2; line_h = 8;
        }

        lv_coord_t adj_x = x_pos - (line_w / 2);

        /* Top tick */
        if (d->num_ticks >= MAX_RPM_LINES * 2) break;
        lv_obj_t *tick_top = lv_obj_create(container);
        lv_obj_set_size(tick_top, line_w, line_h);
        lv_obj_set_style_radius(tick_top, 0, 0);
        lv_obj_set_style_bg_color(tick_top, THEME_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(tick_top, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tick_top, 0, 0);
        lv_obj_set_style_pad_all(tick_top, 0, 0);
        lv_obj_clear_flag(tick_top, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tick_top, adj_x, bar_y_top);
        d->tick_lines[d->num_ticks++] = tick_top;

        /* Label for 1000 RPM ticks */
        if (add_label && d->num_labels < MAX_RPM_LINES) {
            lv_obj_t *label = lv_label_create(container);
            char rpm_str[8];
            snprintf(rpm_str, sizeof(rpm_str), "%d", rpm_value / 1000);
            lv_label_set_text(label, rpm_str);
            lv_obj_set_style_text_color(label, THEME_COLOR_BG, 0);
            lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
            lv_obj_set_style_text_font(label, THEME_FONT_DASH_TICK, 0);
            lv_obj_align_to(label, tick_top, LV_ALIGN_OUT_BOTTOM_MID, 0, 7);
            d->tick_labels[d->num_labels++] = label;
        }

        /* Bottom tick */
        if (d->num_ticks >= MAX_RPM_LINES * 2) break;
        lv_obj_t *tick_bot = lv_obj_create(container);
        lv_obj_set_size(tick_bot, line_w, line_h);
        lv_obj_set_style_radius(tick_bot, 0, 0);
        lv_obj_set_style_bg_color(tick_bot, THEME_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(tick_bot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tick_bot, 0, 0);
        lv_obj_set_style_pad_all(tick_bot, 0, 0);
        lv_obj_clear_flag(tick_bot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tick_bot, adj_x, bar_y_bot + (13 - line_h));
        d->tick_lines[d->num_ticks++] = tick_bot;
    }
}

/* ── Update redline overlay position ── */
static void _update_redline(rpm_bar_data_t *d, lv_coord_t bar_h) {
    if (!d->redline_obj) return;

    float pct = (float)d->redline / (float)d->gauge_max;
    if (pct > 1.0f) pct = 1.0f;
    if (pct < 0.0f) pct = 0.0f;

    /* Use tick_w (765/783 ratio) to align redline with tick marks */
    lv_coord_t tick_w = (lv_coord_t)((int32_t)d->bar_w_px * 765 / 783);
    lv_coord_t redline_x = d->bar_x_pos + (lv_coord_t)(pct * tick_w);
    lv_coord_t redline_w = (d->bar_x_pos + d->bar_w_px) - redline_x;

    if (pct >= 1.0f || redline_w <= 0) {
        lv_obj_add_flag(d->redline_obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_coord_t rl_h = bar_h * 12 / 55;
    /* Firmware uses LV_ALIGN_CENTER + y=22 in 55px container.
     * Center-aligned y=22 means: top = (h/2 + 22) - rl_h/2 */
    lv_coord_t rl_y = (bar_h / 2 + (bar_h * 22 / 55)) - rl_h / 2;

    lv_obj_clear_flag(d->redline_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(d->redline_obj, redline_w, rl_h);
    lv_obj_set_pos(d->redline_obj, redline_x, rl_y);
}

/* ── Set RPM value + limiter logic ── */
static void _set_rpm_value(widget_t *w, int rpm) {
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    if (!d || !d->bar_obj) return;

    if (rpm < 0) rpm = 0;
    d->current_rpm = rpm;

    /* Scale RPM to extended bar range (firmware uses 782.5/765 ratio) */
    const float ext_ratio = 782.5f / 765.0f;
    int32_t ext_max = (int32_t)(d->gauge_max * ext_ratio);
    int32_t scaled = (rpm * ext_max) / d->gauge_max;
    lv_bar_set_value(d->bar_obj, scaled, LV_ANIM_OFF);

    /* Limiter effect */
    bool in_limiter = (rpm >= d->limiter_value && d->limiter_effect > 0);
    if (in_limiter) {
        if (!d->limiter_timer) {
            d->limiter_timer = lv_timer_create(_limiter_timer_cb, 100, w);
        } else {
            lv_timer_resume(d->limiter_timer);
        }
    } else {
        if (d->limiter_timer) {
            lv_timer_pause(d->limiter_timer);
        }
        /* Restore bar color */
        lv_obj_set_style_bg_color(d->bar_obj, d->bar_color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        d->limiter_flash_state = false;
    }
}

/* ── Signal callback ── */
static void _rpm_signal_cb(float value, bool is_stale, void *user_data) {
    widget_t *w = (widget_t *)user_data;
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    if (!d->bar_obj) return;

    if (is_stale) {
        _set_rpm_value(w, 0);
        return;
    }
    _set_rpm_value(w, (int)value);
}

/* ── vtable: create ── */
static void _create(widget_t *w, lv_obj_t *parent) {
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;

    /* Container */
    w->root = lv_obj_create(parent);
    lv_obj_set_size(w->root, w->w, w->h);
    lv_obj_set_align(w->root, LV_ALIGN_CENTER);
    lv_obj_set_pos(w->root, w->x, w->y);
    lv_obj_clear_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);

    /* Color indicator panel (Panel9) — firmware uses a square matching container height.
     * Panel9 is 55x55 at 800w/55h. Scale proportionally. */
    lv_coord_t panel_w = w->h;  /* square: width = height, matching firmware */
    d->color_panel_obj = lv_obj_create(w->root);
    lv_obj_set_size(d->color_panel_obj, panel_w, w->h);
    lv_obj_set_pos(d->color_panel_obj, 0, 0);
    lv_obj_clear_flag(d->color_panel_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(d->color_panel_obj, 0, 0);
    lv_obj_set_style_bg_color(d->color_panel_obj, d->bar_color, 0);
    lv_obj_set_style_bg_opa(d->color_panel_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d->color_panel_obj, 0, 0);

    /* RPM bar gauge — fills from panel right edge to container right edge */
    const float ext_ratio = 782.5f / 765.0f;
    int32_t ext_max = (int32_t)(d->gauge_max * ext_ratio);
    lv_coord_t bar_w = w->w - panel_w;

    d->bar_obj = lv_bar_create(w->root);
    lv_bar_set_range(d->bar_obj, 0, ext_max);
    lv_bar_set_value(d->bar_obj, 0, LV_ANIM_OFF);
    lv_obj_set_size(d->bar_obj, bar_w, w->h);
    lv_obj_set_pos(d->bar_obj, panel_w, 0);
    d->bar_x_pos = panel_w;
    d->bar_w_px = bar_w;

    lv_obj_set_style_radius(d->bar_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d->bar_obj, THEME_COLOR_RPM_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d->bar_obj, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(d->bar_obj, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(d->bar_obj, d->bar_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(d->bar_obj, LV_OPA_COVER, LV_PART_INDICATOR);

    /* Redline zone overlay — positioned by _update_redline */
    lv_coord_t rl_h = w->h * 12 / 55;
    /* Firmware: LV_ALIGN_CENTER + y=22 → absolute top = (h/2 + 22*h/55) - rl_h/2 */
    lv_coord_t rl_y = (w->h / 2 + (w->h * 22 / 55)) - rl_h / 2;
    d->redline_obj = lv_obj_create(w->root);
    lv_obj_set_height(d->redline_obj, rl_h);
    lv_obj_set_y(d->redline_obj, rl_y);
    lv_obj_clear_flag(d->redline_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(d->redline_obj, 0, 0);
    lv_obj_set_style_bg_color(d->redline_obj, THEME_COLOR_RED, 0);
    lv_obj_set_style_bg_opa(d->redline_obj, 180, 0);
    lv_obj_set_style_border_width(d->redline_obj, 0, 0);
    _update_redline(d, w->h);

    /* Tick marks — pass bar geometry explicitly (LVGL hasn't laid out yet) */
    _build_tick_marks(d, w->root, panel_w, bar_w, w->h);

    /* Signal subscription */
    if (d->signal_name[0] != '\0') {
        d->signal_index = signal_find_by_name(d->signal_name);
        if (d->signal_index >= 0)
            signal_subscribe(d->signal_index, _rpm_signal_cb, w);
    }
}

/* ── vtable: from_json ── */
static void _from_json(widget_t *w, cJSON *in) {
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    widget_base_from_json(w, in);

    cJSON *cfg = cJSON_GetObjectItemCaseSensitive(in, "config");
    if (!cfg) return;

    cJSON *item;
    /* Accept both "rpm_max" (editor) and "gauge_max" (firmware) */
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "rpm_max")) && cJSON_IsNumber(item))
        d->gauge_max = item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "gauge_max")) && cJSON_IsNumber(item))
        d->gauge_max = item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "redline")) && cJSON_IsNumber(item))
        d->redline = item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "bar_color")) && cJSON_IsNumber(item))
        d->bar_color.full = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "limiter_effect")) && cJSON_IsNumber(item))
        d->limiter_effect = (uint8_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "limiter_value")) && cJSON_IsNumber(item))
        d->limiter_value = item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "limiter_color")) && cJSON_IsNumber(item))
        d->limiter_color.full = (uint16_t)item->valueint;
    if ((item = cJSON_GetObjectItemCaseSensitive(cfg, "signal_name")) && cJSON_IsString(item))
        safe_strncpy(d->signal_name, item->valuestring, sizeof(d->signal_name));

    if (d->signal_name[0] != '\0')
        d->signal_index = signal_find_by_name(d->signal_name);
}

/* ── vtable: to_json ── */
static void _to_json(widget_t *w, cJSON *out) {
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    widget_base_to_json(w, out);
    cJSON *cfg = cJSON_AddObjectToObject(out, "config");
    if (!cfg) return;

    cJSON_AddNumberToObject(cfg, "rpm_max", d->gauge_max);
    cJSON_AddNumberToObject(cfg, "redline", d->redline);
    cJSON_AddNumberToObject(cfg, "bar_color", (int)d->bar_color.full);
    cJSON_AddNumberToObject(cfg, "limiter_effect", d->limiter_effect);
    cJSON_AddNumberToObject(cfg, "limiter_value", d->limiter_value);
    cJSON_AddNumberToObject(cfg, "limiter_color", (int)d->limiter_color.full);
    if (d->signal_name[0])
        cJSON_AddStringToObject(cfg, "signal_name", d->signal_name);
}

/* ── vtable: apply_overrides ── */
static void _apply_overrides(widget_t *w, const rule_override_t *ov, uint8_t count) {
    if (!w || !w->root || !lv_obj_is_valid(w->root)) return;
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    if (!d) return;

    lv_color_t bar_col = d->bar_color;
    lv_color_t lim_col = d->limiter_color;

    for (uint8_t i = 0; i < count; i++) {
        const rule_override_t *o = &ov[i];
        if (strcmp(o->field_name, "bar_color") == 0 && o->value_type == RULE_VAL_COLOR) {
            bar_col.full = (uint16_t)o->value.color;
        } else if (strcmp(o->field_name, "limiter_color") == 0 && o->value_type == RULE_VAL_COLOR) {
            lim_col.full = (uint16_t)o->value.color;
        }
    }

    if (d->bar_obj && lv_obj_is_valid(d->bar_obj)) {
        lv_obj_set_style_bg_color(d->bar_obj, bar_col, LV_PART_INDICATOR);
    }
    if (d->redline_obj && lv_obj_is_valid(d->redline_obj)) {
        lv_obj_set_style_bg_color(d->redline_obj, lim_col, 0);
    }
    if (d->color_panel_obj && lv_obj_is_valid(d->color_panel_obj)) {
        lv_obj_set_style_bg_color(d->color_panel_obj, bar_col, 0);
    }
}

/* ── vtable: resize ── */
static void _resize(widget_t *w, uint16_t nw, uint16_t nh) {
    w->w = nw; w->h = nh;
}

/* ── vtable: destroy ── */
static void _destroy(widget_t *w) {
    rpm_bar_data_t *d = (rpm_bar_data_t *)w->type_data;
    if (d) {
        if (d->signal_index >= 0)
            signal_unsubscribe(d->signal_index, _rpm_signal_cb, w);
        if (d->limiter_timer)
            lv_timer_del(d->limiter_timer);
    }
    widget_rules_free(w);
    if (w->root && lv_obj_is_valid(w->root))
        lv_obj_del(w->root);
    free(d);
    free(w);
}

/* ── Factory ── */
widget_t *widget_rpm_bar_create_instance(void) {
    widget_t *w = calloc(1, sizeof(widget_t));
    rpm_bar_data_t *d = calloc(1, sizeof(rpm_bar_data_t));
    if (!w || !d) { free(w); free(d); return NULL; }

    w->type = WIDGET_RPM_BAR;
    w->type_data = d;
    snprintf(w->id, sizeof(w->id), "rpm_bar_0");
    w->x = 0; w->y = -213;
    w->w = 800; w->h = 55;

    d->gauge_max = 8000;
    d->redline = 6500;
    d->bar_color = lv_color_hex(0x00FF00);
    d->limiter_effect = 0;
    d->limiter_value = 7500;
    d->limiter_color = lv_color_hex(0xFF0000);
    d->signal_index = -1;

    w->create = _create;
    w->resize = _resize;
    w->from_json = _from_json;
    w->to_json = _to_json;
    w->destroy = _destroy;
    w->apply_overrides = _apply_overrides;

    return w;
}
