/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(balloon);
LV_IMG_DECLARE(mountain);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_boids(struct zmk_widget_status *widget);

static int16_t abs_i16(int16_t value) { return value < 0 ? -value : value; }

static int16_t sign_i16(int16_t value) {
    if (value > 0) {
        return 1;
    }
    if (value < 0) {
        return -1;
    }
    return 0;
}

static int16_t clamp_velocity(int16_t value) {
    if (value > PERIPHERAL_BOID_MAX_SPEED) {
        return PERIPHERAL_BOID_MAX_SPEED;
    }
    if (value < -PERIPHERAL_BOID_MAX_SPEED) {
        return -PERIPHERAL_BOID_MAX_SPEED;
    }
    return value;
}

static int16_t wrap_coordinate(int16_t value, int16_t max) {
    if (value < 0) {
        return max - 1;
    }
    if (value >= max) {
        return 0;
    }
    return value;
}

static int16_t random_velocity(void) {
    return (sys_rand32_get() % 3) - 1;
}

static int16_t random_velocity_strong(void) {
    return (sys_rand32_get() % (PERIPHERAL_BOID_MAX_SPEED * 2 + 1)) - PERIPHERAL_BOID_MAX_SPEED;
}

static void reset_stationary_boid(struct peripheral_boid *boid) {
    if (boid->vx == 0 && boid->vy == 0) {
        boid->vx = (sys_rand32_get() & 1) ? 1 : -1;
        boid->vy = ((sys_rand32_get() >> 1) & 1) ? 1 : -1;
    }
}

static void perturb_boids(struct zmk_widget_status *widget, uint32_t position) {
    int16_t impulse_x = (position * 17U) % PERIPHERAL_ART_WIDTH;
    int16_t impulse_y = ((position * 29U) + 11U) % PERIPHERAL_ART_HEIGHT;

    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        int16_t dx = widget->boids[i].x - impulse_x;
        int16_t dy = widget->boids[i].y - impulse_y;

        if (abs_i16(dx) <= 20 && abs_i16(dy) <= 20) {
            widget->boids[i].vx =
                clamp_velocity(widget->boids[i].vx + sign_i16(dx) * 2 + random_velocity_strong());
            widget->boids[i].vy =
                clamp_velocity(widget->boids[i].vy + sign_i16(dy) * 2 + random_velocity_strong());
        }
    }

    for (int i = 0; i < 6; i++) {
        int idx = (position + (uint32_t)(i * 7)) % PERIPHERAL_BOID_COUNT;
        widget->boids[idx].vx =
            clamp_velocity(sign_i16(widget->boids[idx].x - impulse_x) * PERIPHERAL_BOID_MAX_SPEED);
        widget->boids[idx].vy =
            clamp_velocity(sign_i16(widget->boids[idx].y - impulse_y) * PERIPHERAL_BOID_MAX_SPEED);
    }

    draw_boids(widget);
}

static void draw_boids(struct zmk_widget_status *widget) {
    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        if (widget->boids[i].x != widget->boid_prev_x[i] || widget->boids[i].y != widget->boid_prev_y[i]) {
            lv_obj_set_pos(widget->boid_dots[i], widget->boids[i].x, widget->boids[i].y);
            widget->boid_prev_x[i] = widget->boids[i].x;
            widget->boid_prev_y[i] = widget->boids[i].y;
        }
    }
}

static void step_boids(struct zmk_widget_status *widget) {
    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        int neighbor_count = 0;
        int avg_x = 0;
        int avg_y = 0;
        int avg_vx = 0;
        int avg_vy = 0;
        int separation_x = 0;
        int separation_y = 0;
        int16_t cohesion_x = 0;
        int16_t cohesion_y = 0;
        int16_t alignment_x = 0;
        int16_t alignment_y = 0;
        int16_t repel_x = 0;
        int16_t repel_y = 0;

        for (int j = 0; j < PERIPHERAL_BOID_COUNT; j++) {
            if (i == j) {
                continue;
            }

            int dx = widget->boids[j].x - widget->boids[i].x;
            int dy = widget->boids[j].y - widget->boids[i].y;

            if (abs_i16(dx) > 10 || abs_i16(dy) > 10) {
                continue;
            }

            neighbor_count++;
            avg_x += widget->boids[j].x;
            avg_y += widget->boids[j].y;
            avg_vx += widget->boids[j].vx;
            avg_vy += widget->boids[j].vy;

            if (abs_i16(dx) <= 4) {
                separation_x -= dx;
            }
            if (abs_i16(dy) <= 4) {
                separation_y -= dy;
            }
        }

        if (neighbor_count > 0) {
            avg_x /= neighbor_count;
            avg_y /= neighbor_count;
            avg_vx /= neighbor_count;
            avg_vy /= neighbor_count;

            cohesion_x = abs_i16(avg_x - widget->boids[i].x) > 6 ? sign_i16(avg_x - widget->boids[i].x) : 0;
            cohesion_y = abs_i16(avg_y - widget->boids[i].y) > 6 ? sign_i16(avg_y - widget->boids[i].y) : 0;
            alignment_x = sign_i16(avg_vx - widget->boids[i].vx);
            alignment_y = sign_i16(avg_vy - widget->boids[i].vy);
            repel_x = sign_i16(separation_x) * 2;
            repel_y = sign_i16(separation_y) * 2;

            widget->boids[i].vx =
                clamp_velocity(widget->boids[i].vx + cohesion_x + alignment_x + repel_x);
            widget->boids[i].vy =
                clamp_velocity(widget->boids[i].vy + cohesion_y + alignment_y + repel_y);
        } else if ((sys_rand32_get() % 3) == 0) {
            widget->boids[i].vx = clamp_velocity(widget->boids[i].vx + random_velocity_strong());
            widget->boids[i].vy = clamp_velocity(widget->boids[i].vy + random_velocity_strong());
        }

        reset_stationary_boid(&widget->boids[i]);
        widget->boids[i].x =
            wrap_coordinate(widget->boids[i].x + widget->boids[i].vx, PERIPHERAL_ART_WIDTH);
        widget->boids[i].y =
            wrap_coordinate(widget->boids[i].y + widget->boids[i].vy, PERIPHERAL_ART_HEIGHT);
    }
}

static void boids_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_status *widget = timer->user_data;

    if (widget == NULL) {
        return;
    }

    step_boids(widget);
    draw_boids(widget);
}

static void init_boids(struct zmk_widget_status *widget) {
    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        widget->boids[i].x = sys_rand32_get() % PERIPHERAL_ART_WIDTH;
        widget->boids[i].y = sys_rand32_get() % PERIPHERAL_ART_HEIGHT;
        widget->boids[i].vx = random_velocity_strong();
        widget->boids[i].vy = random_velocity_strong();
        widget->boid_prev_x[i] = -1;
        widget->boid_prev_y[i] = -1;
        reset_stationary_boid(&widget->boids[i]);
    }
}

static void init_static_art(struct zmk_widget_status *widget) {
    lv_obj_t *art = lv_img_create(widget->obj);
    bool random = sys_rand32_get() & 1;

    if (art == NULL) {
        LOG_ERR("Failed to create peripheral art image");
        widget->art_obj = NULL;
        return;
    }

    lv_img_set_src(art, random ? &balloon : &mountain);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 0, 0);
    widget->art_obj = art;
    widget->boids_timer = NULL;
}

static bool init_boids_art(struct zmk_widget_status *widget) {
    widget->art_obj = lv_obj_create(widget->obj);
    if (widget->art_obj == NULL) {
        LOG_ERR("Failed to create boids art container");
        return false;
    }

    lv_obj_set_size(widget->art_obj, PERIPHERAL_ART_WIDTH, PERIPHERAL_ART_HEIGHT);
    lv_obj_align(widget->art_obj, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(widget->art_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->art_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->art_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->art_obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(widget->art_obj, LVGL_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(widget->art_obj, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        widget->boid_dots[i] = lv_obj_create(widget->art_obj);
        if (widget->boid_dots[i] == NULL) {
            LOG_ERR("Failed to create boid dot %d", i);
            lv_obj_del(widget->art_obj);
            widget->art_obj = NULL;
            return false;
        }

        lv_obj_set_size(widget->boid_dots[i], 1, 1);
        lv_obj_set_style_radius(widget->boid_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(widget->boid_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(widget->boid_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(widget->boid_dots[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(widget->boid_dots[i], LVGL_FOREGROUND, LV_PART_MAIN);
    }

    init_boids(widget);
    draw_boids(widget);
    widget->boids_timer = lv_timer_create(boids_timer_cb, 250, widget);

    if (widget->boids_timer == NULL) {
        LOG_ERR("Failed to create boids timer");
        lv_obj_del(widget->art_obj);
        widget->art_obj = NULL;
        return false;
    }

    return true;
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

static int boids_keypress_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (!IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS) || ev == NULL || !ev->state ||
        ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { perturb_boids(widget, ev->position); }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_peripheral_boids_keypress, boids_keypress_listener);
ZMK_SUBSCRIPTION(widget_peripheral_boids_keypress, zmk_position_state_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    if (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS)) {
        if (!init_boids_art(widget)) {
            init_static_art(widget);
        }
    } else {
        init_static_art(widget);
    }

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
