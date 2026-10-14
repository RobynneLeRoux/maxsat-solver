#ifndef MAXSAT__I_SOLVER_H
#define MAXSAT__I_SOLVER_H

#include <cstddef>
#include <vector>

class ISolver {
public:
  virtual void solve() = 0;
  virtual void print_solve_metrics() const = 0;
  virtual std::vector<bool> get_best_assignment() const = 0;
  virtual std::vector<bool> get_assignment() const = 0;
  virtual bool get_hard_satisfied() const = 0;
  virtual size_t get_soft_cost() const = 0;
  virtual void set_seed(size_t seed) = 0;

  virtual ~ISolver() = default;
};

#endif
