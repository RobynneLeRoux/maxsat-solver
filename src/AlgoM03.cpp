#include "maxsat/AlgoM03.hpp"
#include "maxsat/ISolver.hpp"
#include "maxsat/SolverLimits.hpp"
#include "maxsat/Utils.hpp"
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdint>
#include <print>
#include <random>
#include <vector>

/*
 * Improving the naive implementation of AlgoM03. Key areas to improve:
 * - Select random softclauses in a better, more performant way.
 * - Select random variable in clause to flip in a better way.
 */
AlgoM03::AlgoM03(const Formula &f, SolverLimits limits)
    : n_vars(f.n_vars), n_clauses(f.n_clauses),
      n_hard_clauses(f.n_hard_clauses), max_weight(f.max_weight), type(f.type),
      clauses(std::move(f.clauses)), weights(std::move(f.weights)),
      cardinality(std::move(f.cardinality)), limits(limits) {

  score = 0;
  best_score = 0;

  assignment.resize(n_vars, false);
  best_assignment.resize(n_vars, false);

  make_occurrences();
  initialise_costs();
}

void AlgoM03::initialise_costs() {
  costs.resize(n_clauses, 0);

  // How many literals are set correctly for each clause?
  for (size_t clause_idx = 0; clause_idx < clauses.size(); clause_idx++) {
    costs[clause_idx] = 0;
    for (int literal : clauses[clause_idx]) {
      // abs(literal) - 1;

      // if (literal > 0 && assignment[literal - 1]) {
      //   // clause wants TRUE and assigned to TRUE;
      //   costs[clause_idx]++;
      // } else if (literal < 0 && !assignment[(literal * -1) - 1]) {
      //   // clause wants FALSE and assigned to FALSE;
      //   costs[clause_idx]++;
      // }

      size_t var_idx = abs(literal) - 1;
      costs[clause_idx] +=
          static_cast<size_t>((literal > 0 && assignment[var_idx]) ||
                              (literal < 0 && !assignment[var_idx]));
    }
  }

  validate_costs();
}

void AlgoM03::make_occurrences() {
  occurrences.resize(2 * n_vars);

  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    for (int literal : clauses[clause_idx]) {
      occurrences[lit_to_index(literal)].push_back(clause_idx);
    }
  }
}

int AlgoM03::evaluate_breakcount(size_t var_idx) const {
  assert(var_idx >= 0 && var_idx < n_vars);

  int breakcount = 0;

  size_t curr_literal_idx = lit_to_index(var_idx + 1);
  size_t swap_literal_idx = curr_literal_idx + 1;

  if (!assignment[var_idx]) {
    std::swap(curr_literal_idx, swap_literal_idx);
  }

  for (auto clause_idx : occurrences[curr_literal_idx]) {
    if (costs[clause_idx] == 1) {
      // INFO: If this clause is SAT and flipping this literal would make it
      // UNSAT.
      breakcount++;
    }
  }

  for (auto clause_idx : occurrences[swap_literal_idx]) {
    if (costs[clause_idx] == 0) {
      // INFO: This clause is UNSAT and flipping literal would make it SAT.
      breakcount--;
    }
  }

  return breakcount;
}

void AlgoM03::flip(size_t var_idx) {
  assert(var_idx >= 0 && var_idx < n_vars);

  size_t curr_literal_idx = lit_to_index(var_idx + 1);
  size_t swap_literal_idx = curr_literal_idx + 1;

  if (!assignment[var_idx]) {
    std::swap(curr_literal_idx, swap_literal_idx);
  }

  // Flip variable in assignment
  assignment[var_idx] = !assignment[var_idx];

  // update costs
  for (size_t clause_idx : occurrences[curr_literal_idx]) {

    costs[clause_idx]--;
  }

  for (size_t clause_idx : occurrences[swap_literal_idx]) {
    costs[clause_idx]++;
  }
  n_flips++;
  validate_costs();
}

bool AlgoM03::compute_clause_is_sat(size_t clause_idx) {
  for (int literal : clauses[clause_idx]) {
    size_t literal_idx = abs(literal) - 1;

    if ((literal > 0 && assignment[literal_idx]) ||
        (literal < 0 && !assignment[literal_idx])) {
      return true;
    }
  }
  return false;
}

bool AlgoM03::is_sat_S() const {
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    if (costs[clause_idx] == 0) {
      return false;
    }
  }
  return true;
}

void AlgoM03::compute_assignment_score() {
  // TODO: Change to cost, I think it will improve performance since we spend
  // most of the time with more clauses SAT than UNSAT.
  score = 0;
  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    if (costs[clause_idx] > 0) {
      score += weights[clause_idx];
    }
  }
}

void AlgoM03::compute_unsat_clauses_H(size_t variable_idx) {
  assert(0 <= variable_idx && variable_idx < n_vars);

  size_t occurrence_idx = lit_to_index(variable_idx + 1);

  for (size_t offset = 0; offset < 2; offset++) {
    size_t temp = occurrence_idx + offset;
    assert(temp < n_vars * 2);
    for (size_t clause_idx : occurrences[temp]) {

      if (clause_idx < n_hard_clauses) {
        // bool is_sat = compute_clause_is_sat(clause_idx);
        /*
         * was SAT, now SAT, change
         * T,       T,        0
         * T,       F,       +1
         * T,       T,       -1
         * T,       F,        0
         */
        // unsat_H_count += (1 - unsat_clauses[clause_idx] - is_sat);
        // unsat_clauses[clause_idx] = !is_sat;
        costs[clause_idx] = compute_clause_cost(clause_idx);
      }
    }
  }

  first_hard_unsat_idx = get_first_unsat_hard_idx();
}

void AlgoM03::compute_unsat_clauses_S() {
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    costs[clause_idx] = compute_clause_cost(clause_idx);
  }
}

void AlgoM03::solve() {
  limits.start_timer();

  generate();
  compute_unsat_clauses();
  compute_assignment_score(); // this is necessary in the case that the random
                              // assignment is SAT
  store_new_assignment();

  while (first_hard_unsat_idx < n_hard_clauses || !is_sat_S()) {
    if (limits.should_stop()) {
      goto interrupted_exit;
    }

    while (first_hard_unsat_idx < n_hard_clauses) {
      if (limits.should_stop()) {
        goto interrupted_exit;
      }

      size_t clause_idx = pick_hard_clause_to_flip();
      size_t v = pick_variable_idx_to_flip(clause_idx);

      flip(v);

      compute_unsat_clauses_H(v);
    }

    compute_unsat_clauses_S();
    compute_assignment_score();
    if (score > best_score) {
      store_new_assignment();
    }

    if (!is_sat_S()) {
      size_t random_soft_clause_idx = pick_soft_clause_to_flip();
      size_t v = pick_variable_idx_to_flip(random_soft_clause_idx);

      flip(v);

      compute_unsat_clauses(); // Only need todo [v] clauses.
    }
  }

  // ===================== SOLVING DONE =========================
interrupted_exit:
  compute_assignment_score();

  limits.end_timer();
}

void AlgoM03::print_solve_metrics() const {
  println("n_flips: {}", n_flips);
  println("CPU time: {:%Q%q}", limits.get_solve_time());
}

// PRIVATE

void AlgoM03::generate() {
  // INFO: The idea is to generate a random 64-bit int and use it's binary
  // representation for our random 0/1 (false/true assignment).
  std::mt19937_64 gen(seed == 0 ? std::random_device{}() : seed);

  size_t available_bits = 0;
  uint64_t current_bits = 0;

  for (size_t lit_idx = 0; lit_idx < n_vars; lit_idx++) {
    // regenerate 64-bit number if it is empty
    if (available_bits == 0) {
      current_bits = gen();
      available_bits = 64;
    }

    // returns true if current_bits ends in 1, else set to false.
    assignment[lit_idx] = current_bits & 1;

    // bitshift right by 1 and assign to current_bits;
    current_bits >>= 1;
    --available_bits;
  }
}

size_t AlgoM03::compute_clause_cost(size_t clause_idx) {
  size_t cost = 0;
  for (int literal : clauses[clause_idx]) {
    size_t var_idx = abs(literal) - 1;

    cost += static_cast<int>((literal > 0 && assignment[var_idx]) ||
                             (literal < 0 && !assignment[var_idx]));
  }
  return cost;
}

void AlgoM03::compute_unsat_clauses() {
  // sets first_hard_unsat_idx
  first_hard_unsat_idx = n_hard_clauses;

  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    costs[clause_idx] = compute_clause_cost(clause_idx);

    if (first_hard_unsat_idx == n_hard_clauses && costs[clause_idx] == 0) {
      first_hard_unsat_idx = clause_idx;
    }
  }
}

size_t AlgoM03::get_first_unsat_hard_idx() const {
  for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
    if (costs[clause_idx] == 0) {
      return clause_idx;
    }
  }
  return n_hard_clauses;
}

size_t AlgoM03::pick_variable_idx_to_flip(const size_t clause_idx) {
  // Return the only variable if there is only 1 in the clause (impossible?)
  if (clauses[clause_idx].size() == 1) {
    return abs(clauses[clause_idx][0]) - 1;
  }

  // Return a random variable if noise < 57
  size_t noise = rng.rand(100);
  if (noise < 57) {
    size_t idx = rng.rand(clauses[clause_idx].size());
    return abs(clauses[clause_idx][idx]) - 1;
  }

  // Else return the variable with the lowest breakcount
  size_t best_var_idx = n_vars;
  int best_breakcount = INT_MAX;

  for (size_t idx = 0; idx < clauses[clause_idx].size(); idx++) {
    size_t var_idx = abs(clauses[clause_idx][idx]) - 1;
    int breakcount = evaluate_breakcount(var_idx);

    if (breakcount < best_breakcount) {
      best_breakcount = breakcount;
      best_var_idx = var_idx;
    }
  }

  return best_var_idx;
}

size_t AlgoM03::pick_soft_clause_to_flip() {
  // WARN: This sucks for performance. Must change
  // std::random_device rd;
  // std::mt19937 gen(rd());
  // std::uniform_int_distribution<int> distrib(n_hard_clauses, n_clauses - 1);
  // return distrib(gen);

  size_t n_unsat_soft_clauses = 0;
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    n_unsat_soft_clauses += (costs[clause_idx] == 0);
  }

  size_t idx = 0;
  if (n_unsat_soft_clauses > 1) {
    idx = rng.rand(n_unsat_soft_clauses);
  }

  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    if (costs[clause_idx] == 0 && idx-- == 0) {
      return clause_idx;
    }
  }
  // INFO: We should never get to this point, as we will definitely return a
  // random clause above.
  assert(false);
  return n_hard_clauses;
}

size_t AlgoM03::pick_hard_clause_to_flip() {
  // return first_hard_unsat_idx;

  size_t noise = rng.rand(100);
  if (noise < 57) {
    return first_hard_unsat_idx;
  }

  size_t n_unsat_hard_clauses = 0;
  for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
    n_unsat_hard_clauses += (costs[clause_idx] == 0);
  }

  size_t idx = 0;
  if (n_unsat_hard_clauses > 1) {
    idx = rng.rand(n_unsat_hard_clauses);
  }

  for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
    if (costs[clause_idx] == 0 && idx-- == 0) {
      return clause_idx;
    }
  }
  // INFO: We should never get to this point, as we will definitely return a
  // random clause above.
  assert(false);
  return 0;
}

void AlgoM03::store_new_assignment() {
  best_score = score;
  for (size_t literal_idx = 0; literal_idx < n_vars; literal_idx++) {
    best_assignment[literal_idx] = assignment[literal_idx];
  }
}

void AlgoM03::validate_costs() {
  // INFO: Only validate_costs if in "debug" mode
#ifdef NDEBUG
#else
  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    size_t cost = 0;

    for (int literal : clauses[clause_idx]) {
      size_t var_idx = abs(literal) - 1;

      cost += static_cast<size_t>((literal > 0 && assignment[var_idx]) ||
                                  (literal < 0 && !assignment[var_idx]));
    }

    assert(costs[clause_idx] == cost);
  }
#endif
}

size_t AlgoM03::getScore() const { return score; }
vector<bool> AlgoM03::get_best_assignment() const { return best_assignment; }
vector<bool> AlgoM03::get_assignment() const { return assignment; }
bool AlgoM03::get_hard_satisfied() const {
  return first_hard_unsat_idx >= n_hard_clauses;
}
size_t AlgoM03::get_soft_cost() const {
  size_t cost = 0;
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    if (costs[clause_idx] == 0) {
      cost += weights[clause_idx];
    }
  }
  return cost;
};

void AlgoM03::set_seed(size_t s) {
  seed = s;
  rng = FastRangeRNG(seed);
}
