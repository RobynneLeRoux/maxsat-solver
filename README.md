# MaxSAT Solver

A high-performance **stochastic local search solver for weighted MaxSAT** in modern C++ (C++23), built as a team capstone project at the University of Cape Town (2026).

MaxSAT is an NP-hard optimisation problem: satisfy every hard constraint while minimising the total weight of broken soft constraints. It underpins scheduling, planning and logistics problems.

## Results
- The final solver ran **~85× faster** than our first Algorithm M implementation and reached **~1.5 million flips per second**
- On standard SATLIB benchmarks the heuristics solved problems **~7× faster** than a random baseline, matched the reference solver **YalSAT** on small instances (uf20) and came within 3–6% on medium ones (uf50)

## How it's fast
- Clauses are stored in one flat, length-sorted array of literals, so the hot loop walks contiguous memory instead of chasing pointers
- Each hard clause tracks how many literals satisfy it and which single literal is "critical", so a variable's break-count is computed in one pass over its occurrence list
- Flips update satisfied-counts incrementally instead of re-evaluating the formula
- Random assignments are generated 64 bits at a time from one random number
- Heuristics plug into the solver through C++ templates (CRTP), so choosing a heuristic costs nothing at runtime

## My role: search heuristics
I designed, implemented, unit-tested and benchmarked the heuristics in `include/maxsat/Heuristics.hpp`:

| Heuristic | Idea |
|---|---|
| WalkSAT | Pick a random broken clause, flip the variable that breaks the fewest others (with noise) |
| WalkSAT + Tabu | WalkSAT with a ban list so recent flips are not immediately undone |
| GSAT | Consider every broken clause and take the best flip overall |
| Roulette-wheel | Weighted random choice of clause and variable, balancing greed and exploration; stubborn clauses gain weight |
| Adaptive | Starts with uniform random choices and switches to roulette once the search stops improving |
| Pure random | Baseline for comparison |

`heuristic_shootout.sh` runs every heuristic side by side; `tests/HeuristicsTest.cpp` holds their unit tests. I also co-developed the core solver loop.

## Project layout
| Path | Contents |
|---|---|
| `include/maxsat/` | Solver, heuristics, formula, restart strategies, RNG |
| `src/` | Parser (`.cnf`, `.wcnf`, `.wcard`), preprocessor, Algorithm M versions |
| `app/main.cpp` | Command-line entry point |
| `tests/` | Catch2 unit tests and benchmark scripts |
| `test_data/` | SATLIB benchmark instances and small weighted test formulas |

## Build

There are 2 build options `debug` and `release`. Executables are compiled to `build/debug` or `build/release`.

### Debug

This is the default option.

```bash
make
```

### Release

You can compile with optimization set to the max. Asserts are removed, so it's good for performance testing, but not for development.

```bash
make release
```

## Testing

### Run all tests
All tests are compiled and run with:

```bash
make test
```

You can run individual tests with:

```bash
./build/debug/tests/PreprocessorTest
```
...replacing `PreprocessorTest` with the test you want to run

### Filtering
You can run specific test cases by passing a filter argument

```cpp
TEST_CASE("Can do foo", "[foo_test]") {
  // ... test logic here
}
```

By test case name

```bash
./build/debug/tests/PreprocessorTest "Can do foo"
```

Or by tag

```bash
./build/debug/tests/PreprocessorTest "[foo_test]"
```

## Usage
You can call the solver with:

```bash
./build/debug/app/main <formula> <solver> -t <seconds>
```

where:

- formula is the filepath of the .cnf, .wcnf or .wcard file you want to solve.
- solver is the solving technique you want to use (e.g. `walksat`, `walksat_tabu`, `gsat`, `roulette`, `adaptive`, `random`)
- -t allows you to specify how long the solver should run for (default = 0, no time limit)

## Dependencies

- make
- [hyperfine](https://github.com/sharkdp/hyperfine) for nice statistics used in `./tests/benchmark.sh`

## External libraries used

The header files for the following repositories have been copied into our repository.

- [catch2](https://github.com/catchorg/Catch2) for testing
- [cxxopts](https://github.com/jarro2783/cxxopts) for easy command line options

**Compiler:** the Makefile targets Clang with libc++ (the default on macOS). C++23 is required (`<print>`).

## Team
Robynne Le Roux (heuristics), Ashley Minshall and Laith Mohamed. Supervisor: Johan Bontes.
