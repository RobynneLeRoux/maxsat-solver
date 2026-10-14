#include "maxsat/AlgoM02.hpp"
#include "maxsat/SolverLimits.hpp"
#include "maxsat/Utils.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <print>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/*
 * Improving the naive implementation of AlgoM02. Key areas to improve:
 * - Select random softclauses in a better, more performant way.
 * - Select random variable in clause to flip in a better way.
 */
AlgoM02::AlgoM02(const Formula &f, SolverLimits limits)
    : n_vars(f.n_vars), n_clauses(f.n_clauses),
      n_hard_clauses(f.n_hard_clauses), max_weight(f.max_weight), type(f.type),
      clauses(std::move(f.clauses)), weights(std::move(f.weights)),
      cardinality(std::move(f.cardinality)), limits(limits) {

  score = 0;
  best_score = 0;

  assignment.resize(n_vars, false);
  best_assignment.resize(n_vars, false);
  unsat_clauses.resize(n_clauses, false);

  make_occurrences();
}

// INFO: CONSTRUCTOR
// ideally, we pass in a ProblemInstance object, then the algo can do it's own
// thing
AlgoM02::AlgoM02(const string &input_problem_filename) {
  int ret = parse_cnf_file(input_problem_filename);

  if (ret != 0) {
    println("failed to open {}", input_problem_filename);
    exit(1);
  }

  // ================ .CNF ONLY 'HACK' =====================
  n_hard_clauses = n_clauses;
  max_weight = 1;
  weights.assign(n_clauses, max_weight);
  assert(n_clauses == weights.size());
  assert(n_hard_clauses <= n_clauses);

  // assert that the hard clauses are stored at the top of the clause array
  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    if (clause_idx < n_hard_clauses) {
      assert(weights[clause_idx] == max_weight);
    } else {
      assert(weights[clause_idx] < max_weight);
    }
  }

  score = 0;
  best_score = 0;
  assignment.resize(n_vars, false);
  best_assignment.resize(n_vars, false);
  unsat_clauses.resize(n_clauses, false);

  make_occurrences();
}

void AlgoM02::make_occurrences() {
  occurrences.resize(2 * n_vars);

  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    for (int literal : clauses[clause_idx]) {
      occurrences[lit_to_index(literal)].push_back(clause_idx);
    }
  }
}

int AlgoM02::parse_cnf_file(const string &input_problem_filename) {
  // WARN: I think this function need to be made more robust. Test for bad .cnf
  // formatting.

  ifstream in_file{input_problem_filename, std::ios::binary};

  if (!in_file.is_open()) {
    return 1;
  }

  string line;
  string _p, _cnf; // We aren't making use of cnf variable yet.
  // int n_vars, n_clauses;

  while (in_file) {
    getline(in_file, line);                  // reads up to \n character
    istringstream input_string_stream{line}; // constructor method

    if (line.empty() || line[0] == 'c')
      continue;

    if (line[0] == 'p') {
      // INFO: This line "dumps" each component of the line into variables p,
      // cnf and n_clauses. It works because istringstream's stream extraction
      // operator (>>) is configured by default to ignore leading whitespace.
      // Very Cool!
      // GENAI: I was originally doing this in an inefficient manor. Gemini
      // suggested this.
      input_string_stream >> _p >> _cnf >> n_vars >> n_clauses;

      clauses.resize(n_clauses);
      break;
    }
  }

  // now reading through clauses
  size_t clause_idx = 0;
  int literal; // Variable (positive or negated)
  while (in_file >> literal) {
    // INFO: Put the contents of 'in_file' upto the next whitespace into literal
    // and convert to int
    if (literal == 0) {
      // clauses end '0' in the .cnf file

      assert(clauses[clause_idx].size() > 1);
      clause_idx++;
    } else if (clause_idx < n_clauses) {
      // WARN: Are these vector<int> store sequentially?
      // After parsing through the entire file, we know exactly how much memory
      // we need to store all the clauses.
      // TODO: We could start by storing the clauses as 2d matrix with fixed
      // size, then flatten into something else?
      clauses[clause_idx].push_back(literal);
    }
  }

  return 0;
}

bool AlgoM02::is_sat_S() const {
  for (size_t clause_idx = n_hard_clauses; clause_idx < unsat_clauses.size();
       clause_idx++) {
    if (unsat_clauses[clause_idx]) {
      return false;
    }
  }
  return true;
}

void AlgoM02::compute_assignment_score() {
  // TODO: Change to cost, I think it will improve performance since we spend
  // most of the time with more clauses SAT than UNSAT.
  score = 0;
  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    if (!unsat_clauses[clause_idx]) {
      score += weights[clause_idx];
    }
  }
}

void AlgoM02::compute_unsat_clauses_H(size_t variable_idx) {
  assert(0 <= variable_idx && variable_idx < n_vars);

  size_t occurrence_idx = lit_to_index(variable_idx + 1);

  for (size_t offset = 0; offset < 2; offset++) {
    size_t temp = occurrence_idx + offset;
    assert(temp < n_vars * 2);
    for (size_t clause_idx : occurrences[temp]) {

      if (clause_idx < n_hard_clauses) {
        bool is_sat = compute_clause_is_sat(clause_idx);
        /*
         * was SAT, now SAT, change
         * T,       T,        0
         * T,       F,       +1
         * T,       T,       -1
         * T,       F,        0
         */
        // unsat_H_count += (1 - unsat_clauses[clause_idx] - is_sat);
        unsat_clauses[clause_idx] = !is_sat;
      }
    }
  }

  first_hard_unsat_idx = get_first_unsat_hard_idx();
}

void AlgoM02::compute_unsat_clauses_S() {
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    unsat_clauses[clause_idx] = !compute_clause_is_sat(clause_idx);
  }
}

void AlgoM02::solve() {
  // ===================== SOLVE =========================
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

      // size_t clause_idx = pick_random_unsat_hard_clause();
      size_t clause_idx = first_hard_unsat_idx;
      size_t v = pick_random_variable_idx_to_flip(clause_idx);

      assignment[v] = !assignment[v];
      n_flips++;

      compute_unsat_clauses_H(v);
    }

    for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
      assert(compute_clause_is_sat(clause_idx));
      assert(!unsat_clauses[clause_idx]);
    }

    // COMPUTE COMPUTE UNSAT_clauses.S
    // TODO: Improve this section once we have wcnf instances.
    compute_unsat_clauses_S();
    compute_assignment_score();
    if (score > best_score) {
      store_new_assignment();
    }

    // WARN: This code does nothing until we test with wcnf instances
    if (!is_sat_S()) {
      size_t random_soft_clause_idx = choose_random_soft_clause_to_flip();

      size_t v = pick_random_variable_idx_to_flip(random_soft_clause_idx);

      assignment[v] = !assignment[v];
      n_flips++;

      compute_unsat_clauses(); // Only need todo [v] clauses.
    }
  }
interrupted_exit:
  compute_assignment_score();

  limits.end_timer();
}

void AlgoM02::print_solve_metrics() const {
  println("n_flips: {}", n_flips);
  println("CPU time: {:%Q%q}", limits.get_solve_time());

  println("SAT");
  println("score: {}", best_score);
  println("assignment: {}", assignment);
}

// PRIVATE

void AlgoM02::generate() {
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

bool AlgoM02::compute_clause_is_sat(size_t clause_idx) {
  for (int signed_literal : clauses[clause_idx]) {
    size_t literal_idx = abs(signed_literal) - 1;

    if ((signed_literal > 0 && assignment[literal_idx]) ||
        (signed_literal < 0 && !assignment[literal_idx])) {
      return true;
    }
  }
  return false;
}

void AlgoM02::compute_unsat_clauses() {
  // sets first_hard_unsat_idx
  first_hard_unsat_idx = n_hard_clauses;
  // unsat_H_count = 0;

  for (size_t clause_idx = 0; clause_idx < n_clauses; clause_idx++) {
    unsat_clauses[clause_idx] = !compute_clause_is_sat(clause_idx);
    // unsat_H_count += static_cast<int>(clause_idx < n_hard_clauses &&
    //                                   unsat_clauses[clause_idx]);

    if (first_hard_unsat_idx == n_hard_clauses && unsat_clauses[clause_idx]) {
      first_hard_unsat_idx = clause_idx;
    }
  }
}

size_t AlgoM02::get_first_unsat_hard_idx() const {
  for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
    if (unsat_clauses[clause_idx]) {
      return clause_idx;
    }
  }
  return n_hard_clauses;
}

size_t AlgoM02::pick_random_variable_idx_to_flip(const size_t clause_idx) {
  // std::random_device rd;
  // std::mt19937 gen(rd());
  // std::uniform_int_distribution<int> distrib(0, clauses[clause_idx].size() -
  // 1); size_t random_idx = distrib(gen); return
  // abs(clauses[clause_idx][random_idx]);

  size_t idx = rng.rand(clauses[clause_idx].size());
  return abs(clauses[clause_idx][idx]) - 1;
}

void AlgoM02::store_new_assignment() {
  best_score = score;
  for (size_t literal_idx = 0; literal_idx < n_vars; literal_idx++) {
    best_assignment[literal_idx] = assignment[literal_idx];
  }
}

size_t AlgoM02::choose_random_soft_clause_to_flip() {
  // WARN: This sucks for performance. Must change
  std::mt19937_64 gen(seed == 0 ? std::random_device{}() : seed);
  std::uniform_int_distribution<int> distrib(n_hard_clauses, n_clauses - 1);
  return distrib(gen);
}

size_t AlgoM02::getScore() const { return score; }
vector<bool> AlgoM02::get_best_assignment() const { return best_assignment; }
vector<bool> AlgoM02::get_assignment() const { return assignment; }
bool AlgoM02::get_hard_satisfied() const {
  return first_hard_unsat_idx >= n_hard_clauses;
}
size_t AlgoM02::get_soft_cost() const {
  size_t cost = 0;
  for (size_t clause_idx = n_hard_clauses; clause_idx < n_clauses;
       clause_idx++) {
    if (unsat_clauses[clause_idx]) {
      cost += weights[clause_idx];
    }
  }
  return cost;
};

void AlgoM02::set_seed(size_t s) {
  seed = s;
  rng = FastRangeRNG(seed);
}
