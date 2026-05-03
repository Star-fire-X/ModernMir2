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

mir2::CharacterRecord make_character(std::int32_t mp) {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Caster";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 1;
  character.ability.mc = mir2::make_word(0, 4);
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = static_cast<std::uint16_t>(mp);
  character.ability.max_mp = 10;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.magics[0].magic_id = 1;
  character.magics[0].level = 1;
  return character;
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

mir2::LogicCommand make_spell(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                              std::int32_t magic_id = 1) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

bool has_spell_action(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacySpell" && trace.action == action;
                     });
}

std::vector<std::uint64_t> spell_rng_targets(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::uint64_t> targets;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacySpell" && trace.action == "magic_defense_roll") {
      targets.push_back(trace.target_actor_id);
    }
  }
  return targets;
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "SpellMap", {}, 0, 0, 10, 10});
    config.magics.push_back(mir2::MagicConfig{1, "Fireball", 4, 6});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 10, 9, 30000, 1, 12, 3, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(71, make_character(0))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(71, 10, 9)));
    const auto dispatch = runtime.tick();
    assert(has_spell_action(dispatch, "mp_reject"));
    assert(!has_spell_action(dispatch, "ack"));
    assert(!find_packet(dispatch, mir2::kSmStruck).has_value());
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 1;
    config.maps.push_back(mir2::MapConfig{"0", "AoeMap", {}, 0, 0, 10, 10});
    config.magics.push_back(mir2::MagicConfig{1, "Burst", 1, 1, 2});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "A", 9, 10, 30000, 1, 20, 3, 0, 0, 20});
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "B", 11, 10, 30000, 1, 20, 3, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(72, make_character(10))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(72, 10, 10)));
    const auto dispatch = runtime.tick();
    assert(has_spell_action(dispatch, "ack"));
    assert(has_spell_action(dispatch, "spell_broadcast"));
    assert(has_spell_action(dispatch, "target_collect"));
    const auto targets = spell_rng_targets(dispatch);
    assert((targets == std::vector<std::uint64_t>{1, 2}));
  }

  return 0;
}
