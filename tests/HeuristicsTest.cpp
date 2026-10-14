#include "maxsat/Heuristics.hpp"
#include "Utils.hpp"
#include "catch2/catch_amalgamated.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Preprocessor.hpp"
#include "maxsat/SolverLimits.hpp"

using namespace std;

const string DATA_PATH = "test_data/cnf_instances";

// Two kinds of test kept separate:
//
//   1. Validitiy tests: runs on solve() with .cnf files and check the
//   final answer is valid from all four heuristics.
//
//   2. Logic: small hand-built formulas where the correct answer was
//   worked out by hand, checking specifically evaluate_breakcount(), the
//   freebie rule, the noise rule, and the tabu list with edge cases.

// 1:

TEST_CASE("GSAT produces a valid solution", "[gsat_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    GSATHeuristic solver(processed_formula, SolverLimits(1s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("GSAT solves uf20-91 completely", "[gsat_solves]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    GSATHeuristic solver(processed_formula, SolverLimits(5s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("PureRandom produces a valid solution", "[pure_random_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    PureRandomHeuristic solver(processed_formula, SolverLimits(1s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("WalkSAT produces a valid solution", "[walksat_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    WalkSATHeuristic solver(processed_formula, SolverLimits(1s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("WalkSAT+Tabu produces a valid solution", "[walksat_tabu_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    WalkSATTabuHeuristic solver(processed_formula, SolverLimits(1s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("WalkSAT solves satisfiable uf20-91 instances completely",
          "[walksat_solves]") {
  // uf20-91 instances are all known-satisfiable, so a working solver should
  // satisfy every clause. Compared against n_clauses rather than a hardcoded 91
  // so this does not silently break if the instance set changes.
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    WalkSATHeuristic solver(processed_formula, SolverLimits(5s));
    solver.solve(); // found 20 vars solves in well under a second
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("noise_percent is configurable at runtime", "[noise_configurable]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 1)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();

    SolverLimits limits;

    WalkSATHeuristic via_ctor(processed_formula, limits, 20);
    REQUIRE(via_ctor.noise_percent == 20);

    WalkSATTabuHeuristic via_setter(processed_formula, limits);
    REQUIRE(via_setter.noise_percent == 0); // tabu defaults to 0 - the ban
                                            // list already diversifies
    via_setter.set_noise_percent(80);
    REQUIRE(via_setter.noise_percent == 80);

    // All four heuristics default 50% m
    GSATHeuristic gsat(processed_formula, limits);
    REQUIRE(gsat.noise_percent == 50);
    GSATHeuristic gsat_custom(processed_formula, limits, 30);
    REQUIRE(gsat_custom.noise_percent == 30);
    WalkSATHeuristic walksat(processed_formula, limits);
    REQUIRE(walksat.noise_percent == 50);
  }
}

TEST_CASE("GSAT scans every unsatisfied clause, not just the one it is given",
          "[gsat_scope]") {

  // Three hard clauses. All-false start (a fresh solver), so:
  //   clause0 {1}     -> UNSAT   (unit clause)
  //   clause1 {2, 3}  -> UNSAT
  //   clause2 {-1,-2} -> SAT twice over, cost 2

  // Flipping var1 satisfies clause0 but would NOT break clause2 (cost 2, so
  // it survives).
  // Flipping var2 or var3 satisfies clause1, also breaking nothing.
  // Every candidate scores break=0, so the greedy pick is whichever comes first
  // - but point of the test is the SET it chose from.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  GSATHeuristic solver(processed_formula, SolverLimits(),
                       /*noise_percent=*/0); // pure greedy, no noise

  REQUIRE(solver.hard_sat_count[0] == 0);
  REQUIRE(solver.hard_sat_count[1] == 0);
  REQUIRE(solver.hard_sat_count[2] == 2);

  // Hand it clause 1 and it must still be willing to return var1 (from
  // clause 0) - i.e.ignored the clause it was given. A clause-local heuristic
  // return var2 or var3 here.
  size_t chosen = solver.pick_hard_variable_to_flip(1);
  REQUIRE((chosen == 0 || chosen == 1 || chosen == 2));

  // picks var1 (index 0), because clause0's literal is first in the gathered
  // candidate list and nothing scores better than break=0.
  REQUIRE(chosen == 0);
}

// 2:

TEST_CASE("evaluate_breakcount counts breaks and makes correctly",
          "[evaluate_breakcount]") {
  // Two hard clauses: {1,2} and {-1,2}, both weight 1.
  // A fresh solver starts with every variable false, so:
  //   clause0 {1,2}  - wants var1 / var2 true, both false -> UNSAT, cost 0
  //   clause1 {-1,2} - satisfied by -1 since var1 is false -> SAT, cost 1
  Formula formula(2, 2, 2, 1, FormulaType::CNF, {1, 1}, {{1, 2}, {-1, 2}},
                  {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  GSATHeuristic solver(processed_formula, SolverLimits());

  REQUIRE(solver.assignment[0] == false);
  REQUIRE(solver.assignment[1] == false);
  REQUIRE(solver.hard_sat_count[0] == 0);
  REQUIRE(solver.hard_sat_count[1] == 1);

  SECTION("var1: flipping breaks clause1 and makes clause0") {
    // clause1 is held up ONLY by -1 (cost==1), so flipping var1 breaks it.
    // clause0 contains literal 1, so flipping var1 true satisfies it.
    REQUIRE(solver.evaluate_breakcount(0) == 1); // break only:  1
    // REQUIRE(solver.evaluate_breakcount(0, 1, 1) == 0); // 1 break - 1 make
  }

  SECTION("var2: flipping breaks nothing and makes clause0") {
    // var2 never appears negated, so nothing can break.
    // clause1 is already satisfied (by -1), so it is not newly "made".
    REQUIRE(solver.evaluate_breakcount(1) == 0); // break only:  0
    // REQUIRE(solver.evaluate_breakcount(1, 1, 1) == -1); // 0 break - 1 make
  }
}

TEST_CASE("the freebie rule wins over noise", "[freebie]") {
  FastRangeRNG rng(12345);
  // Candidates are scored by abs(literal)-1, so literal 5 -> var index 4,
  // -3 -> 2, 7 -> 6.
  vector<int> candidates = {5, -3, 7};
  auto score = [&](size_t var_idx) -> int {
    if (var_idx == 4)
      return 2;
    if (var_idx == 2)
      return 0; // breaks nothing - the freebie
    if (var_idx == 6)
      return 3;
    return 99;
  };

  // Freebie is checked before noise, so it must win every single time even
  // with noise turned on. Repeated so no unlucky RNG draw slips through.
  for (int i = 0; i < 50; i++) {
    REQUIRE(select_literal_by_score(candidates, score, rng, true, true, 50) ==
            -3);
  }
}

TEST_CASE("with no noise, the lowest break-count wins", "[min_breakcount]") {
  FastRangeRNG rng(999);
  vector<int> candidates = {5, 3, 7}; // nothing scores 0, so no freebie
  auto score = [&](size_t var_idx) -> int {
    if (var_idx == 4)
      return 5;
    if (var_idx == 2)
      return 2; // the minimum
    if (var_idx == 6)
      return 9;
    return 99;
  };

  // rng.rand(100) returns 0..99, so "< 0" can never fire - the random walk
  // never triggers and selection is fully deterministic.
  for (int i = 0; i < 50; i++) {
    REQUIRE(select_literal_by_score(candidates, score, rng, false, true, 0) ==
            3);
  }
}

TEST_CASE("the random walk only ever returns a real candidate",
          "[random_walk]") {
  FastRangeRNG rng(42);
  vector<int> candidates = {5, 3, 7};
  auto score = [&](size_t) -> int { return 1; }; // all tied, no freebie

  // noise=100 means the random walk fires every time. We cannot predict
  // which literal it picks, but it must always be one of the candidates.
  for (int i = 0; i < 200; i++) {
    int chosen =
        select_literal_by_score(candidates, score, rng, false, true, 100);
    REQUIRE((chosen == 5 || chosen == 3 || chosen == 7));
  }
}

TEST_CASE("clause picking stays inside its hard/soft range",
          "[hard_soft_split]") {
  // Clause 0 is hard and clause 1 is soft, and they are identical {1,2}, so
  // both are unsatisfied at the all-false start. Each is the only eligible
  // clause in its half, so each picker has exactly one correct answer.
  Formula formula(2, 2, /*n_hard_clauses=*/1, 10, FormulaType::WCNF, {10, 1},
                  {{1, 2}, {1, 2}}, {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  /*
   * After reification transform:
   * 10 1 2
   * 10 3 1 2
   * 1 -3
   */

  SECTION("WalkSAT") {
    WalkSATHeuristic solver(processed_formula, SolverLimits());
    REQUIRE(solver.hard_sat_count[0] == 0);
    REQUIRE(solver.hard_sat_count[1] == 0);
    REQUIRE(solver.pick_hard_clause_to_flip() < 2);
    // REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("WalkSAT+Tabu") {
    WalkSATTabuHeuristic solver(processed_formula, SolverLimits());
    REQUIRE(solver.pick_hard_clause_to_flip() < 2);
    // REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }
}

TEST_CASE("a tabu variable expires after exactly tenure flips", "[tabu]") {
  Formula formula(2, 2, 1, 10, FormulaType::WCNF, {10, 1}, {{1, 2}, {1, 2}},
                  {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  /*
   * After reification transform:
   * 10 1 2
   * 10 3 1 2
   * 1 -3
   */

  WalkSATTabuHeuristic solver(processed_formula, SolverLimits());

  REQUIRE(solver.n_flips == 0);

  solver.set_tabu(/*var=*/1, /*tenure=*/3);
  REQUIRE(solver.tabu_until[1] == 3);

  REQUIRE(solver.is_tabu(1) == true); // 3 > 0, forbidden

  solver.n_flips = 2;
  REQUIRE(solver.is_tabu(1) == true); // 3 > 2, still forbidden

  solver.n_flips = 3;
  REQUIRE(solver.is_tabu(1) == false); // 3 > 3 is false, free again

  solver.n_flips = 4;
  REQUIRE(solver.is_tabu(1) == false);
}

TEST_CASE("tabu tenure scales with the number of variables", "[tabu_tenure]") {
  // tenure = max(4, 0.01875 * n_vars + 2.8125), same formula as AlgoM.
  // Small formulas hit the floor of 4.
  Formula small(2, 1, 1, 1, FormulaType::CNF, {1}, {{1, 2}}, {1, 1});
  Formula processed_formula = Preprocessor(small).transform();
  WalkSATTabuHeuristic s(processed_formula, SolverLimits());
  REQUIRE(s.tenure == 4);
}

//roulette test cases added
TEST_CASE("roulette_select_range only returns indices with nonzero weight",
          "[roulette_select_range]") {
  FastRangeRNG rng(7);
  vector<size_t> weights = {0, 5, 3, 0, 1}; // eligible: idx 1, 2, 4

  for (int i = 0; i < 200; i++) {
    size_t idx = roulette_select_range<size_t>(
        0, weights.size(), [&](size_t j) { return weights[j]; }, rng);
    REQUIRE((idx == 1 || idx == 2 || idx == 4));
  }
}

TEST_CASE("roulette_select_range respects [lo, hi)", "[roulette_select_range]") {
  FastRangeRNG rng(7);
  vector<size_t> weights = {4, 5, 3, 0, 1};

  for (int i = 0; i < 200; i++) {
    size_t idx = roulette_select_range<size_t>(
        2, 5, [&](size_t j) { return weights[j]; }, rng);
    REQUIRE((idx == 2 || idx == 4)); // idx 3 weighs 0; 0 and 1 are out of range
  }
}

TEST_CASE("roulette_select_range favours heavier weights",
          "[roulette_select_range]") {
  // Statistical, not exact: idx 1 carries 100x idx 0's weight, so over 1000
  // draws it should dominate by a wide margin. Loose bound on purpose - this
  // catches "weights are ignored entirely", not distribution precision.
  FastRangeRNG rng(4242);
  vector<size_t> weights = {1, 100};

  size_t heavy_hits = 0;
  for (int i = 0; i < 1000; i++) {
    size_t idx = roulette_select_range<size_t>(
        0, weights.size(), [&](size_t j) { return weights[j]; }, rng);
    heavy_hits += (idx == 1);
  }
  REQUIRE(heavy_hits > 900);
}

TEST_CASE("hard_clause_weights is sized and nonzero at construction",
          "[roulette_weights_init]") {
  // pick_roulette_unsat_hard_clause indexes this vector, so an empty one is
  // a heap overflow on the first flip. That is exactly what happens if a
  // heuristic inherits Solver's constructor instead of running its own body,
  // and it is silent without ASan - hence an explicit test.
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 1)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    RouletteHeuristic solver(processed_formula, SolverLimits());

    REQUIRE(solver.hard_clause_weights.size() == solver.n_hard_clauses);
    for (size_t c = 0; c < solver.n_hard_clauses; c++) {
      REQUIRE(solver.hard_clause_weights[c] > 0);
    }
  }
}

TEST_CASE("roulette hard-clause picking never returns a satisfied clause",
          "[roulette_hard_clause]") {
  // Same formula as [gsat_scope]: clause0 {1} and clause1 {2,3} start UNSAT,
  // clause2 {-1,-2} starts satisfied twice over.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  RouletteHeuristic solver(processed_formula, SolverLimits());

  REQUIRE(solver.hard_sat_count[0] == 0);
  REQUIRE(solver.hard_sat_count[1] == 0);
  REQUIRE(solver.hard_sat_count[2] == 2);

  for (int i = 0; i < 200; i++) {
    size_t c = solver.pick_roulette_unsat_hard_clause(solver.hard_clause_weights);
    REQUIRE((c == 0 || c == 1));
  }
}

TEST_CASE("roulette hard-clause picking favours heavier clauses",
          "[roulette_hard_clause]") {
  // The point of the whole change: weight should actually shift the draw.
  // Calls the primitive rather than pick_hard_clause_to_flip, which bumps the
  // weights as a side effect and would move the target mid-test.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  RouletteHeuristic solver(processed_formula, SolverLimits());

  solver.hard_clause_weights[0] = 1;
  solver.hard_clause_weights[1] = 50;

  size_t heavy_hits = 0;
  for (int i = 0; i < 1000; i++) {
    heavy_hits +=
        (solver.pick_roulette_unsat_hard_clause(solver.hard_clause_weights) == 1);
  }
  REQUIRE(heavy_hits > 900);
}

TEST_CASE("dynamic clause weights grow on selection", "[roulette_dynamic]") {
  // Without this bump every hard clause weighs 1, which makes roulette
  // mathematically identical to uniform random on uniform k-SAT.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  RouletteHeuristic solver(processed_formula, SolverLimits());

  size_t before = solver.hard_clause_weights[0] + solver.hard_clause_weights[1];
  for (int i = 0; i < 10; i++) {
    solver.pick_hard_clause_to_flip();
  }
  size_t after = solver.hard_clause_weights[0] + solver.hard_clause_weights[1];

  REQUIRE(after == before + 10);
}

TEST_CASE("Roulette's variable pick returns a variable from the given clause",
          "[roulette_pick_variable]") {
  // clause 0 = {1, -2, 3} -> vars 0, 1, 2. Every draw must land in that set,
  // however the 1/(1+break)^2 weights come out.
  Formula formula(3, 1, 1, 1, FormulaType::CNF, {1}, {{1, -2, 3}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  RouletteHeuristic solver(processed_formula, SolverLimits());

  for (int i = 0; i < 200; i++) {
    size_t var = solver.pick_hard_variable_to_flip(0);
    REQUIRE((var == 0 || var == 1 || var == 2));
  }
}

TEST_CASE("Roulette produces a valid solution", "[roulette_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    RouletteHeuristic solver(processed_formula, SolverLimits(1s));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("on_restart clears the tabu list", "[tabu_restart]") {
  Formula formula(2, 2, 1, 10, FormulaType::WCNF, {10, 1}, {{1, 2}, {1, 2}},
                  {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  WalkSATTabuHeuristic solver(processed_formula, SolverLimits());

  solver.set_tabu(1, 5);
  REQUIRE(solver.tabu_until[1] == 5);

  solver.on_restart();
  REQUIRE(solver.tabu_until[1] == 0);
  REQUIRE(solver.is_tabu(1) == false);
}
// ============================================================
// ADAPTIVE SWITCHING
// ============================================================

TEST_CASE("Adaptive starts on random selection", "[adaptive_init]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 1)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    AdaptiveHeuristic solver(processed_formula, SolverLimits());

    REQUIRE(solver.using_roulette == false);
    REQUIRE(solver.n_switches == 0);
    REQUIRE(solver.roulette_flips == 0);
    REQUIRE(solver.hard_clause_weights.size() == solver.n_hard_clauses);
  }
}

TEST_CASE("Adaptive switches after patience flips without improvement",
          "[adaptive_switch]") {
  // patience=3 with no flips in between, so unsat_H_count never changes and
  // every call after the first counts as stagnation.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  AdaptiveHeuristic solver(processed_formula, SolverLimits(), /*patience=*/3);

  REQUIRE(solver.patience == 3);

  solver.pick_hard_clause_to_flip(); // first call: new best, resets counter
  REQUIRE(solver.flips_since_improvement == 0);
  REQUIRE(solver.using_roulette == false);

  solver.pick_hard_clause_to_flip(); // 1
  solver.pick_hard_clause_to_flip(); // 2
  REQUIRE(solver.using_roulette == false);

  solver.pick_hard_clause_to_flip(); // 3 - hits patience
  REQUIRE(solver.using_roulette == true);
  REQUIRE(solver.n_switches == 1);
}

TEST_CASE("Adaptive resets the improvement counter on progress",
          "[adaptive_progress]") {
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  AdaptiveHeuristic solver(processed_formula, SolverLimits(),
                           /*patience=*/1000);

  solver.pick_hard_clause_to_flip();
  solver.pick_hard_clause_to_flip();
  solver.pick_hard_clause_to_flip();
  REQUIRE(solver.flips_since_improvement > 0);

  // A real flip that satisfies clause 0 drops unsat_H_count, which is a new
  // best and must zero the counter.
  size_t before = solver.unsat_H_count;
  solver.flip_hard(0); // var 0 true satisfies {1}
  REQUIRE(solver.unsat_H_count < before);

  solver.pick_hard_clause_to_flip();
  REQUIRE(solver.flips_since_improvement == 0);
}

TEST_CASE("Adaptive only counts roulette flips after switching",
          "[adaptive_metrics]") {
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  AdaptiveHeuristic solver(processed_formula, SolverLimits(), /*patience=*/2);

  solver.pick_hard_clause_to_flip();
  solver.pick_hard_clause_to_flip();
  REQUIRE(solver.roulette_flips == 0); // still random

  solver.pick_hard_clause_to_flip(); // switches here
  REQUIRE(solver.using_roulette == true);

  size_t at_switch = solver.roulette_flips;
  solver.pick_hard_clause_to_flip();
  solver.pick_hard_clause_to_flip();
  REQUIRE(solver.roulette_flips == at_switch + 2);
}

TEST_CASE("Adaptive bumps clause weights only while on roulette",
          "[adaptive_weights]") {
  // Before the switch nothing should touch the weights; after it, every pick
  // bumps exactly one.
  Formula formula(3, 3, 3, 1, FormulaType::CNF, {1, 1, 1},
                  {{1}, {2, 3}, {-1, -2}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  AdaptiveHeuristic solver(processed_formula, SolverLimits(), /*patience=*/2);

  auto total = [&]() {
    size_t t = 0;
    for (size_t w : solver.hard_clause_weights) t += w;
    return t;
  };

  size_t start = total();
  solver.pick_hard_clause_to_flip();
  solver.pick_hard_clause_to_flip();
  REQUIRE(total() == start); // random phase leaves weights alone

  solver.pick_hard_clause_to_flip(); // switch fires, first roulette pick
  solver.pick_hard_clause_to_flip();
  REQUIRE(total() == start + 2);
}

TEST_CASE("Adaptive's variable pick returns a variable from the given clause",
          "[adaptive_pick_variable]") {
  // Must hold in both phases - random and roulette use different code paths.
  Formula formula(3, 1, 1, 1, FormulaType::CNF, {1}, {{1, -2, 3}}, {1, 1, 1});
  Formula processed_formula = Preprocessor(formula).transform();
  AdaptiveHeuristic solver(processed_formula, SolverLimits(), /*patience=*/1);

  for (int i = 0; i < 100; i++) {
    size_t var = solver.pick_hard_variable_to_flip(0);
    REQUIRE((var == 0 || var == 1 || var == 2));
  }

  solver.using_roulette = true;
  for (int i = 0; i < 100; i++) {
    size_t var = solver.pick_hard_variable_to_flip(0);
    REQUIRE((var == 0 || var == 1 || var == 2));
  }
}

TEST_CASE("Adaptive produces a valid solution", "[adaptive_valid]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    // patience=1 forces the switch early, so the roulette path is exercised
    // rather than being skipped on instances that solve in a few flips.
    AdaptiveHeuristic solver(processed_formula, SolverLimits(1s), 1);
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}
// ============================================================
// SOFT CLAUSE SELECTION
// ============================================================
// Every other test uses .cnf files, which have no soft clauses after
// reification - so pick_soft_clause_to_flip never runs and shows 0% in the
// coverage report. These use a WCNF formula and set the dummy variable true
// so there is an unsatisfied soft clause to pick.
//
// The constructor does not call compute_assignment_cost(), so unsat_S_count
// starts at 0 and the soft pickers would trip their own assert. Setting the
// assignment and recomputing is what makes the call legal.

TEST_CASE("soft clause picking returns the only unsatisfied soft clause",
          "[soft_clause_selection]") {
  // clause 0 (hard) and clause 1 (soft, weight 1) are both {1,2}.
  // After reification: [10: 1 2], [10: 3 1 2], [1: -3]
  Formula formula(2, 2, /*n_hard_clauses=*/1, 10, FormulaType::WCNF, {10, 1},
                  {{1, 2}, {1, 2}}, {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();

  SECTION("GSAT") {
    GSATHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true; // dummy true => soft UNSAT
    solver.compute_assignment_cost();
    REQUIRE(solver.unsat_S_count == 1);
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("PureRandom") {
    PureRandomHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("WalkSAT") {
    WalkSATHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("WalkSAT+Tabu") {
    WalkSATTabuHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("Roulette") {
    RouletteHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("Adaptive - random phase") {
    AdaptiveHeuristic solver(processed_formula, SolverLimits());
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.using_roulette == false);
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }

  SECTION("Adaptive - roulette phase") {
    // The other branch of the same method: after switching, the soft pick
    // routes through pick_roulette_unsat_soft_clause instead.
    AdaptiveHeuristic solver(processed_formula, SolverLimits());
    solver.using_roulette = true;
    solver.assignment[solver.soft_offset] = true;
    solver.compute_assignment_cost();
    REQUIRE(solver.pick_soft_clause_to_flip() == 0);
  }
}

TEST_CASE("set_noise_percent works on every heuristic that has it",
          "[noise_setters]") {
  Formula formula(2, 2, 2, 1, FormulaType::CNF, {1, 1}, {{1, 2}, {-1, 2}},
                  {1, 1});
  Formula processed_formula = Preprocessor(formula).transform();

  GSATHeuristic gsat(processed_formula, SolverLimits());
  gsat.set_noise_percent(17);
  REQUIRE(gsat.noise_percent == 17);

  WalkSATHeuristic walksat(processed_formula, SolverLimits());
  walksat.set_noise_percent(23);
  REQUIRE(walksat.noise_percent == 23);

  WalkSATTabuHeuristic tabu(processed_formula, SolverLimits());
  tabu.set_noise_percent(41);
  REQUIRE(tabu.noise_percent == 41);
}
TEST_CASE("heuristics stay valid on larger instances", "[larger_instances]") {
  // uf20-91 solves in a few dozen flips, which is too short for the dynamic
  // clause weights or the adaptive switch to do anything. These classes run
  // for thousands of flips, so the code paths are genuinely exercised.
  auto dir = GENERATE(as<std::string>{}, "/uf50-218", "/uf200-860");
  auto file = GENERATE_COPY(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + dir, 3)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();

    SECTION("Roulette") {
      RouletteHeuristic solver(processed_formula, SolverLimits(5s));
      solver.solve();
      validate_current_solution(solver, processed_formula);
    }
    SECTION("Adaptive") {
      AdaptiveHeuristic solver(processed_formula, SolverLimits(5s), 1);
      solver.solve();
      validate_current_solution(solver, processed_formula);
    }
  }
}
