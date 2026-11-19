#!/bin/sh
# Restore the Q2's MIPS libcurl runtime from a local sysroot or rootfs.
# Never copy the host Ubuntu libcurl: it is a different CPU architecture.
set -eu
sysroot=${1:-build/sysroot}
requested=${2:-build/q2-rootfs.squashfs}
mkdir -p "$sysroot/lib"
sysroot=$(cd "$sysroot" && pwd)
target="$sysroot/lib/libcurl.so.4"
if [ -f "$target" ]; then
    exit 0
fi
if [ -f "$sysroot/usr/lib/libcurl.so.4" ]; then
    cp -L "$sysroot/usr/lib/libcurl.so.4" "$target"
    echo "Restored $target from sysroot/usr/lib"
    exit 0
fi

rootfs=''
for candidate in "$requested" build/q2-rootfs.squashfs \
    "$(dirname "$sysroot")/q2-rootfs.squashfs" \
    "$(dirname "$sysroot")/rootfs.squashfs"; do
    if [ -f "$candidate" ]; then
        rootfs=$candidate
        break
    fi
done
if [ -z "$rootfs" ]; then
    echo "Missing Q2 MIPS libcurl.so.4 in $sysroot/lib and $sysroot/usr/lib." >&2
    echo "No cached Q2 rootfs.squashfs was found to restore it from." >&2
    echo "Repair your existing sysroot using:" >&2
    echo "  sh tools/sysroot.sh '$sysroot' build/q2-rootfs.squashfs" >&2
    echo "This downloads/extracts the Q2 runtime from the upstream Q2 Pod firmware if needed." >&2
    exit 1
fi
if ! command -v unsquashfs >/dev/null 2>&1; then
    echo "Install squashfs-tools (sudo apt install squashfs-tools)." >&2
    exit 1
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
unsquashfs -q -n -d "$tmp" "$rootfs" usr/lib/libcurl.so.4 usr/lib/libcurl.so.4.* >/dev/null
if [ ! -f "$tmp/usr/lib/libcurl.so.4" ]; then
    echo "Q2 rootfs at $rootfs does not contain usr/lib/libcurl.so.4" >&2
    exit 1
fi
cp -L "$tmp/usr/lib/libcurl.so.4" "$target"
echo "Restored Q2 MIPS libcurl.so.4 from $rootfs"
