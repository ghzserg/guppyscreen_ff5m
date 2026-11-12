#!/bin/sh
# Run every test case of a doctest binary in its own process.
# guppyscreen is built from process-wide singletons, so each test needs a
# fresh app. Output of failing tests is printed; all output is kept in
# <bin dir>/logs/.
#
# usage: run_tests.sh <test binary> [doctest name filter, e.g. '*homing*']

BIN=$1
FILTER=${2:-*}
[ -n "$FILTER" ] || FILTER='*'
TIMEOUT=${TEST_TIMEOUT:-60}
LOGS=$(dirname "$BIN")/logs

rm -rf "$LOGS" && mkdir -p "$LOGS"

"$BIN" --list-test-cases --test-case="$FILTER" --no-version > "$LOGS/list.txt" 2>&1 || {
  cat "$LOGS/list.txt"; exit 1;
}
grep -v -e '^\[doctest\]' -e '^=====' "$LOGS/list.txt" | grep -v '^$' > "$LOGS/names.txt"

total=0; failed=0; failed_names=""
while IFS= read -r name; do
  total=$((total + 1))
  log="$LOGS/$(echo "$name" | tr -c 'A-Za-z0-9_-' '_').log"
  start=$(date +%s)
  if timeout "$TIMEOUT" "$BIN" --test-case="$name" --no-version > "$log" 2>&1; then
    echo "PASS  $name ($(( $(date +%s) - start ))s)"
  else
    rc=$?
    failed=$((failed + 1))
    failed_names="$failed_names\n  $name"
    [ $rc -eq 124 ] && why="timeout after ${TIMEOUT}s" || why="exit $rc"
    echo "FAIL  $name ($why)"
    echo "----- $log (last 60 lines) -----"
    tail -n 60 "$log"
    echo "-----"
  fi
done < "$LOGS/names.txt"

if [ "$total" -eq 0 ]; then
  echo "no test cases match '$FILTER'"
  exit 1
fi
if [ "$failed" -gt 0 ]; then
  printf "\n%d of %d tests failed:%b\n" "$failed" "$total" "$failed_names"
  exit 1
fi
echo "\nall $total tests passed"
