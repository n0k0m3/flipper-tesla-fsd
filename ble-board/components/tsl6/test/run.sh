#!/bin/sh
# Build the TSL6 engine for the host (32-bit) and replay a capture.
#   test/run.sh CAPTURE.log[.gz] [HEX56]
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/tsl6-replay
rm -rf "$out"
mkdir -p "$out"
for f in "$here"/vendor/m*/*.c "$here"/port/*.c "$here"/gen/*.c "$here"/test/replay.c; do
    mod=$(dirname "$f")
    gcc -m32 -O1 -g -fsigned-char -fno-strict-aliasing -w -fsanitize=address,undefined \
        -I"$here/vendor/include" -I"$here/port" -I"$mod" -c "$f" -o "$out/$(basename "$mod")_$(basename "$f").o"
done
gcc -m32 -c "$here/gen/ram_regions.S" -o "$out/ram_regions.o"
gcc -m32 -fsanitize=address,undefined -o "$out/replay" "$out"/*.o
cap=$1
shift
case "$cap" in
*.gz) gzip -dc "$cap" > "$out/capture.log"; cap=$out/capture.log ;;
esac
exec "$out/replay" "$cap" "$@"
