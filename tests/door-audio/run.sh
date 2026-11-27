#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/door-audio/stubs -Itests/daily/stubs -Isimulator/compat -I"${CJSON_INCLUDE:-/usr/include/cjson}" \
    -Icomponents/board/include -Icomponents/board -Icomponents/loxone/include -Icomponents/door_media -Icomponents/door_media/include -Icomponents/update/include \
    tests/door-audio/worker.c components/board/{board_sound,board_click}.c components/door_media/{app_intercom_audio,app_intercom_audio_protocol}.c components/update/tones/app_ringtone_wav.c \
    -Wl,--wrap=calloc,--wrap=free -pthread -o "$binary"
"$binary"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/door-audio/touch-stubs -Isimulator/compat -Icomponents/board -Icomponents/board/include \
    tests/door-audio/touch_sensor.c components/board/{board_touch,board_click}.c -o "$binary"
"$binary"
