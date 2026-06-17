/*
 *
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <string.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include "peripheral_graph.h"

#define GRAPH_SATURATION_WINDOW_STEPS 64
#define GRAPH_STALE_PIXEL_THRESHOLD 8
#define GRAPH_STALE_WINDOW_LIMIT 4

static struct zmk_widget_peripheral_graph *active_graph;

static const int8_t sin_lut[256] = {
    0,    3,    6,    9,    12,   16,   19,   22,   25,   28,   31,   34,
    37,   40,   43,   46,   49,   51,   54,   57,   60,   63,   65,   68,
    71,   73,   76,   78,   81,   83,   85,   88,   90,   92,   94,   96,
    98,   100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
    117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,
    126,  127,  127,  127,  127,  127,  127,  127,  126,  126,  126,  125,
    125,  124,  123,  122,  122,  121,  120,  118,  117,  116,  115,  113,
    112,  111,  109,  107,  106,  104,  102,  100,  98,   96,   94,   92,
    90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,
    60,   57,   54,   51,   49,   46,   43,   40,   37,   34,   31,   28,
    25,   22,   19,   16,   12,   9,    6,    3,    0,    -3,   -6,   -9,
    -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
    -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,
    -81,  -83,  -85,  -88,  -90,  -92,  -94,  -96,  -98,  -100, -102, -104,
    -106, -107, -109, -111, -112, -113, -115, -116, -117, -118, -120, -121,
    -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122,
    -122, -121, -120, -118, -117, -116, -115, -113, -112, -111, -109, -107,
    -106, -104, -102, -100, -98,  -96,  -94,  -92,  -90,  -88,  -85,  -83,
    -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
    -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,
    -12,  -9,   -6,   -3,
};

static uint32_t next_rand(struct zmk_widget_peripheral_graph *graph) {
    graph->rng ^= graph->rng << 13;
    graph->rng ^= graph->rng >> 17;
    graph->rng ^= graph->rng << 5;

    return graph->rng;
}

static uint8_t rand_range(struct zmk_widget_peripheral_graph *graph,
                          uint8_t min, uint8_t max) {
    return min + (next_rand(graph) % (max - min + 1));
}

static uint16_t rand_range_u16(struct zmk_widget_peripheral_graph *graph,
                               uint16_t min, uint16_t max) {
    return min + (next_rand(graph) % (max - min + 1));
}

static void clear_bitmap(struct zmk_widget_peripheral_graph *graph) {
    memset(graph->art_buf + PERIPHERAL_GRAPH_PALETTE_BYTES, 0xff,
           PERIPHERAL_GRAPH_BITMAP_BYTES);
}

static bool get_pixel(struct zmk_widget_peripheral_graph *graph, int16_t x,
                      int16_t y) {
    if (x < 0 || y < 0 || x >= PERIPHERAL_GRAPH_WIDTH ||
        y >= PERIPHERAL_GRAPH_HEIGHT) {
        return false;
    }

    size_t byte_index = PERIPHERAL_GRAPH_PALETTE_BYTES +
                        (size_t)y * PERIPHERAL_GRAPH_STRIDE_BYTES +
                        ((size_t)x / 8U);
    uint8_t mask = 1U << (7U - ((size_t)x % 8U));

    return (graph->art_buf[byte_index] & mask) == 0;
}

static bool set_pixel(struct zmk_widget_peripheral_graph *graph, int16_t x,
                      int16_t y) {
    if (x < 0 || y < 0 || x >= PERIPHERAL_GRAPH_WIDTH ||
        y >= PERIPHERAL_GRAPH_HEIGHT) {
        return false;
    }

    size_t byte_index = PERIPHERAL_GRAPH_PALETTE_BYTES +
                        (size_t)y * PERIPHERAL_GRAPH_STRIDE_BYTES +
                        ((size_t)x / 8U);
    uint8_t mask = 1U << (7U - ((size_t)x % 8U));

    if (get_pixel(graph, x, y)) {
        return false;
    }

    graph->art_buf[byte_index] &= (uint8_t)~mask;
    return true;
}

static uint16_t draw_line(struct zmk_widget_peripheral_graph *graph, int16_t x0,
                          int16_t y0, int16_t x1, int16_t y1) {
    uint16_t new_pixels = 0;
    int16_t dx = x0 < x1 ? x1 - x0 : x0 - x1;
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = y0 < y1 ? y0 - y1 : y1 - y0;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        if (set_pixel(graph, x0, y0)) {
            new_pixels++;
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int16_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }

    return new_pixels;
}

static int16_t graph_x(const struct zmk_widget_peripheral_graph *graph) {
    const struct graph_params *params = &graph->params;
    int16_t x = PERIPHERAL_GRAPH_WIDTH / 2;

    x += (sin_lut[(uint8_t)(params->ax * graph->t + params->phase_x1)] *
          params->amp_x1) /
         127;
    x += (sin_lut[(uint8_t)(params->bx * graph->t + params->phase_x2)] *
          params->amp_x2) /
         127;

    return x;
}

static int16_t graph_y(const struct zmk_widget_peripheral_graph *graph) {
    const struct graph_params *params = &graph->params;
    int16_t y = PERIPHERAL_GRAPH_HEIGHT / 2;

    y += (sin_lut[(uint8_t)(params->ay * graph->t + params->phase_y1)] *
          params->amp_y1) /
         127;
    y += (sin_lut[(uint8_t)(params->by * graph->t + params->phase_y2)] *
          params->amp_y2) /
         127;

    return y;
}

static uint8_t frequency_choice(struct zmk_widget_peripheral_graph *graph) {
    static const uint8_t choices[] = {2, 3, 4, 5, 7};

    return choices[next_rand(graph) % (sizeof(choices) / sizeof(choices[0]))];
}

static void generate_params(struct zmk_widget_peripheral_graph *graph) {
    struct graph_params *params = &graph->params;

    params->ax = frequency_choice(graph);
    params->ay = frequency_choice(graph);
    if (params->ay == params->ax) {
        params->ay = params->ay == 2 ? 3 : 2;
    }

    params->bx = frequency_choice(graph);
    params->by = frequency_choice(graph);
    if (params->by == params->bx) {
        params->by = params->by == 2 ? 3 : 2;
    }

    params->phase_x1 = (uint8_t)next_rand(graph);
    params->phase_x2 = (uint8_t)next_rand(graph);
    params->phase_y1 = (uint8_t)next_rand(graph);
    params->phase_y2 = (uint8_t)next_rand(graph);

    params->amp_x1 = rand_range(graph, 38, 58);
    params->amp_x2 = rand_range(graph, 6, 18);
    if (params->amp_x1 + params->amp_x2 > 66) {
        params->amp_x2 = 66 - params->amp_x1;
    }

    params->amp_y1 = rand_range(graph, 18, 28);
    params->amp_y2 = rand_range(graph, 4, 10);
    if (params->amp_y1 + params->amp_y2 > 32) {
        params->amp_y2 = 32 - params->amp_y1;
    }

    params->steps_per_keypress = rand_range(graph, 4, 8);
    graph->max_steps = rand_range_u16(graph, 384, 896);
}

static void reset_graph(struct zmk_widget_peripheral_graph *graph) {
    clear_bitmap(graph);
    generate_params(graph);

    graph->t = 0;
    graph->steps_drawn = 0;
    graph->window_steps = 0;
    graph->window_new_pixels = 0;
    graph->stale_windows = 0;
    graph->last_x = graph_x(graph);
    graph->last_y = graph_y(graph);

    set_pixel(graph, graph->last_x, graph->last_y);
}

static void advance_graph(struct zmk_widget_peripheral_graph *graph,
                          uint32_t position) {
    uint16_t new_pixels = 0;
    uint8_t steps = graph->params.steps_per_keypress;

    graph->rng ^=
        position + 0x9e3779b9U + (graph->rng << 6) + (graph->rng >> 2);

    for (uint8_t i = 0; i < steps; i++) {
        graph->t++;
        int16_t x = graph_x(graph);
        int16_t y = graph_y(graph);

        new_pixels += draw_line(graph, graph->last_x, graph->last_y, x, y);
        graph->last_x = x;
        graph->last_y = y;
    }

    graph->steps_drawn += steps;
    graph->window_steps += steps;
    graph->window_new_pixels += new_pixels;

    if (graph->window_steps >= GRAPH_SATURATION_WINDOW_STEPS) {
        if (graph->window_new_pixels < GRAPH_STALE_PIXEL_THRESHOLD) {
            graph->stale_windows++;
        } else if (graph->stale_windows > 0) {
            graph->stale_windows--;
        }

        graph->window_steps = 0;
        graph->window_new_pixels = 0;
    }

    if (graph->steps_drawn >= graph->max_steps ||
        graph->stale_windows >= GRAPH_STALE_WINDOW_LIMIT) {
        reset_graph(graph);
    }

    lv_obj_invalidate(graph->obj);
}

static void init_bitmap(struct zmk_widget_peripheral_graph *graph) {
    if (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED)) {
        graph->art_buf[0] = 0xff;
        graph->art_buf[1] = 0xff;
        graph->art_buf[2] = 0xff;
        graph->art_buf[3] = 0xff;
        graph->art_buf[4] = 0x00;
        graph->art_buf[5] = 0x00;
        graph->art_buf[6] = 0x00;
        graph->art_buf[7] = 0xff;
    } else {
        graph->art_buf[0] = 0x00;
        graph->art_buf[1] = 0x00;
        graph->art_buf[2] = 0x00;
        graph->art_buf[3] = 0xff;
        graph->art_buf[4] = 0xff;
        graph->art_buf[5] = 0xff;
        graph->art_buf[6] = 0xff;
        graph->art_buf[7] = 0xff;
    }

    graph->img.header.cf = LV_IMG_CF_INDEXED_1BIT;
    graph->img.header.always_zero = 0;
    graph->img.header.reserved = 0;
    graph->img.header.w = PERIPHERAL_GRAPH_WIDTH;
    graph->img.header.h = PERIPHERAL_GRAPH_HEIGHT;
    graph->img.data_size = sizeof(graph->art_buf);
    graph->img.data = graph->art_buf;

    clear_bitmap(graph);
}

bool zmk_widget_peripheral_graph_init(struct zmk_widget_peripheral_graph *graph,
                                      lv_obj_t *parent) {
    graph->obj = lv_img_create(parent);
    if (graph->obj == NULL) {
        LOG_ERR("Failed to create graph art image");
        return false;
    }

    graph->rng = 0x6d2b79f5U;

    lv_obj_align(graph->obj, LV_ALIGN_TOP_LEFT, 0, 0);
    init_bitmap(graph);
    reset_graph(graph);
    lv_img_set_src(graph->obj, &graph->img);
    active_graph = graph;

    return true;
}

void zmk_widget_peripheral_graph_keypress_all(uint32_t position) {
    if (active_graph != NULL) {
        advance_graph(active_graph, position);
    }
}

static int graph_keypress_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev =
        as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state ||
        ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_widget_peripheral_graph_keypress_all(ev->position);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_peripheral_graph_keypress, graph_keypress_listener);
ZMK_SUBSCRIPTION(widget_peripheral_graph_keypress, zmk_position_state_changed);
