/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define PERIPHERAL_ART_WIDTH 140
#define PERIPHERAL_ART_HEIGHT 68
#define PERIPHERAL_BOID_COUNT 20
#define PERIPHERAL_BOID_MAX_SPEED 2
#define PERIPHERAL_ART_STRIDE_BYTES ((PERIPHERAL_ART_WIDTH + 7) / 8)
#define PERIPHERAL_ART_BITMAP_BYTES (PERIPHERAL_ART_STRIDE_BYTES * PERIPHERAL_ART_HEIGHT)
#define PERIPHERAL_ART_PALETTE_BYTES 8
#define PERIPHERAL_ART_BUFFER_BYTES (PERIPHERAL_ART_PALETTE_BYTES + PERIPHERAL_ART_BITMAP_BYTES)

struct peripheral_boid {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
};

struct zmk_widget_peripheral_boids {
    lv_obj_t *obj;
    lv_timer_t *timer;
    struct peripheral_boid boids[PERIPHERAL_BOID_COUNT];
    uint8_t art_buf[PERIPHERAL_ART_BUFFER_BYTES];
    lv_img_dsc_t img;
};

bool zmk_widget_peripheral_boids_init(struct zmk_widget_peripheral_boids *boids, lv_obj_t *parent);
void zmk_widget_peripheral_boids_perturb_all(uint32_t position);
