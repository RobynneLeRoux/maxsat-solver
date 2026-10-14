#ifndef MAXSAT__ALGO_M_03_H
#define MAXSAT__ALGO_M_03_H
#include "maxsat/FastRangeRNG.hpp"
#include "maxsat/Formula.hpp"
#include "maxsat/ISolver.hpp"
#include "maxsat/SolverLimits.hpp"

using namespace std;

class AlgoM03 : public ISolver {
public:
  AlgoM03(const string &input_filename);
  AlgoM03(const Formula &formula, SolverLimits limits);

  bool is_sat_S() const;
  int parse_cnf_file(const string &input_filename);
  void solve();
  void print_solve_metrics() const;

  size_t getScore() const;

  // private: WARN: Should be private
  vector<bool> assignment;
  vector<bool> best_assignment;
  size_t n_vars, n_clauses, n_hard_clauses, max_weight, score, best_score;
  FormulaType type;
  vector<vector<size_t>> occurrences;
  vector<vector<int>> clauses;
  vector<size_t> weights;
  // vector<bool> unsat_clauses;
  vector<int> cardinality;

  size_t n_flips = 0;
  size_t first_hard_unsat_idx = 0;

  SolverLimits limits;

  size_t seed;
  FastRangeRNG rng; // Allows for RNG function

  void generate();
  void make_occurrences();

  bool compute_clause_is_sat(size_t clause_idx);
  void compute_unsat_clauses();
  void compute_unsat_clauses_H(size_t variable_idx);
  void compute_unsat_clauses_S();

  void compute_assignment_score();

  size_t get_first_unsat_hard_idx() const;

  size_t pick_variable_idx_to_flip(const size_t clause_idx);
  size_t pick_soft_clause_to_flip();
  size_t pick_hard_clause_to_flip();

  void store_new_assignment();

  vector<size_t> costs;
  void validate_costs();
  void initialise_costs();
  int evaluate_breakcount(size_t variable_idx) const;
  void flip(size_t variable_idx);
  size_t compute_clause_cost(size_t clause_idx);

  vector<bool> get_best_assignment() const;
  vector<bool> get_assignment() const;
  bool get_hard_satisfied() const;
  size_t get_soft_cost() const;
  void set_seed(size_t seed);
};

#endif
