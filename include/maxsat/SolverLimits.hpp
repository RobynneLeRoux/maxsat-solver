#ifndef MAXSAT__SOLVER_LIMITS_H
#define MAXSAT__SOLVER_LIMITS_H

#include <atomic>
#include <chrono>
#include <cstddef>

class SolverLimits {
private:
  std::atomic<bool> *solving;
  std::chrono::duration<double> time_limit;

  bool should_check_timeout;
  bool should_check_solving;

  std::chrono::steady_clock::time_point deadline;
  std::chrono::steady_clock::time_point initial_time;
  std::chrono::duration<double> solve_time;

  size_t step_counter = 0;

public:
  SolverLimits(std::chrono::duration<double> time_limit =
                   std::chrono::steady_clock::duration::zero(),
               std::atomic<bool> *solving = nullptr)
      : solving(solving), time_limit(time_limit) {

    should_check_timeout =
        (time_limit != std::chrono::duration<double>::zero());
    should_check_solving = (solving != nullptr);
  }

  void start_timer() {
    step_counter = 0;

    initial_time = std::chrono::steady_clock::now();
    if (should_check_timeout) {
      auto native_duration =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              time_limit);
      deadline = initial_time + native_duration;
    }
  }

  void end_timer() {
    solve_time = std::chrono::steady_clock::now() - initial_time;
  }

  bool should_stop() {
    // GENAI: I asked Gemini about the performance cost of checking an atomic
    // flag every iteration of the hot loop. It suggested that the loop only
    // queries the flag every N loops and the `std::memory_order_relaxed` part.

    // don't bother checking unless (counter % 0xFFFF == 0) (65_536)
    // INFO: I reduced this done to 0xFFF (4096) because the main loop doesn't
    // run fast enough yet.
    if ((++step_counter & 0xFFF) != 0) {
      return false;
    }

    if (should_check_solving && !solving->load(std::memory_order_relaxed)) {
      return true;
    }

    // Querying the system clock is even worse than checking an atomic boolean.
    if (should_check_timeout &&
        (std::chrono::steady_clock::now() >= deadline)) {
      return true;
    }

    return false;
  }

  std::chrono::duration<double> get_solve_time() const { return solve_time; }
};

#endif
