#!/usr/bin/env bash
# Публикует только скриншоты из готового отчёта на img402.dev для вставки в PR.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo 'Usage: bash ci/publish-review.sh <review-output-dir>' >&2
  exit 2
fi
output=$(realpath "$1")
[ -f "$output/review.md" ] || { echo 'No review.md found' >&2; exit 1; }
command -v curl >/dev/null || { echo 'curl is required' >&2; exit 1; }
shopt -s nullglob

upload() {
  local file=$1 url
  [ "$(wc -c < "$file")" -le 1048576 ] || {
    echo "PNG exceeds 1 MiB (free permanent-hosting limit): $file" >&2
    return 1
  }
  local response
  response=$(curl --fail --silent --show-error --retry 2 --connect-timeout 10 --max-time 90 \
    -F "image=@$file;type=image/png" https://img402.dev/api/free)
  url=$(python3 -c 'import json,sys; d=json.load(sys.stdin); assert d.get("expiresAt", "missing") is None, "temporary URL"; print(d["url"])' <<< "$response")
  if [[ ! "$url" =~ ^https://i\.img402\.dev/[A-Za-z0-9_-]+\.png$ ]]; then
    echo "Invalid image URL: $url" >&2
    return 1
  fi
  curl --fail --location --silent --show-error --max-time 30 --output /dev/null "$url"
  printf '%s' "$url"
}

report=$(mktemp "$output/.review-public.XXXXXX")
trap 'rm -f "$report"' EXIT
{
  sed -n '1,3p' "$output/review.md"
  echo
  for profile in c5pro ad5x ad5m; do
    dir="$output/$profile"
    images=("$dir"/before/*.png)
    [ "${#images[@]}" -gt 0 ] || { echo "No PNG files found: $profile" >&2; exit 1; }
    head -n 1 "$dir/review.md"
    echo
    echo '| До | После |'
    echo '|---|---|'
    for before in "${images[@]}"; do
      file=$(basename "$before")
      after="$dir/after/$file"
      [ -f "$after" ] || { echo "Missing after screenshot: $profile/$file" >&2; exit 1; }
      before_url=$(upload "$before")
      if cmp -s "$before" "$after"; then
        after_url=$before_url
      else
        after_url=$(upload "$after")
      fi
      printf '| ![До: %s](%s) | ![После: %s](%s) |\n' "$file" "$before_url" "$file" "$after_url"
    done
    echo
    sed -n '/^### Память тестового процесса/,$p' "$dir/review.md"
    echo
  done
} > "$report"
mv "$report" "$output/review-public.md"
cat "$output/review-public.md"
