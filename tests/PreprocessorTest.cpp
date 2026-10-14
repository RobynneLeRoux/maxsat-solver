#include "maxsat/Preprocessor.hpp"
#include "Utils.hpp"
#include "catch2/catch_amalgamated.hpp"
#include "maxsat/Formula.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Utils.hpp"
#include <random>

using namespace std;

const string DATA_PATH = "test_data/PreprocessorTest";

TEST_CASE("Can purge clause", "[purge_clause]") {
  string formula_string = R"(p wcnf 4 3 10
  10 1 -1 2 0
  1 1 2 3 0
  1 4 -3 0)";

  Preprocessor proc = Preprocessor(Parser::parse_string(formula_string));

  REQUIRE(proc.get_active_clause_count() == 3);

  proc.purge_clause(0); // has tautology
  REQUIRE(proc.is_deleted[0]);

  // INFO: After removing clause 0, expect:
  // 0. 10 1 -1 2 0 [SAT]
  // 1. 1 1 2 3 0
  // 2. 1 4 -3 0
  vector<vector<size_t>> expected_list = {{1}, {}, {1}, {}, {1}, {2}, {2}, {}};
  vector<vector<size_t>> actual_occurrences = proc.make_occurrences();

  for (size_t i = 0; i < expected_list.size(); i++) {
    REQUIRE(actual_occurrences[i].size() == expected_list[i].size());

    for (size_t j = 0; j < expected_list[i].size(); j++) {
      REQUIRE(actual_occurrences[i][j] == expected_list[i][j]);
    }
  }
}

TEST_CASE("Can eliminate a pure literal", "[pure_literal_elimination]") {
  string formula_string = R"(p wcnf 4 3 10
  10 1 -1 2 0
  1 1 2 3 0
  1 4 -3 0)";

  Preprocessor proc = Preprocessor(Parser::parse_string(formula_string));

  REQUIRE(proc.get_active_clause_count() == 3);

  // INFO: Should remove first clause
  proc.remove_tautologies();
  REQUIRE(proc.is_deleted[0]);

  REQUIRE(proc.get_active_clause_count() == 2);

  bool made_changes = false;

  // INFO: Remove all clauses
  proc.pure_literal_elimination(made_changes);
  REQUIRE(made_changes == true);
  REQUIRE(proc.get_active_clause_count() == 0);

  // INFO: No more work to be done
  made_changes = false;
  proc.pure_literal_elimination(made_changes);
  REQUIRE(made_changes == false);
}

TEST_CASE("Can order literals in clauses", "[order_literals_in_clauses]") {
  auto file =
      GENERATE(Catch::Generators::from_range(get_files_in_dir(DATA_PATH)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());

    Preprocessor prep = Preprocessor(std::move(formula));

    prep.order_literals_in_clauses();

    // literals in clauses are ordered by variable (ASC) and then polarity (1,
    // -1)
    for (auto clause : prep.f.clauses) {
      REQUIRE(clause.size() > 0);
      for (size_t literal_idx = 0; literal_idx < clause.size() - 1;
           literal_idx++) {
        int lit_a = clause[literal_idx];
        int lit_b = clause[literal_idx + 1];

        REQUIRE(abs(lit_a) <= abs(lit_b));
        REQUIRE(lit_to_index(lit_a) <= lit_to_index(lit_b));

        if (abs(lit_a) == abs(lit_b)) {
          // same variable, positive comes first.
          REQUIRE(lit_a > lit_b);
        }
      }
    }
  }
}

TEST_CASE("Can force assign a variable", "[force_assign_variable]") {
  string formula_string = R"(p wcnf 4 3 10
  10 1 2 0
  1 3 -1 0
  1 -2 -3 0)";

  Preprocessor proc = Preprocessor(Parser::parse_string(formula_string));

  REQUIRE(proc.get_active_clause_count() == 3);

  // INFO: Force assign variable 1 = true
  proc.force_assign_variable(0, 1);

  REQUIRE(proc.get_active_clause_count() == 2);
  REQUIRE(proc.is_deleted[0]);

  // INFO: -1 should not be present anywhere
  // 10 1 2 0 [SAT]
  // 1 3 0
  // 1 -2 -3 0
  vector<vector<size_t>> expected_occurrences = {{}, {}, {}, {2}, {1}, {2}};
  vector<vector<size_t>> actual_occurrences = proc.make_occurrences();

  for (size_t i = 0; i < expected_occurrences.size(); i++) {
    REQUIRE(actual_occurrences[i].size() == expected_occurrences[i].size());

    for (size_t j = 0; j < expected_occurrences[i].size(); j++) {
      REQUIRE(actual_occurrences[i][j] == expected_occurrences[i][j]);
    }
  }

  REQUIRE(proc.force_assignments[0] == 1);
}

TEST_CASE("Can force assign a negative variable",
          "[force_assign_neg_variable]") {
  string formula_string = R"(p wcnf 4 3 10
  10 1 2 0
  1 3 -1 0
  1 -2 -3 0)";

  Preprocessor proc = Preprocessor(Parser::parse_string(formula_string));

  REQUIRE(proc.get_active_clause_count() == 3);

  // INFO: Force assign variable 1 = false
  proc.force_assign_variable(0, -1);

  REQUIRE(proc.get_active_clause_count() == 2);
  REQUIRE(proc.is_deleted[1]);

  // INFO: 1 should not be present anywhere
  // 10 2 0
  // 1 3 -1 0 [SAT]
  // 1 -2 -3 0
  vector<vector<size_t>> expected_occurrences = {{}, {}, {0}, {2}, {}, {2}};
  vector<vector<size_t>> actual_occurrences = proc.make_occurrences();

  for (size_t i = 0; i < expected_occurrences.size(); i++) {
    REQUIRE(actual_occurrences[i].size() == expected_occurrences[i].size());

    for (size_t j = 0; j < expected_occurrences[i].size(); j++) {
      REQUIRE(actual_occurrences[i][j] == expected_occurrences[i][j]);
    }
  }

  REQUIRE(proc.force_assignments[0] == -1);
}

// Helper to quickly build a basic Formula object for testing
Formula make_test_formula(size_t n_vars, size_t max_weight,
                          std::vector<size_t> weights,
                          std::vector<std::vector<int>> clauses) {
  size_t n_clauses = clauses.size();
  size_t n_hard = 0;
  for (size_t w : weights) {
    if (w == max_weight)
      n_hard++;
  }
  std::vector<int> cardinality(n_clauses, 0);

  return Formula(n_vars, n_clauses, n_hard, max_weight, FormulaType::WCNF,
                 weights, clauses, cardinality);
}

TEST_CASE("Preprocessor - Tautology and Zero-Weight Cleanup",
          "[preprocessor]") {
  // Clause 0: (1 OR -1) -> Tautology
  // Clause 1: (2 OR 3)   -> Soft clause with weight 0
  // Clause 2: (1 OR 2)   -> Hard clause
  size_t max_w = 100;
  Formula f =
      make_test_formula(3, max_w, {max_w, 0, max_w}, {{1, -1}, {2, 3}, {1, 2}});

  Preprocessor prep(f);

  SECTION("Zero-weight and tautology removal cleans up formula") {
    prep.remove_zero_weight_clauses();
    prep.order_literals_in_clauses();
    prep.remove_tautologies();

    // 2 clauses should be marked deleted
    REQUIRE(prep.get_active_clause_count() == 1);
  }
}

TEST_CASE("Preprocessor - Hard Unit Propagation", "[unit_propagation]") {
  // Clause 0: (1)        [Hard] -> Unit clause
  // Clause 1: (-1 OR 2)  [Soft] -> Should shrink to (2)
  // Clause 2: (1 OR 3)   [Hard] -> Should be satisfied
  size_t max_w = 100;
  Formula f =
      make_test_formula(3, max_w, {max_w, 10, max_w}, {{1}, {-1, 2}, {1, 3}});

  Preprocessor prep(f);
  bool changed = false;

  SECTION("Forces variable 1 to true and simplifies formula") {
    bool contradiction = prep.unit_propagation(changed);

    REQUIRE_FALSE(contradiction);
    REQUIRE(changed == true);
    REQUIRE(prep.get_force_assignments()[0] == 1); // Var 1 assigned +1
    REQUIRE(prep.get_active_clause_count() == 1);  // Only Clause 1 remaining
  }
}

TEST_CASE("Preprocessor - Pure Literal Elimination",
          "[pure_literal_elimination]") {
  // Clause 0: (1 OR 2)   [Hard]
  // Clause 1: (1 OR -3)  [Soft]
  // Var 1 appears only positively (pure positive)
  // Var 2 appears only positively (pure positive)
  // Var 3 appears only negatively (pure negative)
  size_t max_w = 100;
  Formula f = make_test_formula(3, max_w, {max_w, 15}, {{1, 2}, {1, -3}});

  Preprocessor prep(f);
  bool changed = false;

  SECTION("Eliminates pure literals") {
    bool contradiction = prep.pure_literal_elimination(changed);

    REQUIRE_FALSE(contradiction);
    REQUIRE(changed == true);
    REQUIRE(prep.get_force_assignments()[0] == 1);  // Var 1 forced positive
    REQUIRE(prep.get_force_assignments()[2] == -1); // Var 3 forced negative
  }
}

TEST_CASE("Preprocessor - Contradiction Detection",
          "[contradiction_detection]") {
  SECTION("Hard unit contradiction (1) AND (-1)") {
    size_t max_w = 100;
    Formula f = make_test_formula(1, max_w, {max_w, max_w}, {{1}, {-1}});

    Preprocessor prep(f);
    bool contradiction = prep.reduce();

    REQUIRE(contradiction == true);
  }

  SECTION("Empty hard clause generated via propagation") {
    // Clause 0: (1)
    // Clause 1: (-1) [Hard]
    size_t max_w = 100;
    Formula f = make_test_formula(2, max_w, {max_w, max_w, max_w},
                                  {{1}, {-1, 2}, {-2}});

    Preprocessor prep(f);
    bool contradiction = prep.reduce();

    REQUIRE(contradiction == true);
  }
}

TEST_CASE("Preprocessor - Full Pipeline, Reification, and Compaction",
          "[full_pipeline]") {
  // Clause 0: ( 1 OR  2) [Hard]
  // Clause 1: (-1 OR -2) [Hard]
  // Clause 2: ( 1 OR -2) [Soft, weight 5]
  // Clause 3: (-1 OR  2) [Soft, weight 5]
  //
  // Neither var 1 nor var 2 is pure (both appear + and - in hard & soft
  // clauses). No unit clauses exist. reduce() leaves all 4 clauses active.
  size_t max_w = 100;

  // clang-format off
  Formula f = make_test_formula(
      2, max_w,
      {max_w, max_w, 5, 5},
      {{1, 2}, {-1, -2}, {1, -2}, {-1, 2}}
  );
  // clang-format on

  Preprocessor prep(f);
  Formula compacted = prep.run();

  SECTION("Reification converts soft clauses and compaction maps variables") {
    // 2 original vars + 2 dummy reification vars = 4 total active vars
    REQUIRE(compacted.n_vars == 4);

    // 2 original hard + 2 reified hard + 2 new soft dummy clauses = 6 clauses
    REQUIRE(compacted.n_clauses == 6);

    // All 4 reified hard clauses should have max_weight
    REQUIRE(compacted.n_hard_clauses == 4);
    REQUIRE(compacted.max_weight == max_w);
  }
}

TEST_CASE("Preprocessor - Partial Pure Literal Elimination & Reification",
          "[preprocessor][pure]") {
  // Clause 0: ( 1 OR  2) [Hard]
  // Clause 1: (-1 OR -2) [Hard]
  // Clause 2: ( 3 OR  3) [Soft, w=10] -> Var 3 is pure positive!
  // Clause 3: ( 1 OR -2) [Soft, w=5]  -> Non-pure soft clause (var 1, 2 appear
  // + and -)
  size_t max_w = 100;
  Formula f = make_test_formula(3, max_w, {max_w, max_w, 10, 5},
                                {{1, 2}, {-1, -2}, {3}, {1, -2}});

  Preprocessor prep(f);
  Formula compacted = prep.run();

  SECTION("Eliminates pure soft clause and reifies remaining soft clause") {
    // Var 3 was pure positive -> forced to +1 -> Clause 2 satisfied
    REQUIRE(prep.get_force_assignments()[2] == 1);

    // Active variables left: Var 1, Var 2 + 1 Dummy Reification Var for Clause
    // 3 = 3 vars
    REQUIRE(compacted.n_vars == 3);

    // Clauses remaining:
    // 2 Hard Original + 1 Reified Hard (1 OR -2 OR 4) + 1 Reified Soft Unit
    // (-4) = 4 clauses
    REQUIRE(compacted.n_clauses == 4);
    REQUIRE(compacted.n_hard_clauses == 3);
  }
}

TEST_CASE("Preprocessor - Cascading Unit Propagation", "[preprocessor][unit]") {
  // Chain reaction across multiple iteration loops:
  // Clause 0: (1)        [Hard] -> Forces Var 1 = +1
  // Clause 1: (-1 OR 2)  [Hard] -> Shrinks to (2), forces Var 2 = +1
  // Clause 2: (-2 OR 3)  [Hard] -> Shrinks to (3), forces Var 3 = +1
  // Clause 3: (-3 OR 4)  [Soft] -> Shrinks to (4)
  // Clause 4: (-4)       [Soft] -> Prevents Clause 3 from becoming SAT.
  size_t max_w = 100;
  Formula f = make_test_formula(5, max_w, {max_w, max_w, max_w, 15, 15},
                                {{1}, {-1, 2}, {-2, 3}, {-3, 4}, {-4}});

  Preprocessor prep(f);
  bool contradiction = prep.reduce();

  SECTION("Propagates through multiple iterations until quiescence") {
    REQUIRE_FALSE(contradiction);
    REQUIRE(prep.get_force_assignments()[0] == 1);
    REQUIRE(prep.get_force_assignments()[1] == 1);
    REQUIRE(prep.get_force_assignments()[2] == 1);

    // Hard clauses 0, 1, 2 are all satisfied and deleted
    // Soft clause 3 became (4), clause 4 still active.
    REQUIRE(prep.get_active_clause_count() == 2);
  }
}

TEST_CASE("Preprocessor - Soft Clause Emptied vs Hard Contradiction",
          "[preprocessor][unsat]") {
  size_t max_w = 100;

  SECTION(
      "Emptying a Soft Clause purges it without causing global contradiction") {
    // Clause 0: (1)  [Hard] -> Forces Var 1 = +1
    // Clause 1: (-1) [Soft, w=20] -> Becomes empty (), should be purged
    Formula f = make_test_formula(1, max_w, {max_w, 20}, {{1}, {-1}});

    Preprocessor prep(f);
    bool contradiction = prep.reduce();

    REQUIRE_FALSE(contradiction);
    REQUIRE(prep.get_active_clause_count() == 0);
  }

  SECTION("Emptying a Hard Clause flags a global contradiction") {
    // Clause 0: (1)  [Hard] -> Forces Var 1 = +1
    // Clause 1: (-1) [Hard] -> Becomes empty (), contradiction!
    Formula f = make_test_formula(1, max_w, {max_w, max_w}, {{1}, {-1}});

    Preprocessor prep(f);
    bool contradiction = prep.reduce();

    REQUIRE(contradiction == true);
  }
}

TEST_CASE("Preprocessor - Compaction Re-indexes Variable Gaps",
          "[preprocessor][compact]") {
  // Formula uses variables 1 and 4. Variables 2 and 3 do not exist.
  // Clause 0: (1 OR -4)  [Hard]
  // Clause 1: (-1 OR 4) [Hard]
  size_t max_w = 100;
  Formula f = make_test_formula(4, max_w, {max_w, max_w}, {{1, -4}, {-1, 4}});

  Preprocessor prep(f);
  Formula compacted = prep.run();

  SECTION("Compacts variable index space down to contiguous range") {
    // Active original vars (1 and 4) are remapped to contiguous indices 1 and 2
    REQUIRE(compacted.n_vars == 2);
    REQUIRE(compacted.n_clauses == 2);
  }
}

TEST_CASE("Preprocessor - Duplicate Literal and Tautology Edge Cases",
          "[preprocessor][clean]") {
  size_t max_w = 100;

  SECTION("Tautologies (x OR -x) are removed") {
    Formula f = make_test_formula(2, max_w, {max_w, 10}, {{1, -1, 2}, {2}});

    Preprocessor prep(f);
    prep.order_literals_in_clauses();
    prep.remove_tautologies();

    // Clause 0 is a tautology, clause 1 remains
    REQUIRE(prep.get_active_clause_count() == 1);
  }
}

TEST_CASE("Solution Reconstruction - Forced Units & Partial Solving",
          "[reconstruct]") {
  size_t max_w = 100;
  Formula original_f = make_test_formula(4, max_w, {max_w, max_w, 10, 15},
                                         {{1}, {-1, 2}, {3, 4}, {-3, -4}});

  Preprocessor prep(original_f);
  Formula compact_f = prep.run();

  std::vector<bool> compact_solution = {true, false, false, false};
  REQUIRE(compact_f.n_vars == compact_solution.size());
  vector<bool> original_solution = prep.reconstruct_solution(compact_solution);

  SECTION("Output vector size strictly equals original variable count") {
    REQUIRE(original_solution.size() == original_f.n_vars);
  }

  SECTION(
      "Forced variables and solver-assigned variables match expected values") {
    REQUIRE(original_solution[0] == true); // Var 1 (forced unit)
    REQUIRE(original_solution[1] ==
            true); // Var 2 (forced unit via propagation)
    REQUIRE(original_solution[2] == true);  // Var 3 (solver output)
    REQUIRE(original_solution[3] == false); // Var 4 (solver output)
  }

  SECTION("Reconstructed score matches evaluation on original formula") {
    EvalResult eval = original_f.evaluate_assignment(original_solution);
    REQUIRE(eval.hard_satisfied == true);
    REQUIRE(eval.soft_score == 25);
  }
}

TEST_CASE("Solution Reconstruction - Non-contiguous Variable Remapping",
          "[reconstruct]") {
  size_t max_w = 100;
  Formula original_f =
      make_test_formula(4, max_w, {max_w, 20}, {{-1, 4}, {1, -4}});

  Preprocessor prep(original_f);
  Formula compact_f = prep.run();

  vector<bool> compact_solution = {false, false, false};
  vector<bool> original_solution = prep.reconstruct_solution(compact_solution);

  SECTION("Output vector size matches original dimension (4)") {
    REQUIRE(original_solution.size() == original_f.n_vars);
  }

  SECTION("Mapped variables maintain correctness in original index slots") {
    REQUIRE(original_solution[0] == false); // Original Var 1
    REQUIRE(original_solution[3] == false); // Original Var 4
  }

  SECTION("Reconstructed score matches direct formula evaluation") {
    EvalResult eval = original_f.evaluate_assignment(original_solution);
    REQUIRE(eval.hard_satisfied == true);
    REQUIRE(eval.soft_score == 20);
  }
}

TEST_CASE("Solution Reconstruction - Stripping Dummy Reification Variables",
          "[reconstruct]") {
  size_t max_w = 100;
  Formula original_f = make_test_formula(2, max_w, {max_w, max_w, 30, 40},
                                         {{1, 2}, {-1, -2}, {1, -2}, {-1, 2}});

  Preprocessor prep(original_f);
  Formula compact_f = prep.run();

  // No reductions can be made.
  REQUIRE(prep.get_active_clause_count() == 4);

  std::vector<bool> compact_solution = {false, true, false, true};
  vector<bool> original_solution = prep.reconstruct_solution(compact_solution);

  SECTION("Output length is trimmed down to original variable count (2)") {
    REQUIRE(original_solution.size() == original_f.n_vars);
    REQUIRE(original_solution.size() == 2);
    REQUIRE(original_solution[0] == false);
    REQUIRE(original_solution[1] == true);
  }

  SECTION("Evaluation on original formula matches returned score") {
    EvalResult eval = original_f.evaluate_assignment(original_solution);
    REQUIRE(eval.hard_satisfied == true);
    REQUIRE(eval.soft_score == 40);
  }
}

TEST_CASE("Solution Reconstruction - Fully Reduced Formula (All Pure)",
          "[reconstruct]") {
  size_t max_w = 100;
  Formula original_f =
      make_test_formula(4, max_w, {max_w, 15, 25}, {{1, 2}, {2, 3}, {-4}});

  Preprocessor prep(original_f);
  Formula compact_f = prep.run();

  REQUIRE(compact_f.n_vars == 0);

  std::vector<bool> empty_compact_solution = {};
  vector<bool> original_solution =
      prep.reconstruct_solution(empty_compact_solution);

  SECTION("Output size recovers full original dimension despite empty input") {
    REQUIRE(original_solution.size() == original_f.n_vars);
    REQUIRE(original_solution.size() == 4);
  }

  SECTION("Restores all forced assignments accurately") {
    REQUIRE(original_solution[0] == true);  // Var 1
    REQUIRE(original_solution[1] == true);  // Var 2
    REQUIRE(original_solution[2] == true);  // Var 3
    REQUIRE(original_solution[3] == false); // Var 4
  }

  SECTION("Calculates full score directly from pure literal satisfaction") {
    EvalResult eval = original_f.evaluate_assignment(original_solution);
    REQUIRE(eval.hard_satisfied == true);
    REQUIRE(eval.soft_score == 40);
  }
}

TEST_CASE("Can_reduce_this", "[run]") {
  string formula_string = R"(p wcnf 4 4 10
10 1 -1 2 0
1 1 2 3 0
1 3 2 1 0
1 4 -3 0)";

  Formula original_f = Parser::parse_string(formula_string);
  Preprocessor prep(original_f);
  Formula compact_f = prep.run();

  REQUIRE(compact_f.n_vars == 0);
  REQUIRE(compact_f.n_clauses == 0);
  REQUIRE(prep.get_active_clause_count() == 0);
}

// Helper to mock a Formula for isolated testing.
// Adjust the parameters if your Formula constructor differs slightly.
Formula create_mock_formula(size_t n_vars,
                            const std::vector<std::vector<int>> &clauses,
                            const std::vector<size_t> &weights) {
  size_t max_weight = 10000; // High weight acting as infinity for hard clauses
  size_t n_hard_clauses = 0;

  for (size_t w : weights) {
    if (w >= max_weight)
      n_hard_clauses++;
  }

  std::vector<int> cardinality(clauses.size(), 0);

  // Assuming FormulaType::WCNF since WCARD is caught in the constructor
  return Formula(n_vars, clauses.size(), n_hard_clauses, max_weight,
                 FormulaType::WCNF, weights, clauses, cardinality);
}

TEST_CASE("Clause Subsumption evaluates MaxSAT rules correctly",
          "[preprocessor][subsumption]") {
  size_t hard_weight = 10000;

  SECTION("Hard clause subsumes another Hard clause") {
    // C1: (1 v 2)    [Hard]
    // C2: (1 v 2 v 3)[Hard] -> Should be subsumed by C1
    std::vector<std::vector<int>> clauses = {{1, 2}, {1, 2, 3}};
    std::vector<size_t> weights = {hard_weight, hard_weight};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses(); // Precondition for subset check

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    // Active clauses should be reduced to 1
    REQUIRE(prep.get_active_clause_count() == 1);

    Formula compacted = prep.compact();
    REQUIRE(compacted.clauses.size() == 1);
    REQUIRE(compacted.clauses[0].size() == 2);
  }

  SECTION("Hard clause subsumes a Soft clause") {
    // C1: (1 v 2)    [Hard]
    // C2: (1 v 2 v 3)[Soft] -> Should be subsumed by C1
    std::vector<std::vector<int>> clauses = {{1, 2}, {1, 2, 3}};
    std::vector<size_t> weights = {hard_weight, 5};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    REQUIRE(made_changes == true);
    REQUIRE(prep.get_active_clause_count() == 1);

    Formula compacted = prep.compact();
    REQUIRE(compacted.clauses.size() == 1);
    REQUIRE(compacted.weights[0] == hard_weight);
  }

  SECTION("Identical Soft clauses are merged") {
    // C1: (1 v 2) [Soft, weight 3]
    // C2: (1 v 2) [Soft, weight 4]
    // -> Should merge into one clause with weight 7
    std::vector<std::vector<int>> clauses = {{1, 2}, {1, 2}};
    std::vector<size_t> weights = {3, 4};

    Formula f = create_mock_formula(2, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    REQUIRE(made_changes == true);
    REQUIRE(prep.get_active_clause_count() == 1);

    Formula compacted = prep.compact();
    REQUIRE(compacted.clauses.size() == 1);
    REQUIRE(compacted.weights[0] == 7);
  }

  SECTION("Strict Soft subsuming Soft is ignored (MaxSAT constraint)") {
    // C1: (1 v 2)     [Soft, weight 3]
    // C2: (1 v 2 v 3) [Soft, weight 4]
    // -> Neither can be removed in MaxSAT without resolving weights safely
    std::vector<std::vector<int>> clauses = {{1, 2}, {1, 2, 3}};
    std::vector<size_t> weights = {3, 4};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    REQUIRE(made_changes == false);
    REQUIRE(prep.get_active_clause_count() == 2);
  }

  SECTION("Soft clause subsuming Hard clause is ignored") {
    // C1: (1 v 2)     [Soft, weight 5]
    // C2: (1 v 2 v 3) [Hard]
    // -> Soft cannot subsume Hard
    std::vector<std::vector<int>> clauses = {{1, 2}, {1, 2, 3}};
    std::vector<size_t> weights = {5, hard_weight};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    REQUIRE(made_changes == false);
    REQUIRE(prep.get_active_clause_count() == 2);
  }

  SECTION("No subsumption - Fast fail on disjoint sets") {
    // Tests that unrelated clauses don't trigger false positives
    std::vector<std::vector<int>> clauses = {{1, 2}, {3, 4}};
    std::vector<size_t> weights = {hard_weight, hard_weight};

    Formula f = create_mock_formula(4, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.clause_subsumption(made_changes);

    REQUIRE(made_changes == false);
    REQUIRE(prep.get_active_clause_count() == 2);
  }
}

TEST_CASE("Self-Subsuming Elimination shortens clauses correctly",
          "[preprocessor][self_subsumption]") {
  size_t hard_weight = 10000;

  SECTION("Hard clause strengthens another Hard clause") {
    // C1: (1 v 2)        [Hard]
    // C2: (-1 v 2 v 3)   [Hard]
    // -> C2 should be strengthened to (2 v 3)
    std::vector<std::vector<int>> clauses = {{1, 2}, {-1, 2, 3}};
    std::vector<size_t> weights = {hard_weight, hard_weight};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.self_subsuming_elimination(made_changes);

    REQUIRE(made_changes == true);

    Formula compacted = prep.compact();
    REQUIRE(compacted.clauses.size() == 2);

    // Find clause that originally was C2
    auto c2_it =
        std::find_if(compacted.clauses.begin(), compacted.clauses.end(),
                     [](const std::vector<int> &c) { return c.size() == 2; });
    REQUIRE(c2_it != compacted.clauses.end());
    // Verify -1 was removed
    REQUIRE(std::find(c2_it->begin(), c2_it->end(), -1) == c2_it->end());
  }

  SECTION("Hard clause strengthens a Soft clause") {
    // C1: (1 v 2)        [Hard]
    // C2: (-1 v 2 v 3)   [Soft, weight 25]
    // -> C2 should be strengthened to (2 v 3) [Soft, weight 25]
    std::vector<std::vector<int>> clauses = {{1, 2}, {-1, 2, 3}};
    std::vector<size_t> weights = {hard_weight, 25};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.self_subsuming_elimination(made_changes);

    REQUIRE(made_changes == true);

    Formula compacted = prep.compact();
    REQUIRE(compacted.clauses.size() == 2);

    // Verify weights remain untouched
    REQUIRE(compacted.weights[0] == hard_weight);
    REQUIRE(compacted.weights[1] == 25);
  }

  SECTION("Soft clause attempting to strengthen Hard clause is ignored") {
    // C1: (1 v 2)        [Soft, weight 15]
    // C2: (-1 v 2 v 3)   [Hard]
    // -> Must not shorten C2 because C1 is Soft
    std::vector<std::vector<int>> clauses = {{1, 2}, {-1, 2, 3}};
    std::vector<size_t> weights = {15, hard_weight};

    Formula f = create_mock_formula(3, clauses, weights);
    Preprocessor prep(f);
    prep.order_literals_in_clauses();

    bool made_changes = false;
    prep.self_subsuming_elimination(made_changes);

    REQUIRE(made_changes == false);
  }
}

vector<bool> make_assignment(size_t n_vars, size_t seed) {
  // INFO: The idea is to generate a random 64-bit int and use it's binary
  // representation for our random 0/1 (false/true assignment).
  std::mt19937_64 gen(seed == 0 ? std::random_device{}() : seed);

  vector<bool> assignment(n_vars, false);

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
  return assignment;
}

TEST_CASE("Can reduce and compact a large formula", "[compact2]") {
  Formula f =
      Parser::parse("./test_data/cnf_instances/ssa/ssa7552-158-simplified.cnf");

  Preprocessor p = Preprocessor(f);
  Formula fp = p.run();

  // make a fake assignment for the preprocessed formula.
  vector<bool> assignment_p = make_assignment(fp.n_vars, 1);

  // reconstruct the assignment in terms of the original
  vector<bool> assignment = p.reconstruct_solution(assignment_p);

  vector<int> my_ass(f.n_vars, 0);
  for (size_t var_id = 0; var_id < fp.n_vars; var_id++) {

    size_t new_var_id_plus_1 = p.map.old_2_new[var_id + 1];
    if (new_var_id_plus_1 != 0) {
      // This variable was not removed from Formula.
      my_ass[var_id] = assignment_p[new_var_id_plus_1 - 1] ? 1 : -1;
    } else {
      // This variable was force assigned by the Preprocessor
      my_ass[var_id] = p.force_assignments[var_id];
    }
  }

  for (size_t clause_id = 0; clause_id < f.n_hard_clauses; ++clause_id) {
    for (auto lit : f.clauses[clause_id]) {
      size_t var_id = abs(lit) - 1;

      size_t new_var_id_plus_1 = p.map.old_2_new[var_id + 1];
      if (new_var_id_plus_1 != 0) {
        // This variable was not removed from Formula.

        REQUIRE(0 < new_var_id_plus_1);
        REQUIRE(new_var_id_plus_1 <= p.map.active_vars);
        REQUIRE(p.map.new_2_old[new_var_id_plus_1] - 1 == var_id);

        REQUIRE(assignment_p[new_var_id_plus_1 - 1] == assignment[var_id]);
      } else {
        // This variable was force assigned, check that it was moved over.
        REQUIRE(p.force_assignments[var_id] != 0);
        REQUIRE(assignment[var_id] == p.force_assignments[var_id] > 0);
      }
    }
  }
}
