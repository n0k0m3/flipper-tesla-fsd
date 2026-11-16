#!/usr/bin/env bash
# Build the BLE board firmware for one or more boards.
#
#   tools/build-boards.sh                 every file in boards/
#   tools/build-boards.sh esp32-mcp2515   only the named builds
#
# Each build has its own directory and sdkconfig: build/<build-id>/. The app
# image goes to build/out/scrl-<build-id>.bin (BLE update), and the full flash
# image (bootloader, partition table, otadata, app at offset 0) to
# build/out/scrl-<build-id>-full.bin (first flash over USB).
#
# Needs ESP-IDF v5.5 in the environment (. $IDF_PATH/export.sh).
set -euo pipefail

cd "$(dirname "$0")/.."
command -v idf.py >/dev/null || { echo "idf.py not found: run . \$IDF_PATH/export.sh first" >&2; exit 1; }

if [ $# -eq 0 ]; then
    set -- $(ls boards/*.defaults | xargs -n1 basename | sed 's/\.defaults$//')
fi

mkdir -p build/out
failed=()
for id in "$@"; do
    defaults="boards/$id.defaults"
    [ -f "$defaults" ] || { echo "no $defaults" >&2; failed+=("$id"); continue; }
    target=$(sed -n 's/^CONFIG_IDF_TARGET="\(.*\)"/\1/p' "$defaults")
    dir="build/$id"
    echo "=== $id ($target)"
    if idf.py -B "$dir" -D IDF_TARGET="$target" -D SDKCONFIG="$dir/sdkconfig" \
            -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;$defaults" build >"$dir.log" 2>&1; then
        cp "$dir/scrl-ble-board.bin" "build/out/scrl-$id.bin"
        (cd "$dir" && esptool.py --chip "$target" merge_bin -o "../out/scrl-$id-full.bin" \
            @flash_args >/dev/null)
        size=$(stat -c %s "build/out/scrl-$id.bin")
        echo "    ok: build/out/scrl-$id.bin ($size bytes)"
    else
        echo "    FAILED: see $dir.log" >&2
        failed+=("$id")
    fi
done

if [ ${#failed[@]} -gt 0 ]; then
    echo "failed: ${failed[*]}" >&2
    exit 1
fi
