#!/usr/bin/env bash
# Публикует только скриншоты из готового отчёта на Catbox для вставки в PR.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo 'Usage: bash ci/publish-review.sh <review-output-dir>' >&2
  exit 2
fi
output=$(realpath "$1")
[ -f "$output/review.md" ] || { echo 'No review.md found' >&2; exit 1; }
command -v curl >/dev/null || { echo 'curl is required' >&2; exit 1; }
shopt -s nullglob
images=("$output"/before/*.png)
[ "${#images[@]}" -gt 0 ] || { echo 'No PNG files found' >&2; exit 1; }

upload() {
  local file=$1 url
  url=$(curl --fail --silent --show-error --retry 2 --connect-timeout 10 --max-time 90 \
    -F 'reqtype=fileupload' -F "fileToUpload=@$file;type=image/png" \
    https://catbox.moe/user/api.php)
  if [[ ! "$url" =~ ^https://files\.catbox\.moe/[A-Za-z0-9]+\.png$ ]]; then
    echo "Invalid Catbox response: $url" >&2
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
  echo '| До | После |'
  echo '|---|---|'
  for before in "${images[@]}"; do
    file=$(basename "$before")
    after="$output/after/$file"
    [ -f "$after" ] || { echo "Missing after screenshot: $file" >&2; exit 1; }
    before_url=$(upload "$before")
    if cmp -s "$before" "$after"; then
      after_url=$before_url
    else
      after_url=$(upload "$after")
    fi
    printf '| ![До: %s](%s) | ![После: %s](%s) |\n' "$file" "$before_url" "$file" "$after_url"
  done
  echo
  sed -n '/^### Память тестового процесса/,$p' "$output/review.md"
} > "$report"
mv "$report" "$output/review-public.md"
cat "$output/review-public.md"
