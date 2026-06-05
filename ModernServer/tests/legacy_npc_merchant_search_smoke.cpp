#include <cassert>
#include <string>
#include <utility>
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

std::vector<std::string> stage_actions(const mir2::RuntimeDispatch& dispatch,
                                       const std::string& stage) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage) {
      actions.push_back(trace.action);
    }
  }
  return actions;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "NpcMerchantSearch", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Torch", 1, 10});

  auto trader = make_npc("trader", "Trader", "buy", 10);
  trader.merchant_goods.push_back(1);
  config.npcs.push_back(trader);
  config.npcs.push_back(make_npc("guide", "Guide", "dialog", 11));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.tick(1000));

  const auto dispatch = runtime.tick(2101);
  const std::vector<std::string> merchant_expected{"begin", "search_refresh", "run"};
  const std::vector<std::string> npc_expected{"begin", "search_refresh", "run"};
  assert(stage_actions(dispatch, "ProcessMerchants") == merchant_expected);
  assert(stage_actions(dispatch, "ProcessNpcs") == npc_expected);
  return 0;
}
