#pragma once

#include <cstdint>

namespace mir2 {

class LegacyRandom {
 public:
  explicit LegacyRandom(std::uint32_t seed = 1) : seed_(seed) {}

  void seed(std::uint32_t value) { seed_ = value; }
  [[nodiscard]] std::uint32_t state() const { return seed_; }

  [[nodiscard]] std::int32_t random(std::int32_t range) {
    if (range <= 0) {
      return 0;
    }
    seed_ = seed_ * 0x08088405u + 1u;
    return static_cast<std::int32_t>((static_cast<std::uint64_t>(seed_) *
                                      static_cast<std::uint32_t>(range)) >>
                                     32u);
  }

 private:
  std::uint32_t seed_{1};
};

}  // namespace mir2
