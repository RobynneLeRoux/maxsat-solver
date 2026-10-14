#ifndef MAXSAT__UTILS_H
#define MAXSAT__UTILS_H

#include <cmath>
#include <cstddef>

/*
 * TODO: Benchmark the difference between these funcs?
 *
inline int lit_to_idx(int lit) {
  int var = std::abs(lit) - 1;
  return (var * 2) + (lit < 0 ? 1 : 0);
}

inline int idx_to_lit(size_t idx) {
  int var = idx / 2;
  return (idx % 2 == 0 ? var : -var);
}
*/

inline size_t lit_to_index(int literal) {
  return 2 * (abs(literal) - 1) + (literal > 0 ? 0 : 1);
}

// Reverses the index back to the signed literal
inline int index_to_lit(int index) {
  int var = (index >> 1) + 1;
  return (index & 1) ? -var : var;
}

#endif
