#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                        \
  do {                                                                            \
    if (!(expression)) {                                                          \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);      \
      std::abort();                                                               \
    }                                                                             \
  } while (false)

namespace {

mir2::MagicConfig make_fireball(std::int32_t max_train0, std::int32_t spell = 4) {
  mir2::MagicConfig magic;
  magic.id = 1;
  magic.name = "Fireball";
  magic.affect_players = false;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = 1;
  magic.legacy.spell = spell;
  magic.legacy.min_power = 8;
  magic.legacy.max_power = 8;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {max_train0, 500, 1000, 1000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.def_spell = 0;
  magic.legacy.def_min_power = 0;
  magic.legacy.def_max_power = 0;
  magic.legacy.is_sword_skill = false;
  return magic;
}

mir2::HostConfig base_config(mir2::MagicConfig magic) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "TrainMap", {}, 20, 20, 10, 10});
  config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "Target", 10, 2, 30000, 1, 500, 0, 0, 0, 20});
  config.magics.push_back(std::move(magic));
  return config;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t mp = 100,
                                     std::uint8_t level = 0, std::int32_t cur_train = 0) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = static_cast<std::uint16_t>(mp);
  character.ability.max_mp = static_cast<std::uint16_t>(std::max(mp, 100));
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.magics[0].magic_id = 1;
  character.magics[0].level = level;
  character.magics[0].key = '1';
  character.magics[0].cur_train = cur_train;
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

mir2::LogicCommand make_spell(std::uint64_t session_id, std::uint64_t target_actor_id = 1) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = 10;
  command.y = 2;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = 1;
  return command;
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

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action;
                     });
}

std::vector<mir2::DecodedLegacyGamePacket> decoded_packets(const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (const auto decoded = mir2::decode_legacy_game_packet(event.packet); decoded.has_value()) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::int32_t count_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  const auto packets = decoded_packets(dispatch);
  return static_cast<std::int32_t>(std::count_if(
      packets.begin(), packets.end(),
      [&](const mir2::DecodedLegacyGamePacket& packet) { return packet.message.ident == ident; }));
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return count_packet(dispatch, ident) > 0;
}

std::optional<mir2::DecodedLegacyGamePacket> first_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& packet : decoded_packets(dispatch)) {
    if (packet.message.ident == ident) {
      return packet;
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  {
    mir2::LogicRuntime runtime(base_config(make_fireball(500)));
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(801, make_character("Trainer"))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(801)));
    const auto immediate = runtime.tick();

    assert(has_trace(immediate, "train_skill"));
    assert(!has_packet(immediate, mir2::kSmMagicLvExp));
    auto snapshot = runtime.snapshot_character_actor("Trainer");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].magic_id == 1);
    assert(snapshot->magics[0].level == 0);
    assert(snapshot->magics[0].cur_train > 0);

    const auto before_sync = advance_ticks(runtime, 49);
    assert(!has_packet(before_sync, mir2::kSmMagicLvExp));
    const auto sync = advance_ticks(runtime, 2);
    assert(has_packet(sync, mir2::kSmMagicLvExp));
  }

  {
    mir2::LogicRuntime runtime(base_config(make_fireball(2)));
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(make_enter(811, make_character("Leveler"))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(811)));
    const auto first = runtime.tick();
    assert(has_trace(first, "train_skill"));
    auto snapshot = runtime.snapshot_character_actor("Leveler");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].level == 0);
    assert(snapshot->magics[0].cur_train == 1);

    static_cast<void>(runtime.route_logic_command(make_spell(811)));
    const auto second = runtime.tick();
    assert(has_trace(second, "train_skill"));
    snapshot = runtime.snapshot_character_actor("Leveler");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].level == 1);
    assert(snapshot->magics[0].cur_train == 0);
    assert(!has_packet(second, mir2::kSmMagicLvExp));

    mir2::RuntimeDispatch delayed = advance_ticks(runtime, 40);
    append_dispatch(delayed, advance_ticks(runtime, 20));
    assert(count_packet(delayed, mir2::kSmMagicLvExp) == 1);
    assert(has_trace(delayed, "magic_lvexp_stale"));
    const auto lvexp = first_packet(delayed, mir2::kSmMagicLvExp);
    assert(lvexp.has_value());
    assert(lvexp->message.recog == 1);
    assert(lvexp->message.param == 1);
    assert(mir2::make_long(lvexp->message.tag, lvexp->message.series) == 0);
  }

  {
    mir2::LogicRuntime runtime(base_config(make_fireball(500, 40)));
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(make_enter(821, make_character("MpFailure", 0, 0, 7))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(821)));
    const auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "mp_reject"));
    const auto snapshot = runtime.snapshot_character_actor("MpFailure");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].cur_train == 7);
  }

  {
    mir2::LogicRuntime runtime(base_config(make_fireball(500)));
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(make_enter(831, make_character("MissingTarget", 100, 0, 9))));
    static_cast<void>(runtime.tick());
    auto missing_target_spell = make_spell(831, 999);
    missing_target_spell.x = 0;
    missing_target_spell.y = 0;
    static_cast<void>(runtime.route_logic_command(missing_target_spell));
    const auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "target_reject"));
    assert(!has_trace(dispatch, "spell_fail"));
    assert(has_packet(dispatch, mir2::kSmMagicFire));
    assert(!has_packet(dispatch, mir2::kSmMagicFireFail));
    const auto snapshot = runtime.snapshot_character_actor("MissingTarget");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].cur_train == 9);
  }

  {
    mir2::LogicRuntime runtime(base_config(make_fireball(500)));
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(make_enter(841, make_character("Cooldown", 100, 0, 0))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(841)));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(make_spell(841)));
    static_cast<void>(runtime.tick());
    const auto after_second = runtime.snapshot_character_actor("Cooldown");
    assert(after_second.has_value());

    static_cast<void>(runtime.route_logic_command(make_spell(841)));
    const auto third = runtime.tick();
    assert(has_trace(third, "cooldown_reject"));
    const auto after_third = runtime.snapshot_character_actor("Cooldown");
    assert(after_third.has_value());
    assert(after_third->magics[0].cur_train == after_second->magics[0].cur_train);
    assert(after_third->magics[0].level == after_second->magics[0].level);
  }

  return 0;
}
