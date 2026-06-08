#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
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

constexpr std::uint64_t kScriptMonsterBase = 0x6000000000000000ULL;

mir2::MagicConfig make_sword_magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "Sword" + std::to_string(id);
  magic.legacy.legacy_present = true;
  magic.legacy.is_sword_skill = true;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {2, 500, 1000, 1000};
  magic.legacy.max_train_level = 3;
  if (id == 25) {
    magic.legacy.def_spell = 3;
  } else if (id == 26) {
    magic.legacy.def_spell = 7;
  } else if (id == 27) {
    magic.legacy.spell = 15;
  } else if (id == 34) {
    magic.legacy.def_spell = 4;
  }
  return magic;
}

mir2::MagicConfig make_spell_magic(std::int32_t id) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = "Magic" + std::to_string(id);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = 1;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  return magic;
}

mir2::MonsterDefConfig monster_def(std::string name) {
  mir2::MonsterDefConfig def;
  def.name = std::move(name);
  def.level = 1;
  def.hp = 40;
  def.dc = 1;
  def.dc_max = 5;
  def.accurate = 10;
  def.walk_speed_ms = 200;
  def.attack_speed_ms = 200;
  return def;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SwordPhase5", {}, 24, 24, 10, 10});
  config.magics.push_back(make_spell_magic(17));
  for (const auto id : {3, 7, 12, 25, 26, 27, 34}) {
    config.magics.push_back(make_sword_magic(id));
  }
  config.items.push_back(mir2::ItemConfig{501, "Bujuk", 1, 10, 25, 5, 1, 1000, 9});
  config.monsters.push_back(monster_def("__WhiteSkeleton"));
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
  record.equipped_items[mir2::kEquipBujuk].index = 501;
  record.equipped_items[mir2::kEquipBujuk].make_index = 9001;
  record.equipped_items[mir2::kEquipBujuk].dura = 1000;
  record.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
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

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, const std::string& action) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.action == action;
      }));
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

std::int32_t first_packet_index(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (std::int32_t index = 0; index < static_cast<std::int32_t>(dispatch.session_events.size());
       ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[index].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return -1;
}

std::vector<std::pair<std::int32_t, std::int32_t>> packet_recogs_and_delays(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  std::vector<std::pair<std::int32_t, std::int32_t>> values;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      values.push_back({decoded->message.recog, event.delay_ms});
    }
  }
  return values;
}

bool has_raw_prefix(const mir2::RuntimeDispatch& dispatch, const std::string& prefix) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       const std::string body(event.packet.body.begin(), event.packet.body.end());
                       return event.packet.header.ident == 0 && body.rfind(prefix, 0) == 0;
                     });
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
    assert(has_trace(dispatch, "train_skill", 1));

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
    assert(has_packet_ident(dispatch, mir2::kSmHealthSpellChanged));
    const auto cross_after = runtime.snapshot_character_actor("Cross34");
    assert(cross_after.has_value() && cross_after->ability.mp == 116);
    assert(count_trace(dispatch, "struck") >= 2);
    const auto struck = packet_recogs_and_delays(dispatch, mir2::kSmStruck);
    assert(struck.size() >= 2);
    assert(struck[0] == std::make_pair(2, 500));
    assert(struck[1] == std::make_pair(1, 200));
    assert(first_packet_index(dispatch, mir2::kSmStruck) <
           first_packet_index(dispatch, mir2::legacy::kSmCrossHit));
    assert(has_trace(dispatch, "train_skill"));
    assert(has_trace(dispatch, "train_skill", 1));

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
    config.spawns.push_back(spawn("CrossLowMpTarget", 10, 9, 200));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto record = character("CrossLowMp", {34, 3});
    record.ability.mp = 3;
    static_cast<void>(runtime.route_logic_command(enter(1515, std::move(record))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1515, 34));
    append(dispatch, runtime.tick());
    assert(has_raw_prefix(dispatch, "+CRS/"));
    append(dispatch, runtime.route_logic_command(attack(1515, 10, 9, mir2::kCmCrossHit)));
    append(dispatch, runtime.tick());
    assert(has_trace(dispatch, "sword_mp_downgrade"));
    assert(has_packet_ident(dispatch, mir2::legacy::kSmHit));
    assert(!has_packet_ident(dispatch, mir2::legacy::kSmCrossHit));
    const auto low_mp = runtime.snapshot_character_actor("CrossLowMp");
    assert(low_mp.has_value() && low_mp->ability.mp == 3);
  }

  {
    auto config = base_config();
    config.maps[0].fight_zone = true;
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto attacker = character("SwordPvp", {3});
    attacker.attack_mode = 0;
    static_cast<void>(runtime.route_logic_command(enter(1513, attacker)));
    auto target = character("SwordPvpTarget", {}, 10, 9);
    target.ability.exp_count = 1;
    static_cast<void>(runtime.route_logic_command(enter(1514, target)));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(attack(1513, 10, 9));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmStruck));
    assert(!has_trace(dispatch, "train_skill"));
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
    assert(has_packet_ident(dispatch, mir2::kSmBackStep));
    assert(has_trace(dispatch, "rush_push", 27));
    assert(has_trace(dispatch, "train_skill"));
    const auto hero = runtime.snapshot_character_actor("Rush27");
    const auto monster = runtime.legacy_monster_snapshot("0", 1);
    assert(hero.has_value() && hero->y == 7 && hero->ability.hp == hero->ability.max_hp);
    assert(monster.has_value() && monster->y == 6);

    mir2::RuntimeDispatch cooldown;
    append(cooldown, runtime.route_logic_command(spell(1521, 27, 0)));
    append(cooldown, runtime.tick());
    assert(has_trace(cooldown, "sword_cooldown_reject", 27));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("StackedRushTarget", 10, 9, 200, 1));
    mir2::NpcConfig npc;
    npc.id = "stacked_rush_blocker";
    npc.map_id = "0";
    npc.name = "StackedRushBlocker";
    npc.x = 10;
    npc.y = 9;
    npc.service = "none";
    config.npcs.push_back(std::move(npc));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(
        runtime.route_logic_command(enter(1522, character("StackedRush27", {27}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1522, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmBackStep));
    assert(!has_packet_ident(dispatch, mir2::kSmRush));
    assert(has_packet_ident(dispatch, mir2::kSmRushKung));
    assert(!has_trace(dispatch, "train_skill"));
    const auto hero = runtime.snapshot_character_actor("StackedRush27");
    const auto monster = runtime.legacy_monster_snapshot("0", 1);
    assert(hero.has_value() && hero->y == 10 && hero->ability.hp == hero->ability.max_hp);
    assert(monster.has_value() && monster->y == 8 && monster->hp < monster->max_hp);
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("RushChainFront", 10, 9, 200, 1));
    config.spawns.push_back(spawn("RushChainBack", 10, 8, 200, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto chain_record = character("RushChain27", {27});
    chain_record.magics[0].level = 3;
    static_cast<void>(runtime.route_logic_command(enter(1523, std::move(chain_record))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1523, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRush));
    assert(has_packet_ident(dispatch, mir2::kSmBackStep));
    const auto hero = runtime.snapshot_character_actor("RushChain27");
    const auto front = runtime.legacy_monster_snapshot("0", 1);
    const auto back = runtime.legacy_monster_snapshot("0", 2);
    assert(hero.has_value() && hero->y == 5);
    assert(front.has_value() && front->y == 4 && front->hp < front->max_hp);
    assert(back.has_value() && back->y == 3 && back->hp == back->max_hp);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto own_slave_record = character("RushOwnSlave27", {17, 27});
    own_slave_record.attack_mode = 0;
    own_slave_record.magics[0].level = 1;
    static_cast<void>(runtime.route_logic_command(enter(1524, std::move(own_slave_record))));
    static_cast<void>(runtime.tick());
    static_cast<void>(runtime.route_logic_command(spell(1524, 17, 0)));
    static_cast<void>(runtime.tick());
    const auto slave_before = runtime.legacy_monster_snapshot("0", kScriptMonsterBase);
    runtime.set_legacy_random_seed(1);
    auto dispatch = runtime.route_logic_command(spell(1524, 27, 0));
    append(dispatch, runtime.tick());
    assert(slave_before.has_value() && slave_before->y == 9);
    assert(count_trace(dispatch, "rush_gate") > 0);
    assert(has_packet_ident(dispatch, mir2::kSmBackStep));
    assert(has_packet_ident(dispatch, mir2::kSmRush));
    assert(has_trace(dispatch, "rush_push", 27));
    assert(has_trace(dispatch, "train_skill"));
    const auto hero = runtime.snapshot_character_actor("RushOwnSlave27");
    const auto slave = runtime.legacy_monster_snapshot("0", kScriptMonsterBase);
    assert(hero.has_value() && hero->y == 7 && hero->ability.hp == hero->ability.max_hp);
    assert(slave.has_value() && slave->y == 6);
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
    const auto crash_hero = runtime.snapshot_character_actor("Crash27");
    assert(crash_hero.has_value() && crash_hero->ability.hp < crash_hero->ability.max_hp);

    mir2::RuntimeDispatch retry;
    append(retry, runtime.route_logic_command(spell(1531, 27, 0)));
    append(retry, runtime.tick());
    assert(!has_trace(retry, "sword_cooldown_reject", 27));
    assert(has_packet_ident(retry, mir2::kSmRushKung));
  }

  {
    auto config = base_config();
    config.spawns.push_back(spawn("StrongRushTarget", 10, 9, 200, 100));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(enter(1541, character("BlockedPush27", {27}))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1541, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRushKung));
    const auto blocked_push = runtime.snapshot_character_actor("BlockedPush27");
    assert(blocked_push.has_value() && blocked_push->ability.hp == blocked_push->ability.max_hp);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto step_crash_record = character("StepCrash27", {27}, 10, 3);
    step_crash_record.magics[0].level = 3;
    static_cast<void>(
        runtime.route_logic_command(enter(1542, std::move(step_crash_record))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1542, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRush));
    assert(has_packet_ident(dispatch, mir2::kSmRushKung));
    const auto step_crash = runtime.snapshot_character_actor("StepCrash27");
    assert(step_crash.has_value() && step_crash->y == 0 &&
           step_crash->ability.hp >= step_crash->ability.max_hp - 49 &&
           step_crash->ability.hp <= step_crash->ability.max_hp - 25);
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto step_blocked_record = character("StepBlocked27", {27}, 10, 2);
    step_blocked_record.magics[0].level = 3;
    static_cast<void>(
        runtime.route_logic_command(enter(1543, std::move(step_blocked_record))));
    static_cast<void>(runtime.route_logic_command(
        enter(1544, character("StepBlocker", {}, 10, 0))));
    static_cast<void>(runtime.tick());
    auto dispatch = runtime.route_logic_command(spell(1543, 27, 0));
    append(dispatch, runtime.tick());
    assert(has_packet_ident(dispatch, mir2::kSmRush));
    assert(!has_packet_ident(dispatch, mir2::kSmRushKung));
    const auto step_blocked = runtime.snapshot_character_actor("StepBlocked27");
    assert(step_blocked.has_value() && step_blocked->y == 1 &&
           step_blocked->ability.hp == step_blocked->ability.max_hp);
  }

  return 0;
}
