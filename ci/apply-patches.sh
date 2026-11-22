#!/usr/bin/env bash
set -euo pipefail

model=${1:?ad5x or ad5m required}
git -C lv_drivers apply "../patches/0001-lv_driver_fb_ioctls_flashforge_${model}.patch"
git -C spdlog apply ../patches/0002-spdlog_fmt_initializer_list.patch
git -C libhv apply ../patches/0004-fix-cppflags-override.patch
git -C libhv apply ../patches/0005-libhv-websocket-release-big-message-buffer.patch
git -C libhv apply ../patches/0011-libhv-fix-data-races.patch
git -C libhv apply ../patches/0012-libhv-fix-io-loop-time-races.patch
git -C libhv apply ../patches/0013-libhv-fix-write-timestamp-races.patch
git -C lvgl apply ../patches/0006-lvgl-draw-indexed-8bit-decoded-images.patch
git -C lvgl apply ../patches/0007-lvgl-draw-indexed-8bit-images-whole.patch
git -C lvgl apply ../patches/0009-lvgl-transformed-area-margin-only-when-transformed.patch
git -C lvgl apply ../patches/0010-lvgl-redraw-skips-children-under-a-covering-one.patch
