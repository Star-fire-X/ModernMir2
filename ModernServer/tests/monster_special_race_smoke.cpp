#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_hero(std::string name, std::int32_t x, std::int32_t y,
                                std::int32_t hp = 200) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = name;
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 20;
  hero.ability.hp = hp;
  hero.ability.max_hp = hp;
  hero.ability.mp = 50;
  hero.ability.max_mp = 50;
  hero.ability.max_exp = 1000;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

mir2::MonsterDefConfig make_monster(std::string name, std::int32_t race_server) {
  mir2::MonsterDefConfig monster;
  monster.name = std::move(name);
  monster.race_server = race_server;
  monster.hp = 80;
  monster.dc = 8;
  monster.dc_max = 12;
  monster.accurate = 100;
  monster.agility = 5;
  monster.walk_speed_ms = 200;
  monster.attack_speed_ms = 200;
  monster.exp = 1;
  return monster;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.area = 0;
  spawn.count = 1;
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  return spawn;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id, std::string name,
           std::int32_t x, std::int32_t y, std::int32_t hp = 200) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = name;
  command.character_name = name;
  command.map_id = "0";
  command.x = x;
  command.y = y;
  command.character = make_hero(std::move(name), x, y, hp);
  static_cast<void>(runtime.route_logic_command(command));
}

mir2::RuntimeDispatch run_until(mir2::LogicRuntime& runtime, std::uint64_t start_ms,
                                std::uint64_t end_ms, std::uint64_t step_ms = 20) {
  mir2::RuntimeDispatch aggregate;
  for (auto now = start_ms; now <= end_ms; now += step_ms) {
    auto dispatch = runtime.tick(now);
    aggregate.legacy_traces.insert(aggregate.legacy_traces.end(),
                                   dispatch.legacy_traces.begin(),
                                   dispatch.legacy_traces.end());
    aggregate.session_events.insert(aggregate.session_events.end(),
                                    std::make_move_iterator(dispatch.session_events.begin()),
                                    std::make_move_iterator(dispatch.session_events.end()));
  }
  return aggregate;
}

std::optional<std::uint64_t> spawned_actor_id(const mir2::RuntimeDispatch& dispatch,
                                             std::string_view name = {}) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterSpawn" && trace.action == "spawned" &&
        (name.empty() || trace.object_name == name)) {
      return trace.actor_id;
    }
  }
  return std::nullopt;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action, std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action &&
                              (label.empty() || trace.label == label);
                     });
}

mir2::HostConfig base_config(std::string map_name) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", std::move(map_name), {}, 0, 0, 40, 40});
  return config;
}

}  // namespace

int main() {
  {
    auto config = base_config("RaceMap");
    config.monsters.push_back(make_monster("DualAxe", 87));
    config.monsters.push_back(make_monster("BeeQueen", 103));
    config.spawns.push_back(make_spawn("DualAxe", 10, 10));
    config.spawns.push_back(make_spawn("BeeQueen", 14, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto dispatch = runtime.tick(1000);
    const auto dual_id = spawned_actor_id(dispatch, "DualAxe");
    const auto bee_id = spawned_actor_id(dispatch, "BeeQueen");
    assert(dual_id.has_value());
    assert(bee_id.has_value());
    const auto dual = runtime.legacy_monster_snapshot("0", *dual_id);
    const auto bee = runtime.legacy_monster_snapshot("0", *bee_id);
    assert(dual.has_value());
    assert(dual->chain_shot_count == 2);
    assert(!dual->hide_mode);
    assert(bee.has_value());
    assert(bee->stick_mode);
    assert(bee->summon_limit == 15);
  }

  {
    auto config = base_config("SpitSpider");
    config.monsters.push_back(make_monster("SpitSpider", 82));
    config.spawns.push_back(make_spawn("SpitSpider", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = runtime.tick(1000);
    assert(spawned_actor_id(spawn_dispatch).has_value());
    enter(runtime, 7, "Hero", 10, 8);
    auto dispatch = run_until(runtime, 1020, 2600);
    assert(has_trace(dispatch, "MonsterSpecial", "attack_broadcast", "RM_HIT"));
    assert(has_trace(dispatch, "MonsterSpecial", "struck", "SM_STRUCK"));
  }

  {
    auto config = base_config("FlyAxe");
    config.monsters.push_back(make_monster("DualAxe", 87));
    config.spawns.push_back(make_spawn("DualAxe", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = runtime.tick(1000);
    assert(spawned_actor_id(spawn_dispatch).has_value());
    enter(runtime, 7, "Hero", 10, 5);
    auto dispatch = run_until(runtime, 1020, 3200);
    assert(has_trace(dispatch, "MonsterSpecial", "attack_broadcast", "RM_FLYAXE"));
    assert(has_trace(dispatch, "MonsterSpecial", "struck", "SM_STRUCK"));
  }

  {
    auto config = base_config("StickHide");
    config.monsters.push_back(make_monster("Herb", 85));
    config.spawns.push_back(make_spawn("Herb", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = runtime.tick(1000);
    const auto herb_id = spawned_actor_id(spawn_dispatch);
    assert(herb_id.has_value());
    auto hidden = runtime.legacy_monster_snapshot("0", *herb_id);
    assert(hidden.has_value());
    assert(hidden->hide_mode);
    enter(runtime, 7, "Hero", 10, 13);
    auto dispatch = run_until(runtime, 1020, 1800);
    auto visible = runtime.legacy_monster_snapshot("0", *herb_id);
    assert(visible.has_value());
    assert(!visible->hide_mode);
    assert(has_trace(dispatch, "MonsterSpecial", "dig_up", "RM_DIGUP"));
    assert(!runtime.find_legacy_event("0", 10, 10,
                                      mir2::LegacyEventType::digout_zombi).has_value());
  }

  {
    auto config = base_config("DigOutZombi");
    config.monsters.push_back(make_monster("DigOutZombi", 95));
    config.spawns.push_back(make_spawn("DigOutZombi", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = runtime.tick(1000);
    const auto zombie_id = spawned_actor_id(spawn_dispatch);
    assert(zombie_id.has_value());
    auto hidden = runtime.legacy_monster_snapshot("0", *zombie_id);
    assert(hidden.has_value());
    assert(hidden->hide_mode);
    assert(hidden->dig_up_range == 3);
    enter(runtime, 7, "Hero", 13, 10);
    auto dispatch = run_until(runtime, 1020, 1800);
    auto visible = runtime.legacy_monster_snapshot("0", *zombie_id);
    assert(visible.has_value());
    assert(!visible->hide_mode);
    assert(has_trace(dispatch, "MonsterSpecial", "dig_up", "RM_DIGUP"));
    auto event = runtime.find_legacy_event("0", 10, 10,
                                           mir2::LegacyEventType::digout_zombi);
    assert(event.has_value());
    assert(!event->blocks_walk);
    static_cast<void>(runtime.run_legacy_event_manager(
        event->open_start_ms + event->continue_ms + 1ULL));
    assert(!runtime.find_legacy_event("0", 10, 10,
                                      mir2::LegacyEventType::digout_zombi).has_value());
  }

  {
    auto config = base_config("Summon");
    config.monsters.push_back(make_monster("BeeQueen", 103));
    config.spawns.push_back(make_spawn("BeeQueen", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = runtime.tick(1000);
    const auto queen_id = spawned_actor_id(spawn_dispatch);
    assert(queen_id.has_value());
    enter(runtime, 7, "Hero", 10, 9);
    auto dispatch = run_until(runtime, 1020, 2600);
    assert(has_trace(dispatch, "MonsterSpecial", "summon_child", "__Bee"));
    const auto queen = runtime.legacy_monster_snapshot("0", *queen_id);
    assert(queen.has_value());
    assert(queen->child_actor_count >= 1);
  }

  {
    auto config = base_config("Guard");
    auto guard = make_monster("ArcherGuard", 112);
    guard.attack_speed_ms = 200;
    config.monsters.push_back(guard);
    config.spawns.push_back(make_spawn("ArcherGuard", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(runtime.tick(1000));
    enter(runtime, 7, "BlueHero", 10, 5);
    auto calm = run_until(runtime, 1020, 2200);
    assert(!has_trace(calm, "MonsterSpecial", "attack_broadcast", "RM_FLYAXE"));
  }

  return 0;
}
