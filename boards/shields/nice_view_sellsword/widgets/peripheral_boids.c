/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/events/position_state_changed.h>
#include <zmk/event_manager.h>

#include "peripheral_boids.h"

static struct zmk_widget_peripheral_boids *active_boids;

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

static int16_t random_velocity(void) { return (sys_rand32_get() % 3) - 1; }

static int16_t random_velocity_strong(void) {
    return (sys_rand32_get() % (PERIPHERAL_BOID_MAX_SPEED * 2 + 1)) - PERIPHERAL_BOID_MAX_SPEED;
}

static int16_t center_pull(int16_t value, int16_t center, int16_t deadzone) {
    int16_t delta = center - value;

    return abs_i16(delta) > deadzone ? sign_i16(delta) : 0;
}

static void reset_stationary_boid(struct peripheral_boid *boid) {
    if (boid->vx == 0 && boid->vy == 0) {
        boid->vx = (sys_rand32_get() & 1) ? 1 : -1;
        boid->vy = ((sys_rand32_get() >> 1) & 1) ? 1 : -1;
    }
}

static void clear_bitmap(struct zmk_widget_peripheral_boids *boids) {
    memset(boids->art_buf + PERIPHERAL_ART_PALETTE_BYTES, 0xff, PERIPHERAL_ART_BITMAP_BYTES);
}

static void set_pixel(struct zmk_widget_peripheral_boids *boids, int16_t x, int16_t y) {
    size_t byte_index = PERIPHERAL_ART_PALETTE_BYTES + (size_t)y * PERIPHERAL_ART_STRIDE_BYTES +
                        ((size_t)x / 8U);
    uint8_t mask = 1U << (7U - ((size_t)x % 8U));

    boids->art_buf[byte_index] &= (uint8_t)~mask;
}

static void draw_boids(struct zmk_widget_peripheral_boids *boids) {
    clear_bitmap(boids);

    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        set_pixel(boids, boids->boids[i].x, boids->boids[i].y);
    }

    lv_obj_invalidate(boids->obj);
}

static void perturb_boids(struct zmk_widget_peripheral_boids *boids, uint32_t position) {
    int16_t impulse_x = (position * 17U) % PERIPHERAL_ART_WIDTH;
    int16_t impulse_y = ((position * 29U) + 11U) % PERIPHERAL_ART_HEIGHT;

    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        int16_t dx = boids->boids[i].x - impulse_x;
        int16_t dy = boids->boids[i].y - impulse_y;

        if (abs_i16(dx) <= 18 && abs_i16(dy) <= 18) {
            boids->boids[i].vx =
                clamp_velocity(boids->boids[i].vx + sign_i16(dx) + random_velocity());
            boids->boids[i].vy =
                clamp_velocity(boids->boids[i].vy + sign_i16(dy) + random_velocity());
        }
    }

    for (int i = 0; i < 4; i++) {
        int idx = (position + (uint32_t)(i * 7)) % PERIPHERAL_BOID_COUNT;
        boids->boids[idx].vx =
            clamp_velocity(sign_i16(boids->boids[idx].x - impulse_x) * PERIPHERAL_BOID_MAX_SPEED);
        boids->boids[idx].vy =
            clamp_velocity(sign_i16(boids->boids[idx].y - impulse_y) * PERIPHERAL_BOID_MAX_SPEED);
    }

    draw_boids(boids);
}

static void step_boids(struct zmk_widget_peripheral_boids *boids) {
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

            int dx = boids->boids[j].x - boids->boids[i].x;
            int dy = boids->boids[j].y - boids->boids[i].y;

            if (abs_i16(dx) > 20 || abs_i16(dy) > 20) {
                continue;
            }

            neighbor_count++;
            avg_x += boids->boids[j].x;
            avg_y += boids->boids[j].y;
            avg_vx += boids->boids[j].vx;
            avg_vy += boids->boids[j].vy;

            if (abs_i16(dx) <= 3) {
                separation_x -= dx;
            }
            if (abs_i16(dy) <= 3) {
                separation_y -= dy;
            }
        }

        if (neighbor_count > 0) {
            avg_x /= neighbor_count;
            avg_y /= neighbor_count;
            avg_vx /= neighbor_count;
            avg_vy /= neighbor_count;

            cohesion_x =
                abs_i16(avg_x - boids->boids[i].x) > 3 ? sign_i16(avg_x - boids->boids[i].x) : 0;
            cohesion_y =
                abs_i16(avg_y - boids->boids[i].y) > 3 ? sign_i16(avg_y - boids->boids[i].y) : 0;
            alignment_x = avg_vx == 0 ? 0 : sign_i16(avg_vx);
            alignment_y = avg_vy == 0 ? 0 : sign_i16(avg_vy);
            repel_x = sign_i16(separation_x);
            repel_y = sign_i16(separation_y);

            boids->boids[i].vx =
                clamp_velocity(boids->boids[i].vx + cohesion_x + alignment_x + repel_x);
            boids->boids[i].vy =
                clamp_velocity(boids->boids[i].vy + cohesion_y + alignment_y + repel_y);
        } else {
            boids->boids[i].vx =
                clamp_velocity(boids->boids[i].vx +
                               center_pull(boids->boids[i].x, PERIPHERAL_ART_WIDTH / 2, 28));
            boids->boids[i].vy =
                clamp_velocity(boids->boids[i].vy +
                               center_pull(boids->boids[i].y, PERIPHERAL_ART_HEIGHT / 2, 14));
        }

        reset_stationary_boid(&boids->boids[i]);
        boids->boids[i].x =
            wrap_coordinate(boids->boids[i].x + boids->boids[i].vx, PERIPHERAL_ART_WIDTH);
        boids->boids[i].y =
            wrap_coordinate(boids->boids[i].y + boids->boids[i].vy, PERIPHERAL_ART_HEIGHT);
    }
}

static void timer_cb(lv_timer_t *timer) {
    struct zmk_widget_peripheral_boids *boids = timer->user_data;

    if (boids == NULL) {
        return;
    }

    step_boids(boids);
    draw_boids(boids);
}

static void init_boids(struct zmk_widget_peripheral_boids *boids) {
    const int columns = 5;
    const int rows = 4;
    const int x_margin = 8;
    const int y_margin = 6;
    const int x_span = PERIPHERAL_ART_WIDTH - x_margin * 2;
    const int y_span = PERIPHERAL_ART_HEIGHT - y_margin * 2;

    for (int i = 0; i < PERIPHERAL_BOID_COUNT; i++) {
        int col = i % columns;
        int row = i / columns;

        boids->boids[i].x = x_margin + (col * x_span) / (columns - 1);
        boids->boids[i].y = y_margin + (row * y_span) / (rows - 1);
        boids->boids[i].vx = ((i + 1) & 1) ? 1 : -1;
        boids->boids[i].vy = ((i / 2) & 1) ? 1 : -1;
        reset_stationary_boid(&boids->boids[i]);
    }
}

static void init_bitmap(struct zmk_widget_peripheral_boids *boids) {
    if (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)) {
        boids->art_buf[0] = 0xff;
        boids->art_buf[1] = 0xff;
        boids->art_buf[2] = 0xff;
        boids->art_buf[3] = 0xff;
        boids->art_buf[4] = 0x00;
        boids->art_buf[5] = 0x00;
        boids->art_buf[6] = 0x00;
        boids->art_buf[7] = 0xff;
    } else {
        boids->art_buf[0] = 0x00;
        boids->art_buf[1] = 0x00;
        boids->art_buf[2] = 0x00;
        boids->art_buf[3] = 0xff;
        boids->art_buf[4] = 0xff;
        boids->art_buf[5] = 0xff;
        boids->art_buf[6] = 0xff;
        boids->art_buf[7] = 0xff;
    }

    boids->img.header.cf = LV_IMG_CF_INDEXED_1BIT;
    boids->img.header.always_zero = 0;
    boids->img.header.reserved = 0;
    boids->img.header.w = PERIPHERAL_ART_WIDTH;
    boids->img.header.h = PERIPHERAL_ART_HEIGHT;
    boids->img.data_size = sizeof(boids->art_buf);
    boids->img.data = boids->art_buf;

    clear_bitmap(boids);
}

bool zmk_widget_peripheral_boids_init(struct zmk_widget_peripheral_boids *boids, lv_obj_t *parent) {
    boids->obj = lv_img_create(parent);
    if (boids->obj == NULL) {
        LOG_ERR("Failed to create boids art image");
        return false;
    }

    lv_obj_align(boids->obj, LV_ALIGN_TOP_LEFT, 0, 0);

    init_bitmap(boids);
    lv_img_set_src(boids->obj, &boids->img);
    init_boids(boids);
    draw_boids(boids);
    boids->timer = lv_timer_create(timer_cb, 250, boids);

    if (boids->timer == NULL) {
        LOG_ERR("Failed to create boids timer");
        lv_obj_del(boids->obj);
        boids->obj = NULL;
        return false;
    }

    active_boids = boids;

    return true;
}

void zmk_widget_peripheral_boids_perturb_all(uint32_t position) {
    if (active_boids != NULL) {
        perturb_boids(active_boids, position);
    }
}

static int boids_keypress_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state || ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_widget_peripheral_boids_perturb_all(ev->position);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_peripheral_boids_keypress, boids_keypress_listener);
ZMK_SUBSCRIPTION(widget_peripheral_boids_keypress, zmk_position_state_changed);
