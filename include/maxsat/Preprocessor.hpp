#ifndef MAXSAT__PREPROCESSOR_H
#define MAXSAT__PREPROCESSOR_H

#include "Formula.hpp"

struct PolarityCount {
  // INFO: Keeps track of polarity for a single variable for Hard and Soft
  // clauses.
  size_t H_pos, H_neg, S_pos, S_neg = 0;
};

struct VariableMapping {
  // 1-indexed!
  vector<size_t> old_2_new;
  vector<size_t> new_2_old;
  size_t active_vars = 0;
};

class Preprocessor {

public:
  Preprocessor(const Formula &formula);

  Formula f;

  vector<bool> is_deleted;
  vector<int> force_assignments;
  bool contradiction = false;

  vector<PolarityCount> make_polarity_counts() const;

  bool reduce();
  Formula transform();
  Formula run();

  // REDUCTIONS
  // one-time
  void order_literals_in_clauses();
  void remove_zero_weight_clauses();
  void remove_tautologies();
  void order_clauses_by_weight();
  void minimal_reductions();

  // repeat until fixed point
  bool unit_propagation(bool &made_changes);
  bool pure_literal_elimination(bool &made_changes);
  bool clause_subsumption(bool &made_changes);
  bool self_subsuming_elimination(bool &made_changes);

  // TRANSFORM
  size_t n_reifications = 0;
  VariableMapping map;
  void apply_soft_clause_reification();
  Formula compact();
  vector<bool> reconstruct_solution(const vector<bool> &solution);

  // HELPERS
  void satisfy_clause(size_t clause_id);
  void purge_clause(size_t clause_id);
  bool force_assign_variable(size_t variable_idx, int assignment);

  inline bool is_hard(size_t clause_id) const { return f.is_hard(clause_id); }

  vector<vector<size_t>> make_occurrences() const;

  size_t get_active_clause_count() const;

  vector<int> get_force_assignments() const;
};

#endif
