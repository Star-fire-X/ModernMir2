#include <algorithm>
#include <optional>
#include <string>
#include <iostream>

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

bool has_raw_text_for(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                      std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto found =
        std::search(event.packet.body.begin(), event.packet.body.end(), text.begin(), text.end());
    if (found != event.packet.body.end()) {
      return true;
    }
  }
  return false;
}

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

mir2::LogicCommand make_attack_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
                                       std::int32_t y, std::uint16_t ident = mir2::kCmHit) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = ident;
  return command;
}

mir2::LogicCommand make_spell_command(std::uint64_t session_id, std::uint64_t target_actor_id,
                                      std::uint8_t dir, std::int32_t x, std::int32_t y,
                                      std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::LogicCommand make_say_command(std::uint64_t session_id, std::string text) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  return command;
}

mir2::LogicCommand make_enter_command(std::uint64_t session_id, mir2::CharacterRecord character) {
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

mir2::CharacterRecord make_broadcast_character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 1;
  character.ability.dc = mir2::make_word(8, 8);
  character.ability.mc = mir2::make_word(1, 1);
  character.ability.hp = 20;
  character.ability.mp = 20;
  character.ability.max_hp = 20;
  character.ability.max_mp = 20;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.magics[0].magic_id = 1;
  character.magics[0].level = 1;
  return character;
}

}  // namespace

int main() {
  auto fail = [](int stage) {
    std::cerr << "combat_smoke failed at stage " << stage << '\n';
    return 1;
  };

  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "CombatMap", {}, 0, 0, 10, 10});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Scarecrow A", 10, 9, 30000, 1, 12, 3, 0, 0, 20});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Scarecrow B", 11, 10, 30000, 1, 8, 3, 0, 0, 20});
  config.magics.push_back(mir2::MagicConfig{1, "Fireball", 4, 6});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.dir = 0;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(8, 8);
  hero.ability.mc = mir2::make_word(0, 2);
  hero.ability.hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.exp = 70;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 1;
  hero.magics[0].level = 1;
  hero.magics[0].cur_train = 0;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 9;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  const auto new_map = find_packet(login_dispatch, mir2::kSmNewMap);
  if (!new_map.has_value()) {
    return fail(1);
  }
  const auto player_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map->message.recog));

  static_cast<void>(runtime.route_logic_command(make_attack_command(9, 0, 10, 9)));
  const auto first_attack_dispatch = runtime.tick();
  const auto first_struck = find_packet(first_attack_dispatch, mir2::kSmStruck);
  if (!first_struck.has_value() || first_struck->message.recog != 1 ||
      first_struck->message.param != 4 || first_struck->message.tag != 12 ||
      first_struck->message.series != 8) {
    return fail(2);
  }
  const auto struck_body = decode_body<mir2::LegacyMessageBodyWL>(first_struck->body);
  if (!struck_body.has_value() || struck_body->ltag1 != static_cast<std::int32_t>(player_actor_id) ||
      struck_body->ltag2 != 0) {
    return fail(3);
  }

  static_cast<void>(runtime.route_logic_command(make_spell_command(9, 1, 0, 10, 9, 1)));
  const auto spell_dispatch = runtime.tick();
  const auto first_death = find_packet(spell_dispatch, mir2::kSmDeath);
  const auto first_win_exp = find_packet(spell_dispatch, mir2::kSmWinExp);
  const auto spell_hpmp = find_packet(spell_dispatch, mir2::kSmHealthSpellChanged);
  if (!first_death.has_value()) {
    return fail(41);
  }
  if (!first_win_exp.has_value()) {
    return fail(42);
  }
  if (!spell_hpmp.has_value()) {
    return fail(43);
  }
  if (first_death->message.recog != 1 || first_death->message.param != 10 ||
      first_death->message.tag != 9) {
    return fail(44);
  }
  if (first_win_exp->message.recog != 90 || first_win_exp->message.param != 20) {
    return fail(45);
  }
  if (spell_hpmp->message.recog != player_actor_id || spell_hpmp->message.param != 15 ||
      spell_hpmp->message.tag != 6 || spell_hpmp->message.series != 15) {
    return fail(46);
  }
  if (find_packet(spell_dispatch, mir2::kSmLevelUp).has_value()) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(make_attack_command(9, 2, 11, 10)));
  const auto second_attack_dispatch = runtime.tick();
  const auto second_death = find_packet(second_attack_dispatch, mir2::kSmDeath);
  const auto second_win_exp = find_packet(second_attack_dispatch, mir2::kSmWinExp);
  const auto level_up = find_packet(second_attack_dispatch, mir2::kSmLevelUp);
  const auto ability = find_packet(second_attack_dispatch, mir2::kSmAbility);
  const auto sub_ability = find_packet(second_attack_dispatch, mir2::kSmSubAbility);
  const auto level_hpmp = find_packet(second_attack_dispatch, mir2::kSmHealthSpellChanged);
  if (!second_death.has_value() || !second_win_exp.has_value() || !level_up.has_value() ||
      !ability.has_value() || !sub_ability.has_value() || !level_hpmp.has_value() ||
      second_death->message.recog != 2 || second_win_exp->message.recog != 110 ||
      second_win_exp->message.param != 20 || level_up->message.recog != 10 ||
      level_up->message.param != 2 || level_hpmp->message.recog != player_actor_id ||
      level_hpmp->message.param != 20 || level_hpmp->message.tag != 13 ||
      level_hpmp->message.series != 20) {
    return fail(6);
  }

  const auto ability_body = decode_body<mir2::LegacyAbility>(ability->body);
  if (!ability_body.has_value() || ability_body->level != 2 || ability_body->hp != 20 ||
      ability_body->mp != 13 || ability_body->max_hp != 20 || ability_body->max_mp != 13 ||
      ability_body->exp != 10 || ability_body->max_exp != 200) {
    return fail(7);
  }

  if (sub_ability->message.param != mir2::make_word(10, 10)) {
    return fail(8);
  }

  mir2::HostConfig broadcast_config;
  broadcast_config.runtime.legacy_random_seed = 1;
  broadcast_config.budgets.tick_ms = 20;
  broadcast_config.maps.push_back(mir2::MapConfig{"0", "BroadcastRangeMap", {}, 0, 0, 60, 60});
  broadcast_config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "Spell Target", 20, 19, 30000, 1, 30, 3, 0, 0, 0});

  mir2::MagicConfig fireball;
  fireball.id = 1;
  fireball.name = "Fireball";
  fireball.legacy.legacy_present = true;
  fireball.legacy.effect_type = 1;
  fireball.legacy.effect = 1;
  fireball.legacy.min_power = 1;
  fireball.legacy.max_power = 1;
  fireball.legacy.def_min_power = 1;
  fireball.legacy.def_max_power = 1;
  broadcast_config.magics.push_back(fireball);

  mir2::LogicRuntime broadcast_runtime(broadcast_config);
  broadcast_runtime.initialize();
  static_cast<void>(broadcast_runtime.route_logic_command(
      make_enter_command(81, make_broadcast_character("Caster", 20, 20))));
  static_cast<void>(broadcast_runtime.tick());
  static_cast<void>(broadcast_runtime.route_logic_command(
      make_enter_command(82, make_broadcast_character("Near", 21, 20))));
  static_cast<void>(broadcast_runtime.tick());
  static_cast<void>(broadcast_runtime.route_logic_command(
      make_enter_command(83, make_broadcast_character("Far", 40, 40))));
  static_cast<void>(broadcast_runtime.tick());

  static_cast<void>(broadcast_runtime.route_logic_command(make_say_command(81, "hello")));
  const auto say_dispatch = broadcast_runtime.tick();
  if (!find_packet_for(say_dispatch, 81, mir2::kSmHear).has_value() ||
      !find_packet_for(say_dispatch, 82, mir2::kSmHear).has_value() ||
      find_packet_for(say_dispatch, 83, mir2::kSmHear).has_value()) {
    return fail(9);
  }

  static_cast<void>(
      broadcast_runtime.route_logic_command(make_spell_command(81, 1, 0, 20, 19, 1)));
  const auto ranged_spell_dispatch = broadcast_runtime.tick();
  if (!has_raw_text_for(ranged_spell_dispatch, 81, "+GOOD/")) {
    return fail(10);
  }
  if (!find_packet_for(ranged_spell_dispatch, 82, mir2::kSmSpell).has_value() ||
      !find_packet_for(ranged_spell_dispatch, 82, mir2::kSmMagicFire).has_value()) {
    return fail(11);
  }
  if (find_packet_for(ranged_spell_dispatch, 83, mir2::kSmSpell).has_value() ||
      find_packet_for(ranged_spell_dispatch, 83, mir2::kSmMagicFire).has_value()) {
    return fail(12);
  }

  return 0;
}
