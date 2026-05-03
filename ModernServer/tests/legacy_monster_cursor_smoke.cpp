#include <cassert>
#include <string>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

std::vector<std::string> monster_names(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> names;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessMonsters" && trace.action != "begin" &&
        trace.action != "gen_check" && !trace.object_name.empty()) {
      names.push_back(trace.object_name);
    }
  }
  return names;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.monster_budget_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "MonsterCursor", {}, 0, 0, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "MonsterCursorB", {}, 0, 0, 10, 10});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Goblin", 10, 8, 30000});
  config.spawns.push_back(mir2::SpawnConfig{"1", "monster", "Oma", 11, 8, 30000});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  assert(runtime.legacy_monster_group_count() == 2);

  static_cast<void>(runtime.tick(1000));
  assert(runtime.legacy_mon_cur() == 1);
  assert(runtime.legacy_mon_sub_cur() == 0);
  assert(runtime.legacy_gen_cur() == 1);

  const auto second = runtime.tick(1100);
  const std::vector<std::string> expected_second{"Oma"};
  assert(monster_names(second) == expected_second);
  assert(runtime.legacy_mon_cur() == 0);
  assert(runtime.legacy_mon_sub_cur() == 0);
  assert(runtime.legacy_gen_cur() == 1);

  const auto third = runtime.tick(1301);
  const std::vector<std::string> expected_third{"Goblin"};
  assert(monster_names(third) == expected_third);
  assert(runtime.legacy_mon_cur() == 1);
  assert(runtime.legacy_mon_sub_cur() == 0);
  assert(runtime.legacy_gen_cur() == 0);

  return 0;
}
