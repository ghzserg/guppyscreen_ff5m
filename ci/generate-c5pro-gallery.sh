#!/usr/bin/env bash
# Воспроизводимая галерея из тестового LVGL framebuffer (C5 Pro).
set -euo pipefail
cd "$(dirname "$0")/.."
out="docs/screenshots/c5pro"
for axis in X Y; do
  test -s "tests/fixtures/resonances_${axis}.png" || {
    echo "Missing tests/fixtures/resonances_${axis}.png — Input Shaper gallery needs both X/Y graphs" >&2
    exit 1
  }
done
mkdir -p "$out"
themes=(blue red green purple pink yellow)
scenario=$(mktemp)
trap 'rm -f "$scenario"' EXIT
# Для каждой темы отдельный процесс; все действия сценария сохраняются.
for i in "${!themes[@]}"; do
  awk -v offset="$i" -v count="${#themes[@]}" '
    $1 == "save" && n++ % count != offset { next }
    { print }
  ' ci/ui-scenarios/c5pro-gallery.txt > "$scenario"
  GUPPY_SCREENSHOT_PROFILE=c5pro \
  GUPPY_SCREENSHOT_THEME="${themes[$i]}" \
  GUPPY_SCREENSHOT_SCRIPT="$scenario" \
  GUPPY_SCREENSHOT_DIR="$PWD/$out" \
  make test TEST='Screenshot: scripted' \
    TEST_SRCS="tests/test_main.cpp tests/screenshot_capture_test.cpp $(printf '%s ' tests/harness/*.cpp)"
done
python3 ci/ppm_to_png.py "$out"
find "$out" -maxdepth 1 -type f -name '*.json' -delete
test "$(find "$out" -maxdepth 1 -name '*.png' | wc -l)" -ge 20
echo "C5 Pro screenshots: $(find "$out" -maxdepth 1 -name '*.png' | wc -l)"
