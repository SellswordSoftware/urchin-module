/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define PERIPHERAL_FAKE_CODE_WIDTH 140
#define PERIPHERAL_FAKE_CODE_HEIGHT 68
#define PERIPHERAL_FAKE_CODE_LOGICAL_WIDTH PERIPHERAL_FAKE_CODE_HEIGHT
#define PERIPHERAL_FAKE_CODE_LOGICAL_HEIGHT PERIPHERAL_FAKE_CODE_WIDTH
#define PERIPHERAL_FAKE_CODE_STRIDE_BYTES ((PERIPHERAL_FAKE_CODE_WIDTH + 7) / 8)
#define PERIPHERAL_FAKE_CODE_BITMAP_BYTES                                                        \
    (PERIPHERAL_FAKE_CODE_STRIDE_BYTES * PERIPHERAL_FAKE_CODE_HEIGHT)
#define PERIPHERAL_FAKE_CODE_PALETTE_BYTES 8
#define PERIPHERAL_FAKE_CODE_BUFFER_BYTES                                                         \
    (PERIPHERAL_FAKE_CODE_PALETTE_BYTES + PERIPHERAL_FAKE_CODE_BITMAP_BYTES)

struct zmk_widget_peripheral_fake_code {
    lv_obj_t *obj;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t indent;
    uint32_t rng;
    uint8_t art_buf[PERIPHERAL_FAKE_CODE_BUFFER_BYTES];
    lv_img_dsc_t img;
};

bool zmk_widget_peripheral_fake_code_init(struct zmk_widget_peripheral_fake_code *fake_code,
                                          lv_obj_t *parent);
void zmk_widget_peripheral_fake_code_keypress_all(uint32_t position);
