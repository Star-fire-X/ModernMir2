#include <cassert>
#include <string>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

mir2::NpcConfig make_npc(std::string id, std::string name, std::string service, int x) {
  mir2::NpcConfig npc;
  npc.id = std::move(id);
  npc.map_id = "0";
  npc.name = std::move(name);
  npc.x = x;
  npc.y = 10;
  npc.service = std::move(service);
  return npc;
}

std::vector<std::string> names_for_stage(const mir2::RuntimeDispatch& dispatch,
                                         const std::string& stage) {
  std::vector<std::string> names;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action != "begin" && !trace.object_name.empty()) {
      names.push_back(trace.object_name);
    }
  }
  return names;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.npc_budget_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "NpcMerchantCursor", {}, 0, 0, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Torch", 1, 10});

  auto trader = make_npc("trader", "Trader", "buy", 10);
  trader.merchant_goods.push_back(1);
  config.npcs.push_back(trader);
  config.npcs.push_back(make_npc("repairer", "Repairer", "repair", 11));
  config.npcs.push_back(make_npc("guide", "Guide", "dialog", 12));
  config.npcs.push_back(make_npc("sage", "Sage", "guild", 13));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  assert(runtime.legacy_merchant_count() == 2);
  assert(runtime.legacy_npc_count() == 2);

  static_cast<void>(runtime.tick(1000));
  assert(runtime.legacy_mer_cur() == 1);
  assert(runtime.legacy_npc_cur() == 1);

  const auto second = runtime.tick(2101);
  const std::vector<std::string> expected_second_merchants{"Repairer"};
  const std::vector<std::string> expected_second_npcs{"Sage"};
  assert(names_for_stage(second, "ProcessMerchants") == expected_second_merchants);
  assert(names_for_stage(second, "ProcessNpcs") == expected_second_npcs);
  assert(runtime.legacy_mer_cur() == 0);
  assert(runtime.legacy_npc_cur() == 0);

  const auto third = runtime.tick(3202);
  const std::vector<std::string> expected_third_merchants{"Trader"};
  const std::vector<std::string> expected_third_npcs{"Guide"};
  assert(names_for_stage(third, "ProcessMerchants") == expected_third_merchants);
  assert(names_for_stage(third, "ProcessNpcs") == expected_third_npcs);
  assert(runtime.legacy_mer_cur() == 1);
  assert(runtime.legacy_npc_cur() == 1);

  return 0;
}
