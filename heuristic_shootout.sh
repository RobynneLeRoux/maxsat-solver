#!/usr/bin/env bash
# Side-by-side comparison of every heuristic. Run from the repo root:
#   bash heuristic_shootout.sh 2>&1 | tee ~/Downloads/shootout.txt
M=./build/release/app/main
HEURS="random gsat walksat walksat_tabu roulette adaptive"

echo "=== 1. Weighted MaxSAT: 02.wcnf (known optimum: soft cost 28), 2 s per run, 5 runs ==="
printf "%-14s %s\n" "heuristic" "soft cost per run (lower is better)"
for h in $HEURS; do
  costs=""
  for i in 1 2 3 4 5; do
    c=$($M test_data/PreprocessorTest/02.wcnf $h -t 2 | grep soft_cost | cut -d' ' -f2)
    costs="$costs $c"
  done
  printf "%-14s %s\n" "$h" "$costs"
done

echo
echo "=== 2. Satisfiable 3-SAT: first 10 uf200-860 instances, 10 s cap each ==="
printf "%-14s %8s %14s %10s\n" "heuristic" "solved" "median flips" "total s"
for h in $HEURS; do
  solved=0; flips=(); start=$(date +%s)
  for f in $(ls test_data/cnf_instances/uf200-860/*.cnf | head -10); do
    out=$($M "$f" $h -t 10)
    echo "$out" | grep -q "^SAT" && solved=$((solved+1))
    flips+=($(echo "$out" | grep n_flips | cut -d' ' -f2))
  done
  med=$(printf "%s\n" "${flips[@]}" | sort -n | sed -n 5p)
  printf "%-14s %6s/10 %14s %10s\n" "$h" "$solved" "$med" "$(( $(date +%s) - start ))"
done
