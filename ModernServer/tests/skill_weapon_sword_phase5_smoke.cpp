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
  if (id == 26) {
    magic.legacy.def_spell = 7;
  } else if (id == 27) {
    magic.legacy.spell = 15;
  }
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SwordPhase5", {}, 24, 24, 10, 10});
  for (const auto id : {3, 7, 12, 25, 26, 27, 34}) {
    config.magics.push_back(make_sword_magic(id));
  }
  return config;
}

mir2::SpawnConfig spawn(std::string name, std::int32_t x, std::int32_t y,
                        std::int32_t hp = 160, std::int32_t level = 1) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = level;
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
  record.ability.level = 50;
  record.ability.reserved1 = 200;
  record.ability.dc = mir2::make_word(24, 24);
  record.ability.hp = 120;
  record.ability.max_hp = 120;
  record.ability.mp = 120;
  record.ability.max_mp = 120;
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

mir2::LogicCommand spell(std::uint64_t session_id, std::int32_t magic_id,
                         std::int32_t x = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.x = x;
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

std::int32_t count_packet_ident_delay(const mir2::RuntimeDispatch& dispatch,
                                      std::uint16_t ident, std::int32_t delay_ms) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const mir2::SessionEvent& event) {
        const auto decoded = mir2::decode_legacy_game_packet(event.packet);
        return decoded.has_value() && decoded->message.ident == ident &&
               event.delay_ms == delay_ms;
      }));
}

bool has_raw_prefix(const mir2::RuntimeDispatch& dispatch, const std::string& prefix) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       const std::string body(event.packet.body.begin(), event.packet.body.end());
                       return event.packet.header.ident == 0 && body.rfind(prefix, 0) == 0;
                     });
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
    config.spawns.push_back(spawn("Fire26Target", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1501, character("Fire26", {26}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1501, 26));
    append(dispatch, runtime.tick());
    const auto after_prepare = runtime.snapshot_character_actor("Fire26");
    assert(after_prepare.has_value() && after_prepare->ability.mp < after_prepare->ability.max_mp);
    assert(has_raw_prefix(dispatch, "+FIR/"));
    append(dispatch, runtime.route_logic_command(attack(1501, 10, 9, mir2::kCmFireHit)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(has_trace(dispatch, "fire_hit_bonus"));
    assert(has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch cooldown;
    append(cooldown, runtime.route_logic_command(spell(1501, 26)));
    append(cooldown, runtime.tick());
    assert(has_trace(cooldown, "sword_cooldown_reject", 26));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("CrossFront", 10, 9, 200));
    auto cross_left = spawn("CrossLeft", 9, 9, 200);
    cross_left.defense = 200;
    config.spawns.push_back(cross_left);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1511, character("Cross34", {34}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1511, 34));
    append(dispatch, runtime.tick());
    assert(has_raw_prefix(dispatch, "+CRS/"));
    assert(has_trace(dispatch, "sword_toggle", 34));
    append(dispatch, runtime.route_logic_command(attack(1511, 10, 9, mir2::kCmCrossHit)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmCrossHit));
    assert(count_trace(dispatch, "struck") >= 2);
    assert(has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch disabled;
    append(disabled, runtime.route_logic_command(spell(1511, 34)));
    append(disabled, runtime.tick());
    assert(has_raw_prefix(disabled, "+UCRS/"));
    append(disabled, runtime.route_logic_command(attack(1511, 10, 9, mir2::kCmCrossHit)));
    append(disabled, runtime.tick());
    assert(has_trace(disabled, "sword_reject", 34));
  }

  {
    auto config = base_config();
    auto cross_left = spawn("CrossSideOnly", 9, 9, 200);
    cross_left.defense = 200;
    config.spawns.push_back(cross_left);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1512, character("CrossSide", {34}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1512, 34));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(attack(1512, 0, 0, mir2::kCmCrossHit)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmCrossHit));
    assert(count_trace(dispatch, "struck") == 1);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 200) == 0);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 1);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1516, character("WideCross", {25, 34}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1516, 25));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(spell(1516, 34)));
    append(dispatch, runtime.tick());
    assert(has_raw_prefix(dispatch, "+WID/"));
    assert(has_raw_prefix(dispatch, "+CRS/"));
    assert(!has_raw_prefix(dispatch, "+UWID/"));
    assert(!has_raw_prefix(dispatch, "+UCRS/"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("FireAfterCrossTarget", 10, 9, 160));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(enter(1517, character("FireAfterCross", {26, 34}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1517, 26));
    append(dispatch, runtime.tick());
    append(dispatch, runtime.route_logic_command(spell(1517, 34)));
    append(dispatch, runtime.tick());
    assert(has_raw_prefix(dispatch, "+FIR/"));
    assert(has_raw_prefix(dispatch, "+CRS/"));
    append(dispatch, runtime.route_logic_command(attack(1517, 10, 9, mir2::kCmFireHit)));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(has_trace(dispatch, "fire_hit_bonus"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("RushTarget", 10, 9, 200, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1521, character("Rush27", {27}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1521, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRush));
    assert(has_trace(dispatch, "rush_push", 27));
    assert(has_trace(dispatch, "train_skill"));
    const auto hero = runtime.snapshot_character_actor("Rush27");
    const auto monster = runtime.legacy_monster_snapshot("0", 1);
    assert(hero.has_value() && hero->y == 9);
    assert(monster.has_value() && monster->y == 8);

    mir2::RuntimeDispatch cooldown;
    append(cooldown, runtime.route_logic_command(spell(1521, 27, 0)));
    append(cooldown, runtime.tick());
    assert(has_trace(cooldown, "sword_cooldown_reject", 27));
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1531, character("Crash27", {27}, 10, 0))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1531, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRushKung));
    assert(has_trace(dispatch, "rush_crash", 27));
    assert(!has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch retry;
    append(retry, runtime.route_logic_command(spell(1531, 27, 0)));
    append(retry, runtime.tick());
    assert(!has_trace(retry, "sword_cooldown_reject", 27));
    assert(has_packet_ident(retry, mir2::kSmRushKung));
  }

  return 0;
}
