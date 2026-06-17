/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/events/position_state_changed.h>
#include <zmk/event_manager.h>

#include "peripheral_fake_code.h"

#define FAKE_CODE_LINE_HEIGHT 7
#define FAKE_CODE_INDENT_WIDTH 8
#define FAKE_CODE_MAX_INDENT 4

static struct zmk_widget_peripheral_fake_code *active_fake_code;

static uint32_t next_rand(struct zmk_widget_peripheral_fake_code *fake_code) {
    fake_code->rng ^= fake_code->rng << 13;
    fake_code->rng ^= fake_code->rng >> 17;
    fake_code->rng ^= fake_code->rng << 5;

    return fake_code->rng;
}

static void clear_bitmap(struct zmk_widget_peripheral_fake_code *fake_code) {
    memset(fake_code->art_buf + PERIPHERAL_FAKE_CODE_PALETTE_BYTES, 0xff,
           PERIPHERAL_FAKE_CODE_BITMAP_BYTES);
}

static void set_pixel(struct zmk_widget_peripheral_fake_code *fake_code, int16_t x, int16_t y) {
    if (x < 0 || y < 0 || x >= PERIPHERAL_FAKE_CODE_WIDTH || y >= PERIPHERAL_FAKE_CODE_HEIGHT) {
        return;
    }

    size_t byte_index = PERIPHERAL_FAKE_CODE_PALETTE_BYTES +
                        (size_t)y * PERIPHERAL_FAKE_CODE_STRIDE_BYTES + ((size_t)x / 8U);
    uint8_t mask = 1U << (7U - ((size_t)x % 8U));

    fake_code->art_buf[byte_index] &= (uint8_t)~mask;
}

static void clear_rect(struct zmk_widget_peripheral_fake_code *fake_code, uint8_t x, uint8_t y,
                       uint8_t width, uint8_t height) {
    for (uint8_t yy = y; yy < y + height && yy < PERIPHERAL_FAKE_CODE_HEIGHT; yy++) {
        for (uint8_t xx = x; xx < x + width && xx < PERIPHERAL_FAKE_CODE_WIDTH; xx++) {
            size_t byte_index = PERIPHERAL_FAKE_CODE_PALETTE_BYTES +
                                (size_t)yy * PERIPHERAL_FAKE_CODE_STRIDE_BYTES + (xx / 8U);
            uint8_t mask = 1U << (7U - (xx % 8U));

            fake_code->art_buf[byte_index] |= mask;
        }
    }
}

static void draw_hline(struct zmk_widget_peripheral_fake_code *fake_code, uint8_t x, uint8_t y,
                       uint8_t width) {
    for (uint8_t i = 0; i < width; i++) {
        set_pixel(fake_code, x + i, y);
    }
}

static void draw_vline(struct zmk_widget_peripheral_fake_code *fake_code, uint8_t x, uint8_t y,
                       uint8_t height) {
    for (uint8_t i = 0; i < height; i++) {
        set_pixel(fake_code, x, y + i);
    }
}

static void scroll_up(struct zmk_widget_peripheral_fake_code *fake_code) {
    uint8_t *bitmap = fake_code->art_buf + PERIPHERAL_FAKE_CODE_PALETTE_BYTES;
    size_t line_bytes = FAKE_CODE_LINE_HEIGHT * PERIPHERAL_FAKE_CODE_STRIDE_BYTES;
    size_t remaining = PERIPHERAL_FAKE_CODE_BITMAP_BYTES - line_bytes;

    memmove(bitmap, bitmap + line_bytes, remaining);
    memset(bitmap + remaining, 0xff, line_bytes);
}

static void newline(struct zmk_widget_peripheral_fake_code *fake_code) {
    fake_code->cursor_y += FAKE_CODE_LINE_HEIGHT;
    if (fake_code->cursor_y + FAKE_CODE_LINE_HEIGHT > PERIPHERAL_FAKE_CODE_HEIGHT) {
        scroll_up(fake_code);
        fake_code->cursor_y = PERIPHERAL_FAKE_CODE_HEIGHT - FAKE_CODE_LINE_HEIGHT;
    }

    fake_code->cursor_x = fake_code->indent * FAKE_CODE_INDENT_WIDTH;
    clear_rect(fake_code, fake_code->cursor_x, fake_code->cursor_y,
               PERIPHERAL_FAKE_CODE_WIDTH - fake_code->cursor_x, FAKE_CODE_LINE_HEIGHT);
}

static void ensure_room(struct zmk_widget_peripheral_fake_code *fake_code, uint8_t width) {
    if (fake_code->cursor_x + width >= PERIPHERAL_FAKE_CODE_WIDTH) {
        newline(fake_code);
    }
}

static void draw_word(struct zmk_widget_peripheral_fake_code *fake_code, uint8_t width) {
    uint8_t y = fake_code->cursor_y + 3;
    uint8_t x = fake_code->cursor_x;
    uint8_t run_start = x;
    uint8_t run_width = 0;

    ensure_room(fake_code, width + 3);

    for (uint8_t i = 0; i < width; i++) {
        bool gap = (i > 1 && i + 1 < width && (next_rand(fake_code) % 7) == 0);
        if (gap) {
            if (run_width > 0) {
                draw_hline(fake_code, run_start, y, run_width);
                run_width = 0;
            }
            continue;
        }

        if (run_width == 0) {
            run_start = x + i;
        }
        run_width++;

        if ((next_rand(fake_code) % 9) == 0) {
            set_pixel(fake_code, x + i, y - 1);
        }
        if ((next_rand(fake_code) % 13) == 0) {
            set_pixel(fake_code, x + i, y + 1);
        }
    }

    if (run_width > 0) {
        draw_hline(fake_code, run_start, y, run_width);
    }

    fake_code->cursor_x += width + 2 + (next_rand(fake_code) % 2);
}

static void draw_punctuation(struct zmk_widget_peripheral_fake_code *fake_code) {
    uint8_t kind = next_rand(fake_code) % 6;
    uint8_t x = fake_code->cursor_x;
    uint8_t y = fake_code->cursor_y + 2;

    ensure_room(fake_code, 5);

    switch (kind) {
    case 0:
        set_pixel(fake_code, x, y + 2);
        break;
    case 1:
        set_pixel(fake_code, x, y + 1);
        set_pixel(fake_code, x + 1, y + 1);
        break;
    case 2:
        draw_vline(fake_code, x, y, 4);
        break;
    case 3:
        draw_hline(fake_code, x, y, 3);
        draw_hline(fake_code, x, y + 3, 3);
        break;
    case 4:
        set_pixel(fake_code, x, y);
        set_pixel(fake_code, x + 1, y + 1);
        set_pixel(fake_code, x, y + 2);
        break;
    default:
        set_pixel(fake_code, x, y);
        set_pixel(fake_code, x, y + 2);
        break;
    }

    fake_code->cursor_x += 3 + (next_rand(fake_code) % 2);
}

static void draw_comment(struct zmk_widget_peripheral_fake_code *fake_code) {
    uint8_t width = 24 + (next_rand(fake_code) % 28);

    ensure_room(fake_code, width + 4);
    draw_hline(fake_code, fake_code->cursor_x, fake_code->cursor_y + 3, 2);
    fake_code->cursor_x += 4;
    draw_word(fake_code, width);
}

static void open_block(struct zmk_widget_peripheral_fake_code *fake_code) {
    draw_punctuation(fake_code);
    if (fake_code->indent < FAKE_CODE_MAX_INDENT) {
        fake_code->indent++;
    }
    newline(fake_code);
}

static void close_block(struct zmk_widget_peripheral_fake_code *fake_code) {
    if (fake_code->indent > 0) {
        fake_code->indent--;
    }
    newline(fake_code);
    draw_punctuation(fake_code);
}

static void draw_next_token(struct zmk_widget_peripheral_fake_code *fake_code, uint32_t position) {
    fake_code->rng ^= position + 0x9e3779b9U;
    uint8_t choice = next_rand(fake_code) % 100;

    if (choice < 66) {
        static const uint8_t widths[] = {3, 4, 5, 7, 9, 12, 15, 18};
        draw_word(fake_code, widths[next_rand(fake_code) % (sizeof(widths) / sizeof(widths[0]))]);
    } else if (choice < 78) {
        draw_punctuation(fake_code);
    } else if (choice < 86) {
        newline(fake_code);
    } else if (choice < 91) {
        open_block(fake_code);
    } else if (choice < 95) {
        close_block(fake_code);
    } else {
        draw_comment(fake_code);
    }

    lv_obj_invalidate(fake_code->obj);
}

static void init_bitmap(struct zmk_widget_peripheral_fake_code *fake_code) {
    if (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)) {
        fake_code->art_buf[0] = 0xff;
        fake_code->art_buf[1] = 0xff;
        fake_code->art_buf[2] = 0xff;
        fake_code->art_buf[3] = 0xff;
        fake_code->art_buf[4] = 0x00;
        fake_code->art_buf[5] = 0x00;
        fake_code->art_buf[6] = 0x00;
        fake_code->art_buf[7] = 0xff;
    } else {
        fake_code->art_buf[0] = 0x00;
        fake_code->art_buf[1] = 0x00;
        fake_code->art_buf[2] = 0x00;
        fake_code->art_buf[3] = 0xff;
        fake_code->art_buf[4] = 0xff;
        fake_code->art_buf[5] = 0xff;
        fake_code->art_buf[6] = 0xff;
        fake_code->art_buf[7] = 0xff;
    }

    fake_code->img.header.cf = LV_IMG_CF_INDEXED_1BIT;
    fake_code->img.header.always_zero = 0;
    fake_code->img.header.reserved = 0;
    fake_code->img.header.w = PERIPHERAL_FAKE_CODE_WIDTH;
    fake_code->img.header.h = PERIPHERAL_FAKE_CODE_HEIGHT;
    fake_code->img.data_size = sizeof(fake_code->art_buf);
    fake_code->img.data = fake_code->art_buf;

    clear_bitmap(fake_code);
}

bool zmk_widget_peripheral_fake_code_init(struct zmk_widget_peripheral_fake_code *fake_code,
                                          lv_obj_t *parent) {
    fake_code->obj = lv_img_create(parent);
    if (fake_code->obj == NULL) {
        LOG_ERR("Failed to create fake code art image");
        return false;
    }

    fake_code->cursor_x = 0;
    fake_code->cursor_y = 0;
    fake_code->indent = 0;
    fake_code->rng = 0xa5a55a5aU;

    lv_obj_align(fake_code->obj, LV_ALIGN_TOP_LEFT, 0, 0);
    init_bitmap(fake_code);
    lv_img_set_src(fake_code->obj, &fake_code->img);
    active_fake_code = fake_code;

    return true;
}

void zmk_widget_peripheral_fake_code_keypress_all(uint32_t position) {
    if (active_fake_code != NULL) {
        draw_next_token(active_fake_code, position);
    }
}

static int fake_code_keypress_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state || ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_widget_peripheral_fake_code_keypress_all(ev->position);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_peripheral_fake_code_keypress, fake_code_keypress_listener);
ZMK_SUBSCRIPTION(widget_peripheral_fake_code_keypress, zmk_position_state_changed);
