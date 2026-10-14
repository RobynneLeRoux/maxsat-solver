#include "maxsat/Preprocessor.hpp"
#include "maxsat/Formula.hpp"
#include "maxsat/Utils.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <print>
#include <ranges>
#include <vector>

using std::println;

#define continue_if_deleted(clause_id)                                         \
  if (is_deleted[clause_id])                                                   \
    continue;

#define return_true_if_contradiction(contradiction)                            \
  if (contradiction)                                                           \
    return true;

Preprocessor::Preprocessor(const Formula &formula)
    : f(formula), is_deleted(f.n_clauses, false),
      force_assignments(f.n_vars, 0) {

  if (f.type == FormulaType::WCARD) {
    println("WARNING: WCARD currently unsupported.");
    exit(1);
  }
};

void Preprocessor::minimal_reductions() {
  remove_zero_weight_clauses(); // INFO: Parser should guarantee this
  order_literals_in_clauses();
  remove_tautologies();
  order_clauses_by_weight();
}

void Preprocessor::order_clauses_by_weight() {
  auto zipped =
      std::views::zip(f.weights, f.clauses, f.cardinality, is_deleted);
  std::ranges::sort(zipped, [](auto const &a, auto const &b) {
    return std::get<0>(a) > std::get<0>(b);
  });

#if NDEBUG
#else
  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    assert(f.is_hard(clause_id) == (clause_id < f.n_hard_clauses));
  }
#endif
}

bool Preprocessor::reduce() {
  // Returns true if contradiction found.
  minimal_reductions();

  // store whether a technique made any change to the formula.
  bool made_changes = false;
  bool is_first_pass = true;

  // Keep trying to make changes if we made a change in previous iteration.
  // Stop until no improvements can be made.
  do {
    made_changes = false;
    pure_literal_elimination(made_changes);
    unit_propagation(made_changes);

    if (is_first_pass && !made_changes) {
      // INFO: Early stopping specifically for uniform/random formulas. Deciding
      // to not go into 'heavy-duty' reductions if simpler ones fail to make
      // progress.

      // no changes were made, so contradiction must be false.
      return false;
    }

    clause_subsumption(made_changes);
    self_subsuming_elimination(made_changes);

    // Contradiction found in earlier step
    if (contradiction) {
      return true;
    }
    // Found a clause that has not been deleted, but has no literals.
    for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
      if (!is_deleted[clause_id] && f.clauses[clause_id].size() == 0) {
        return true;
      }
    }
    is_first_pass = false;
  } while (made_changes);

  return false;
};

void Preprocessor::apply_soft_clause_reification() {
  // returns new n_vars;
  size_t dummy_lit = f.n_vars;
  for (size_t clause_id = f.n_hard_clauses; clause_id < f.n_clauses;
       clause_id++) {
    continue_if_deleted(clause_id);
    dummy_lit++;

    // append reified soft clause
    f.clauses.push_back({static_cast<int>(-dummy_lit)});
    f.weights.push_back(f.weights[clause_id]);
    f.cardinality.push_back(f.cardinality[clause_id]);
    is_deleted.push_back(false);

    // insert dummy var into soft clause, make it hard.
    f.clauses[clause_id].insert(f.clauses[clause_id].begin(), dummy_lit);
    f.weights[clause_id] = f.max_weight;
  }

  n_reifications = dummy_lit - f.n_vars;
}

Formula Preprocessor::compact() {
  // 1 indexed!
  map.old_2_new.assign(f.n_vars + n_reifications + 1, 0);
  map.new_2_old.push_back(0);
  map.active_vars = 0;

  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    continue_if_deleted(clause_id);

    for (auto literal : f.clauses[clause_id]) {
      size_t abs_literal = abs(literal);

      if (abs_literal <= f.n_vars) {
        // If literal is not a dummy literal, assert that it has not been force
        // assigned.
        assert(force_assignments[abs_literal - 1] == 0);
      }

      if (map.old_2_new[abs_literal] == 0 && abs_literal <= f.n_vars) {
        // if literal has not been indexes yet
        // AND is not a dummy literal.
        map.old_2_new[abs_literal] = ++map.active_vars;
        map.new_2_old.push_back(abs_literal);
      }
    }
  }

  // guarantees that dummy reification variables are at the back
  // If there is no reificiation, this step doesn't do anything.
  for (size_t dummy_idx = f.n_vars + 1;
       dummy_idx < f.n_vars + n_reifications + 1; dummy_idx++) {
    map.old_2_new[dummy_idx] = ++map.active_vars;
    map.new_2_old.push_back(dummy_idx);
  }

  size_t n_clauses = get_active_clause_count() + n_reifications;
  vector<size_t> weights(n_clauses, 0);
  vector<vector<int>> clauses(n_clauses);
  vector<int> cardinality(n_clauses, 0);
  size_t n_hard_clauses = 0;

  size_t current_clause_id = 0;
  for (size_t clause_id = 0; clause_id < f.clauses.size(); clause_id++) {
    continue_if_deleted(clause_id);

    if (is_hard(clause_id)) {
      n_hard_clauses++;
    }

    weights[current_clause_id] = f.weights[clause_id];
    cardinality[current_clause_id] = f.cardinality[clause_id];

    for (auto literal : f.clauses[clause_id]) {
      int new_literal = (literal > 0) ? map.old_2_new[abs(literal)]
                                      : -map.old_2_new[abs(literal)];

      clauses[current_clause_id].push_back(new_literal);
    }

    current_clause_id++;
  }

  Formula compacted_f =
      Formula(map.active_vars, n_clauses, n_hard_clauses, f.max_weight,
              std::move(f.type), std::move(weights), std::move(clauses),
              std::move(cardinality));

  return compacted_f;
}

Formula Preprocessor::run() {
  bool contradiction_found = reduce();

  if (contradiction_found) {
    println("CONTRADICTION");
    exit(1);
  }

  minimal_reductions();
  apply_soft_clause_reification();
  Formula f = compact();
  f.set_processed(true);
  return f;
}

Formula Preprocessor::transform() {
  minimal_reductions();
  apply_soft_clause_reification();
  Formula f = compact();
  f.set_processed(true);
  return f;
}

vector<bool>
Preprocessor::reconstruct_solution(const vector<bool> &solver_solution) {
  vector<bool> full_solution(f.n_vars, false);
  assert(solver_solution.size() == map.new_2_old.size() - 1);

  for (size_t var_idx = 0; var_idx < map.new_2_old.size() - 1 - n_reifications;
       var_idx++) {
    size_t original_var_idx = map.new_2_old[var_idx + 1] - 1;
    assert(0 <= original_var_idx);
    assert(original_var_idx < f.n_vars);
    full_solution[original_var_idx] = solver_solution[var_idx];
  }

  for (size_t var_idx = 0; var_idx < f.n_vars; var_idx++) {
    if (force_assignments[var_idx] != 0) {
      full_solution[var_idx] = force_assignments[var_idx] > 0;
    }
  }

  return full_solution;
}

void Preprocessor::remove_zero_weight_clauses() {
  for (size_t clause_idx = 0; clause_idx < f.n_clauses; clause_idx++) {
    continue_if_deleted(clause_idx);

    if (f.weights[clause_idx] < 1) {
      purge_clause(clause_idx);
    }
  }
};

void Preprocessor::remove_tautologies() {
  // If a clause contains a literal and it's negation, it is trivially SAT
  // - Hard+Soft: Remove that clause

  for (size_t clause_idx = 0; clause_idx < f.n_clauses; clause_idx++) {
    continue_if_deleted(clause_idx);

    for (size_t literal_idx = 0; literal_idx < f.clauses[clause_idx].size() - 1;
         literal_idx++) {
      int lit1 = f.clauses[clause_idx][literal_idx];
      int lit2 = f.clauses[clause_idx][literal_idx + 1];

      if (lit1 == -lit2) {
        satisfy_clause(clause_idx);
        break;
      }
    }
  }
};

void Preprocessor::order_literals_in_clauses() {
  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    continue_if_deleted(clause_id);

    std::sort(f.clauses[clause_id].begin(), f.clauses[clause_id].end(),
              [](int lit_a, int lit_b) {
                return lit_to_index(lit_a) < lit_to_index(lit_b);
              });
  }
}

bool Preprocessor::unit_propagation(bool &made_changes) {
  // Get all literals that are contained by a single clause.
  // If the clause is Hard, we can force that assignment.

  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    if (!is_deleted[clause_id] && is_hard(clause_id) &&
        f.clauses[clause_id].size() == 1) {
      int literal = f.clauses[clause_id][0];
      made_changes = true;

      return_true_if_contradiction(
          force_assign_variable(abs(literal) - 1, literal > 0 ? 1 : -1));
    }
  }

  return false;
};

bool Preprocessor::pure_literal_elimination(bool &made_changes) {
  vector<PolarityCount> counts = make_polarity_counts();

  for (size_t var_idx = 0; var_idx < f.n_vars; var_idx++) {
    bool pure_pos =
        (counts[var_idx].H_neg == 0 && counts[var_idx].S_neg == 0) &&
        (counts[var_idx].H_pos > 0 || counts[var_idx].S_pos > 0);
    bool pure_neg =
        (counts[var_idx].H_pos == 0 && counts[var_idx].S_pos == 0) &&
        (counts[var_idx].H_neg > 0 || counts[var_idx].S_neg > 0);

    if (pure_pos) {
      return_true_if_contradiction(force_assign_variable(var_idx, 1));
      made_changes = true;
    } else if (pure_neg) {
      return_true_if_contradiction(force_assign_variable(var_idx, -1));
      made_changes = true;
    }
  }

  return false;
};

// Add this helper to calculate the 64-bit signature of a clause
uint64_t compute_signature(const std::vector<int> &clause) {
  uint64_t sig = 0;
  for (int lit : clause) {
    sig |= (1ULL << (std::abs(lit) % 64));
  }
  return sig;
}

bool Preprocessor::clause_subsumption(bool &made_changes) {
  // GENAI: Implementation of better than naive subsuming clauses elimination.
  // 1. Build occurrence lists and clause signatures
  vector<vector<size_t>> occ(f.n_vars * 2); // Indexed by: var * 2 + (sign)
  vector<uint64_t> sigs(f.clauses.size(), 0);
  vector<int> lit_counts(f.n_vars * 2, 0);

  for (size_t clause_id = 0; clause_id < f.n_clauses; ++clause_id) {
    continue_if_deleted(clause_id);

    sigs[clause_id] = compute_signature(f.clauses[clause_id]);
    for (int lit : f.clauses[clause_id]) {
      int idx = lit_to_index(lit);
      occ[idx].push_back(clause_id);
      lit_counts[idx]++;
    }
  }

  // 2. Iterate through potential subsuming clauses (A)
  for (size_t a_idx = 0; a_idx < f.n_clauses; ++a_idx) {
    if (is_deleted[a_idx] || f.clauses[a_idx].empty())
      continue;

    const auto &clause_A = f.clauses[a_idx];
    uint64_t sig_A = sigs[a_idx];
    bool a_is_hard = is_hard(a_idx);

    // Find the Least Frequent Literal (LFL) in A
    int rarest_lit = clause_A[0];
    int min_count = lit_counts[lit_to_index(rarest_lit)];

    for (size_t i = 1; i < clause_A.size(); ++i) {
      int count = lit_counts[lit_to_index(clause_A[i])];
      if (count < min_count) {
        min_count = count;
        rarest_lit = clause_A[i];
      }
    }

    // 3. Only check clauses (B) that contain the rarest literal of A
    const auto &candidates = occ[lit_to_index(rarest_lit)];

    for (size_t b_idx : candidates) {
      if (a_idx == b_idx || is_deleted[b_idx])
        continue;

      const auto &clause_B = f.clauses[b_idx];
      bool b_is_hard = is_hard(b_idx);

      // Filter 1: Size check
      if (clause_A.size() > clause_B.size())
        continue;

      // Filter 2: 64-bit Signature check (Fast fail)
      uint64_t sig_B = sigs[b_idx];
      if ((sig_A & ~sig_B) != 0)
        continue;

      // Filter 3: Exact subset check (we know A and B are sorted)
      if (std::includes(clause_B.begin(), clause_B.end(), clause_A.begin(),
                        clause_A.end())) {

        // MaxSAT Subsumption Rules
        if (a_is_hard && b_is_hard) {
          satisfy_clause(b_idx);
          made_changes = true;
        } else if (a_is_hard && !b_is_hard) {
          satisfy_clause(b_idx);
          made_changes = true;
        } else if (!a_is_hard && !b_is_hard) {
          if (clause_A.size() == clause_B.size()) {
            f.weights[a_idx] += f.weights[b_idx];
            satisfy_clause(b_idx);
            made_changes = true;
          }
          // Strict soft subsuming soft -> Do nothing.
        }
      }
    }
  }
  return false;
}

bool Preprocessor::self_subsuming_elimination(bool &made_changes) {
  // GENAI: Gemini implemented this function.
  // 1. Build occurrence lists and 64-bit signatures
  vector<vector<size_t>> occ(f.n_vars * 2);
  vector<uint64_t> sigs(f.n_clauses, 0);

  for (size_t clause_id = 0; clause_id < f.n_clauses; ++clause_id) {
    continue_if_deleted(clause_id);

    sigs[clause_id] = compute_signature(f.clauses[clause_id]);
    for (int lit : f.clauses[clause_id]) {
      occ[lit_to_index(lit)].push_back(clause_id);
    }
  }

  auto lit_comp = [](int a, int b) {
    return lit_to_index(a) < lit_to_index(b);
  };

  // 2. Iterate through candidate subsuming clauses (A)
  for (size_t a_idx = 0; a_idx < f.n_clauses; ++a_idx) {
    continue_if_deleted(a_idx);

    // Soundness rule: Clause A MUST be Hard to shorten clauses without weight
    // splitting
    if (!is_hard(a_idx) || f.clauses[a_idx].empty()) {
      continue;
    }

    const auto &clause_A = f.clauses[a_idx];

    // Try each literal in A as the pivot literal 'l'
    for (size_t i = 0; i < clause_A.size(); ++i) {
      int lit_A = clause_A[i];
      int neg_lit_A = -lit_A;

      // Compute signature of A \ {lit_A}
      uint64_t sig_A_prime = 0;
      for (size_t j = 0; j < clause_A.size(); ++j) {
        if (i != j) {
          sig_A_prime |= (1ULL << (std::abs(clause_A[j]) % 64));
        }
      }

      // Look at candidate clauses (B) containing ~lit_A
      const auto &candidates = occ[lit_to_index(neg_lit_A)];

      for (size_t b_idx : candidates) {
        if (a_idx == b_idx || is_deleted[b_idx]) {
          continue;
        }

        const auto &clause_B = f.clauses[b_idx];

        // Filter 1: Size check (|A| <= |B|)
        if (clause_A.size() > clause_B.size()) {
          continue;
        }

        // Filter 2: Signature check for (A \ {lit_A}) <= B
        if ((sig_A_prime & ~sigs[b_idx]) != 0) {
          continue;
        }

        // Filter 3: Check if all literals in A (except lit_A) exist in B
        bool is_subset = true;
        for (size_t j = 0; j < clause_A.size(); ++j) {
          if (i == j)
            continue;

          if (!std::binary_search(clause_B.begin(), clause_B.end(), clause_A[j],
                                  lit_comp)) {
            is_subset = false;
            break;
          }
        }

        if (is_subset) {
          // Self-subsumption detected: Remove ~lit_A from clause B
          auto &b_ref = f.clauses[b_idx];
          auto it = std::find(b_ref.begin(), b_ref.end(), neg_lit_A);

          if (it != b_ref.end()) {
            b_ref.erase(it);
            made_changes = true;

            // Recompute signature for B
            sigs[b_idx] = compute_signature(b_ref);

            // Handle clause B becoming empty
            if (b_ref.empty()) {
              if (is_hard(b_idx)) {
                contradiction = true;
                return true;
              } else {
                purge_clause(b_idx);
              }
            }
          }
        }
      }
    }
  }

  return false;
}

bool Preprocessor::force_assign_variable(size_t var_idx, int assignment) {
  assert(assignment == 1 || assignment == -1);
  assert(force_assignments[var_idx] == 0);
  // sets the assignment for this variable
  // returns true if contradiction is found.
  force_assignments[var_idx] = assignment;

  int true_lit = (assignment > 0) ? static_cast<int>(var_idx + 1)
                                  : -static_cast<int>(var_idx + 1);
  int false_lit = -true_lit;

  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    continue_if_deleted(clause_id);

    // If this literal is present in any clause, remove the clause (SAT)
    if (std::ranges::find(f.clauses[clause_id], true_lit) !=
        f.clauses[clause_id].end()) {
      satisfy_clause(clause_id);
    } else {

      // If this -literal is present in any clause, remove the -literal
      auto it = std::ranges::find(f.clauses[clause_id], false_lit);
      if (it != f.clauses[clause_id].end()) {
        f.clauses[clause_id].erase(it);

        // If there is no way to make this clause SAT
        if (f.clauses[clause_id].empty()) {

          if (is_hard(clause_id)) {
            // INFO: Contradiction, early return
            contradiction = true;
            return true;
          } else {
            // The soft clause is simply UNSAT, delete it.
            purge_clause(clause_id);
          }
        }
      }
    }
  }

#if NDEBUG
#else
  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    continue_if_deleted(clause_id);

    for (auto literal : f.clauses[clause_id]) {
      size_t abs_literal = abs(literal);
      assert(force_assignments[abs_literal - 1] == 0);
      assert((abs_literal - 1) != var_idx);
    }
  }
#endif

  return false;
}

void Preprocessor::purge_clause(size_t clause_idx) {
  // Remove clause from Formula
  // INFO: Be careful about using this method if you are iterating clauses.
  is_deleted[clause_idx] = true;
}

vector<vector<size_t>> Preprocessor::make_occurrences() const {
  vector<vector<size_t>> occurrences(f.n_vars * 2, vector<size_t>());

  for (size_t clause_idx = 0; clause_idx < f.n_clauses; clause_idx++) {
    continue_if_deleted(clause_idx);

    for (size_t literal_idx = 0; literal_idx < f.clauses[clause_idx].size();
         literal_idx++) {
      int literal = f.clauses[clause_idx][literal_idx];
      occurrences[lit_to_index(literal)].push_back(clause_idx);
    }
  }

  return occurrences;
}

[[nodiscard]] vector<PolarityCount> Preprocessor::make_polarity_counts() const {
  vector<PolarityCount> polarity(f.n_vars);

  for (size_t clause_idx = 0; clause_idx < f.n_clauses; clause_idx++) {
    continue_if_deleted(clause_idx);

    for (int literal : f.clauses[clause_idx]) {
      size_t var_idx = abs(literal) - 1;

      if (is_hard(clause_idx)) {
        literal > 0 ? polarity[var_idx].H_pos++ : polarity[var_idx].H_neg++;
      } else {
        literal > 0 ? polarity[var_idx].S_pos++ : polarity[var_idx].S_neg++;
      }
    }
  }

  return polarity;
}

void Preprocessor::satisfy_clause(size_t clause_id) { purge_clause(clause_id); }

size_t Preprocessor::get_active_clause_count() const {
  size_t count = 0;
  for (size_t clause_id = 0; clause_id < f.n_clauses; clause_id++) {
    count += !is_deleted[clause_id];
  }
  return count;
}

vector<int> Preprocessor::get_force_assignments() const {
  return force_assignments;
}
