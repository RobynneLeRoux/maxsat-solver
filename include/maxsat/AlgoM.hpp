#ifndef MAXSAT__ALGO_M_H
#define MAXSAT__ALGO_M_H
#include "maxsat/ISolver.hpp"
#include "maxsat/SolverLimits.hpp"
#include <chrono>
#include <vector>

using namespace std;

class AlgoM : public ISolver {
public:
  AlgoM(const string &input_filename, SolverLimits limits);

  bool is_sat_S() const;
  size_t compute_assignment_score() const;

  int parse_cnf_file(const string &input_filename);
  void solve();
  void print_solve_metrics() const;

  // private:  TODO: SHOULD BE PRIVATE!
  vector<bool> assignment;
  vector<bool> best_assignment;
  size_t n_vars, n_clauses, n_hard_clauses, max_weight, score, best_score;
  vector<vector<int>> clauses;
  vector<size_t> clause_weights;
  vector<bool> unsat_clauses;

  size_t n_flips = 0;
  size_t first_hard_unsat_idx = 0;

  void generate();
  void compute_unsat_clauses();
  size_t get_first_unsat_hard_idx() const;
  size_t select_a_random_variable_to_flip(const size_t clause_idx) const;
  void store_new_assignment();
  size_t choose_random_soft_clause_to_flip();

  SolverLimits limits;

  vector<bool> get_best_assignment() const;
  vector<bool> get_assignment() const;
  bool get_hard_satisfied() const;
  size_t get_soft_cost() const;
  size_t seed;
  void set_seed(size_t seed);
};

#endif
