#!/bin/sh
# Linux development host: C compiler, libcjson-dev, libmbedtls-dev.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
cc -std=gnu11 -Wall -Wextra -Werror -DPROFILE_PATH='"panel.json"' \
  -I"$root/main" -I"$root/tests/panel-config/stubs" \
  ${PANEL_TEST_CFLAGS:--I/usr/include/cjson} \
  "$root/main/panel_config.c" "$root/main/panel_schema.c" "$root/main/panel_secret.c" \
  "$root/tests/panel-config/host.c" "$root/tests/panel-config/test.c" \
  ${PANEL_TEST_LIBS:--lcjson -lmbedcrypto} -lpthread -lm -o panel-test
./panel-test init
if grep -q 'secret-admin\|secret-connection' panel.json panel.json.bak; then exit 1; fi
./panel-test reload
echo incomplete >panel.json
./panel-test recover
echo broken >device.key
./panel-test fail-closed
echo 'Panel settings transaction, recovery and credential tests passed.'
