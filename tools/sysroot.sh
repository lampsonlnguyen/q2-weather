#!/bin/sh
# Build a Q2 glibc 2.28 sysroot using Debian's headers and Q2 Pod's Q2 runtime.
set -eu
out=${1:-build/sysroot}
rootfs=${2:-build/q2-rootfs.squashfs}
mkdir -p "$out/lib" build
out=$(cd "$out" && pwd)
if [ ! -f "$rootfs" ]; then
    curl -fL --retry 3 --connect-timeout 20 \
        https://github.com/DiamondBond/q2-pod/releases/latest/download/update.tar \
        -o build/q2-update.tar
    tar -xOf build/q2-update.tar recovery-update/rootfs.squashfs > "$rootfs"
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
while read -r checksum relative; do
    curl -fsSL --retry 3 "https://archive.debian.org/debian/pool/main/$relative" -o "$tmp/p.deb"
    if [ "${#checksum}" -ne 64 ]; then
        printf 'Pin this Debian package SHA-256: %s  %s\\n' "$(sha256sum "$tmp/p.deb" | cut -d' ' -f1)" "$relative" >&2
        exit 1
    fi
    printf '%s  %s\\n' "$checksum" "$tmp/p.deb" | sha256sum -c --quiet
    ( cd "$tmp" && ar x p.deb data.tar.xz && tar -xJf data.tar.xz -C "$out" ./usr/include )
    rm -f "$tmp/p.deb" "$tmp/data.tar.xz"
done <<'PACKAGES'
e28bac8637e98d78045f0fd05613ed2ab298fb1aa6387633b6d609119774d590 g/glibc/libc6-dev_2.28-10+deb10u1_mipsel.deb
5f6ca1c34bb326b38b1ed051c33a2591bf7bfa26276e54fc85c00427c l/linux/linux-libc-dev_4.19.249-2_mipsel.deb
PACKAGES
cp "$out/usr/include/mipsel-linux-gnu/gnu/stubs-o32_hard.h" \
    "$out/usr/include/mipsel-linux-gnu/gnu/stubs-o32_hard_2008.h"
unsquashfs -q -n -d "$tmp/root" "$rootfs" lib usr/lib >/dev/null
for lib in lib/ld-linux-mipsn8.so.1 lib/libc.so.6 lib/libpthread.so.0 \
    lib/libm.so.6 lib/libgcc_s.so.1 usr/lib/libcurl.so.4; do
    test -f "$tmp/root/$lib" || { echo "Missing Q2 runtime library: $lib" >&2; exit 1; }
    cp -L "$tmp/root/$lib" "$out/lib/"
done
echo "$out"
