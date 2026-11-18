#!/usr/bin/env bash
set -euo pipefail

model=${1:?ad5x or ad5m required}
archive=${2:?SDK archive required}
case "$model" in ad5x|ad5m) ;; *) echo "Invalid model: $model" >&2; exit 1 ;; esac
: "${GUPPYSCREEN_VERSION:?GUPPYSCREEN_VERSION is required}"

sdk_dir=$(mktemp -d)
tar -xzf "$archive" -C "$sdk_dir"
relocator=$(find "$sdk_dir" -name relocate-sdk.sh -type f -print -quit)
if [[ -n "$relocator" ]]; then
  sdk_root=$(dirname "$relocator")
  (cd "$sdk_root" && sh ./relocate-sdk.sh)
else
  echo 'SDK has no relocate-sdk.sh; checking raw toolchain layout' >&2
  sdk_root=$sdk_dir
fi
if [[ -f "$sdk_root/environment-setup" ]]; then
  # Buildroot provides target flags, sysroot and tool paths here.
  source "$sdk_root/environment-setup"
fi

compiler=$(find "$sdk_root" -type f -name '*-gcc' -path '*/bin/*' -print -quit)
if [[ -z "$compiler" ]]; then
  compiler=$(find "$sdk_root" -type l -name '*-gcc' -path '*/bin/*' -print -quit)
fi
[[ -n "$compiler" ]] || { echo 'No cross gcc in SDK' >&2; exit 1; }
export CROSS_COMPILE="${compiler%gcc}"
export CC="${CROSS_COMPILE}gcc" CXX="${CROSS_COMPILE}g++" AR="${CROSS_COMPILE}ar"
export PATH="$(dirname "$compiler"):$PATH"
export SYSROOT="$($CC -print-sysroot)"
[[ -d "$SYSROOT" ]] || { echo "Invalid SDK sysroot: $SYSROOT" >&2; exit 1; }

machine=$($CC -dumpmachine)
case "$model:$machine" in
  ad5x:*mips*|ad5m:arm*) ;;
  *) echo "Wrong compiler target: $model requires MIPS or ARM, got $machine" >&2; exit 1 ;;
esac
printf 'Building %s %s with %s (sysroot %s)\n' "$model" "$GUPPYSCREEN_VERSION" "$machine" "$SYSROOT"

# These patches are applied in Buildroot after fetching submodules, too.
git -C lv_drivers apply "../patches/0001-lv_driver_fb_ioctls_flashforge_${model}.patch"
git -C spdlog apply ../patches/0002-spdlog_fmt_initializer_list.patch
git -C libhv apply ../patches/0004-fix-cppflags-override.patch
git -C libhv apply ../patches/0005-libhv-websocket-release-big-message-buffer.patch
git -C lvgl apply ../patches/0006-lvgl-draw-indexed-8bit-decoded-images.patch
git -C lvgl apply ../patches/0007-lvgl-draw-indexed-8bit-images-whole.patch

# Both Buildroot packages use the FF5M screen layout, including small-screen DPI.
make_args=(
  "CROSS_COMPILE=$CROSS_COMPILE" "CC=$CC" "CXX=$CXX" "AR=$AR"
  'GUPPY_FF5M_SCREEN=1'
  "FF5M_CMAKE_FLAGS=-DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_SYSROOT=$SYSROOT"
)
make "${make_args[@]}" libhv.a
make "${make_args[@]}" libspdlog.a
make -j"$(nproc)" "${make_args[@]}"

"${CROSS_COMPILE}strip" build/bin/guppyscreen
file build/bin/guppyscreen
mkdir -p releases/locale
install -m 0755 build/bin/guppyscreen releases/guppyscreen
for po in src/locale/*/LC_MESSAGES/guppyscreen.po; do
  lang=${po#src/locale/}
  lang=${lang%%/*}
  dest="releases/locale/$lang/LC_MESSAGES"
  mkdir -p "$dest"
  msgfmt -o "$dest/guppyscreen.mo" "$po"
done
python3 - "$model" <<'PYJSON'
import json
import os
import pathlib
import sys

model = sys.argv[1]
info = {
    "project_name": "guppyscreen_ff5m",
    "project_owner": "ghzserg",
    "version": os.environ["GUPPYSCREEN_VERSION"],
    "asset_name": f"guppyscreen-{model}.zip",
}
pathlib.Path("releases/release_info.json").write_text(
    json.dumps(info, ensure_ascii=False) + "\n", encoding="utf-8"
)
PYJSON

(cd releases && zip -q -r "../guppyscreen-${model}.zip" guppyscreen locale release_info.json)
