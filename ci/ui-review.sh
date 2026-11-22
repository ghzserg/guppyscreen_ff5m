#!/usr/bin/env bash
# Локальный снимок интерфейса/памяти до и после PR в двух git worktree.
set -euo pipefail
if [ "$#" -ne 4 ]; then
  echo "Usage: bash ci/ui-review.sh <base-ref> <head-ref> <scenario.txt> <output-dir>" >&2
  exit 2
fi
repo=$(git rev-parse --show-toplevel)
base=$1
head=$2
scenario=$(realpath "$3")
output=$(realpath -m "$4")
mkdir -p "$output/before" "$output/after"
for state in before after; do
  rev=$base
  [ "$state" = after ] && rev=$head
  work="$output/.worktree-$state"
  git -C "$repo" worktree add --quiet --detach "$work" "$rev"
  git -C "$work" submodule update --init --recursive
  # Тестовые инструменты берем из проверяемого PR и добавляем к базе.
  cp "$repo/tests/harness/screenshot.cpp" "$repo/tests/harness/screenshot.h" "$work/tests/harness/"
  cp "$repo/tests/screenshot_capture_test.cpp" "$work/tests/"
  (
    cd "$work"
    bash ci/apply-patches.sh ad5m
    GUPPY_SCREENSHOT_SCRIPT="$scenario" GUPPY_SCREENSHOT_DIR="$output/$state" \
      make test TEST='Screenshot: scripted'
  )
  git -C "$repo" worktree remove --force "$work"
done
python3 "$repo/ci/ppm_to_png.py" "$output"
python3 "$repo/ci/compare-ui-memory.py" "$output/before" "$output/after"
echo "Images and memory samples: $output"
