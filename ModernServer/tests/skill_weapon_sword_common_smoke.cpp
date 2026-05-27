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
  magic.legacy.def_spell = id == 26 ? 7 : 0;
  return magic;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SwordMap", {}, 24, 24, 10, 10});
  for (const auto id : {3, 4, 7, 12, 25, 26}) {
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

mir2::HostConfig non_sword_seven_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "NonSwordSevenMap", {}, 24, 24, 10, 10});
  config.spawns.push_back(spawn("NonSwordSevenTarget", 10, 9, 10000));
  mir2::MagicConfig magic;
  magic.id = 7;
  magic.name = "NonSwordSeven";
  magic.mp_cost = 1;
  magic.power = 1;
  config.magics.push_back(magic);
  return config;
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
  record.equipped_items[mir2::kEquipWeapon].index = 100;
  record.equipped_items[mir2::kEquipWeapon].make_index = 1000;
  record.equipped_items[mir2::kEquipWeapon].dura = 1000;
  record.equipped_items[mir2::kEquipWeapon].dura_max = 1000;
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

const mir2::LegacyRuntimeTrace* find_trace(const mir2::RuntimeDispatch& dispatch,
                                           const std::string& action) {
  const auto it = std::find_if(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                               [&](const mir2::LegacyRuntimeTrace& trace) {
                                 return trace.action == action;
                               });
  return it == dispatch.legacy_traces.end() ? nullptr : &*it;
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

std::int32_t count_raw_prefix(const mir2::RuntimeDispatch& dispatch,
                              const std::string& prefix) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const mir2::SessionEvent& event) {
        const std::string body(event.packet.body.begin(), event.packet.body.end());
        return event.packet.header.ident == 0 && body.rfind(prefix, 0) == 0;
      }));
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
    config.spawns.push_back(spawn("PowerTarget", 10, 9, 10000));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1201, character("Power", {7}))));
    static_cast<void>(runtime.tick(1000));
    auto now_ms = 2000ULL;
    auto dispatch = runtime.route_logic_command(attack(1201, 1, 1));
    append(dispatch, runtime.tick(now_ms));
    now_ms += 1000;
    for (int attempt = 0; attempt < 8 && !has_raw_prefix(dispatch, "+PWR/"); ++attempt) {
      append(dispatch, runtime.route_logic_command(attack(1201, 1, 1)));
      append(dispatch, runtime.tick(now_ms));
      now_ms += 1000;
    }
    assert(has_raw_prefix(dispatch, "+PWR/"));

    mir2::RuntimeDispatch normal;
    for (int attempt = 0; attempt < 8; ++attempt) {
      append(normal, runtime.route_logic_command(attack(1201, 10, 9)));
      append(normal, runtime.tick(now_ms));
      now_ms += 1000;
    }
    assert(count_raw_prefix(normal, "+PWR/") == 0);
    assert(!has_packet_ident(normal, mir2::legacy::kSmPowerHit));

    mir2::RuntimeDispatch power;
    append(power, runtime.route_logic_command(attack(1201, 10, 9, mir2::kCmPowerHit)));
    append(power, runtime.tick(now_ms));
    assert(has_packet_ident(power, mir2::legacy::kSmPowerHit));
    assert(has_trace(power, "power_hit_bonus"));
    assert(has_trace(power, "struck"));
    assert(has_trace(power, "train_skill"));
    const auto snapshot = runtime.snapshot_character_actor("Power");
    assert(snapshot.has_value() &&
           (snapshot->magics[0].cur_train > 0 || snapshot->magics[0].level > 0));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("HeavyTarget", 10, 9, 10000));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1204, character("Heavy", {4}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(attack(1204, 10, 9, mir2::kCmHeavyHit));
    append(dispatch, runtime.tick(2000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmHeavyHit));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
    const auto* damage = find_trace(dispatch, "damage");
    assert(damage != nullptr && damage->value == 18 && damage->damage == 18);
  }

  {
    auto config = non_sword_seven_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1203, character("NonSwordSeven", {7}))));
    static_cast<void>(runtime.tick(1000));
    auto now_ms = 2000ULL;
    mir2::RuntimeDispatch dispatch;
    for (int attempt = 0; attempt < 8; ++attempt) {
      append(dispatch, runtime.route_logic_command(attack(1203, 1, 1)));
      append(dispatch, runtime.tick(now_ms));
      now_ms += 1000;
    }
    assert(!has_raw_prefix(dispatch, "+PWR/"));

    mir2::RuntimeDispatch power;
    append(power, runtime.route_logic_command(attack(1203, 10, 9, mir2::kCmPowerHit)));
    append(power, runtime.tick(now_ms));
    assert(!has_packet_ident(power, mir2::legacy::kSmPowerHit));
    assert(has_packet_ident(power, mir2::legacy::kSmHit));
    assert(has_trace(power, "struck"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("PowerNotReadyTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1202, character("PowerNotReady", {7}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(attack(1202, 10, 9, mir2::kCmPowerHit));
    append(dispatch, runtime.tick(2000));
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmPowerHit));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmHit));
    assert(has_trace(dispatch, "struck"));
    assert(!has_trace(dispatch, "power_hit_bonus"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("UnlearnedLongTarget", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1206, character("NoLong", {}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(attack(1206, 10, 8, mir2::kCmLongHit));
    append(dispatch, runtime.tick(2000));
    assert(has_trace(dispatch, "sword_reject", 12));
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(!has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LongTarget", 10, 8));
    config.spawns.back().defense = 200;
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1211, character("Long", {12}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1211, 12));
    append(dispatch, runtime.tick(2000));
    assert(has_raw_prefix(dispatch, "+LNG/"));
    append(dispatch, runtime.route_logic_command(attack(1211, 10, 8, mir2::kCmLongHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 200) == 0);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 1);
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LongInvalid", 11, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1216, character("LongKeep", {12}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1216, 12));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1216, 11, 9, mir2::kCmLongHit)));
    append(dispatch, runtime.tick(3000));
    assert(!has_trace(dispatch, "struck"));
    assert(!has_trace(dispatch, "train_skill"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("LongFrontBlocker", 10, 9));
    config.spawns.push_back(spawn("LongBehindBlocker", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1217, character("LongExact", {12}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1217, 12));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1217, 0, 0, mir2::kCmLongHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmLongHit));
    assert(count_trace(dispatch, "struck") == 1);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 1);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("WideA", 10, 9));
    auto wide_b = spawn("WideB", 9, 9);
    wide_b.defense = 200;
    config.spawns.push_back(wide_b);
    auto wide_c = spawn("WideC", 11, 9);
    wide_c.defense = 200;
    config.spawns.push_back(wide_c);
    config.spawns.push_back(spawn("WideOut", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1221, character("Wide", {25}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1221, 25));
    append(dispatch, runtime.tick(2000));
    assert(has_raw_prefix(dispatch, "+WID/"));
    append(dispatch, runtime.route_logic_command(attack(1221, 10, 9, mir2::kCmWideHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmWideHit));
    assert(count_trace(dispatch, "struck") == 3);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 200) == 1);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 2);
    assert(count_trace(dispatch, "train_skill") == 1);
  }

  {
    auto config = base_config();
    auto left_first = spawn("WideLeftFirst", 9, 9, 80);
    left_first.defense = 200;
    config.spawns.push_back(left_first);
    config.spawns.push_back(spawn("WideFrontSecond", 10, 9, 80));
    config.spawns.push_back(spawn("WideRightThird", 11, 9, 80));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1222, character("WideOrder", {25}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1222, 25));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1222, 10, 9, mir2::kCmWideHit)));
    append(dispatch, runtime.tick(3000));
    assert(count_trace(dispatch, "struck") == 3);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 200) == 1);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 2);
    const auto left = runtime.legacy_monster_snapshot("0", 1);
    assert(left.has_value() && left->hp < left->max_hp);
  }

  {
    auto config = base_config();
    auto left_only = spawn("WideLeftOnly", 9, 9, 80);
    left_only.defense = 200;
    config.spawns.push_back(left_only);
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1223, character("WideSideOnly", {25}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1223, 25));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1223, 0, 0, mir2::kCmWideHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmWideHit));
    assert(count_trace(dispatch, "struck") == 1);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 200) == 0);
    assert(count_packet_ident_delay(dispatch, mir2::kSmStruck, 500) == 1);
    const auto left = runtime.legacy_monster_snapshot("0", 1);
    assert(left.has_value() && left->hp < left->max_hp);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("WideMonsterOnly", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1226, character("WidePk", {25}))));
    static_cast<void>(runtime.route_logic_command(
        enter(1227, character("WideBlockedPlayer", {3}, 9, 9))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1226, 25));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1226, 10, 9, mir2::kCmWideHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmWideHit));
    assert(count_trace(dispatch, "struck") == 1);
    assert(count_trace(dispatch, "train_skill") == 1);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("FireTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1231, character("Fire", {26}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1231, 26));
    append(dispatch, runtime.tick(2000));
    auto before = runtime.snapshot_character_actor("Fire");
    assert(before.has_value() && before->ability.mp < before->ability.max_mp);
    append(dispatch, runtime.route_logic_command(attack(1231, 10, 9, mir2::kCmFireHit)));
    append(dispatch, runtime.tick(3000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(has_trace(dispatch, "sword_prepare", 26));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch second;
    append(second, runtime.route_logic_command(attack(1231, 10, 9)));
    append(second, runtime.tick(4000));
    assert(!has_packet_ident(second, mir2::legacy::kSmFireHit));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("FireLater", 11, 10, 160));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1236, character("FireKeep", {26}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1236, 26));
    append(dispatch, runtime.tick(2000));
    append(dispatch, runtime.route_logic_command(attack(1236, 1, 1)));
    append(dispatch, runtime.tick(3000));
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmFireHit));
    assert(!has_trace(dispatch, "struck"));
    assert(!has_trace(dispatch, "train_skill"));

    mir2::RuntimeDispatch second;
    append(second, runtime.route_logic_command(attack(1236, 11, 10, mir2::kCmFireHit)));
    append(second, runtime.tick(4000));
    assert(has_packet_ident(second, mir2::legacy::kSmFireHit));
    assert(has_trace(second, "struck"));
    assert(has_trace(second, "train_skill"));
  }

  {
    auto config = base_config();
    config.budgets.tick_ms = 1000;
    config.spawns.push_back(spawn("FireExpired", 10, 9, 160));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1237, character("FireExpire", {26}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(spell(1237, 26));
    append(dispatch, runtime.tick(2000));
    assert(has_trace(dispatch, "sword_prepare", 26));
    for (auto now_ms = 3000ULL; now_ms <= 23000ULL; now_ms += 1000ULL) {
      append(dispatch, runtime.tick(now_ms));
    }

    mir2::RuntimeDispatch expired;
    append(expired, runtime.route_logic_command(attack(1237, 10, 9, mir2::kCmFireHit)));
    append(expired, runtime.tick(24000));
    assert(!has_packet_ident(expired, mir2::legacy::kSmFireHit));
    assert(has_packet_ident(expired, mir2::legacy::kSmHit));
    assert(has_trace(expired, "struck"));
    assert(!has_trace(expired, "fire_hit_bonus"));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("BasicTarget", 10, 9));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1241, character("Basic", {3}))));
    static_cast<void>(runtime.tick(1000));
    auto dispatch = runtime.route_logic_command(attack(1241, 10, 9));
    append(dispatch, runtime.tick(2000));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmHit));
    assert(has_trace(dispatch, "struck"));
    assert(has_trace(dispatch, "train_skill"));
  }

  return 0;
}
