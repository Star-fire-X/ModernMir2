#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

std::vector<std::string> monster_names(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> names;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessMonsters" && trace.action != "begin" &&
        trace.action != "gen_check" && trace.action != "complete" &&
        trace.action != "budget_exhausted" && !trace.object_name.empty()) {
      names.push_back(trace.object_name);
    }
  }
  return names;
}

std::vector<std::string> spawn_names(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> names;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterSpawn" && trace.action == "spawned") {
      names.push_back(trace.object_name);
    }
  }
  return names;
}

bool has_stage_action(const mir2::RuntimeDispatch& dispatch, const std::string& stage,
                      const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action == action) {
      return true;
    }
  }
  return false;
}

std::size_t action_count(const mir2::RuntimeDispatch& dispatch, const std::string& stage,
                         const std::string& action) {
  std::size_t count = 0;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action == action) {
      ++count;
    }
  }
  return count;
}

mir2::SpawnConfig fixed_spawn(std::string map_id, std::string name,
                              std::int32_t x, std::int32_t y) {
  mir2::SpawnConfig spawn;
  spawn.map_id = std::move(map_id);
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  return spawn;
}

mir2::SpawnConfig legacy_group_spawn(std::string name, std::int32_t count) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = 5;
  spawn.y = 5;
  spawn.area = 0;
  spawn.count = count;
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  return spawn;
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.budgets.monster_budget_ms = 0;
    config.maps.push_back(mir2::MapConfig{"0", "MonsterCursor", {}, 0, 0, 10, 10});
    config.maps.push_back(mir2::MapConfig{"1", "MonsterCursorB", {}, 0, 0, 10, 10});
    config.spawns.push_back(fixed_spawn("0", "Goblin", 10, 8));
    config.spawns.push_back(fixed_spawn("1", "Oma", 11, 8));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    assert(runtime.legacy_monster_group_count() == 2);

    const auto first = runtime.tick(1000);
    assert(has_stage_action(first, "ProcessMonsters", "budget_exhausted"));
    assert(runtime.legacy_mon_cur() == 1);
    assert(runtime.legacy_mon_sub_cur() == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto second = runtime.tick(1100);
    const std::vector<std::string> expected_second{"Oma"};
    assert(monster_names(second) == expected_second);
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto third = runtime.tick(1301);
    const std::vector<std::string> expected_third{"Goblin"};
    assert(monster_names(third) == expected_third);
    assert(runtime.legacy_mon_cur() == 1);
    assert(runtime.legacy_mon_sub_cur() == 0);
    assert(runtime.legacy_gen_cur() == 1);
  }

  {
    mir2::HostConfig config;
    config.budgets.monster_budget_ms = 0;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "MonsterSubCursor", {}, 0, 0, 10, 10});
    config.spawns.push_back(legacy_group_spawn("Twin", 2));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    assert(runtime.legacy_monster_group_count() == 1);

    const auto first = runtime.tick(1000);
    assert(spawn_names(first).empty());
    assert(action_count(first, "ProcessMonsters", "gen_check") == 0);
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto too_early = runtime.tick(1200);
    assert(spawn_names(too_early).empty());
    assert(action_count(too_early, "ProcessMonsters", "gen_check") == 0);
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto spawned = runtime.tick(1201);
    assert(spawn_names(spawned).size() == 2);
    assert(action_count(spawned, "ProcessMonsters", "gen_check") == 1);
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 1);
    assert(runtime.legacy_gen_cur() == 0);

    const auto second = runtime.tick(1302);
    const std::vector<std::string> expected_second{"Twin"};
    assert(monster_names(second) == expected_second);
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 0);
  }

  {
    mir2::HostConfig config;
    config.budgets.monster_budget_ms = 60000;
    config.maps.push_back(mir2::MapConfig{"0", "MonsterComplete", {}, 0, 0, 10, 10});
    config.maps.push_back(mir2::MapConfig{"1", "MonsterCompleteB", {}, 0, 0, 10, 10});
    config.spawns.push_back(fixed_spawn("0", "Goblin", 10, 8));
    config.spawns.push_back(fixed_spawn("1", "Oma", 11, 8));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.tick(1000));

    const auto dispatch = runtime.tick(1301);
    const std::vector<std::string> expected{"Goblin", "Oma"};
    assert(monster_names(dispatch) == expected);
    assert(has_stage_action(dispatch, "ProcessMonsters", "complete"));
    assert(!has_stage_action(dispatch, "ProcessMonsters", "budget_exhausted"));
    assert(runtime.legacy_mon_cur() == 0);
    assert(runtime.legacy_mon_sub_cur() == 0);
  }

  {
    mir2::HostConfig config;
    config.budgets.monster_budget_ms = 60000;
    config.runtime.legacy_random_seed = 3;
    config.maps.push_back(mir2::MapConfig{"0", "MonsterGenCursor", {}, 0, 0, 10, 10});
    config.spawns.push_back(legacy_group_spawn("First", 1));
    config.spawns.push_back(legacy_group_spawn("Second", 1));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    const auto first = runtime.tick(1000);
    assert(spawn_names(first).empty());
    assert(action_count(first, "ProcessMonsters", "gen_check") == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto too_early = runtime.tick(1200);
    assert(spawn_names(too_early).empty());
    assert(action_count(too_early, "ProcessMonsters", "gen_check") == 0);
    assert(runtime.legacy_gen_cur() == 0);

    const auto first_group = runtime.tick(1201);
    const std::vector<std::string> expected_first{"First"};
    assert(spawn_names(first_group) == expected_first);
    assert(action_count(first_group, "ProcessMonsters", "gen_check") == 1);
    assert(runtime.legacy_gen_cur() == 1);

    const auto second_too_early = runtime.tick(1401);
    assert(spawn_names(second_too_early).empty());
    assert(action_count(second_too_early, "ProcessMonsters", "gen_check") == 0);
    assert(runtime.legacy_gen_cur() == 1);

    const auto second_group = runtime.tick(1402);
    const std::vector<std::string> expected_second{"Second"};
    assert(spawn_names(second_group) == expected_second);
    assert(action_count(second_group, "ProcessMonsters", "gen_check") == 1);
    assert(runtime.legacy_gen_cur() == 0);

    const auto no_catch_up = runtime.tick(5000);
    assert(action_count(no_catch_up, "ProcessMonsters", "gen_check") == 1);
    assert(runtime.legacy_gen_cur() == 1);
  }

  return 0;
}
