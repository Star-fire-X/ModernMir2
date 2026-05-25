#include <cassert>
#include <cstddef>
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

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint16_t ident) {
  for (std::size_t i = 0; i < dispatch.session_events.size(); ++i) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[i].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> packet_index_by_body_contains(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    const std::string& body_fragment) {
  for (std::size_t i = 0; i < dispatch.session_events.size(); ++i) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[i].packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body).find(body_fragment) != std::string::npos) {
      return i;
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
  hero.y = 8;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(20, 20);
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
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 1, 1, "Wooden Sword", 1});
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 1, 1, "Gold", 20});
  config.monster_drops.push_back(mir2::MonsterDropConfig{"Oma", 1, 1, "Missing Item", 1});
  mir2::MapQuestConfig death_quest;
  death_quest.map_id = "0";
  death_quest.set_number = 0;
  death_quest.value = 0;
  death_quest.monster_name = "Oma";
  death_quest.qfile = "death_chain.txt";
  death_quest.dialog_sections.push_back(mir2::NpcDialogSectionConfig{
      "@main", "Death chain marker\\\n#ACT\nMONCLEAR Oma\nSET [1] 1\n"});
  config.map_quests.push_back(death_quest);
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
  static_cast<void>(runtime.tick(1000));
  const auto spawn_dispatch = runtime.tick(1201);
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
  assert(find_packet(runtime.tick(1220), mir2::kSmNewMap).has_value());

  static_cast<void>(runtime.route_logic_command(make_attack(10, 9)));
  const auto kill_dispatch = runtime.tick(1240);
  assert(has_packet(kill_dispatch, mir2::kSmDeath));
  const auto death_index = packet_index(kill_dispatch, mir2::kSmDeath);
  const auto exp_index = packet_index(kill_dispatch, mir2::kSmWinExp);
  const auto mapquest_index =
      packet_index_by_body_contains(kill_dispatch, mir2::kSmMerchantSay,
                                    "Death chain marker");
  const auto drop_index = packet_index(kill_dispatch, mir2::kSmItemShow);
  assert(death_index.has_value());
  assert(exp_index.has_value());
  assert(mapquest_index.has_value());
  assert(drop_index.has_value());
  assert(*exp_index < *mapquest_index);
  assert(*mapquest_index < *drop_index);
  assert(*drop_index < *death_index);
  const auto sword_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Wooden Sword");
  const auto gold_show = find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Gold");
  assert(sword_show.has_value());
  assert(gold_show.has_value());
  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value() && snapshot->quest_marks[0] == 0x80);

  mir2::LogicCommand pickup;
  pickup.kind = mir2::LogicCommandKind::pickup_item;
  pickup.session_id = 7;
  pickup.x = sword_show->message.param;
  pickup.y = sword_show->message.tag;
  static_cast<void>(runtime.route_logic_command(pickup));
  const auto pickup_dispatch = runtime.tick(2500);
  assert(has_packet(pickup_dispatch, mir2::kSmItemHide));
  assert(has_packet(pickup_dispatch, mir2::kSmAddItem));

  return 0;
}
