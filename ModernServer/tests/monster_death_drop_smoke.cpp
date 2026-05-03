#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_body(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, const std::string& body) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body) == body) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return find_packet(dispatch, ident).has_value();
}

mir2::CharacterRecord make_hero() {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(0, 20);
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

mir2::LogicCommand make_attack(std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = 7;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 11;
  config.maps.push_back(mir2::MapConfig{"0", "DropMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Wooden Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0});
  mir2::MonsterDefConfig oma;
  oma.name = "Oma";
  oma.hp = 8;
  oma.dc = 1;
  oma.exp = 10;
  oma.ai_profile = mir2::MonsterAiProfile::aggressive;
  config.monsters.push_back(oma);
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 0, 1, "Wooden Sword", 1});
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 0, 1, "Gold", 20});
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 0, 1, "Missing Item", 1});
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "Oma";
  spawn.x = 10;
  spawn.y = 9;
  spawn.area = 0;
  spawn.count = 1;
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  const auto spawn_dispatch = runtime.tick(1000);
  bool saw_unknown_item = false;
  for (const auto& trace : spawn_dispatch.legacy_traces) {
    saw_unknown_item = saw_unknown_item ||
                       (trace.stage == "MonsterDrop" && trace.action == "unknown_item" &&
                        trace.command == "Missing Item");
  }
  assert(saw_unknown_item);

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = make_hero();
  static_cast<void>(runtime.route_logic_command(enter));
  assert(find_packet(runtime.tick(1020), mir2::kSmNewMap).has_value());

  static_cast<void>(runtime.route_logic_command(make_attack(10, 9)));
  const auto kill_dispatch = runtime.tick(1040);
  assert(has_packet(kill_dispatch, mir2::kSmDeath));
  const auto sword_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Wooden Sword");
  const auto gold_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Gold");
  assert(sword_show.has_value());
  assert(gold_show.has_value());

  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = sword_show->message.param;
  pickup.y = sword_show->message.tag;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = runtime.tick(1060);
  assert(has_packet(pickup_dispatch, mir2::kSmItemHide));
  if (!has_packet(pickup_dispatch, mir2::kSmAddItem)) {
    static_cast<void>(runtime.route_logic_command(pickup));
    const auto second_pickup_dispatch = runtime.tick(1080);
    assert(has_packet(second_pickup_dispatch, mir2::kSmAddItem));
  }

  return 0;
}
