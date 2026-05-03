#include <cassert>
#include <string>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

mir2::HostConfig make_config() {
  mir2::HostConfig config;
  config.budgets.monster_budget_ms = 30;
  config.budgets.npc_budget_ms = 30;
  config.maps.push_back(mir2::MapConfig{"0", "CreatureOrderA", {}, 0, 0, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "CreatureOrderB", {}, 0, 0, 10, 10});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Goblin", 10, 8, 30000});
  config.spawns.push_back(mir2::SpawnConfig{"1", "monster", "Oma", 11, 8, 30000});
  config.items.push_back(mir2::ItemConfig{1, "Torch", 1, 10});

  mir2::NpcConfig trader;
  trader.id = "trader";
  trader.map_id = "0";
  trader.name = "Trader";
  trader.x = 12;
  trader.y = 10;
  trader.service = "buy";
  trader.merchant_goods.push_back(1);
  config.npcs.push_back(trader);

  mir2::NpcConfig guide;
  guide.id = "guide";
  guide.map_id = "1";
  guide.name = "Guide";
  guide.x = 12;
  guide.y = 10;
  guide.service = "dialog";
  config.npcs.push_back(guide);
  return config;
}

std::vector<std::string> begin_stage_order(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> stages;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.action == "begin") {
      stages.push_back(trace.stage);
    }
  }
  return stages;
}

std::vector<std::string> processed_names(const mir2::RuntimeDispatch& dispatch,
                                         const std::string& stage) {
  std::vector<std::string> names;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage != stage || trace.action == "begin" || trace.action == "gen_check" ||
        trace.object_name.empty()) {
      continue;
    }
    names.push_back(trace.object_name);
  }
  return names;
}

}  // namespace

int main() {
  mir2::LogicRuntime runtime(make_config());
  runtime.initialize();
  assert(runtime.legacy_monster_group_count() == 2);
  assert(runtime.legacy_merchant_count() == 1);
  assert(runtime.legacy_npc_count() == 1);

  static_cast<void>(runtime.tick(1000));
  const auto dispatch = runtime.tick(1301);

  const std::vector<std::string> expected_stages{
      "ProcessUserHumans",
      "ProcessMonsters",
      "ProcessMerchants",
      "ProcessNpcs",
  };
  assert(begin_stage_order(dispatch) == expected_stages);

  const std::vector<std::string> expected_monsters{"Goblin", "Oma"};
  const std::vector<std::string> expected_merchants{"Trader"};
  const std::vector<std::string> expected_npcs{"Guide"};
  assert(processed_names(dispatch, "ProcessMonsters") == expected_monsters);
  assert(processed_names(dispatch, "ProcessMerchants") == expected_merchants);
  assert(processed_names(dispatch, "ProcessNpcs") == expected_npcs);

  return 0;
}
