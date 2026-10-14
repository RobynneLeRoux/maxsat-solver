#ifndef MAXSAT__SOLVER_H
#define MAXSAT__SOLVER_H

#include "FastRangeRNG.hpp"
#include "Formula.hpp"
#include "ISolver.hpp"
#include "RestartStrategy.hpp"
#include "SolverLimits.hpp"
#include "maxsat/Utils.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <ranges>

using namespace std;

struct HardClauseLookup {
  uint32_t count;     // max = ~65k
  int32_t correction; // abs_max = ~32k
};
static_assert(sizeof(HardClauseLookup) == 8,
              "HardClauseLookup size must be 8 bytes (64 bits)");

template <typename Heuristic> class Solver : public ISolver {
public:
  Solver(const Formula &formula, SolverLimits limits,
         RestartStrategy restarts = RestartStrategy::None());

  void solve();
  void print_solve_metrics() const;

  // private: WARN: Should be private
  vector<bool> assignment;
  vector<bool> best_assignment;
  const size_t n_vars, n_clauses, n_hard_clauses, max_weight;
  const size_t n_soft_clauses;
  size_t cost, best_cost;
  const size_t soft_offset;

  // bool is_unsat_H = true, is_unsat_S = true;
  size_t unsat_H_count = 0, unsat_S_count = 0;

  const FormulaType type;

  vector<size_t> soft_dummy_id_to_hard_clause_id;
  vector<size_t> weights;

  // vector<vector<int>> hard_clauses;
  inline size_t clause_offset(size_t clause_id) const;
  size_t n_hard_literals;
  vector<size_t> clause_len;
  vector<int> lits;
  vector<HardClauseLookup> lit_lookup;

  vector<size_t> hard_sat_count;
  // stores var_idx + 1
  // 0 means clause has no critcal var
  vector<size_t> critical_lits;

  size_t n_flips = 0;
  // size_t first_unsat_hard_idx;

  vector<vector<size_t>> hard_occurrences;

  size_t seed = 0; // not seeded
  void set_seed(size_t seed);
  FastRangeRNG rng; // Allows for RNG function

  void generate();
  void initialise_hard_sat_counts_and_critical_lits();
  void compute_assignment_cost();

  void make_hard_occurrences();
  void make_1d_lits(const Formula &f);
  void store_new_assignment();

  void flip_hard(size_t variable_idx);
  void flip_soft(size_t variable_idx);

  // Getters
  vector<bool> get_best_assignment() const;
  vector<bool> get_assignment() const;
  bool get_hard_satisfied() const;
  size_t get_soft_cost() const;

  // LIMITS
  SolverLimits limits;
  RestartStrategy restarts;

  void start_timer() { limits.start_timer(); }
  void end_timer() { limits.end_timer(); }

  // Heuristics!
  size_t get_first_unsat_hard_idx() const;
  size_t get_first_unsat_soft_idx() const;
  size_t pick_hard_variable_to_flip(const size_t clause_idx);
  size_t pick_soft_clause_to_flip();
  size_t pick_hard_clause_to_flip();
  // bs/ms are the break-score and make-score weights (Defaults are 1,1 )
  // GSAT uses both terms  -> (var_idx, 1, 1)
  // WalkSAT / WalkSAT+Tabu use break-only -> (var_idx, 1, 0)
  int evaluate_breakcount(size_t variable_idx) const;
  size_t pick_random_unsat_soft_clause();
  size_t pick_random_unsat_hard_clause();
  size_t pick_roulette_unsat_soft_clause();
  size_t pick_roulette_unsat_hard_clause(const vector<size_t> &clause_weights);

  void on_restart() {
  } // Heuristics with per-run state (tabu lists, caches). default is a no-op
};

template <typename Heuristic>
void Solver<Heuristic>::make_1d_lits(const Formula &f) {
  n_hard_literals = 0;
  clause_len.assign(n_hard_clauses, 0);
  vector<size_t> indices(n_hard_clauses);
  std::iota(indices.begin(), indices.end(), 0);
  vector<size_t> reverse_indices(clause_len.size());

  size_t max_len = 0;

  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    clause_len[hard_clause_id] = f.clauses[hard_clause_id].size();
    n_hard_literals += clause_len[hard_clause_id];
    max_len = max(max_len, clause_len[hard_clause_id]);
  }

  {
    auto zipped = std::views::zip(clause_len, indices);
    std::ranges::sort(zipped, [](auto const &a, auto const &b) {
      if (std::get<0>(a) == std::get<0>(b)) {
        return std::get<1>(a) < std::get<1>(b);
      }
      return std::get<0>(a) < std::get<0>(b);
    });
  }

  lit_lookup.assign(max_len + 1, HardClauseLookup{});

  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    lit_lookup[clause_len[hard_clause_id]].count++;
    reverse_indices[indices[hard_clause_id]] = hard_clause_id;
  }

  vector<size_t> insertion_point(max_len + 1, 0);
  for (size_t cl = 1; cl < max_len + 1; cl++) {
    insertion_point[cl] =
        insertion_point[cl - 1] + (lit_lookup[cl - 1].count * (cl - 1));
  }

  lits.assign(n_hard_literals, 0);
  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    size_t old_clause_len = f.clauses[hard_clause_id].size();

    for (int literal : f.clauses[hard_clause_id]) {
      assert(lits[insertion_point[old_clause_len]] == 0);
      lits[insertion_point[old_clause_len]++] = literal;
    }
  }

  size_t running_count = 0;
  size_t actual_idx = 0;
  for (size_t cl = 1; cl < max_len + 1; cl++) {
    lit_lookup[cl].correction = (running_count * cl) - actual_idx;
    running_count += lit_lookup[cl].count;
    actual_idx += (cl * lit_lookup[cl].count);
  }

#if NDEBUG
#else
  size_t curr_lit_idx = 0;
  for (size_t new_clause_idx = 0; new_clause_idx < n_hard_clauses;
       new_clause_idx++) {
    size_t old_clause_id = indices[new_clause_idx];
    size_t cl = clause_len[new_clause_idx];
    assert(f.clauses[old_clause_id].size() == cl);

    const size_t offset = clause_offset(new_clause_idx);
    assert(offset == curr_lit_idx);
    for (size_t lit_idx = 0; lit_idx < clause_len[new_clause_idx]; lit_idx++) {
      assert(offset + lit_idx < lits.size());
      int actual_lit = lits[offset + lit_idx];
      int expected_lit = f.clauses[old_clause_id][lit_idx];
      assert(expected_lit == actual_lit);

      // should be traversing lits top to bottom
      assert(offset + lit_idx == curr_lit_idx);
      curr_lit_idx++;
    }
  }
#endif

  for (size_t clause_id = n_hard_clauses; clause_id < n_clauses; clause_id++) {
    size_t dummy_idx = clause_id - n_soft_clauses;

    assert(f.clauses[clause_id].size() == 1);
    weights.push_back(f.weights[clause_id]);
    soft_dummy_id_to_hard_clause_id.push_back(reverse_indices[dummy_idx]);

    assert(f.clauses[clause_id - n_soft_clauses][0] ==
           -f.clauses[clause_id][0]);
  }
}

template <typename Heuristic>
Solver<Heuristic>::Solver(const Formula &f, SolverLimits limits,
                          RestartStrategy restarts)
    : n_vars(f.n_vars), n_clauses(f.n_clauses),
      n_hard_clauses(f.n_hard_clauses), max_weight(f.max_weight),
      n_soft_clauses(n_clauses - n_hard_clauses),
      soft_offset(n_vars - n_soft_clauses), type(f.type), limits(limits),
      restarts(restarts) {

  // TODO: Make this more robust, should rely on some interface.
  if (!f.get_processed()) {
    println("ERROR: Must reify soft clauses before passing formula to Solver!");
    assert(f.get_processed());
    exit(1);
  }

  make_1d_lits(f);

  cost = INT_MAX;
  best_cost = INT_MAX;

  assignment.resize(n_vars, false);
  best_assignment.resize(n_vars, false);

  hard_sat_count.resize(n_hard_clauses, 0);
  critical_lits.resize(n_hard_clauses, 0);

  initialise_hard_sat_counts_and_critical_lits();
  make_hard_occurrences();
}

template <typename Heuristic> void Solver<Heuristic>::set_seed(size_t s) {
  seed = s;
  rng = FastRangeRNG(seed);
}

template <typename Heuristic> void Solver<Heuristic>::make_hard_occurrences() {
  hard_occurrences.resize(2 * n_vars);
  size_t lit_idx = 0;
  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    for (size_t idx = 0; idx < clause_len[hard_clause_id]; idx++) {
      hard_occurrences[lit_to_index(lits[lit_idx + idx])].push_back(
          hard_clause_id);
    }
    lit_idx += clause_len[hard_clause_id];
  }
}

template <typename Heuristic>
void Solver<Heuristic>::initialise_hard_sat_counts_and_critical_lits() {
  unsat_H_count = 0;

  size_t lit_idx = 0;
  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    hard_sat_count[hard_clause_id] = 0;

    for (size_t _i = 0; _i < clause_len[hard_clause_id]; _i++) {
      int literal = lits[lit_idx + _i];
      size_t var_idx = abs(literal) - 1;
      bool is_sat = (literal > 0 && assignment[var_idx]) ||
                    (literal < 0 && !assignment[var_idx]);
      if (is_sat) {
        hard_sat_count[hard_clause_id]++;
        critical_lits[hard_clause_id] = var_idx + 1;
      }

      if (hard_sat_count[hard_clause_id] != 1) {
        critical_lits[hard_clause_id] = 0;
      }
    }
    if (hard_sat_count[hard_clause_id] == 0) {
      unsat_H_count++;
      // first_unsat_hard_idx = hard_clause_id;
    }

    lit_idx += clause_len[hard_clause_id];
  }
}

template <typename Heuristic>
int Solver<Heuristic>::evaluate_breakcount(size_t var_idx) const {
  assert(var_idx >= 0 && var_idx < n_vars);

  size_t break_count = 0;
  const size_t lit_idx = var_idx + 1;

  // This is faster
  size_t lit_index = 2 * var_idx + (assignment[var_idx] ? 0 : 1);
  for (auto hard_clause_id : hard_occurrences[lit_index]) {
    if (critical_lits[hard_clause_id] == lit_idx) {
      break_count++;
    }
  }

  return break_count;
}
template <typename Heuristic>
size_t Solver<Heuristic>::clause_offset(size_t clause_id) const {
  return (clause_len[clause_id] * clause_id) -
         lit_lookup[clause_len[clause_id]].correction;
}

template <typename Heuristic>
void Solver<Heuristic>::flip_hard(size_t flipped_var_idx) {
  assert(flipped_var_idx >= 0 && flipped_var_idx < n_vars);

  assignment[flipped_var_idx] = !assignment[flipped_var_idx];
  n_flips++;

  const size_t critical_lit_idx = flipped_var_idx + 1;
  size_t true_lit_index = 2 * flipped_var_idx;
  size_t false_lit_index = true_lit_index + 1;

  if (assignment[flipped_var_idx]) {
    std::swap(true_lit_index, false_lit_index);
  }

  for (size_t hard_clause_id : hard_occurrences[true_lit_index]) {
    const size_t offset = clause_offset(hard_clause_id);
    switch (--hard_sat_count[hard_clause_id]) {
    case 0:
      // Was critically SAT by flipped_var, now UNSAT
      critical_lits[hard_clause_id] = 0;
      unsat_H_count++;
      assert(hard_sat_count[hard_clause_id] == 0);
      // first_unsat_hard_idx = hard_clause_id;
      break;
    case 1:
      // Was overSAT, now critically SAT
      for (size_t lit_idx = offset;
           lit_idx < clause_len[hard_clause_id] + offset; lit_idx++) {
        int literal = lits[lit_idx];
        size_t a_lit = abs(literal);
        if ((literal > 0) == assignment[a_lit - 1]) {
          critical_lits[hard_clause_id] = a_lit;
          break;
        }
      }
      break;
    default:
      // Was overSAT, still is overSAT. Don't do anything.
      break;
    };
    assert(hard_sat_count[hard_clause_id] <= clause_len[hard_clause_id]);
  }

  for (size_t hard_clause_id : hard_occurrences[false_lit_index]) {
    switch (++hard_sat_count[hard_clause_id]) {
    case 1:
      // Was UNSAT, now SAT
      critical_lits[hard_clause_id] = critical_lit_idx;
      unsat_H_count--;
      // if (first_unsat_hard_idx == hard_clause_id) {
      //   // must reset it
      //   first_unsat_hard_idx = n_hard_clauses;
      // }
      break;
    case 2:
      // Was SAT by some other literal(s), still SAT, Clause is now overSAT
      critical_lits[hard_clause_id] = 0;
      break;
    default:
      // Was overSAT, stil overSAT. Don't do anything.
      break;
    };
    assert(hard_sat_count[hard_clause_id] <= clause_len[hard_clause_id]);
  }

  // if (first_unsat_hard_idx == n_hard_clauses && unsat_H_count > 0) {
  //   for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
  //     if (hard_sat_count[clause_idx] == 0) {
  //       first_unsat_hard_idx = clause_idx;
  //       break;
  //     }
  //   }
  // }
  // if (unsat_H_count > 0) {
  //   assert(first_unsat_hard_idx < n_hard_clauses);
  //   assert(hard_sat_count[first_unsat_hard_idx] == 0);
  // }
}

template <typename Heuristic>
void Solver<Heuristic>::flip_soft(size_t flipped_var_idx) {
  assert(soft_offset <= flipped_var_idx && flipped_var_idx < n_vars);

  assignment[flipped_var_idx] = !assignment[flipped_var_idx];
  n_flips++;

  const size_t soft_var_idx = flipped_var_idx - soft_offset;
  assert(0 <= soft_var_idx);
  assert(soft_var_idx < n_soft_clauses);

  size_t hard_clause_id = soft_dummy_id_to_hard_clause_id[soft_var_idx];
  assert(0 <= hard_clause_id);
  assert(hard_clause_id < n_hard_clauses);

  if (assignment[flipped_var_idx]) {
    // Dummy variable is satisfying it's hard clause; unSATting the soft

    // Hard clause is now overSAT
    hard_sat_count[hard_clause_id]++;
    critical_lits[hard_clause_id] = 0;
    assert(hard_sat_count[hard_clause_id] > 1);

    // if (first_unsat_hard_idx == hard_clause_id) {
    //   first_unsat_hard_idx = n_hard_clauses;
    // }

    // Soft clause is UNSAT
    cost += weights[soft_var_idx];
    unsat_S_count++;
  } else {
    cost -= weights[soft_var_idx];
    unsat_S_count--;
    const size_t offset = clause_offset(hard_clause_id);
    switch (--hard_sat_count[hard_clause_id]) {
    case 0:
      // Hard clause was SAT by this literal, now UNSAT
      assert(critical_lits[hard_clause_id] == flipped_var_idx + 1);
      critical_lits[hard_clause_id] = 0;
      // is_unsat_H = true;
      unsat_H_count++;
      // first_unsat_hard_idx = hard_clause_id;
      break;
    case 1:
      // Was SAT, new critical literal

      for (size_t lit_idx = offset + 1;
           lit_idx < clause_len[hard_clause_id] + offset; lit_idx++) {
        int literal = lits[lit_idx];
        size_t a_lit = abs(literal);
        if ((literal > 0) == assignment[a_lit - 1]) {
          critical_lits[hard_clause_id] = a_lit;
          break;
        }
      }
      break;
    default:
      // Was SAT, still overSAT. Do nothing
      break;
    };
  }

  // if (first_unsat_hard_idx == n_hard_clauses && unsat_H_count > 0) {
  //   for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
  //     if (hard_sat_count[clause_idx] == 0) {
  //       first_unsat_hard_idx = clause_idx;
  //       break;
  //     }
  //   }
  // }
  // if (unsat_H_count > 0) {
  //   assert(first_unsat_hard_idx < n_hard_clauses);
  //   assert(hard_sat_count[first_unsat_hard_idx] == 0);
  // }
}

template <typename Heuristic>
void Solver<Heuristic>::compute_assignment_cost() {
  unsat_S_count = 0;
  cost = 0;
  for (size_t soft_clause_id = 0; soft_clause_id < n_soft_clauses;
       soft_clause_id++) {
    if (assignment[soft_clause_id + soft_offset]) {
      // If the dummy variable is TRUE, then its dummy clause is UNSAT.
      cost += weights[soft_clause_id];
      unsat_S_count++;
    }
  }
}

template <typename Heuristic> void Solver<Heuristic>::solve() {
  start_timer();
restart_point:
  generate();
  initialise_hard_sat_counts_and_critical_lits();
  compute_assignment_cost();
  static_cast<Heuristic *>(this)->on_restart();

  while (unsat_H_count > 0 || unsat_S_count > 0) {
    if (limits.should_stop()) {
      goto interrupted_exit;
    }

    while (unsat_H_count > 0) {
      if (limits.should_stop()) {
        goto interrupted_exit;
      }

      size_t clause_idx = pick_hard_clause_to_flip();
      size_t v = pick_hard_variable_to_flip(clause_idx);

      flip_hard(v);
      if (restarts.on_flip()) {
        goto restart_point;
      }
    }

    compute_assignment_cost();

    if (cost < best_cost) {
      store_new_assignment();
    }

    if (unsat_S_count > 0) {
      size_t random_soft_clause_idx = pick_soft_clause_to_flip();
      assert(0 <= random_soft_clause_idx);
      assert(random_soft_clause_idx < n_soft_clauses);

      size_t var_to_flip = soft_offset + random_soft_clause_idx;
      assert(0 <= var_to_flip);
      assert(var_to_flip < n_vars);

      flip_soft(var_to_flip);
      if (restarts.on_flip()) {
        goto restart_point;
      }
    }
  }

interrupted_exit:
  compute_assignment_cost();

  if (cost < best_cost && unsat_H_count == 0) {
    store_new_assignment();
  }

  end_timer();
}

template <typename Heuristic>
void Solver<Heuristic>::print_solve_metrics() const {
  println("n_flips: {}", n_flips);
  println("CPU time: {:%Q%q}", limits.get_solve_time());
}

template <typename Heuristic> void Solver<Heuristic>::generate() {
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

template <typename Heuristic> void Solver<Heuristic>::store_new_assignment() {
  best_cost = cost;
  for (size_t var_idx = 0; var_idx < n_vars; var_idx++) {
    best_assignment[var_idx] = assignment[var_idx];
  }
}

template <typename Heuristic>
size_t Solver<Heuristic>::get_first_unsat_hard_idx() const {
  assert(unsat_H_count > 0);
  for (size_t clause_idx = 0; clause_idx < n_hard_clauses; clause_idx++) {
    if (hard_sat_count[clause_idx] == 0) {
      return clause_idx;
    }
  }
  assert(false);
  return n_hard_clauses;
  // return first_unsat_hard_idx;
}

template <typename Heuristic>
size_t Solver<Heuristic>::get_first_unsat_soft_idx() const {
  assert(unsat_S_count > 0);
  for (size_t soft_clause_id = 0; soft_clause_id < n_soft_clauses;
       soft_clause_id++) {
    if (assignment[soft_clause_id + soft_offset]) {
      return soft_clause_id;
    }
  }
  assert(false);
  return n_soft_clauses;
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_hard_variable_to_flip(const size_t clause_idx) {
  return static_cast<Heuristic *>(this)->pick_hard_variable_to_flip(clause_idx);
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_soft_clause_to_flip() {
  if (unsat_S_count == 1) {
    return get_first_unsat_soft_idx();
  }
  return static_cast<Heuristic *>(this)->pick_soft_clause_to_flip();
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_hard_clause_to_flip() {
  if (unsat_H_count == 1) {
    return get_first_unsat_hard_idx();
  }
  return static_cast<Heuristic *>(this)->pick_hard_clause_to_flip();
}

template <typename Heuristic>
vector<bool> Solver<Heuristic>::get_best_assignment() const {
  return best_assignment;
}

template <typename Heuristic>
vector<bool> Solver<Heuristic>::get_assignment() const {
  return assignment;
}

template <typename Heuristic>
bool Solver<Heuristic>::get_hard_satisfied() const {
  return unsat_H_count == 0;
}

template <typename Heuristic> size_t Solver<Heuristic>::get_soft_cost() const {
  return cost;
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_random_unsat_soft_clause() {
  size_t idx = rng.rand(unsat_S_count);

  for (size_t soft_clause_id = 0; soft_clause_id < n_soft_clauses;
       soft_clause_id++) {
    if (assignment[soft_clause_id + soft_offset] && idx-- == 0) {
      return soft_clause_id;
    }
  }

  // Should never get here.
  assert(false);
  return n_soft_clauses;
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_random_unsat_hard_clause() {
  if (unsat_H_count == 1) {
    return get_first_unsat_hard_idx();
  }

  size_t idx = rng.rand(unsat_H_count);

  for (size_t hard_clause_id = 0; hard_clause_id < n_hard_clauses;
       hard_clause_id++) {
    if (hard_sat_count[hard_clause_id] == 0 && idx-- == 0) {
      return hard_clause_id;
    }
  }

  // Should never get here.
  assert(false);
  return n_hard_clauses;
}

// Optimised version of picking 1 unsat (used for both picking clause and
// literal ) Balance of randomness and greediness
template <typename IndexT, typename WeightFn>
inline IndexT roulette_select_range(IndexT lo, IndexT hi, WeightFn weight_of,
                                    FastRangeRNG &rng) {
  size_t total_weight = 0;
  for (IndexT i = lo; i < hi; i++) {
    total_weight += weight_of(i);
  }
  assert(total_weight > 0);

  size_t target = rng.rand(total_weight);
  size_t cumulative = 0;
  for (IndexT i = lo; i < hi; i++) {
    cumulative += weight_of(i);
    if (target < cumulative) {
      return i;
    }
  }

  assert(false);
  return lo;
}

// optimised version of old "pick_random_unsat_soft_clause" and same for hard
template <typename Heuristic>
size_t Solver<Heuristic>::pick_roulette_unsat_soft_clause() {
  size_t target = rng.rand(cost);
  size_t cumulative = 0;
  for (size_t soft_clause_idx = 0; soft_clause_idx < n_soft_clauses;
       soft_clause_idx++) {
    if (assignment[soft_clause_idx + soft_offset]) {
      cumulative += weights[soft_clause_idx];
      if (target < cumulative) {
        return soft_clause_idx;
      }
    }
  }

  assert(false);
  return n_soft_clauses;
}

template <typename Heuristic>
size_t Solver<Heuristic>::pick_roulette_unsat_hard_clause(
    const vector<size_t> &clause_weights) {
  // if (unsat_H_count == 1) {
  //   return get_first_unsat_hard_idx();
  // }
  return roulette_select_range<size_t>(
      0, n_hard_clauses,
      [&](size_t hard_clause_id) {
        return hard_sat_count[hard_clause_id] == 0
                   ? clause_weights[hard_clause_id]
                   : 0;
      },
      rng);
}

#endif
