#!/usr/bin/env bash
# Универсальный LVGL review: одинаковый сценарий для двух коммитов.
set -euo pipefail
if [ "$#" -ne 4 ]; then
  echo "Usage: bash ci/ui-review.sh <base-ref> <head-ref> <scenario.txt> <output-dir>" >&2
  exit 2
fi
repo=$(git rev-parse --show-toplevel)
base=$(git -C "$repo" rev-parse --verify "$1^{commit}")
head=$(git -C "$repo" rev-parse --verify "$2^{commit}")
scenario=$(realpath "$3")
output=$(realpath -m "$4")
mkdir -p "$output"
cp "$scenario" "$output/scenario.txt"

cleanup() {
  [ ! -d "$output/.worktree-before" ] || git -C "$repo" worktree remove --force "$output/.worktree-before" || true
  [ ! -d "$output/.worktree-after" ] || git -C "$repo" worktree remove --force "$output/.worktree-after" || true
}
trap cleanup EXIT

prepare() {
  local work=$1 rev=$2 file
  git -C "$repo" worktree add --quiet --detach "$work" "$rev"
  git -C "$work" submodule update --init --recursive
  # Старым коммитам даем только универсальный тестовый harness.
  for file in tests/harness/screenshot.cpp tests/harness/screenshot.h tests/screenshot_capture_test.cpp; do
    cp "$repo/$file" "$work/$file"
  done
  (cd "$work" && bash ci/apply-patches.sh ad5m)
}
capture() {
  local phase=$1 work=$2 profile
  # Один бинарник на коммит, отдельный процесс на каждый профиль.
  for profile in c5pro ad5x ad5m; do
    mkdir -p "$output/$profile/$phase"
    (cd "$work" && GUPPY_SCREENSHOT_PROFILE="$profile" GUPPY_SCREENSHOT_SCRIPT="$scenario" \
      GUPPY_SCREENSHOT_DIR="$output/$profile/$phase" make test TEST='Screenshot: scripted' \
      TEST_SRCS="tests/test_main.cpp tests/screenshot_capture_test.cpp $(printf '%s ' tests/harness/*.cpp)")
  done
}

prepare "$output/.worktree-before" "$base"
capture before "$output/.worktree-before"
if [ "$base" = "$head" ]; then
  # Контрольный прогон: повтор без второй компиляции.
  capture after "$output/.worktree-before"
else
  prepare "$output/.worktree-after" "$head"
  capture after "$output/.worktree-after"
fi

python3 "$repo/ci/ppm_to_png.py" "$output"
{
  echo "### Снимки LVGL (800×480, без масштабирования)"
  echo
  echo "База: $base — PR: $head. Источник: тестовый framebuffer, не экран принтера."
  echo
  shopt -s nullglob
  for profile in c5pro ad5x ad5m; do
    case "$profile" in
      c5pro) model="C5 Pro" ;;
      ad5x) model="AD5X" ;;
      ad5m) model="AD5M" ;;
    esac
    {
      echo "### $model"
      echo
      echo "| До | После |"
      echo "|---|---|"
      found=0
      for before in "$output/$profile"/before/*.png; do
        file=$(basename "$before")
        after="$output/$profile/after/$file"
        [ -f "$after" ] || { echo "No matching screenshot: $after" >&2; exit 1; }
        echo "| ![До: $file]($before) | ![После: $file]($after) |"
        found=$((found + 1))
      done
      [ "$found" -gt 0 ] || { echo "No screenshots: $profile" >&2; exit 1; }
      for after in "$output/$profile"/after/*.png; do
        [ -f "$output/$profile/before/$(basename "$after")" ] || {
          echo "No matching before screenshot: $after" >&2; exit 1;
        }
      done
      echo
      python3 "$repo/ci/compare-ui-memory.py" "$output/$profile/before" "$output/$profile/after"
    } > "$output/$profile/review.md"
    cat "$output/$profile/review.md"
    echo
  done
} > "$output/review.md"
cat "$output/review.md"
echo "Files: $output (review.md, scenario.txt, c5pro/, ad5x/, ad5m/)"
