#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
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

constexpr std::uint64_t kScriptMonsterBase = 0x6000000000000000ULL;

mir2::MagicConfig make_magic(std::int32_t id, std::string name) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = std::move(name);
  magic.affect_players = true;
  magic.affect_monsters = true;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = id;
  magic.legacy.effect = id;
  magic.legacy.spell = 1;
  magic.legacy.min_power = 1;
  magic.legacy.max_power = 1;
  magic.legacy.job = 99;
  magic.legacy.need_level = {1, 1, 1, 1};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 0;
  return magic;
}

mir2::MonsterDefConfig make_monster_def(std::string name, std::int32_t hp,
                                        std::int32_t dc_max) {
  mir2::MonsterDefConfig def;
  def.name = std::move(name);
  def.level = 1;
  def.hp = hp;
  def.dc = 1;
  def.dc_max = dc_max;
  def.accurate = 10;
  def.walk_speed_ms = 200;
  def.attack_speed_ms = 200;
  return def;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y,
                             std::int32_t level = 1, std::int32_t life_attrib = 0) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  spawn.level = level;
  spawn.max_hp = 30;
  spawn.attack_power = 1;
  spawn.defense = 0;
  spawn.magic_defense = 0;
  spawn.exp_reward = 10;
  spawn.life_attrib = life_attrib;
  return spawn;
}

mir2::HostConfig base_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "SlaveSkill", {}, 24, 24, 10, 10});
  config.items.push_back(
      mir2::ItemConfig{501, "Bujuk", 1, 10, 25, 5, 1, 1000, 9, 0, 0});
  config.magics.push_back(make_magic(17, "Summon Skeleton"));
  config.magics.push_back(make_magic(20, "Temptation"));
  config.magics.push_back(make_magic(30, "Summon ShinSu"));
  config.monsters.push_back(make_monster_def("__WhiteSkeleton", 40, 5));
  config.monsters.push_back(make_monster_def("__ShinSu", 80, 8));
  return config;
}

mir2::CharacterRecord make_character(std::string name, std::vector<std::int32_t> magic_ids,
                                     std::int32_t item_dura = 1000) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 30;
  character.ability.hp = 40;
  character.ability.max_hp = 40;
  character.ability.mp = 100;
  character.ability.max_mp = 100;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.equipped_items[mir2::kEquipBujuk].index = 501;
  character.equipped_items[mir2::kEquipBujuk].make_index = 7501;
  character.equipped_items[mir2::kEquipBujuk].dura = static_cast<std::uint16_t>(item_dura);
  character.equipped_items[mir2::kEquipBujuk].dura_max = 1000;
  for (std::size_t index = 0; index < magic_ids.size() && index < character.magics.size();
       ++index) {
    character.magics[index].magic_id = static_cast<std::uint16_t>(magic_ids[index]);
    character.magics[index].level = 1;
    character.magics[index].key = static_cast<char>('1' + index);
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

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
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

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action, std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action &&
                              (label.empty() || trace.label == label);
                     });
}

std::int32_t count_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
                         std::string_view action) {
  return static_cast<std::int32_t>(std::count_if(
      dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
      [&](const mir2::LegacyRuntimeTrace& trace) {
        return trace.stage == stage && trace.action == action;
      }));
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1201, make_character("Skeletonist", {17}, 1000))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(1201, 17)));
    auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "LegacySpell", "bujuk_used"));
    assert(has_trace(dispatch, "LegacySlave", "summon", "__WhiteSkeleton"));
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
    assert(has_packet(dispatch, mir2::kSmDuraChange));

    auto slave = runtime.legacy_monster_snapshot("0", kScriptMonsterBase);
    assert(slave.has_value());
    assert(slave->name == "__WhiteSkeleton");
    assert(slave->is_slave);
    assert(slave->master_actor_id == 1);
    assert(slave->no_item);
    assert(slave->slave_make_level == 1);
    assert(slave->slave_exp_level == 1);
    assert(slave->master_royalty_time_ms > 9ULL * 24ULL * 60ULL * 60ULL * 1000ULL);

    static_cast<void>(advance_ticks(runtime, 60));
    static_cast<void>(runtime.route_logic_command(make_spell(1201, 17)));
    dispatch = runtime.tick();
    assert(has_trace(dispatch, "LegacySlave", "summon_reject", "max_slave"));
    assert(!has_trace(dispatch, "LegacySkill", "train_skill"));
    assert(!runtime.legacy_monster_snapshot("0", kScriptMonsterBase + 1).has_value());
  }

  {
    auto config = base_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1211, make_character("ShinSuist", {30}, 500))));
    static_cast<void>(runtime.tick());

    static_cast<void>(runtime.route_logic_command(make_spell(1211, 30)));
    const auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "LegacySpell", "bujuk_used"));
    assert(has_trace(dispatch, "LegacySlave", "summon", "__ShinSu"));
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
    auto slave = runtime.legacy_monster_snapshot("0", kScriptMonsterBase);
    assert(slave.has_value());
    assert(slave->name == "__ShinSu");
    assert(slave->master_actor_id == 1);
    assert(slave->no_item);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("Hen", 10, 8));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1221, make_character("Tamer", {20}, 0))));
    static_cast<void>(runtime.tick());

    runtime.set_legacy_random_seed(674);
    static_cast<void>(runtime.route_logic_command(make_spell(1221, 20, 10, 8, 1)));
    const auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "LegacySlave", "tame", "Hen"));
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
    auto target = runtime.legacy_monster_snapshot("0", 1);
    assert(target.has_value());
    assert(target->is_slave);
    assert(target->master_actor_id == 2);
    assert(target->no_item);
    assert(target->slave_make_level == 1);
    assert(target->slave_life_time_ms > 0);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("TooStrong", 10, 8, 40));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1231, make_character("WeakTamer", {20}, 0))));
    static_cast<void>(runtime.tick());

    runtime.set_legacy_random_seed(6);
    static_cast<void>(runtime.route_logic_command(make_spell(1231, 20, 10, 8, 1)));
    const auto dispatch = runtime.tick();
    assert(count_trace(dispatch, "LegacySlave", "tame") == 0);
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
    auto target = runtime.legacy_monster_snapshot("0", 1);
    assert(target.has_value());
    assert(!target->is_slave);
  }

  {
    auto config = base_config();
    config.spawns.push_back(make_spawn("HenA", 8, 8));
    config.spawns.push_back(make_spawn("HenB", 9, 8));
    config.spawns.push_back(make_spawn("HenC", 10, 8));
    config.spawns.push_back(make_spawn("HenD", 11, 8));
    config.spawns.push_back(make_spawn("UndeadHen", 12, 8, 1, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.route_logic_command(
        make_enter(1241, make_character("ManyTamer", {20}, 0))));
    static_cast<void>(runtime.tick());

    for (std::uint64_t actor_id = 1; actor_id <= 3; ++actor_id) {
      static_cast<void>(advance_ticks(runtime, 60));
      runtime.set_legacy_random_seed(674);
      static_cast<void>(
          runtime.route_logic_command(make_spell(1241, 20, 8 + static_cast<std::int32_t>(actor_id) - 1,
                                                 8, actor_id)));
      const auto dispatch = runtime.tick();
      assert(has_trace(dispatch, "LegacySlave", "tame"));
    }

    static_cast<void>(advance_ticks(runtime, 60));
    runtime.set_legacy_random_seed(674);
    static_cast<void>(runtime.route_logic_command(make_spell(1241, 20, 11, 8, 4)));
    auto dispatch = runtime.tick();
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
    assert(count_trace(dispatch, "LegacySlave", "tame") == 0);

    auto target = runtime.legacy_monster_snapshot("0", 4);
    assert(target.has_value());
    assert(!target->is_slave);

    auto single_config = base_config();
    single_config.spawns.push_back(make_spawn("UndeadHen", 10, 8, 1, 1));
    mir2::LogicRuntime single_runtime(single_config);
    single_runtime.initialize();
    static_cast<void>(single_runtime.route_logic_command(
        make_enter(1251, make_character("UndeadReject", {20}, 0))));
    static_cast<void>(single_runtime.tick());
    single_runtime.set_legacy_random_seed(674);
    static_cast<void>(single_runtime.route_logic_command(make_spell(1251, 20, 10, 8, 1)));
    dispatch = single_runtime.tick();
    assert(has_trace(dispatch, "LegacySlave", "lighting_shock_death"));
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));

    auto no_tame_config = base_config();
    auto no_tame = make_spawn("NoTame", 10, 8);
    no_tame.tameable = false;
    no_tame_config.spawns.push_back(no_tame);
    mir2::LogicRuntime no_tame_runtime(no_tame_config);
    no_tame_runtime.initialize();
    static_cast<void>(no_tame_runtime.route_logic_command(
        make_enter(1261, make_character("NoTameReject", {20}, 0))));
    static_cast<void>(no_tame_runtime.tick());
    no_tame_runtime.set_legacy_random_seed(5);
    static_cast<void>(no_tame_runtime.route_logic_command(make_spell(1261, 20, 10, 8, 1)));
    dispatch = no_tame_runtime.tick();
    assert(has_trace(dispatch, "LegacySlave", "crazy"));
    assert(has_trace(dispatch, "LegacySkill", "train_skill"));
  }

  return 0;
}
