#!/bin/sh
# Build Chez Scheme as a position-independent kernel for Hyprscheme.
#
# Result: build/chez/{kernel objects, boot files, scheme.h}
#
# Uses an upstream --pic flag if the Chez checkout has it; otherwise falls
# back to CFLAGS injection, which works on the same versions.

set -e

CHEZ_SOURCE=${CHEZ_SOURCE:-"$PWD/build/chez-src"}
CHEZ_OUT=${CHEZ_OUT:-"$PWD/build/chez"}
CHEZ_REF=${CHEZ_REF:-"v10.3.0"}

if [ -f "$CHEZ_OUT/petite.boot" ] && [ -f "$CHEZ_OUT/kernel.o" ]; then
    echo "chez: already built at $CHEZ_OUT (rm -rf it to rebuild)"
    exit 0
fi

if [ ! -d "$CHEZ_SOURCE" ]; then
    echo "cloning Chez Scheme ($CHEZ_REF)..."
    git clone --depth 1 --branch "$CHEZ_REF" https://github.com/cisco/ChezScheme.git "$CHEZ_SOURCE"
fi
cd "$CHEZ_SOURCE"

git submodule update --init stex zuo 2>/dev/null || true

# the build bootstraps itself from the portable boot files shipped in the
# repository; no installed Chez is required
if grep -q -- '--pic)' configure 2>/dev/null; then
    echo "chez: using the upstream --pic flag"
    CFLAGS="-O2" ./configure --pic --threads
else
    echo "chez: injecting -fPIC via CFLAGS (upstream --pic flag not present)"
    CFLAGS="-fPIC -O2" ./configure --threads
fi

make -j"$(nproc)"

mkdir -p "$CHEZ_OUT"
# workarea layout differs across versions (c/ vs ta6le/c/); find the artifacts
for f in $(find . -path "*ta6le/c/*.o" -o -path "./c/*.o" 2>/dev/null | grep -v "main.o" | sort -u); do
    cp "$f" "$CHEZ_OUT/"
done
rm -f "$CHEZ_OUT/main.o"
for f in $(find . -name "petite.boot" -o -name "scheme.boot" -o -name "scheme.h" 2>/dev/null | grep -vE "pb/|bin/" | sort -u); do
    cp "$f" "$CHEZ_OUT/"
done
LZ4O=$(find . -name "lz4.o" 2>/dev/null | head -1)
[ -n "$LZ4O" ] && cp "$LZ4O" "$CHEZ_OUT/lz4.o"

# single-file artifact: the same objects as one static archive
ar rcs "$CHEZ_OUT/libchez-pic.a" "$CHEZ_OUT"/*.o
# and a relocatable combined object, drop-in compatible with the old
# kernel.o layout
ld -r -o "$CHEZ_OUT/kernel.o" "$CHEZ_OUT"/*.o

# kernel objects from a given Chez version need THAT build's boot files
echo "chez: PIC kernel + boot files at $CHEZ_OUT"
