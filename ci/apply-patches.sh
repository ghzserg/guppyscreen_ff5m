#!/usr/bin/env bash
set -euo pipefail

model=${1:?ad5x or ad5m required}
git -C lv_drivers apply "../patches/0001-lv_driver_fb_ioctls_flashforge_${model}.patch"
git -C spdlog apply ../patches/0002-spdlog_fmt_initializer_list.patch
git -C libhv apply ../patches/0004-fix-cppflags-override.patch
git -C libhv apply ../patches/0005-libhv-websocket-release-big-message-buffer.patch
git -C lvgl apply ../patches/0006-lvgl-draw-indexed-8bit-decoded-images.patch
git -C lvgl apply ../patches/0007-lvgl-draw-indexed-8bit-images-whole.patch
