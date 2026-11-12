#!/bin/sh
# Run every test case of a doctest binary in its own process, several at a
# time. guppyscreen is built from process-wide singletons, so each test needs
# a fresh app; the processes are independent (each gets its own temp dir and
# a free port for the fake Moonraker), so they can run in parallel.
# Results are printed in the binary's order once all are done; output of
# failing tests is printed; all output is kept in <bin dir>/logs/.
#
# usage: run_tests.sh <test binary> [doctest name filter, e.g. '*homing*']
# env:   TEST_JOBS (default: number of CPUs), TEST_TIMEOUT (s, default 60)

# One test (run by xargs below): its log, and its result line in <log>.result.
if [ "$1" = "--one" ]; then
  BIN=$2; LOGS=$3; TIMEOUT=$4; name=$5
  log="$LOGS/$(echo "$name" | tr -c 'A-Za-z0-9_-' '_').log"
  start=$(date +%s%N)
  timeout "$TIMEOUT" "$BIN" --test-case="$name" --no-version > "$log" 2>&1
  rc=$?
  ms=$(( ($(date +%s%N) - start) / 1000000 ))
  # doctest splits --test-case at commas: a name with a comma runs nothing.
  if grep -q 'test cases: *0 |' "$log"; then
    echo "ran no test case (a comma in the name?)" >> "$log"
    echo "FAIL ran no test case" > "$log.result"
  elif [ $rc -eq 0 ]; then
    echo "PASS $ms" > "$log.result"
  elif [ $rc -eq 124 ]; then
    echo "FAIL timeout after ${TIMEOUT}s" > "$log.result"
  else
    echo "FAIL exit $rc" > "$log.result"
  fi
  exit 0
fi

BIN=$1
FILTER=${2:-*}
[ -n "$FILTER" ] || FILTER='*'
TIMEOUT=${TEST_TIMEOUT:-60}
JOBS=${TEST_JOBS:-$(nproc)}
LOGS=$(dirname "$BIN")/logs

rm -rf "$LOGS" && mkdir -p "$LOGS"

"$BIN" --list-test-cases --test-case="$FILTER" --no-version > "$LOGS/list.txt" 2>&1 || {
  cat "$LOGS/list.txt"; exit 1;
}
grep -v -e '^\[doctest\]' -e '^=====' "$LOGS/list.txt" | grep -v '^$' > "$LOGS/names.txt"

total=$(wc -l < "$LOGS/names.txt")
if [ "$total" -eq 0 ]; then
  echo "no test cases match '$FILTER'"
  exit 1
fi

start=$(date +%s%N)
tr '\n' '\0' < "$LOGS/names.txt" | xargs -0 -n 1 -P "$JOBS" sh "$0" --one "$BIN" "$LOGS" "$TIMEOUT"
wall_ms=$(( ($(date +%s%N) - start) / 1000000 ))

failed=0; failed_names=""
while IFS= read -r name; do
  log="$LOGS/$(echo "$name" | tr -c 'A-Za-z0-9_-' '_').log"
  read -r status detail < "$log.result" 2>/dev/null || { status=FAIL; detail="no result"; }
  if [ "$status" = PASS ]; then
    printf 'PASS  %s (%d.%01ds)\n' "$name" $((detail / 1000)) $((detail % 1000 / 100))
  else
    failed=$((failed + 1))
    failed_names="$failed_names\n  $name"
    echo "FAIL  $name ($detail)"
    echo "----- $log (last 60 lines) -----"
    tail -n 60 "$log"
    echo "-----"
  fi
done < "$LOGS/names.txt"

if [ "$failed" -gt 0 ]; then
  printf "\n%d of %d tests failed:%b\n" "$failed" "$total" "$failed_names"
  exit 1
fi
printf "\nall %d tests passed in %d.%01ds (%d at a time)\n" "$total" $((wall_ms / 1000)) $((wall_ms % 1000 / 100)) "$JOBS"
