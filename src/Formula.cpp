#include "maxsat/Formula.hpp"
#include <cassert>
#include <iostream>

using std::ifstream;

Formula::Formula(size_t n_vars, size_t n_clauses, size_t n_hard_clauses,
                 size_t max_weight, FormulaType type, vector<size_t> weights,
                 vector<vector<int>> clauses, vector<int> cardinality)
    : n_vars(n_vars), n_clauses(n_clauses), n_hard_clauses(n_hard_clauses),
      max_weight(max_weight), type(type), weights(weights), clauses(clauses),
      cardinality(cardinality) {};

void Formula::print() const {
  for (size_t i = 0; i < n_clauses; i++) {
    std::cout << weights[i] << " ";

    for (size_t j = 0; j < clauses[i].size(); j++) {
      std::cout << clauses[i][j] << " ";
    }

    // std::cout << cardinality[i] << " ";
    std::cout << "\n";
  }
};

double Formula::get_max_score() const {
  double max_score = 0;
  for (size_t clause_id = 0; clause_id < n_clauses; clause_id++) {
    max_score += weights[clause_id];
  }
  return max_score;
}

EvalResult Formula::evaluate_assignment(const vector<bool> &assignment) const {
  EvalResult res;

  for (size_t clause_id = 0; clause_id < n_clauses; ++clause_id) {
    bool clause_sat = false;
    for (int lit : clauses[clause_id]) {
      size_t var_idx = abs(lit) - 1;

      bool var_val = assignment[var_idx];
      bool lit_val = (lit > 0) ? var_val : !var_val;

      if (lit_val) {
        clause_sat = true;
        break;
      }
    }

    if (clause_sat) {
      res.score += weights[clause_id];
      if (!is_hard(clause_id)) {
        res.soft_score += weights[clause_id];
      }
    } else {
      if (is_hard(clause_id)) {
        res.hard_satisfied = false;
      } else {
        res.soft_cost += weights[clause_id];
      }
    }
  }
  return res;
}

void Formula::set_processed(bool p) { processed = p; }
bool Formula::get_processed() const { return processed; }
