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
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE)
#include "peripheral_fake_code.h"
#endif
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS)
#include "peripheral_boids.h"
#endif
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH)
#include "peripheral_graph.h"
#endif

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE)
    struct zmk_widget_peripheral_fake_code fake_code;
#endif
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS)
    struct zmk_widget_peripheral_boids boids;
#endif
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH)
    struct zmk_widget_peripheral_graph graph;
#endif
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
