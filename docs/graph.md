# Peripheral Harmonograph Widget Plan

## Goal

Add a peripheral nice!view art widget inspired by a spirograph, implemented as a phase-walker harmonograph. The widget should draw additively into a 1-bit bitmap as typing happens. It should not animate on a timer; each local keypress advances the drawing by a small number of curve steps.

The visual target is a slowly emerging geometric plotter: loops, knots, petals, and orbital curves that build up over time, then reset with a new randomized configuration once the current drawing is sufficiently complete.

## Rendering Model

Use the same proven mutable bitmap image approach as the fake-code and boids widgets:

- Physical art area: `140x68`
- Indexed 1-bit LVGL image: `LV_IMG_CF_INDEXED_1BIT`
- Row-padded bitmap storage
- Palette bytes followed by bitmap bytes
- `lv_img_dsc_t` backed by widget-owned memory
- `lv_obj_invalidate()` after keypress updates

The renderer should be additive:

- Do not clear the bitmap on every keypress.
- Set pixels into the existing bitmap.
- Clear only when starting a new generated graph.

Draw short line segments between the previous point and current point using integer Bresenham. This avoids dotted-looking curves when a keypress advances multiple steps.

## Widget Files

Add:

```text
boards/shields/nice_view_sellsword/widgets/peripheral_graph.h
boards/shields/nice_view_sellsword/widgets/peripheral_graph.c
```

Public API:

```c
bool zmk_widget_peripheral_graph_init(struct zmk_widget_peripheral_graph *graph,
                                      lv_obj_t *parent);
void zmk_widget_peripheral_graph_keypress_all(uint32_t position);
```

## Config And Selection

Add a Kconfig flag:

```conf
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH=y
```

Compile conditionally in `CMakeLists.txt`:

```cmake
zephyr_library_sources_ifdef(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH widgets/peripheral_graph.c)
```

Peripheral art priority in `peripheral_status.c` should be:

```text
fake-code > graph > boids > static art
```

This keeps the newest explicitly selected visual widget ahead of boids while preserving existing behavior.

Increase `LV_Z_MEM_POOL_SIZE` to `8192` when graph is enabled, matching the other bitmap widgets.

## State

Widget state should include:

```c
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
    uint8_t art_buf[GRAPH_BUFFER_BYTES];
    lv_img_dsc_t img;
};
```

Parameter state:

```c
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
```

Use one active widget pointer, like the current fake-code and boids widgets. The display path creates one peripheral status widget, so a single active pointer is enough.

## Curve Formula

Use fixed-point integer sine lookup instead of floats.

Formula:

```c
x = cx
  + (sin(ax * t + phase_x1) * amp_x1) / 127
  + (sin(bx * t + phase_x2) * amp_x2) / 127;

y = cy
  + (sin(ay * t + phase_y1) * amp_y1) / 127
  + (sin(by * t + phase_y2) * amp_y2) / 127;
```

Constants:

```c
cx = GRAPH_WIDTH / 2;
cy = GRAPH_HEIGHT / 2;
```

Use a 256-entry `int8_t` sine lookup table from `-127..127`. This is fast, deterministic, and small enough for firmware. If flash size becomes a concern later, replace it with a triangle wave or a smaller quarter-wave table.

## Parameter Generation

Use bounded random choices rather than free random values.

Primary frequencies:

```c
uint8_t choices[] = {2, 3, 4, 5, 7};
```

Constraints:

- Prefer unequal `ax`, `ay`.
- Prefer unequal `bx`, `by`.
- Secondary harmonics should be smaller than primary harmonics.
- Keep amplitudes within margins so the drawing stays on-screen.

Suggested ranges:

```c
amp_x1 = 38..58;
amp_x2 = 6..18;
amp_y1 = 18..28;
amp_y2 = 4..10;
```

For a `140x68` display, this keeps curves centered with a small margin.

Steps:

```c
steps_per_keypress = 4..8;
max_steps = 384..896;
```

Seed `rng` from a constant mixed with key positions over time. Each keypress can xor in the local position before advancing, so repeated typing patterns still vary the next generated graph.

## Keypress Behavior

Subscribe to:

```c
zmk_position_state_changed
```

Ignore:

- release events
- non-local events

On each local keypress:

1. Mix the key position into `rng`.
2. Advance `steps_per_keypress` curve steps.
3. Draw a Bresenham line from previous point to current point.
4. Track whether plotted pixels were new.
5. Invalidate the LVGL image.
6. Reset if complete or saturated.

No timer should be used.

## Pixel Tracking

Before setting a pixel, check whether it is already set.

Return `true` when a pixel changes from background to foreground. This lets the widget estimate saturation without scanning the whole bitmap.

Helpers:

```c
static bool get_pixel(...);
static bool set_pixel(...);
static void clear_bitmap(...);
static void draw_line(...);
```

`draw_line()` should count new pixels from each `set_pixel()` call.

## Completion And Reset

Reset when either condition is met:

- `steps_drawn >= max_steps`
- saturation suggests the graph is no longer adding much

Saturation logic:

```c
window_steps += steps_advanced;
window_new_pixels += new_pixels;

if (window_steps >= 64) {
    if (window_new_pixels < 8) {
        stale_windows++;
    } else if (stale_windows > 0) {
        stale_windows--;
    }

    window_steps = 0;
    window_new_pixels = 0;
}

if (stale_windows >= 4) {
    reset_graph();
}
```

This avoids clearing too early while still preventing long periods where keypresses add no visible change.

On reset:

1. Clear bitmap.
2. Generate new parameters.
3. Reset `t`, counters, stale tracking.
4. Compute an initial point.
5. Draw the first pixel.

## Drawing Details

Use physical display coordinates directly for this widget. Unlike fake-code, it does not need text orientation logic.

Pixel behavior:

- background is `0xff` in bitmap data
- foreground clears a bit to `0`
- preserve the same palette convention used by existing art widgets

Bresenham line drawing is important because with `steps_per_keypress > 1`, consecutive generated points may be separated by several pixels.

## Integration Steps

1. Add Kconfig symbol in `Kconfig.shield`.
2. Add `LV_Z_MEM_POOL_SIZE` default in `Kconfig.defconfig`.
3. Add conditional source in `CMakeLists.txt`.
4. Add `peripheral_graph.h`.
5. Add `peripheral_graph.c`.
6. Add graph state to `struct zmk_widget_status` behind config.
7. Update `zmk_widget_status_init()` priority:

```c
#if IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE)
    ...
#elif IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH)
    ...
#elif IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS)
    ...
#else
    init_static_art(widget);
#endif
```

8. Verify config-off builds still use static art.

## Test Matrix

Build configurations:

```conf
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH=y
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS=n
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE=n
```

```conf
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH=y
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE=y
```

Expected: fake-code wins.

```conf
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH=y
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS=y
```

Expected: graph wins.

```conf
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_GRAPH=n
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_BOIDS=n
CONFIG_NICE_VIEW_WIDGET_PERIPHERAL_FAKE_CODE=n
```

Expected: static art wins.

Runtime checks:

- No drawing should happen while idle.
- Each local keypress adds visible curve progress.
- Curves should remain inside the art area.
- Curves should eventually reset.
- Reset should produce a visibly different pattern.
- Battery/link status should remain visible and unaffected.

## Future Tuning

Possible later improvements:

- Add optional center dot or faint origin marker.
- Draw two pixels per plotted point for a bolder curve.
- Add a reset fade approximation by clearing only parts of the bitmap over several keypresses.
- Use key position to influence phase drift or graph family.
- Add a Kconfig option for `steps_per_keypress` if the default feels too slow or too dense.
