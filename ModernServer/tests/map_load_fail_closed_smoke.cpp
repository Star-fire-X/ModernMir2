#include <cassert>
#include <filesystem>

#include "world/map_actor.hpp"

int main() {
  mir2::MapActor fail_closed_map(
      mir2::MapConfig{"0", "BrokenMap",
                      std::filesystem::temp_directory_path() / "missing_legacy_map_file.map",
                      0, 0, 0, 0},
      mir2::LogicBudgetConfig{}, {}, {});
  assert(!fail_closed_map.can_walk_tile(0, 0));

  mir2::MapActor sized_fail_closed_map(
      mir2::MapConfig{"0", "SizedBrokenMap",
                      std::filesystem::temp_directory_path() / "missing_sized_legacy_map_file.map",
                      5, 5, 0, 0},
      mir2::LogicBudgetConfig{}, {}, {});
  assert(!sized_fail_closed_map.can_walk_tile(0, 0));

  mir2::MapActor synthetic_map(
      mir2::MapConfig{"0", "SyntheticMap", {}, 5, 5, 0, 0},
      mir2::LogicBudgetConfig{}, {}, {});
  assert(synthetic_map.can_walk_tile(0, 0));
  assert(!synthetic_map.can_walk_tile(5, 5));
  return 0;
}
