#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <input.png> <output.c> [symbol_name]" >&2
    exit 1
fi

input_png=$1
output_c=$2
symbol_name=${3:-$(basename "${output_c%.c}")}

if [[ ! -f "$input_png" ]]; then
    echo "Input file not found: $input_png" >&2
    exit 1
fi

if ! command -v npx >/dev/null 2>&1; then
    echo "npx is required. Install Node.js and npm first." >&2
    exit 1
fi

mkdir -p "$(dirname "$output_c")"

exec npx --yes lv_img_conv \
    "$input_png" \
    -o "$output_c" \
    -f c \
    -cf CF_ALPHA_1_BIT \
    --name "$symbol_name"
