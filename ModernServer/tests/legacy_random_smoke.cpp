#include <array>
#include <cassert>
#include <cstdint>

#include "world/legacy_random.hpp"
#include "world/logic_runtime.hpp"

int main() {
  mir2::LegacyRandom rng(1);
  const std::array<std::int32_t, 8> ranges{10, 10, 100, 5, 3, 1000, 0, 1};
  const std::array<std::int32_t, 8> expected{0, 8, 20, 1, 2, 318, 0, 0};

  for (std::size_t index = 0; index < ranges.size(); ++index) {
    const auto before = rng.state();
    const auto value = rng.random(ranges[index]);
    assert(value == expected[index]);
    if (ranges[index] <= 0) {
      assert(rng.state() == before);
    }
  }

  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "RandomMap", {}, 10, 10, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  assert(runtime.legacy_random_state() == 1);
  runtime.set_legacy_random_seed(42);
  assert(runtime.legacy_random_state() == 42);

  return 0;
}
