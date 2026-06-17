/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

#define PERIPHERAL_ART_WIDTH 140
#define PERIPHERAL_ART_HEIGHT 68
#define PERIPHERAL_BOID_COUNT 20
#define PERIPHERAL_BOID_MAX_SPEED 2
#define PERIPHERAL_ART_BITMAP_BYTES (((PERIPHERAL_ART_WIDTH * PERIPHERAL_ART_HEIGHT) + 7) / 8)
#define PERIPHERAL_ART_PALETTE_BYTES 8
#define PERIPHERAL_ART_BUFFER_BYTES (PERIPHERAL_ART_PALETTE_BYTES + PERIPHERAL_ART_BITMAP_BYTES)

struct peripheral_boid {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
};

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *art_obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
    lv_timer_t *boids_timer;
    struct peripheral_boid boids[PERIPHERAL_BOID_COUNT];
    uint8_t art_buf[PERIPHERAL_ART_BUFFER_BYTES];
    lv_img_dsc_t boids_img;
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
