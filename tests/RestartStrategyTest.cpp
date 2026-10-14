#include "maxsat/RestartStrategy.hpp"
#include "catch2/catch_amalgamated.hpp"
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Drives on_flip() exactly `n` times and returns how many of those calls
// returned true (i.e. how many restarts were signalled).
static size_t drive_flips(RestartStrategy &s, size_t n) {
  size_t restarts = 0;
  for (size_t i = 0; i < n; i++) {
    if (s.on_flip()) restarts++;
  }
  return restarts;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("Default construction leaves raw member initializers untouched",
          "[construction]") {
  // RestartStrategy() = default -- reset() is NOT invoked, so state is
  // exactly the in-class member initializers.
  RestartStrategy s;

  CHECK(s.type == RestartType::NONE);
  CHECK(s.base_unit == 100);
  CHECK(s.factor == 1.5);
  CHECK(s.total_flips == 0);
  CHECK(s.flips_in_run == 0);
  CHECK(s.current_cutoff == 0); // never computed, still 0
  CHECK(s.luby_u == 1);
  CHECK(s.luby_v == 1);
  CHECK(s.get_n_flips() == 0);
}

TEST_CASE("Parameterized constructor calls reset() and derives current_cutoff",
          "[construction]") {
  SECTION("NONE") {
    RestartStrategy s(RestartType::NONE, 5, 3.0);
    CHECK(s.current_cutoff == 0);
  }
  SECTION("FIXED") {
    RestartStrategy s(RestartType::FIXED, 37);
    CHECK(s.current_cutoff == 37);
  }
  SECTION("LUBY") {
    RestartStrategy s(RestartType::LUBY, 25);
    CHECK(s.current_cutoff == 25); // v=1 * base_unit
  }
  SECTION("GEOMETRIC") {
    RestartStrategy s(RestartType::GEOMETRIC, 10, 2.0);
    CHECK(s.current_cutoff == 10);
  }
}

TEST_CASE("Factory methods produce the expected type and base_unit",
          "[construction][factories]") {
  SECTION("None()") {
    RestartStrategy s = RestartStrategy::None();
    CHECK(s.type == RestartType::NONE);
    CHECK(s.current_cutoff == 0);
  }
  SECTION("Fixed(limit)") {
    RestartStrategy s = RestartStrategy::Fixed(37);
    CHECK(s.type == RestartType::FIXED);
    CHECK(s.base_unit == 37);
    CHECK(s.current_cutoff == 37);
  }
  SECTION("Luby(base_unit)") {
    RestartStrategy s = RestartStrategy::Luby(25);
    CHECK(s.type == RestartType::LUBY);
    CHECK(s.base_unit == 25);
    CHECK(s.current_cutoff == 25);
  }
  SECTION("Geometric(base_unit, factor)") {
    RestartStrategy s = RestartStrategy::Geometric(10, 2.0);
    CHECK(s.type == RestartType::GEOMETRIC);
    CHECK(s.base_unit == 10);
    CHECK(s.factor == 2.0);
    CHECK(s.current_cutoff == 10);
  }
}

// ---------------------------------------------------------------------------
// NONE behavior
// ---------------------------------------------------------------------------

TEST_CASE("NONE strategy never signals a restart", "[none]") {
  RestartStrategy s = RestartStrategy::None();

  size_t restarts = drive_flips(s, 10000);

  CHECK(restarts == 0);
  CHECK(s.flips_in_run == 10000); // keeps incrementing, never reset
  CHECK(s.total_flips == 0);      // reset() never called
  CHECK(s.get_n_flips() == 0);
}

TEST_CASE("NONE strategy still accumulates total_flips via explicit reset()",
          "[none]") {
  RestartStrategy s = RestartStrategy::None();

  drive_flips(s, 500);
  s.reset();
  CHECK(s.total_flips == 500);
  CHECK(s.flips_in_run == 0);
  CHECK(s.get_n_flips() == 500);

  drive_flips(s, 250);
  s.reset();
  CHECK(s.total_flips == 750);
  CHECK(s.get_n_flips() == 750);
}

TEST_CASE("NONE strategy ignores base_unit and factor entirely", "[none]") {
  RestartStrategy s(RestartType::NONE, 5, 3.0);
  CHECK(s.current_cutoff == 0);
  CHECK(drive_flips(s, 1000) == 0);
}

// ---------------------------------------------------------------------------
// FIXED behavior
// ---------------------------------------------------------------------------

TEST_CASE("FIXED strategy restarts exactly on the limit-th flip", "[fixed]") {
  const size_t limit = 10;
  RestartStrategy s = RestartStrategy::Fixed(limit);

  for (size_t i = 0; i < limit - 1; i++) {
    INFO("flip " << i);
    CHECK_FALSE(s.on_flip());
  }
  CHECK(s.on_flip());
  CHECK(s.flips_in_run == 0);       // reset on restart
  CHECK(s.current_cutoff == limit); // FIXED cutoff never changes
}

TEST_CASE("FIXED strategy repeats its period indefinitely", "[fixed]") {
  const size_t limit = 7;
  RestartStrategy s = RestartStrategy::Fixed(limit);

  for (int period = 0; period < 20; period++) {
    size_t restarts_this_period = drive_flips(s, limit);
    CHECK(restarts_this_period == 1);
  }
}

TEST_CASE("FIXED strategy with limit=1 restarts on every flip",
          "[fixed][edge]") {
  RestartStrategy s = RestartStrategy::Fixed(1);
  for (int i = 0; i < 50; i++) {
    CHECK(s.on_flip());
    CHECK(s.flips_in_run == 0);
  }
}

TEST_CASE("FIXED strategy with limit=0 restarts immediately every time",
          "[fixed][edge]") {
  // current_cutoff == 0, so flips_in_run(1) >= 0 is true on the very first
  // call -- every single flip should signal a restart.
  RestartStrategy s = RestartStrategy::Fixed(0);
  REQUIRE(s.current_cutoff == 0);
  for (int i = 0; i < 20; i++) {
    CHECK(s.on_flip());
  }
}

TEST_CASE("FIXED strategy total_flips requires a manual reset() call",
          "[fixed]") {
  // Demonstrates that automatic restarts triggered via on_flip() do NOT
  // accumulate into total_flips -- only an explicit reset() call does.
  RestartStrategy s = RestartStrategy::Fixed(5);
  drive_flips(s, 5 * 4); // 4 automatic restarts happen internally
  CHECK(s.total_flips == 0);
  CHECK(s.get_n_flips() == 0);

  s.reset();
  // flips_in_run was 0 at the moment of the manual reset (20 is an exact
  // multiple of 5), so total_flips gains nothing here.
  CHECK(s.total_flips == 0);

  drive_flips(s, 3); // partial run, no restart yet
  s.reset();
  CHECK(s.total_flips == 3);
}

// ---------------------------------------------------------------------------
// LUBY behavior
// ---------------------------------------------------------------------------

TEST_CASE("LUBY sequence matches the canonical Luby series", "[luby]") {
  // Canonical Luby sequence: 1,1,2,1,1,2,4,1,1,2,1,1,2,4,8,1,...
  const std::vector<size_t> expected = {1, 1, 2, 1, 1, 2, 4,
                                         1, 1, 2, 1, 1, 2, 4, 8};

  RestartStrategy s = RestartStrategy::Luby(1); // base_unit=1 => cutoffs == v

  for (size_t i = 0; i < expected.size(); i++) {
    INFO("term index " << i);
    size_t cutoff_before = s.current_cutoff;
    CHECK(cutoff_before == expected[i]);

    // Drive exactly `cutoff_before` flips; only the last should restart.
    for (size_t f = 0; f + 1 < cutoff_before; f++) {
      CHECK_FALSE(s.on_flip());
    }
    CHECK(s.on_flip());
  }
}

TEST_CASE("LUBY sequence scales linearly with base_unit", "[luby]") {
  const size_t base_unit = 50;
  const std::vector<size_t> expected_v = {1, 1, 2, 1, 1, 2, 4};
  RestartStrategy s = RestartStrategy::Luby(base_unit);

  for (size_t v : expected_v) {
    CHECK(s.current_cutoff == v * base_unit);
    drive_flips(s, s.current_cutoff);
  }
}

TEST_CASE("Explicit reset() rewinds the LUBY sequence to its start",
          "[luby]") {
  RestartStrategy s = RestartStrategy::Luby(1);

  // Advance a few steps into the sequence: 1, 1, 2 ...
  drive_flips(s, 1); // consumes cutoff=1 -> now at v=1 (2nd term)
  drive_flips(s, 1); // consumes cutoff=1 -> now at v=2 (3rd term)
  REQUIRE(s.current_cutoff == 2);
  REQUIRE(s.luby_u == 2);

  // An explicit reset() rewinds luby_u/luby_v back to the start of the
  // sequence (unlike an automatic restart via on_flip()).
  s.reset();
  CHECK(s.luby_u == 1);
  CHECK(s.luby_v == 1);
  CHECK(s.current_cutoff == 1);
}

// ---------------------------------------------------------------------------
// GEOMETRIC behavior
// ---------------------------------------------------------------------------

TEST_CASE("GEOMETRIC strategy grows cleanly with an integer factor",
          "[geometric]") {
  RestartStrategy s = RestartStrategy::Geometric(10, 2.0);
  const std::vector<size_t> expected_cutoffs = {10, 20, 40, 80, 160};

  for (size_t expected : expected_cutoffs) {
    CHECK(s.current_cutoff == expected);
    drive_flips(s, s.current_cutoff);
  }
}

TEST_CASE("GEOMETRIC strategy rounds fractional growth up via ceil()",
          "[geometric]") {
  // factor = 1.5 forces non-integer intermediate values, exercising the
  // std::ceil() in advance_next_cutoff().
  RestartStrategy s = RestartStrategy::Geometric(100, 1.5);
  // 100 -> ceil(150)=150 -> ceil(225)=225 -> ceil(337.5)=338 -> ceil(507)=507
  const std::vector<size_t> expected_cutoffs = {100, 150, 225, 338, 507};

  for (size_t expected : expected_cutoffs) {
    CHECK(s.current_cutoff == expected);
    drive_flips(s, s.current_cutoff);
  }
}

TEST_CASE("GEOMETRIC strategy with factor < 1.0 shrinks the cutoff",
          "[geometric][edge]") {
  // Not a typical use case, but the implementation places no restriction on
  // factor < 1.0, so cutoffs should shrink (still rounded up).
  RestartStrategy s = RestartStrategy::Geometric(100, 0.5);
  const std::vector<size_t> expected_cutoffs = {100, 50, 25, 13, 7, 4, 2, 1};

  for (size_t expected : expected_cutoffs) {
    CHECK(s.current_cutoff == expected);
    drive_flips(s, s.current_cutoff);
  }
}

TEST_CASE("GEOMETRIC strategy's base_unit growth resets on explicit reset()",
          "[geometric]") {
  RestartStrategy s = RestartStrategy::Geometric(10, 2.0);
  drive_flips(s, 10); // -> base_unit becomes 20
  REQUIRE(s.current_cutoff == 20);
  REQUIRE(s.geo_cutoff == 20);

  // Unlike LUBY, GEOMETRIC keeps growing base_unit across an explicit
  // reset() -- reset() only re-derives current_cutoff from base_unit, it
  // does not roll base_unit back.
  s.reset();
  CHECK(s.base_unit == 10);
  CHECK(s.geo_cutoff == 10);
  CHECK(s.current_cutoff == 10);
}

// ---------------------------------------------------------------------------
// Cross-cutting / misc
// ---------------------------------------------------------------------------

TEST_CASE("Independent instances do not share state", "[misc]") {
  RestartStrategy a = RestartStrategy::Fixed(5);
  RestartStrategy b = RestartStrategy::Fixed(5);

  drive_flips(a, 3);
  CHECK(a.flips_in_run == 3);
  CHECK(b.flips_in_run == 0);
}

TEST_CASE("A copy diverges independently of the original after mutation",
          "[misc]") {
  RestartStrategy original = RestartStrategy::Luby(1);
  drive_flips(original, 1); // consumes cutoff=1 -> u=2, v=1
  REQUIRE(original.luby_u == 2);
  REQUIRE(original.luby_v == 1);

  RestartStrategy copy = original; // no custom copy ctor -> memberwise copy
  drive_flips(copy, 1);            // advance the copy only -> u=2, v=2

  CHECK(original.luby_v == 1); // untouched by the copy's mutation
  CHECK(copy.luby_v == 2);     // copy advanced independently
}

TEST_CASE("get_n_flips() reflects only committed (reset) flips", "[misc]") {
  RestartStrategy s = RestartStrategy::Fixed(4);
  CHECK(s.get_n_flips() == 0);

  drive_flips(s, 4); // one internal restart, no manual reset yet
  CHECK(s.get_n_flips() == 0);

  s.reset();
  CHECK(s.get_n_flips() == 0); // flips_in_run was already 0 at reset time
}
