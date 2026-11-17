#!/bin/sh
# Fetch the pinned nouveau kernel sources that nvkm is built from.
#
# Only drivers/gpu/drm/nouveau (plus one DisplayPort constants header) from
# one Linux release tag is checked out (blobless, sparse), into ./nouveau. The tree is used unmodified; anything
# this port needs changed lives in src_driver/lkpi or in nouveau_patches/.
#
# v6.6 is the last LTS before GSP-RM support was merged into nvkm, and it
# still performs DisplayPort link training inside nvkm, which suits a port
# that brings its own (non-DRM) modesetting client.
set -eu
TAG=${NOUVEAU_LINUX_TAG:-v6.6}
EXPECT=${NOUVEAU_LINUX_COMMIT:-ffc253263a1375a65fa6c9f62a893e9767fbebfa}
URL=${NOUVEAU_LINUX_URL:-https://github.com/torvalds/linux}
root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/nouveau"

if [ -f "$dest/.pinned" ] && [ "$(cat "$dest/.pinned")" = "$EXPECT" ]; then
    echo "nouveau already at $TAG ($EXPECT)"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git clone --quiet --depth 1 --branch "$TAG" --filter=blob:none --sparse "$URL" "$tmp/linux"
git -C "$tmp/linux" sparse-checkout set drivers/gpu/drm/nouveau include/drm/display
got=$(git -C "$tmp/linux" rev-parse HEAD)
if [ "$got" != "$EXPECT" ]; then
    echo "fetched $TAG is $got, expected $EXPECT; refusing to use it" >&2
    exit 1
fi
rm -rf "$dest"
mkdir -p "$dest"
src="$tmp/linux/drivers/gpu/drm/nouveau"
cp -r "$src/nvkm" "$src/nvif" "$src/include" "$dest/"
# DisplayPort DPCD register constants (permissively licensed, constants only).
mkdir -p "$dest/linux-include/drm/display"
cp "$tmp/linux/include/drm/display/drm_dp.h" "$dest/linux-include/drm/display/"
# Local changes are kept as patches so the pinned tree stays reviewable.
for p in "$root"/nouveau_patches/*.patch; do
    [ -f "$p" ] || continue
    patch -s -p1 -d "$dest" < "$p"
    echo "applied $(basename "$p")"
done
echo "$got" > "$dest/.pinned"
echo "nouveau $TAG ($got) -> $dest"
