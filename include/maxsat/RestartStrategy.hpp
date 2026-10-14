#ifndef MAXSAT__RESTART_STRATEGY_HPP
#define MAXSAT__RESTART_STRATEGY_HPP

#include <cmath>
#include <cstddef>
#include <vector>

enum class RestartType { NONE, FIXED, LUBY, GEOMETRIC };

class RestartStrategy {
public:
  RestartType type = RestartType::NONE;
  size_t base_unit = 100; // base number of flips before a restart unit
  double factor = 1.5;    // for Geometric strategy

  // Internal tracking state
  size_t total_flips = 0;
  size_t flips_in_run = 0;
  size_t current_cutoff = 0;
  size_t luby_u = 1;
  size_t luby_v = 1;
  size_t geo_cutoff = base_unit;

  RestartStrategy() = default;

  RestartStrategy(RestartType type, size_t base_unit = 100, double factor = 1.5)
      : type(type), base_unit(base_unit), factor(factor) {
    reset();
  }

  // Factory methods for convenience
  static RestartStrategy None() { return RestartStrategy(RestartType::NONE); }
  static RestartStrategy Fixed(size_t limit) {
    return RestartStrategy(RestartType::FIXED, limit);
  }
  static RestartStrategy Luby(size_t base_unit) {
    return RestartStrategy(RestartType::LUBY, base_unit);
  }
  static RestartStrategy Geometric(size_t base_unit, double factor) {
    return RestartStrategy(RestartType::GEOMETRIC, base_unit, factor);
  }

  void reset() {
    total_flips += flips_in_run;
    flips_in_run = 0;
    luby_u = 1;
    luby_v = 1;
    geo_cutoff = base_unit;
    update_cutoff();
  }

  // Call on every flip. Returns true if solver should reset assignment.
  inline bool on_flip() {
    flips_in_run++;
    if (type == RestartType::NONE)
      return false;

    if (flips_in_run >= current_cutoff) {
      flips_in_run = 0;
      advance_next_cutoff();
      return true;
    }
    return false;
  }

  size_t get_n_flips() const { return total_flips; }

private:
  void update_cutoff() {
    switch (type) {
    case RestartType::FIXED:
      current_cutoff = base_unit;
      break;
    case RestartType::LUBY:
      current_cutoff = luby_v * base_unit;
      break;
    case RestartType::GEOMETRIC:
      current_cutoff = base_unit;
      break;
    case RestartType::NONE:
      current_cutoff = 0;
      break;
    }
  }

  void advance_next_cutoff() {
    if (type == RestartType::LUBY) {
      if ((luby_u & -luby_u) == luby_v) {
        luby_u = luby_u + 1;
        luby_v = 1;
      } else {
        luby_v = luby_v * 2;
      }
      current_cutoff = luby_v * base_unit;
    } else if (type == RestartType::GEOMETRIC) {
      geo_cutoff = static_cast<size_t>(std::ceil(geo_cutoff * factor));
      current_cutoff = geo_cutoff;
    }
  }
};

#endif
