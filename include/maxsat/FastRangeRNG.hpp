#ifndef MAXSAT__FAST_RANGE_RNG_H
#define MAXSAT__FAST_RANGE_RNG_H

#include <cstdint>
#include <random>

class FastRangeRNG {

public:
  FastRangeRNG(uint32_t supplied_seed = 0) {
    seed = supplied_seed;
    if (seed == 0) {
      std::random_device rd;
      seed = rd();
      if (seed == 0) {
        // for xorshift, we can't let seed = 0
        seed = 1;
      }
    }
  }
  inline uint32_t rand(uint32_t N) {
    // https://github.com/lemire/fastrange/blob/master/fastrange.h
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(xorshift32_seed()) * N) >> 32);
  }

private:
  uint32_t seed;

  inline uint32_t xorshift32_seed() {
    uint32_t x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return seed = x;
  }
};

#endif
