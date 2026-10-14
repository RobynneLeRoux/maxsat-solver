#include "Utils.hpp"
#include "catch2/catch_amalgamated.hpp"
#include "maxsat/AlgoM.hpp"
#include "maxsat/Heuristics.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Preprocessor.hpp"

using namespace std;

const string DATA_PATH = "test_data/cnf_instances";

TEST_CASE("AlgoM can solve basic cnf problems", "[can_solve]") {
  auto file = GENERATE(Catch::Generators::from_range(
      get_files_in_dir(DATA_PATH + "/uf20-91", 20)));

  DYNAMIC_SECTION("Checking: " << file.filename().string()) {

    AlgoM solver(file.string(), SolverLimits());

    solver.solve();
    REQUIRE(solver.best_score == 91);
  }
}

TEST_CASE("Solver can load a formula", "[solver_can_load_formula]") {
  string formula_string = R"(p cnf 4 6
  1 2 3 0
  1 2 0
  1 2 3 4 0
  1 2 0
  1 2 3 0
  1 2 0)";

  // Formula f = Preprocessor(Parser::parse_string(formula_string)).transform();
  Formula f = Preprocessor(Parser::parse(
                               "./test_data/cnf_instances/ssa/ssa7552-158.cnf"))
                  .transform();

  WalkSATHeuristic solver(f, SolverLimits());
}
