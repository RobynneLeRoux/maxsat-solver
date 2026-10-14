#ifndef MAXSAT__HEURISTICS_H
#define MAXSAT__HEURISTICS_H

#include "Solver.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>

#define MAKE_CLAUSE_SPAN(clause_id)                                            \
  std::span { &lits[clause_offset(clause_id)], clause_len[clause_id] }

// TODO: change when critical var code in solver implemented
//  Adapted directly from from AlgoM4::select_literal(). Freebie, noise, greedy.
template <typename ScoreFn>
inline int select_literal_by_score(const std::span<int> &candidates,
                                   ScoreFn score, FastRangeRNG &rng,
                                   bool freebie, bool random_walk,
                                   unsigned noise_percent = 50) {
  assert(!candidates.empty()); // empty clause would read candidates[0] below

  size_t best_breakcount = INT_MAX;
  size_t best_idx = candidates.size();
  for (size_t lit_idx = 0; lit_idx < candidates.size(); lit_idx++) {
    int lit = candidates[lit_idx];
    size_t new_breakcount = score((size_t)std::abs(lit) - 1);

    // Freebie: breaks nothing. Free progress, take it.
    if (freebie && new_breakcount == 0) {
      return lit;
      // Greedy: fewest clauses broken.
    } else if (best_breakcount > new_breakcount) {
      best_breakcount = new_breakcount;
      best_idx = lit_idx;
    }
  }

  // Noise: a deliberate bad move, to escape a spot where nothing looks better.
  if (random_walk && rng.rand(100) < noise_percent) {
    return candidates[rng.rand(candidates.size())];
  }

  return candidates[best_idx];
}

// Uniform pick from a clause's literals
template <typename S>
inline size_t pick_random_literal(S &s, const std::span<int> &candidates) {
  assert(!candidates.empty());
  return (size_t)std::abs(candidates[s.rng.rand(candidates.size())]) - 1;
}

// roulette pick from a clause's literals - optimised random
template <typename S>
inline size_t pick_roulette_literal(S &s, const std::span<int> &candidates) {
  assert(!candidates.empty());
  static constexpr size_t SCALE = 1024;

  size_t idx = roulette_select_range<size_t>(
      0, candidates.size(),
      [&](size_t i) {
        size_t b =
            (size_t)s.evaluate_breakcount((size_t)std::abs(candidates[i]) - 1);
        return std::max<size_t>(1, SCALE / ((1 + b) * (1 + b)));
      },
      s.rng);

  return (size_t)std::abs(candidates[idx]) - 1;
}

// GSAT. Looks at every unsatisfied clause and takes the best flip overall.
// Ignores the clause it is handed.

class GSATHeuristic : public Solver<GSATHeuristic> {
public:
  unsigned noise_percent = 50; // chance of flipping at random instead
  vector<size_t> hard_clause_weights;

  GSATHeuristic(const Formula &formula, SolverLimits limits,
                unsigned noise_percent = 50,
                RestartStrategy restarts = RestartStrategy::None())
      : Solver<GSATHeuristic>(formula, limits, restarts),
        noise_percent(noise_percent) {
    hard_clause_weights.assign(n_hard_clauses, 1);
  }

  void set_noise_percent(unsigned p) { noise_percent = p; }

  // cheapest valid answer that keeps the loop happy since pick_variable_to_flip
  // ignores pick_hard_clause_to_flip
  size_t pick_hard_clause_to_flip() {
    return get_first_unsat_hard_idx();
  } // pick_hard_variable_to_flip ignores index handed so no point doing work
    // for roulette

  size_t pick_soft_clause_to_flip() {
    return pick_roulette_unsat_soft_clause();
    // return pick_random_unsat_soft_clause();
  }

  size_t pick_hard_variable_to_flip(size_t) {

    // Every literal sitting in an unsatisfied clause. Duplicates left in:
    // filtering them costs more than the repeated scoring does.
    vector<int> candidate_literals;
    size_t lit_idx = 0;
    for (size_t clause_id = 0; clause_id < n_hard_clauses; clause_id++) {
      if (hard_sat_count[clause_id] == 0) {
        for (size_t _i = 0; _i < clause_len[clause_id]; _i++) {
          candidate_literals.push_back(lits[_i + lit_idx]);
        }
      }
      lit_idx += clause_len[clause_id];
    }
    assert(!candidate_literals.empty());

    // Noise checked before scoring - much faster
    if (rng.rand(100) < noise_percent) {
      int random_literal =
          candidate_literals[rng.rand(candidate_literals.size())];
      return (size_t)std::abs(random_literal) - 1;
    }

    // Lowest score wins. (1, 1) counts breaks and makes.
    int chosen_literal = candidate_literals[0];
    int best_score_delta =
        evaluate_breakcount((size_t)std::abs(chosen_literal) - 1);

    for (size_t i = 1; i < candidate_literals.size(); i++) {
      size_t var_idx = (size_t)std::abs(candidate_literals[i]) - 1;
      int score_delta = evaluate_breakcount(var_idx);

      if (score_delta < best_score_delta) {
        best_score_delta = score_delta;
        chosen_literal = candidate_literals[i];
      }
    }

    return (size_t)std::abs(chosen_literal) - 1;
  }
};

class PureRandomHeuristic : public Solver<PureRandomHeuristic> {
public:
  using Solver<PureRandomHeuristic>::Solver;

  size_t pick_hard_variable_to_flip(size_t clause_idx) {
    // return pick_random_literal(*this, hard_clauses[clause_idx]);
    return pick_random_literal(*this, MAKE_CLAUSE_SPAN(clause_idx));
  }

  size_t pick_soft_clause_to_flip() { return pick_random_unsat_soft_clause(); }

  size_t pick_hard_clause_to_flip() { return get_first_unsat_hard_idx(); }
};
// balances randomness and greediness.
// Roulette on both: weighted clause pick, then literal pick.
class RouletteHeuristic : public Solver<RouletteHeuristic> {
public:
  vector<size_t> hard_clause_weights;

  // Own constructor needed to calc hard_clause_weights
  RouletteHeuristic(const Formula &formula, SolverLimits limits,
                    RestartStrategy restarts = RestartStrategy::None())
      : Solver<RouletteHeuristic>(formula, limits, restarts) {
    hard_clause_weights.assign(n_hard_clauses, 1);
  }

  size_t pick_hard_clause_to_flip() {
    size_t c = pick_roulette_unsat_hard_clause(hard_clause_weights);
    hard_clause_weights[c]++; // clauses the search keeps failing to fix get
                              // picked more (since weights hard clause = 1 we
                              // actually want to see a diff from the weight in
                              // rroulette)
    return c;
    // return pick_random_unsat_hard_clause();
  }

  size_t pick_soft_clause_to_flip() {
    return pick_roulette_unsat_soft_clause();
    // return pick_random_unsat_soft_clause();
  }

  size_t pick_hard_variable_to_flip(size_t clause_idx) {
    return pick_roulette_literal(*this, MAKE_CLAUSE_SPAN(clause_idx));
  }
};

// WalkSAT. Picks one unsatisfied clause at random, then looks only inside it.
class WalkSATHeuristic : public Solver<WalkSATHeuristic> {
public:
  unsigned noise_percent = 50;
  vector<size_t> hard_clause_weights;

  WalkSATHeuristic(const Formula &formula, SolverLimits limits,
                   unsigned noise_percent = 50,
                   RestartStrategy restarts = RestartStrategy::None())
      : Solver<WalkSATHeuristic>(formula, limits, restarts),
        noise_percent(noise_percent) {
    hard_clause_weights.assign(n_hard_clauses, 1);
  }

  void set_noise_percent(unsigned p) { noise_percent = p; }

  size_t pick_hard_clause_to_flip() {
    //size_t c = pick_roulette_unsat_hard_clause(hard_clause_weights);
    //hard_clause_weights[c]++; // without this every weight stays 1, and the
                              // roulette draw is uniform - i.e. identical to
                              // pick_random_unsat_hard_clause, only slower
    //return c;
    return pick_random_unsat_hard_clause();
  }

  size_t pick_soft_clause_to_flip() {
    return pick_roulette_unsat_soft_clause();
    // return pick_random_unsat_soft_clause();
  }

  size_t pick_hard_variable_to_flip(size_t clause_idx) {
    // (1, 0) = breaks only. The clause gets fixed whichever literal we flip =
    // gain is identical for all candidates aka cannot break the tie.
    int chosen_literal = select_literal_by_score(
        MAKE_CLAUSE_SPAN(clause_idx),
        [&](size_t var_idx) { return evaluate_breakcount(var_idx); }, rng,
        /*freebie=*/true,
        /*random_walk=*/true, noise_percent);

    return (size_t)std::abs(chosen_literal) - 1;
  }
};

// WalkSAT plus a ban list. A flipped variable is forbidden for `tenure` flips
// (eliminates 'undoing' of work)
class WalkSATTabuHeuristic : public Solver<WalkSATTabuHeuristic> {
public:
  unsigned noise_percent = 0;
  vector<size_t> hard_clause_weights;
  vector<size_t> tabu_until; // flip number at which each variable is free again
  size_t tenure;

  // Cannot inherit the base constructor: tabu_until needs n_vars first.
  WalkSATTabuHeuristic(const Formula &formula, SolverLimits limits,
                       unsigned noise_percent = 0,
                       RestartStrategy restarts = RestartStrategy::None())
      : Solver<WalkSATTabuHeuristic>(formula, limits, restarts),
        noise_percent(noise_percent) {
    hard_clause_weights.assign(n_hard_clauses, 1);
    tabu_until.assign(n_vars + 1,
                      0); // flip number at which each variable is free again

    // Longer bans on bigger problems. Same formula AlgoM used.
    tenure = std::max<size_t>(4, (size_t)(0.01875 * (double)n_vars + 2.8125));
  }

  // Restarts throw away the assignment bans
  void on_restart() { std::fill(tabu_until.begin(), tabu_until.end(), 0); }

  void set_noise_percent(unsigned p) { noise_percent = p; }

  bool is_tabu(size_t var) const { return tabu_until[var] > n_flips; }
  void set_tabu(size_t var, size_t t) { tabu_until[var] = n_flips + t; }

  size_t pick_hard_clause_to_flip() {
    //size_t c = pick_roulette_unsat_hard_clause(hard_clause_weights);
    //hard_clause_weights[c]++; // without this every weight stays 1, and the
                              // roulette draw is uniform - i.e. identical to
                              // pick_random_unsat_hard_clause, only slower
    //return c;
    return pick_random_unsat_hard_clause();
  }

  size_t pick_soft_clause_to_flip() {
    return pick_roulette_unsat_soft_clause();
    // return pick_random_unsat_soft_clause();
  }

  size_t pick_hard_variable_to_flip(size_t clause_idx) {
    const std::span<int> &clause = MAKE_CLAUSE_SPAN(clause_idx);

    vector<int> candidates;
    for (int lit : clause) {
      if (!is_tabu((size_t)std::abs(lit))) {
        candidates.push_back(lit);
      }
    }

    // Whole clause banned. Ignore the ban rather than stall.
    int chosen_literal = 0;
    if (candidates.empty()) {
      chosen_literal = select_literal_by_score(
          clause, [&](size_t var_idx) { return evaluate_breakcount(var_idx); },
          rng,
          /*freebie=*/true, /*random_walk=*/true, noise_percent);

    } else {
      chosen_literal = select_literal_by_score(
          candidates,
          [&](size_t var_idx) { return evaluate_breakcount(var_idx); }, rng,
          /*freebie=*/true, /*random_walk=*/true, noise_percent);
    }

    size_t var = (size_t)std::abs(chosen_literal);
    set_tabu(var, tenure); // this is the variable solve() is about to flip
    return var - 1;
  }
};

// Runs random selection strategy until the search stops improving then switches
// to Roulette's.
class AdaptiveHeuristic : public Solver<AdaptiveHeuristic> {
public:
  vector<size_t> hard_clause_weights;

  size_t patience;
  size_t best_unsat_H = SIZE_MAX;
  size_t best_cost_seen = SIZE_MAX;
  size_t flips_since_improvement = 0;
  bool using_roulette = false;

  size_t n_switches = 0;     // metric: did the switch ever fire?
  size_t roulette_flips = 0; // metric: how much of the run ran roulette

  AdaptiveHeuristic(const Formula &formula, SolverLimits limits,
                    size_t patience = 0,
                    RestartStrategy restarts = RestartStrategy::None())
      : Solver<AdaptiveHeuristic>(formula, limits, restarts) {
    hard_clause_weights.assign(n_hard_clauses, 1);
    // Bigger formulas plateau for longer before a plateau means anything.
    this->patience = patience > 0 ? patience : std::max<size_t>(50, n_vars);
  }

  // A restart discards the assignment, so the stagnation measurement it was
  // based on is meaningless. Clause weights are deliberately kept - they are
  // the record of which clauses resist, which is what makes the next run
  // smarter than the last.
  void on_restart() {
    best_unsat_H = SIZE_MAX;
    best_cost_seen = SIZE_MAX;
    flips_since_improvement = 0;
  }

  // Improvement = a new best on (unsat_H_count, cost), lexicographically.
  void note_progress() {
    bool improved = unsat_H_count < best_unsat_H ||
                    (unsat_H_count == best_unsat_H && cost < best_cost_seen);

    if (improved) {
      best_unsat_H = unsat_H_count;
      best_cost_seen = cost;
      flips_since_improvement = 0;
    } else {
      flips_since_improvement++;
    }

    if (!using_roulette && flips_since_improvement >= patience) {
      using_roulette = true;
      n_switches++;
    }
  }

  size_t pick_hard_clause_to_flip() {
    note_progress(); // called once per hard flip, before the flip happens

    if (!using_roulette) {
      return pick_random_unsat_hard_clause();
    }

    roulette_flips++;
    size_t c = pick_roulette_unsat_hard_clause(hard_clause_weights);
    hard_clause_weights[c]++; // clauses that keep resisting get picked more
    return c;
  }

  size_t pick_soft_clause_to_flip() {
    return using_roulette ? pick_roulette_unsat_soft_clause()
                          : pick_random_unsat_soft_clause();
  }

  size_t pick_hard_variable_to_flip(size_t clause_idx) {
    return using_roulette
               ? pick_roulette_literal(*this, MAKE_CLAUSE_SPAN(clause_idx))
               : pick_random_literal(*this, MAKE_CLAUSE_SPAN(clause_idx));
  }
};

#endif
