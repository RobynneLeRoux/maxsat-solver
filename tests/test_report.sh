#!/usr/bin/env bash
# Per-test-case pass counts for HeuristicsTest.
# Runs each tag on its own so Catch2 reports counts for that test alone,
# then prints the grand total. Run from the repo root.
#
#   ./tests/test_report.sh 2>&1 | tee ~/Downloads/heuristics_report.txt

BIN=./build/debug/tests/HeuristicsTest
[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }

echo "\$ $BIN --list-tests"
echo
"$BIN" --list-tests
echo
echo "==============================================================================="
echo "Per-test-case results"
echo "==============================================================================="
echo

TAGS=(gsat_valid gsat_solves pure_random_valid walksat_valid walksat_tabu_valid
      walksat_solves noise_configurable gsat_scope evaluate_breakcount freebie
      min_breakcount random_walk hard_soft_split tabu tabu_tenure)

for t in "${TAGS[@]}"; do
  line=$("$BIN" "[$t]" 2>/dev/null | grep -E "All tests passed|FAILED|assertion")
  printf "  %-24s %s\n" "[$t]" "$line"
done

echo
echo "==============================================================================="
echo "Whole suite"
echo "==============================================================================="
echo
echo "\$ $BIN"
"$BIN"
