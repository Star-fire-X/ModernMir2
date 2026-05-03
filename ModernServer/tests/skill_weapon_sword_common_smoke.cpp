#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "shared/legacy/action_ids.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                       \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);     \
      std::abort();                                                              \
    }                                                                            \
  } while (false)

namespace {

mir2::MagicConfig make_sword_magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "Sword" + std::to_string(id);
  magic.legacy.legacy_present = true;
  magic.legacy.is_sword_skill = true;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {2, 500, 1000, 1000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  magic.legacy.spell = id == 25 ? 4 : 0;
  magic.legacy.def_spell = id == 25 ? 1 : 0;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SwordMap", {}, 24, 24, 10, 10});
  for (const auto id : {3, 4, 7, 12, 25}) {
    config.magics.push_back(make_sword_magic(id));
  }
  return config;
}

mir2::SpawnConfig spawn(std::string name, std::int32_t x, std::int32_t y,
                        std::int32_t hp = 80) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = 1;
  spawn.max_hp = hp;
  spawn.attack_power = 0;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  spawn.exp_reward = 10;
  return spawn;
}

mir2::CharacterRecord character(std::string name, std::vector<std::int32_t> magics,
                                std::int32_t x = 10, std::int32_t y = 10) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.dir = 0;
  record.ability.level = 40;
  record.ability.reserved1 = 200;
  record.ability.dc = mir2::make_word(18, 18);
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 100;
  record.ability.max_mp = 100;
  record.ability.max_exp = 1000;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  for (std::size_t index = 0; index < magics.size() && index < record.magics.size(); ++index) {
    record.magics[index].magic_id = static_cast<std::uint16_t>(magics[index]);
    record.magics[index].level = 0;
  }
  return record;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand spell(std::uint64_t session_id, std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::LogicCommand attack(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                          std::uint16_t ident = mir2::kCmHit) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.dir = 0;
  command.game_message.ident = ident;
  return command;
}

void append(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action,
               std::int32_t value = -1) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.action == action && (value < 0 || trace.value == value);
                     });
}

bool has_packet_ident(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       const auto decoded = mir2::decode_legacy_game_packet(event.packet);
                       return decoded.has_value() && decoded->message.ident == ident;
                     });
}

std::int32_t count_packet_ident(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const mir2::SessionEvent& event) {
        const auto decoded = mir2::decode_legacy_game_packet(event.packet);
        return decoded.has_value() && decoded->message.ident == ident;
      }));
}

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) { return trace.action == action; }));
}

}  // namespace

int main() {
  {
    auto config = base_config();
    config.spawns.push_back(spawn("PowerTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1201, character("Power", {4}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1201, 4));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "sword_prepare", 4));
    append(dispatch, runtime.route_logic_command(attack(1201, 10, 9)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmPowerHit));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
    const auto snapshot = runtime.snapshot_character_actor("Power");
    assert(snapshot.has_value() &&
           (snapshot->magics[0].cur_train > 0 || snapshot->magics[0].level > 0));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("UnlearnedLongTarget", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1206, character("NoLong", {}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(attack(1206, 10, 8, mir2::kCmLongHit));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "sword_reject", 7));
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(!has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LongTarget", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1211, character("Long", {7}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1211, 7));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1211, 10, 8)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LongInvalid", 11, 9));
    config.spawns.push_back(spawn("LongValid", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1216, character("LongKeep", {7}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1216, 7));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1216, 11, 9)));
    append(dispatch, runtime.tick());
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(!has_trace(dispatch, "struck"));
    assert(!has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch second;
    append(second, runtime.route_logic_command(attack(1216, 10, 8)));
    append(second, runtime.tick());
    assert(has_packet_ident(second, mir2::legacy::kSmLongHit));
    assert(has_trace(second, "struck"));
    assert(has_trace(second, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("WideA", 10, 9));
    config.spawns.push_back(spawn("WideB", 9, 9));
    config.spawns.push_back(spawn("WideC", 11, 9));
    config.spawns.push_back(spawn("WideOut", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1221, character("Wide", {12}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1221, 12));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1221, 10, 9)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmWideHit));
    assert(count_trace(dispatch, "struck") == 3);
    assert(count_trace(dispatch, "train_skill") == 1);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("WideMonsterOnly", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1226, character("WidePk", {12}))));
    static_cast<void>(runtime.route_logic_command(
        enter(1227, character("WideBlockedPlayer", {3}, 9, 9))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1226, 12));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1226, 10, 9)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmWideHit));
    assert(count_trace(dispatch, "struck") == 1);
    assert(count_trace(dispatch, "train_skill") == 1);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("FireTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1231, character("Fire", {25}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1231, 25));
    append(dispatch, runtime.tick());
    auto before = runtime.snapshot_character_actor("Fire");
    assert(before.has_value() && before->ability.mp < before->ability.max_mp);
    append(dispatch, runtime.route_logic_command(attack(1231, 10, 9)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(has_trace(dispatch, "sword_prepare", 25));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch second;
    append(second, runtime.route_logic_command(attack(1231, 10, 9)));
    append(second, runtime.tick());
    assert(!has_packet_ident(second, mir2::legacy::kSmFireHit));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("FireLater", 11, 10, 160));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1236, character("FireKeep", {25}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1236, 25));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1236, 1, 1)));
    append(dispatch, runtime.tick());
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(!has_trace(dispatch, "struck"));
    assert(!has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch second;
    append(second, runtime.route_logic_command(attack(1236, 11, 10)));
    append(second, runtime.tick());
    assert(has_packet_ident(second, mir2::legacy::kSmFireHit));
    assert(has_trace(second, "struck"));
    assert(has_trace(second, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("BasicTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1241, character("Basic", {3}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(attack(1241, 10, 9));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmHit));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
  }

  return 0;
}
