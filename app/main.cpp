#include "cxxopts.hpp"
#include "maxsat/AlgoM.hpp"
#include "maxsat/AlgoM02.hpp"
#include "maxsat/AlgoM03.hpp"
#include "maxsat/Heuristics.hpp"
#include "maxsat/ISolver.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Preprocessor.hpp"
#include <cassert>
#include <csignal>
#include <cstddef>
#include <memory>
#include <print>
#include <string>

using namespace std;
using namespace std::chrono_literals;

atomic<bool> solving(true);
atomic<size_t> exit_counter(0);

void signal_handler(int signum) {
  println("Interrupted({}). Stopping solve... (press ctrl-C {} more times to "
          "force exit)",
          signum, 5 - exit_counter);

  solving = false;
  // fail safe in case solver does not respect the solver=false flag
  if (++exit_counter == 5) {
    println("User forced exit.");
    exit(signum);
  }
}

int main(int argc, char *argv[]) {
  signal(SIGINT, signal_handler);

  string input_filename, solver_name, restart_strategy;
  size_t restart_cutoff, seed;
  unsigned noise_percent;
  bool should_preprocess = false;
  double time_limit, restart_factor;

  try {
    cxxopts::Options options("MySolver", "My SAT Solver");

    // clang-format off
    options.add_options()
      ("formula", "The formula file to solve.", cxxopts::value<std::string>())
      ("solver", "The solver config to use.", cxxopts::value<std::string>()->default_value("gsat"))
      ("restart_strategy", "The restart strategy to use.", cxxopts::value<std::string>()->default_value("none"))
      ("rc,restart_cutoff", "Flips before a restart. 0 = scale with n_vars (50 per var).", cxxopts::value<size_t>()->default_value("0"))
      ("rf,restart_factor", "The factor to use for geometric strategy.", cxxopts::value<double>()->default_value("1.5"))
      ("t,timeout", "Max time allowed to solve (seconds).", cxxopts::value<double>()->default_value("0"))
      ("n,noise", "Noise percent 0-100 for gsat/walksat/walksat_tabu. Default 50.", cxxopts::value<unsigned>()->default_value("50"))
      ("p,preprocess", "Should preprocessing be applied?")
      ("s,seed", "Seed value for Solving", cxxopts::value<size_t>()->default_value("0"))
      ("h,help", "Print usage")
    ;
    // clang-format on
    options.parse_positional({"formula", "solver", "restart_strategy"});
    options.positional_help("<formula> <solver> <restart_strategy>");
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      println("{}", options.help());
      exit(0);
    }

    input_filename = result["formula"].as<string>();
    solver_name = result["solver"].as<string>();
    noise_percent = result["noise"].as<unsigned>();
    time_limit = result["timeout"].as<double>();
    should_preprocess = result["preprocess"].as<bool>();
    restart_strategy = result["restart_strategy"].as<string>();
    restart_cutoff = result["restart_cutoff"].as<size_t>();
    restart_factor = result["restart_factor"].as<double>();
    seed = result["seed"].as<size_t>();

    if (time_limit < 0) {
      throw cxxopts::exceptions::parsing(
          "Option '--timeout' cannot be negative");
    }
  } catch (const cxxopts::exceptions::exception &e) {
    println("error parsing: {}", e.what());
    exit(1);
  }

  Formula f = Parser::parse(input_filename);
  SolverLimits limits(std::chrono::duration<double>(time_limit), &solving);

  Preprocessor preprocessor(f);
  optional<Formula> compact_f;
  const Formula *formula = &f;

  if (should_preprocess) {
    compact_f = preprocessor.run();
  } else {
    compact_f = preprocessor.transform();
  }
  formula = &compact_f.value();

  // Don't need to use pointers, but this works for now.
  unique_ptr<ISolver> solver = nullptr;

  RestartStrategy restarts = RestartStrategy::None();

  // CLAUDE REPORT/HELP:
  //  Restart cutoffs scale with problem size: the search space is 2^n_vars,
  //  so a flip budget suited to 20 variables starves 250. --rc 0 means
  //  "scale automatically"; any explicit value overrides.
  //  ~50 flips per variable was the sweet spot on uf200-860 (median 8.8k
  //  flips vs 38k with no restarts; a cutoff of 1000 there was worse than
  //  no restarts at all).
  size_t cutoff = restart_cutoff;
  if (cutoff == 0) {
    cutoff = 50 * formula->n_vars;
  }

  if (restart_strategy == "luby") {
    restarts = RestartStrategy::Luby(cutoff);
  } else if (restart_strategy == "fixed") {
    restarts = RestartStrategy::Fixed(cutoff);
  } else if (restart_strategy == "geometric") {
    restarts = RestartStrategy::Geometric(cutoff, restart_factor);
  }

  if (solver_name == "AlgoM" || solver_name == "AlgoM01") {
    solver = unique_ptr<AlgoM>(new AlgoM(input_filename, limits));
  } else if (solver_name == "AlgoM02") {
    solver = unique_ptr<AlgoM02>(new AlgoM02(*formula, limits));
  } else if (solver_name == "AlgoM03") {
    solver = unique_ptr<AlgoM03>(new AlgoM03(*formula, limits));
  } else if (solver_name == "gsat" || solver_name == "AlgoM04") {
    solver = unique_ptr<GSATHeuristic>(
        new GSATHeuristic(*formula, limits, noise_percent, restarts));
  } else if (solver_name == "walksat") {
    solver = unique_ptr<WalkSATHeuristic>(
        new WalkSATHeuristic(*formula, limits, noise_percent, restarts));
  } else if (solver_name == "walksat_tabu") {
    solver = unique_ptr<WalkSATTabuHeuristic>(
        new WalkSATTabuHeuristic(*formula, limits, noise_percent, restarts));
  } else if (solver_name == "random") {
    solver = unique_ptr<PureRandomHeuristic>(
        new PureRandomHeuristic(*formula, limits, restarts));
  } else if (solver_name == "roulette") {
    solver = unique_ptr<RouletteHeuristic>(
        new RouletteHeuristic(*formula, limits, restarts));
  } else if (solver_name == "adaptive") {
    // 0 = use the built-in default, max(50, n_vars)
    solver = unique_ptr<AdaptiveHeuristic>(
        new AdaptiveHeuristic(*formula, limits, 0, restarts));
  } else {
    println("Unrecognised solver config. Options: AlgoM01, AlgoM02, "
            "AlgoM03, gsat, walksat, walksat_tabu, random, roulette, adaptive");
    exit(1);
  }

  solver->set_seed(seed);
  solver->solve();

  vector<bool> assignment = solver->get_best_assignment();
  EvalResult evaluation;

  vector<bool> original_assignment =
      preprocessor.reconstruct_solution(assignment);
  evaluation = f.evaluate_assignment(original_assignment);

  solver->print_solve_metrics();

  println("{}", evaluation.hard_satisfied ? "SAT" : "UNSAT");
  println("score: {}/{} ({:.2f}%)", evaluation.score, f.get_max_score(),
          (evaluation.score / f.get_max_score()) * 100);
  println("soft_cost: {}", evaluation.soft_cost);

  return 0;
}
