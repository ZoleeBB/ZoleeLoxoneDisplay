#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
cc -std=gnu11 -Wall -Wextra -Werror -DPANEL_DAILY_BASE='"day"' -I"$root/main" \
    -I"$root/components/loxone/include" -I"$root/components/update/include" \
    -I"$root/tests/daily/stubs" -I"$root/simulator/compat" \
    ${PANEL_TEST_CFLAGS:--I/usr/include/cjson} "$root/tests/daily/test.c" "$root/main/panel_energy_stats.c" \
    -Wl,--wrap=time ${PANEL_DAILY_LIBS:--lcjson} -lm -o daily
./daily measure
# A legutóbbi, 600 másodperces mentés elvesztésekor a korábbi visszaolvasható.
printf broken > day.0
./daily restore
