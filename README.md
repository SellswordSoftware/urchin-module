# urchin-module
My own zmk module for my urchin keyboard

## Converting Monochrome PNG Art for LVGL 8

This repo includes a small wrapper around LVGL's image converter so you can turn a monochrome PNG into an LVGL v8-compatible `CF_ALPHA_1_BIT` C array without using Python.

Usage:

```bash
./scripts/convert-art.sh path/to/input.png path/to/output.c [symbol_name]
```

Example:

```bash
./scripts/convert-art.sh assets/mountain.png boards/shields/nice_view_sellsword/widgets/mountain.c mountain
```

Notes:

- Requires `npx` from Node.js/npm.
- Uses `lv_img_conv` via `npx`, output format `c`, and LVGL color format `CF_ALPHA_1_BIT`.
- If `symbol_name` is omitted, the script uses the output filename without the `.c` extension.
- The source PNG should already be prepared as clean monochrome art at the target display size.
