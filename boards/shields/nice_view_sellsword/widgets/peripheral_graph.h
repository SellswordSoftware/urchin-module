/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define PERIPHERAL_GRAPH_WIDTH 140
#define PERIPHERAL_GRAPH_HEIGHT 68
#define PERIPHERAL_GRAPH_STRIDE_BYTES ((PERIPHERAL_GRAPH_WIDTH + 7) / 8)
#define PERIPHERAL_GRAPH_BITMAP_BYTES                                          \
    (PERIPHERAL_GRAPH_STRIDE_BYTES * PERIPHERAL_GRAPH_HEIGHT)
#define PERIPHERAL_GRAPH_PALETTE_BYTES 8
#define PERIPHERAL_GRAPH_BUFFER_BYTES                                          \
    (PERIPHERAL_GRAPH_PALETTE_BYTES + PERIPHERAL_GRAPH_BITMAP_BYTES)

struct graph_params {
    uint8_t ax;
    uint8_t bx;
    uint8_t ay;
    uint8_t by;
    uint8_t phase_x1;
    uint8_t phase_x2;
    uint8_t phase_y1;
    uint8_t phase_y2;
    uint8_t amp_x1;
    uint8_t amp_x2;
    uint8_t amp_y1;
    uint8_t amp_y2;
    uint8_t steps_per_keypress;
};

struct zmk_widget_peripheral_graph {
    lv_obj_t *obj;
    uint32_t rng;
    uint16_t t;
    uint16_t steps_drawn;
    uint16_t max_steps;
    uint16_t window_steps;
    uint16_t window_new_pixels;
    uint8_t stale_windows;
    int16_t last_x;
    int16_t last_y;
    struct graph_params params;
    uint8_t art_buf[PERIPHERAL_GRAPH_BUFFER_BYTES];
    lv_img_dsc_t img;
};

bool zmk_widget_peripheral_graph_init(struct zmk_widget_peripheral_graph *graph,
                                      lv_obj_t *parent);
void zmk_widget_peripheral_graph_keypress_all(uint32_t position);
