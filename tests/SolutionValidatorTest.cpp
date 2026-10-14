#include "Utils.hpp"
#include "catch2/catch_amalgamated.hpp"
#include "maxsat/Heuristics.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Preprocessor.hpp"
#include <chrono>

using namespace std;
using namespace std::chrono_literals;

const string DATA_PATH = "test_data/cnf_instances";

// CHANGED: was Solver<PureRandomHeuristic> / Solver<GSATHeuristic>.
// Construct the heuristic class itself, not the base template. GSATHeuristic
// now holds a noise_percent member, so the base template is too small for it.

TEST_CASE("Can produce a valid score (sat formulas)", "[sat_valid_score_sat]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    PureRandomHeuristic solver(processed_formula, SolverLimits(100ms));
    // Solver<PureRandomHeuristic> solver(formula, SolverLimits(100ms));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("Can produce a valid score (unsat formulas)",
          "[unsat_valid_score_sat]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uuf250-1065", 3)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();
    GSATHeuristic solver(processed_formula, SolverLimits(1ms));
    // Solver<GSATHeuristic> solver(formula, SolverLimits(1ms));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("Can produce a valid score (unsat wcnf formulas)",
          "[unsat_valid_score_sat]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir("./test_data/PreprocessorTest")));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula formula = Parser::parse(file.string());
    Formula processed_formula = Preprocessor(formula).transform();

    GSATHeuristic solver(processed_formula, SolverLimits(1ms));
    solver.solve();
    validate_current_solution(solver, processed_formula);
  }
}

TEST_CASE("Can produce valid score (sat preprocessed cnf formula)",
          "[preprocessed_valid_score_sat]") {

  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir("./test_data/cnf_instances/ssa/")));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {
    Formula f = Parser::parse(file.string());
    Preprocessor p = Preprocessor(f);
    Formula fp = p.run();

    WalkSATHeuristic solver(fp, SolverLimits());
    solver.set_seed(1);

    solver.solve();
    REQUIRE(solver.unsat_H_count == 0);
    REQUIRE(solver.unsat_S_count == 0);

    vector<bool> best_assignment = solver.get_best_assignment();

    EvalResult result = fp.evaluate_assignment(best_assignment);
    REQUIRE(result.hard_satisfied == true);
    REQUIRE(result.hard_satisfied == solver.get_hard_satisfied());
    REQUIRE(result.soft_cost == solver.get_soft_cost());
    REQUIRE(result.soft_cost == 0);

    vector<bool> original_assignment = p.reconstruct_solution(best_assignment);
    EvalResult original_result = f.evaluate_assignment(original_assignment);

    REQUIRE(original_result.hard_satisfied == true);
    REQUIRE(original_result.hard_satisfied == solver.get_hard_satisfied());
    REQUIRE(original_result.soft_cost == solver.get_soft_cost());
    REQUIRE(original_result.soft_cost == 0);

    for (size_t clause_id = 0; clause_id < f.n_hard_clauses; ++clause_id) {
      for (auto lit : f.clauses[clause_id]) {
        size_t var_id = abs(lit) - 1;
        size_t new_var_id_plus_1 = p.map.old_2_new[var_id + 1];

        if (new_var_id_plus_1 != 0) {
          // This variable was not removed
          REQUIRE(0 < new_var_id_plus_1);
          REQUIRE(new_var_id_plus_1 <= p.map.active_vars);

          REQUIRE(p.map.new_2_old[new_var_id_plus_1] - 1 == var_id);

          REQUIRE(best_assignment[new_var_id_plus_1 - 1] ==
                  original_assignment[var_id]);
        } else {
          // This variable was force assigned, check that it was moved over.
          REQUIRE(p.force_assignments[var_id] != 0);
          REQUIRE(original_assignment[var_id] ==
                  p.force_assignments[var_id] > 0);
        }
      }
    }
  }
}
