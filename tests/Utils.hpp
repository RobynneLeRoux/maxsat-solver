#include "catch2/catch_amalgamated.hpp"
#include "maxsat/Formula.hpp"
#include "maxsat/ISolver.hpp"
#include <filesystem>

using namespace std;

namespace fs = filesystem;

inline vector<fs::path> get_files_in_dir(const fs::path &dirPath,
                                         size_t n_files = 0) {
  vector<fs::path> files;
  if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
    for (const auto &entry : fs::directory_iterator(dirPath)) {
      if (entry.is_regular_file() &&
          ((n_files == 0) || (files.size() < n_files))) {
        files.push_back(entry.path());
      }
    }
  }
  return files;
}

inline void validate_current_solution(ISolver &s, Formula &f) {
  // NOTE: If the solver s is using a Preprocessed formula, you must pass that
  // preprocessed formula into this validator. It will fail otherwise.
  EvalResult result = f.evaluate_assignment(s.get_assignment());
  REQUIRE(result.hard_satisfied == s.get_hard_satisfied());
  REQUIRE(result.soft_cost == s.get_soft_cost());
}
