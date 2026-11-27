#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
lib=(-lcjson)
include=${CJSON_INCLUDE:-/usr/include/cjson}
if [[ ! -f "$include/cJSON.h" ]]; then
    include=managed_components/espressif__cjson/cJSON
    lib=("$include/cJSON.c")
fi
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -I"$include" -Isimulator/compat -Icomponents/loxone -Icomponents/loxone/include \
    tests/touch-commands/test.c components/loxone/{app_loxone_model,app_loxone_home_runtime,panel_profile}.c "${lib[@]}" -lm -o "$tmp/test"
"$tmp/test"
