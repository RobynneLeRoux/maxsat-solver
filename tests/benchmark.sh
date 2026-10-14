#!/usr/bin/env bash

# GENAI: Used to get all files in a directory to iterate them
export SOLVER_EXEC="${SOLVER_EXEC:-./build/release/app/main}"
export YALSAT_EXEC="${YALSAT_EXEC:-./build/yalsat/yalsat}"

bench_dir() {
  local solver="$1"
  local input_class="$2"
  local N="$3"
  local prep_mode="$4"
  local timeout_secs="${5:-0}" # empty = no limit (main.cpp treats -t 0 as unlimited)
  local restart_strategy="${6:-none}" # none | fixed | luby | geometric

  shopt -s nullglob
  files=( "./test_data/cnf_instances/${input_class}"/* )

  local prep_flag=""
  if [[ "$prep_mode" == "on" ]]; then
    prep_flag="--preprocess"
  fi

  # Unsatisfiable classes never terminate on their own, so they need a budget.
  local timeout_flag=""
  if [[ -n "$timeout_secs" ]]; then
    timeout_flag="-t $timeout_secs"
  fi

  for file in "${files[@]:0:N}"; do
    if [[ "${solver}" == "yalsat" ]]; then
      sed '/^%/,$d' "${file}" | "${YALSAT_EXEC}"
      code=$?
      if [[ $code -eq 10 || $code -eq 20 ]]; then
        :
      elif [[ $code -ne 0 ]]; then
        return $code
      fi
    elif [[ "${solver}" == "walksat_tabu" ]]; then
      "${SOLVER_EXEC}" "${file}" "${solver}" "${restart_strategy}" $prep_flag $timeout_flag -n 0
    else
      "${SOLVER_EXEC}" "${file}" "${solver}" "${restart_strategy}" $prep_flag $timeout_flag
    fi
  done
}
export -f bench_dir

# GENAI: Used to compose arguments to hyperfine based on solvers supplied
bench_hyperfine() {
  local input_class="$1"
  local n="$2"
  local prep_mode="$3"
  local timeout_secs="${4:-0}"
  local restart_strategy="${5:-none}"
  shift 5
  local solvers=("$@")

  local hyperfine_args=("--warmup" "5" "--shell" "bash" \
                        "--export-json" "results_${input_class}_$(date +%s).json")

  # for solver in "${solvers[@]}"; do
  #   # Benchmark without preprocessing
  #   hyperfine_args+=(
  #     "-n" "${input_class}(${n}) | ${solver} [No Prep, rs=${restart_strategy}]"
  #     "bench_dir ${solver} ${input_class} ${n} off ${timeout_secs} ${restart_strategy}"
  #   )
  #
  #   # Benchmark with preprocessing
  #   if [[ "$prep_mode" == "on" && "$solver" != "yalsat" ]]; then
  #     hyperfine_args+=(
  #       "-n" "${input_class}(${n}) | ${solver} [With Prep, rs=${restart_strategy}]" 
  #       "bench_dir ${solver} ${input_class} ${n} on ${timeout_secs} ${restart_strategy}"
  #     )
  #   fi
  # done
  while IFS= read -r arg; do
    hyperfine_args+=("$arg")
  done < <(add_solver_args "$input_class" "$n" "$prep_mode" "$timeout_secs" "$restart_strategy" "${solvers[@]}")

  hyperfine "${hyperfine_args[@]}"
}

add_solver_args() {
  local input_class="$1"
  local n="$2"
  local prep_mode="$3"
  local timeout_secs="$4"
  local restart_strategy="$5"
  shift 5
  local solvers=("$@")

  for solver in "${solvers[@]}"; do
    # Benchmark without preprocessing
    printf '%s\n' "-n"
    printf '%s\n' "${input_class}(${n}) | ${solver} [No Prep, rs=${restart_strategy}]"
    printf '%s\n' "bench_dir ${solver} ${input_class} ${n} off ${timeout_secs} ${restart_strategy}"

    # Benchmark with preprocessing
    if [[ "$prep_mode" == "on" && "$solver" != "yalsat" ]]; then
      printf '%s\n' "-n"
      printf '%s\n' "${input_class}(${n}) | ${solver} [With Prep, rs=${restart_strategy}]"
      printf '%s\n' "bench_dir ${solver} ${input_class} ${n} on ${timeout_secs} ${restart_strategy}"
    fi
  done
}

# Fails loudly on a broken setup, rather than letting a solver that never runs
# be reported as a suspiciously fast benchmark.
preflight() {
  [[ -x "${SOLVER_EXEC}" ]] || { echo "FATAL: ${SOLVER_EXEC} missing - run 'make release'"; exit 1; }

  for s in roulette adaptive; do
    "${SOLVER_EXEC}" ./test_data/cnf_instances/uf50-218/uf50-01.cnf "$s" "none" \
      | grep -q "score:" \
      || { echo "FATAL: '$s' produced no score - is it dispatched in main.cpp?"; exit 1; }
  done

  echo "preflight OK"
}



solvers=("AlgoM" "AlgoM02" "AlgoM03" "gsat" "walksat" "walksat_tabu" \
         "random" "roulette" "adaptive" "yalsat")

uf20_91() {
  bench_hyperfine "uf20-91" 500 "off" "" "none" "${solvers[@]}"
}

uf50_218() {
  bench_hyperfine "uf50-218" 50 "off" "" "none" "${solvers[@]}"
}
uf200_860() {
  # The big instances - only the heuristic solvers, the naive ones are far
  # too slow here.
  bench_hyperfine "uf200-860" 20 "off" "" "none" \
    "AlgoM03" "walksat" "walksat_tabu" "roulette" "adaptive" "yalsat"
}

#+-75min - remove if want faster testing 
uuf250_1065() {
  # Unsatisfiable: every solver runs to its budget, so this measures behaviour
  # under a fixed deadline rather than time-to-solve. 
  # Without -t these never return (main.cpp treats a timeout of 0 as no limit).
  bench_hyperfine "uuf250-1065" 10 "off" "5" "none" \
    "AlgoM03" "walksat" "walksat_tabu" "roulette" "adaptive" "yalsat"
}

selection_comparison() {
  # The head-to-head this whole change is about, isolated from the other rows.
  bench_hyperfine "uf200-860" 20 "off" "" "none" "walksat" "roulette" "adaptive" #removed random - can hang indefinately
}

ssa() {
  # Testing the preprocessing. AlgoM03 is too slow with the wcnf problems.
  #bench_hyperfine "ssa" 20 "on" "walksat" "yalsat"
    bench_hyperfine "ssa" 20 "on" "" "none" "walksat" "roulette" "adaptive" "yalsat"
}

restart_comparison() {
  # Same solvers, four restart strategies. Restarts should matter most for
  # heuristics that can get stuck: tabu clears its ban list on restart, and
  # adaptive resets its stagnation measurement, so both have real per-run
  # state to throw away. Cutoff comes from --rc, default 1000 flips.
  local strats=("none" "fixed" "luby" "geometric")
  local solvers=("walksat" "walksat_tabu" "roulette" "adaptive")

  local hyperfine_args=(
    "--warmup" "5"
    "--shell" "bash"
    "--export-json" "results_restart_comparison_$(date +%s).json"
  )

  for strat in "${strats[@]}"; do
    while IFS= read -r arg; do
      hyperfine_args+=("$arg")
    done < <(add_solver_args "uf200-860" 20 "off" 0 "$strat" "${solvers[@]}")
  done

  hyperfine "${hyperfine_args[@]}"
}


if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  preflight
  echo ">>>>>>>>>>>>>>>>>>>>"
  uf20_91
  echo ">>>>>>>>>>>>>>>>>>>>"
  uf50_218
  echo ">>>>>>>>>>>>>>>>>>>>"
  uf200_860
  echo ">>>>>>>>>>>>>>>>>>>>"
  ssa
  echo ">>>>>>>>>>>>>>>>>>>>"
  restart_comparison

  # echo ">>>>>>>>>>>>>>>>>>>>"
  # selection_comparison
  # uuf250_1065
  # echo ">>>>>>>>>>>>>>>>>>>>"
fi
