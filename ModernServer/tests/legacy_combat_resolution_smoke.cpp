#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

std::optional<mir2::DecodedLegacyGamePacket> find_packet_for(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

mir2::LogicCommand make_enter(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand make_attack(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                               std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::CharacterRecord make_character(std::string account, std::string name, std::int32_t x,
                                     std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 1;
  character.ability.dc = mir2::make_word(0, 8);
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 10;
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

std::vector<std::string> combat_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyCombat") {
      actions.push_back(trace.action);
    }
  }
  return actions;
}

bool has_action(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  const auto actions = combat_actions(dispatch);
  return std::find(actions.begin(), actions.end(), action) != actions.end();
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "CombatMap", {}, 0, 0, 10, 10});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 10, 9, 30000, 1, 12, 3, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(51, make_character("a", "Hero", 10, 10))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(51, 10, 9)));
    const auto dispatch = runtime.tick();
    const auto actions = combat_actions(dispatch);
    const std::vector<std::string> expected_prefix{
        "ack", "attack_broadcast", "hit_check", "armor_roll", "damage", "struck"};
    assert(actions.size() >= expected_prefix.size());
    assert(std::equal(expected_prefix.begin(), expected_prefix.end(), actions.begin()));
    assert(find_packet(dispatch, mir2::kSmStruck).has_value());
    assert(runtime.legacy_random_state() != 1);
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 134775814;
    config.maps.push_back(mir2::MapConfig{"0", "PkMap", {}, 0, 0, 10, 10});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto hero = make_character("a", "Hero", 10, 10);
    hero.ability.reserved1 = 1;
    static_cast<void>(runtime.route_logic_command(make_enter(61, hero)));
    const auto hero_login = runtime.tick();
    const auto hero_map = find_packet(hero_login, mir2::kSmNewMap);
    assert(hero_map.has_value());
    const auto hero_actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(hero_map->message.recog));

    static_cast<void>(
        runtime.route_logic_command(make_enter(62, make_character("b", "Rival", 10, 9))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(61, 10, 9, hero_actor_id + 1)));
    const auto dispatch = runtime.tick();
    assert(has_action(dispatch, "miss"));
    assert(!find_packet(dispatch, mir2::kSmStruck).has_value());
    const auto rival = runtime.snapshot_character_actor("Rival");
    assert(rival.has_value());
    assert(rival->ability.hp == 15);
    assert(!has_action(dispatch, "armor_roll"));
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "CombatRangeMap", {}, 0, 0, 60, 60});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 20, 19, 30000, 1, 12, 3, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(make_enter(71, make_character("a", "Hero", 20, 20))));
    static_cast<void>(runtime.tick());
    static_cast<void>(
        runtime.route_logic_command(make_enter(72, make_character("b", "Near", 21, 20))));
    static_cast<void>(runtime.tick());
    static_cast<void>(
        runtime.route_logic_command(make_enter(73, make_character("c", "Far", 40, 40))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_attack(71, 20, 19)));
    const auto dispatch = runtime.tick();
    assert(find_packet_for(dispatch, 72, mir2::kSmHit).has_value());
    assert(find_packet_for(dispatch, 72, mir2::kSmStruck).has_value());
    assert(!find_packet_for(dispatch, 73, mir2::kSmHit).has_value());
    assert(!find_packet_for(dispatch, 73, mir2::kSmStruck).has_value());
    assert(!find_packet_for(dispatch, 73, mir2::kSmDeath).has_value());
  }

  return 0;
}
