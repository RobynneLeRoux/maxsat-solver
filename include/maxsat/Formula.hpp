#ifndef MAXSAT__FORMULA_H
#define MAXSAT__FORMULA_H

#include <print>
#include <string>
#include <vector>

using std::string, std::vector;

enum FormulaType { CNF, WCNF, WCARD };

struct EvalResult {
  bool hard_satisfied{true};
  size_t soft_score{0};
  size_t soft_cost{0};
  size_t score{0};
};

inline FormulaType string_to_formula_type(string str) {
  if (str == "cnf")
    return FormulaType::CNF;
  else if (str == "wcnf")
    return FormulaType::WCNF;
  else if (str == "wcard")
    return FormulaType::WCARD;
  else {
    std::println("WARNING: Unrecognized format!");
    exit(1);
  }
}

class Formula {
private:
  bool processed = false;

public:
  Formula(size_t n_vars, size_t n_clauses, size_t n_hard_clauses,
          size_t max_weight, FormulaType type, vector<size_t> weights,
          vector<vector<int>> clauses, vector<int> cardinality);

  void print() const;

  size_t n_vars, n_clauses, n_hard_clauses, max_weight;
  FormulaType type;

  vector<size_t> weights;
  vector<vector<int>> clauses;
  vector<int> cardinality;

  inline bool is_hard(size_t clause_id) const {
    return weights[clause_id] == max_weight;
  }

  EvalResult evaluate_assignment(const vector<bool> &assignment) const;
  double get_max_score() const;
  void set_processed(bool p);
  bool get_processed() const;
};

#endif
