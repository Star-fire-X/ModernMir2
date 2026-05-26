#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)       \
  do {                           \
    if (!(expression)) {         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();              \
    }                            \
  } while (false)

namespace {

mir2::MagicConfig make_fireball_magic() {
  mir2::MagicConfig magic;
  magic.id = 1;
  magic.name = "Fireball";
  magic.mp_cost = 5;
  magic.power = 10;
  magic.affect_players = false;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = 1;
  magic.legacy.spell = 4;
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.need_level = {7, 11, 16, 16};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 600;
  magic.legacy.def_spell = 1;
  magic.legacy.def_min_power = 2;
  magic.legacy.def_max_power = 2;
  magic.legacy.is_sword_skill = false;
  return magic;
}

mir2::MagicConfig make_heal_magic() {
  mir2::MagicConfig magic;
  magic.id = 2;
  magic.name = "Healing";
  magic.mp_cost = 7;
  magic.power = 14;
  magic.affect_players = true;
  magic.affect_monsters = false;
  magic.instant_heal = 14;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 2;
  magic.legacy.effect = 2;
  magic.legacy.spell = 7;
  magic.legacy.min_power = 14;
  magic.legacy.max_power = 20;
  magic.legacy.need_level = {7, 11, 16, 16};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 400;
  magic.legacy.is_sword_skill = false;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SkillMap", {}, 20, 20, 10, 10});
  config.magics.push_back(make_fireball_magic());
  config.magics.push_back(make_heal_magic());
  return config;
}

mir2::CharacterRecord make_character(const std::string& name, std::int32_t x, std::int32_t y,
                                     std::int32_t hp, std::int32_t max_hp, std::int32_t mp,
                                     std::int32_t magic_id) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = name;
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 1;
  character.ability.mc = mir2::make_word(0, 0);
  character.ability.sc = mir2::make_word(0, 0);
  character.ability.hp = static_cast<std::uint16_t>(hp);
  character.ability.max_hp = static_cast<std::uint16_t>(max_hp);
  character.ability.mp = static_cast<std::uint16_t>(mp);
  character.ability.max_mp = static_cast<std::uint16_t>(std::max(mp, 20));
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  if (magic_id > 0) {
    character.magics[0].magic_id = static_cast<std::uint16_t>(magic_id);
    character.magics[0].level = 1;
    character.magics[0].key = '1';
  }
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

mir2::LogicCommand make_spell(std::uint64_t session_id, std::int32_t magic_id,
                              std::int32_t x = 0, std::int32_t y = 0,
                              std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacySpell" && trace.action == action;
                     });
}

std::vector<std::uint16_t> packet_idents(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::uint16_t> idents;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      idents.push_back(decoded->message.ident);
    }
  }
  return idents;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto idents = packet_idents(dispatch);
  return std::find(idents.begin(), idents.end(), ident) != idents.end();
}

std::optional<std::size_t> first_packet_index(const mir2::RuntimeDispatch& dispatch,
                                             std::uint16_t ident) {
  std::size_t index = 0;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      if (decoded->message.ident == ident) {
        return index;
      }
      ++index;
    }
  }
  return std::nullopt;
}

std::optional<mir2::DecodedLegacyGamePacket> first_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

mir2::RuntimeDispatch advance_ticks(mir2::LogicRuntime& runtime, std::int32_t ticks) {
  mir2::RuntimeDispatch combined;
  for (std::int32_t tick = 0; tick < ticks; ++tick) {
    append_dispatch(combined, runtime.tick());
  }
  return combined;
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 10, 2, 30000, 1, 100, 0, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(601, make_character("MpFail", 10, 10, 15, 15, 0, 1))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(601, 1, 10, 2, 1)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "mp_reject"));
    assert(has_packet(dispatch, mir2::kSmMagicFireFail));
    assert(!has_packet(dispatch, mir2::kSmSpell));
    assert(!has_packet(dispatch, mir2::kSmStruck));
    const auto snapshot = runtime.snapshot_character_actor("MpFail");
    assert(snapshot.has_value());
    assert(snapshot->ability.mp == 0);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(606, make_character("InvalidTarget", 10, 10, 15, 15, 30, 1))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(608, make_character("InvalidWatcher", 12, 10, 15, 15, 20, 0))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(606, 1, 1, 1, 0)));
    const auto dispatch = runtime.tick();

    assert(has_trace(dispatch, "target_reject"));
    assert(!has_trace(dispatch, "delay_magic_queued"));
    assert(has_packet(dispatch, mir2::kSmSpell));
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(!has_packet(dispatch, mir2::kSmMagicFireFail));
    assert(!has_packet(dispatch, mir2::kSmStruck));
    const auto snapshot = runtime.snapshot_character_actor("InvalidTarget");
    assert(snapshot.has_value());
    assert(snapshot->ability.mp < 30);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto login = runtime.route_logic_command(
        make_enter(607, make_character("StoneLocked", 10, 10, 15, 15, 30, 1)));
    append_dispatch(login, runtime.tick());
    const auto new_map = first_packet(login, mir2::kSmNewMap);
    assert(new_map.has_value());
    const auto actor_id =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map->message.recog));

    mir2::ActorMail poison;
    poison.kind = mir2::ActorMailKind::legacy_delayed_effect;
    poison.map_id = "0";
    poison.actor_id = actor_id;
    poison.target_actor_id = actor_id;
    poison.delayed_effect_kind = mir2::LegacyDelayedEffectKind::make_poison;
    poison.poison_kind = 5;
    poison.duration_ticks = 50;
    static_cast<void>(runtime.route_actor_mail(poison));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(607, 1, 10, 2, 0)));
    const auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "poison_stone_reject"));
    assert(!has_packet(dispatch, mir2::kSmSpell));
    assert(!has_packet(dispatch, mir2::kSmMagicFire));
    assert(!has_packet(dispatch, mir2::kSmMagicFireFail));
    const auto snapshot = runtime.snapshot_character_actor("StoneLocked");
    assert(snapshot.has_value());
    assert(snapshot->ability.mp == 30);
  }

  {
    auto config = base_config();
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 10, 2, 30000, 1, 100, 0, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(611, make_character("FireCaster", 10, 10, 15, 15, 30, 1))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(612, make_character("FireWatcher", 12, 10, 15, 15, 20, 0))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(611, 1, 10, 2, 1)));
    const auto immediate = runtime.tick();

    const auto spell_index = first_packet_index(immediate, mir2::kSmSpell);
    const auto magic_fire_index = first_packet_index(immediate, mir2::kSmMagicFire);
    assert(spell_index.has_value());
    assert(magic_fire_index.has_value());
    assert(*spell_index < *magic_fire_index);
    assert(!has_packet(immediate, mir2::kSmStruck));
    assert(has_trace(immediate, "delay_magic_queued"));

    const auto delayed = advance_ticks(runtime, 30);
    assert(has_packet(delayed, mir2::kSmStruck));
    assert(has_trace(delayed, "mag_struck"));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(621, make_character("HealCaster", 10, 10, 5, 20, 30, 2))));
    static_cast<void>(runtime.route_logic_command(
        make_enter(622, make_character("HealWatcher", 12, 10, 15, 15, 20, 0))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(621, 2)));
    const auto immediate = runtime.tick();

    assert(has_packet(immediate, mir2::kSmSpell));
    assert(has_packet(immediate, mir2::kSmMagicFire));
    assert(has_packet(immediate, mir2::kSmHealthSpellChanged));
    assert(has_trace(immediate, "healing_queued"));
    auto snapshot = runtime.snapshot_character_actor("HealCaster");
    assert(snapshot.has_value());
    assert(snapshot->ability.hp == 5);
    assert(snapshot->ability.mp < 30);

    const auto before_delay = advance_ticks(runtime, 39);
    assert(!has_trace(before_delay, "healing_apply"));
    snapshot = runtime.snapshot_character_actor("HealCaster");
    assert(snapshot.has_value());
    assert(snapshot->ability.hp == 5);
    const auto delayed = advance_ticks(runtime, 2);
    assert(has_trace(delayed, "healing_apply"));
    assert(has_packet(delayed, mir2::kSmHealthSpellChanged));
    snapshot = runtime.snapshot_character_actor("HealCaster");
    assert(snapshot.has_value());
    assert(snapshot->ability.hp > 5);
    assert(snapshot->ability.hp < snapshot->ability.max_hp);
  }

  {
    auto config = base_config();
    config.spawns.push_back(
        mir2::SpawnConfig{"0", "monster", "Target", 10, 2, 30000, 1, 100, 0, 0, 0, 20});

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(631, make_character("Cooldown", 10, 10, 15, 15, 50, 1))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(631, 1, 10, 2, 1)));
    const auto first = runtime.tick();
    assert(has_trace(first, "delay_magic_queued"));
    assert(!has_trace(first, "cooldown_reject"));

    static_cast<void>(runtime.route_logic_command(make_spell(631, 1, 10, 2, 1)));
    const auto second = runtime.tick();
    assert(has_trace(second, "delay_magic_queued"));
    assert(!has_trace(second, "cooldown_reject"));

    static_cast<void>(runtime.route_logic_command(make_spell(631, 1, 10, 2, 1)));
    const auto third = runtime.tick();
    assert(has_trace(third, "cooldown_reject"));
    assert(!has_trace(third, "delay_magic_queued"));
  }

  return 0;
}
