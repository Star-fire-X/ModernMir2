#include <set>

#include "config/models.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 0, 0, 10, 10});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Chicken", 11, 10, 30000});
  config.npcs.push_back(mir2::NpcConfig{"npc_1", "0", "Trader", 12, 10, "npc_1.txt"});
  config.items.push_back(mir2::ItemConfig{1, "Short Sword", 3, 100});
  config.magics.push_back(mir2::MagicConfig{1, "Fireball", 4, 8});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  if (runtime.map_count() != 1) {
    return 1;
  }

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].make_index = 1001;
  hero.bag_items[0].dura = 1000;
  hero.bag_items[0].dura_max = 1000;
  hero.magics[0].magic_id = 1;
  hero.magics[0].level = 1;
  hero.magics[0].key = 'F';

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  auto dispatch = runtime.tick();
  if (dispatch.session_events.empty() || runtime.online_session_count() != 1) {
    return 1;
  }

  std::set<std::uint16_t> idents;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value()) {
      continue;
    }
    idents.insert(decoded->message.ident);
  }

  if (!idents.contains(mir2::kSmNewMap) || !idents.contains(mir2::kSmLogon) ||
      !idents.contains(mir2::kSmAbility) || !idents.contains(mir2::kSmSendMyMagic)) {
    return 1;
  }
  return 0;
}
