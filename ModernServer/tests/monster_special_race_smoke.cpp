#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "world/logic_runtime.hpp"

namespace {

void check(bool condition, const char* expression, const char* file, int line) {
  if (condition) {
    return;
  }
  std::cerr << "check failed: " << expression << " at " << file << ":" << line << "\n";
  std::exit(1);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

mir2::CharacterRecord make_hero(std::string name, std::int32_t x, std::int32_t y,
                                std::int32_t hp = 200,
                                std::int32_t pk_point = 0) {
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
  hero.pk_point = pk_point;
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
           std::int32_t x, std::int32_t y, std::int32_t hp = 200,
           std::int32_t pk_point = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = name;
  command.character_name = name;
  command.map_id = "0";
  command.x = x;
  command.y = y;
  command.character = make_hero(std::move(name), x, y, hp, pk_point);
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

mir2::RuntimeDispatch spawn_legacy_groups(mir2::LogicRuntime& runtime,
                                          std::size_t group_count) {
  mir2::RuntimeDispatch aggregate;
  auto append = [&](mir2::RuntimeDispatch dispatch) {
    aggregate.legacy_traces.insert(aggregate.legacy_traces.end(),
                                   dispatch.legacy_traces.begin(),
                                   dispatch.legacy_traces.end());
    aggregate.session_events.insert(aggregate.session_events.end(),
                                    std::make_move_iterator(dispatch.session_events.begin()),
                                    std::make_move_iterator(dispatch.session_events.end()));
  };

  append(runtime.tick(1000));
  for (std::size_t index = 0; index < group_count; ++index) {
    append(runtime.tick(1201 + index * 201));
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

bool has_trace_value(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
                     std::string_view action, std::int32_t value,
                     std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action &&
                              trace.value == value &&
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
    config.monsters.push_back(make_monster("ThornDark", 93));
    config.monsters.push_back(make_monster("ArcherMon", 104));
    config.monsters.push_back(make_monster("BeeQueen", 103));
    config.monsters.push_back(make_monster("SpiderHouse", 116));
    config.monsters.push_back(make_monster("CentipedeKing", 107));
    config.monsters.push_back(make_monster("SculKing", 102));
    config.monsters.push_back(make_monster("CastleDoor", 110));
    config.monsters.push_back(make_monster("Wall", 111));
    config.spawns.push_back(make_spawn("DualAxe", 10, 10));
    config.spawns.push_back(make_spawn("ThornDark", 11, 10));
    config.spawns.push_back(make_spawn("ArcherMon", 12, 10));
    config.spawns.push_back(make_spawn("BeeQueen", 14, 10));
    config.spawns.push_back(make_spawn("SpiderHouse", 15, 10));
    config.spawns.push_back(make_spawn("CentipedeKing", 16, 10));
    config.spawns.push_back(make_spawn("SculKing", 17, 10));
    config.spawns.push_back(make_spawn("CastleDoor", 18, 10));
    config.spawns.push_back(make_spawn("Wall", 19, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto dual_id = spawned_actor_id(dispatch, "DualAxe");
    const auto thorn_id = spawned_actor_id(dispatch, "ThornDark");
    const auto archer_id = spawned_actor_id(dispatch, "ArcherMon");
    const auto bee_id = spawned_actor_id(dispatch, "BeeQueen");
    const auto spider_house_id = spawned_actor_id(dispatch, "SpiderHouse");
    const auto centipede_id = spawned_actor_id(dispatch, "CentipedeKing");
    const auto scul_id = spawned_actor_id(dispatch, "SculKing");
    const auto door_id = spawned_actor_id(dispatch, "CastleDoor");
    const auto wall_id = spawned_actor_id(dispatch, "Wall");
    CHECK(dual_id.has_value());
    CHECK(thorn_id.has_value());
    CHECK(archer_id.has_value());
    CHECK(bee_id.has_value());
    CHECK(spider_house_id.has_value());
    CHECK(centipede_id.has_value());
    CHECK(scul_id.has_value());
    CHECK(door_id.has_value());
    CHECK(wall_id.has_value());
    const auto dual = runtime.legacy_monster_snapshot("0", *dual_id);
    const auto thorn = runtime.legacy_monster_snapshot("0", *thorn_id);
    const auto archer = runtime.legacy_monster_snapshot("0", *archer_id);
    const auto bee = runtime.legacy_monster_snapshot("0", *bee_id);
    const auto spider_house = runtime.legacy_monster_snapshot("0", *spider_house_id);
    const auto centipede = runtime.legacy_monster_snapshot("0", *centipede_id);
    const auto scul = runtime.legacy_monster_snapshot("0", *scul_id);
    const auto door = runtime.legacy_monster_snapshot("0", *door_id);
    const auto wall = runtime.legacy_monster_snapshot("0", *wall_id);
    CHECK(dual.has_value());
    CHECK(dual->chain_shot_count == 2);
    CHECK(!dual->hide_mode);
    CHECK(thorn.has_value());
    CHECK(thorn->chain_shot_count == 3);
    CHECK(archer.has_value());
    CHECK(archer->chain_shot_count == 6);
    CHECK(bee.has_value());
    CHECK(bee->stick_mode);
    CHECK(bee->summon_limit == 15);
    CHECK(spider_house.has_value());
    CHECK(spider_house->stick_mode);
    CHECK(spider_house->summon_limit == 15);
    CHECK(centipede.has_value());
    CHECK(centipede->hide_mode);
    CHECK(centipede->stick_mode);
    CHECK(centipede->dig_up_range == 4);
    CHECK(scul.has_value());
    CHECK(scul->hide_mode);
    CHECK(scul->dig_up_range == 2);
    CHECK(door.has_value());
    CHECK(door->stick_mode);
    CHECK(wall.has_value());
    CHECK(wall->stick_mode);
  }

  {
    struct SpitCase {
      const char* name;
      std::int32_t race;
      bool expects_poison_gate;
    };
    for (const auto test : {SpitCase{"SpitSpider", 82, true},
                            SpitCase{"HighRiskSpider", 118, false},
                            SpitCase{"BigPoisonSpider", 119, true}}) {
      auto config = base_config(test.name);
      config.monsters.push_back(make_monster(test.name, test.race));
      config.spawns.push_back(make_spawn(test.name, 10, 10));
      mir2::LogicRuntime runtime(config);
      runtime.initialize();
      const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
      CHECK(spawned_actor_id(spawn_dispatch).has_value());
      enter(runtime, 7, "Hero", 10, 8);
      auto dispatch = run_until(runtime, 1220, 3600);
      CHECK(has_trace_value(dispatch, "MonsterSpecial", "attack_broadcast",
                            test.race, "RM_HIT"));
      CHECK(has_trace(dispatch, "MonsterSpecial", "struck", "SM_STRUCK"));
      const auto poisoned = has_trace(dispatch, "MonsterSpecial", "poison_gate") ||
                            has_trace(dispatch, "MonsterSpecial", "poison_apply");
      CHECK(poisoned == test.expects_poison_gate);
    }
  }

  {
    struct GasCase {
      const char* name;
      std::int32_t race;
      const char* random_action;
      bool expects_poison_gate;
    };
    for (const auto test : {GasCase{"BigKudeki", 90, "gas_hit", true},
                            GasCase{"MagCow", 91, "anti_magic", false},
                            GasCase{"GasMoth", 105, "gas_hit", true},
                            GasCase{"GasDung", 106, "gas_hit", true},
                            GasCase{"ToxicGhost", 127, "gas_hit", true}}) {
      auto config = base_config(test.name);
      config.monsters.push_back(make_monster(test.name, test.race));
      config.spawns.push_back(make_spawn(test.name, 10, 10));
      mir2::LogicRuntime runtime(config);
      runtime.initialize();
      const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
      CHECK(spawned_actor_id(spawn_dispatch).has_value());
      enter(runtime, 7, "Hero", 10, 9);
      auto dispatch = run_until(runtime, 1220, 3600);
      CHECK(has_trace_value(dispatch, "MonsterSpecial", "attack_broadcast",
                            test.race, "RM_HIT"));
      CHECK(has_trace(dispatch, "MonsterSpecial", test.random_action));
      const auto poisoned = has_trace(dispatch, "MonsterSpecial", "poison_gate") ||
                            has_trace(dispatch, "MonsterSpecial", "poison_apply");
      CHECK(poisoned == test.expects_poison_gate);
    }
  }

  {
    auto config = base_config("FlyAxe");
    config.monsters.push_back(make_monster("DualAxe", 87));
    config.spawns.push_back(make_spawn("DualAxe", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    CHECK(spawned_actor_id(spawn_dispatch).has_value());
    enter(runtime, 7, "Hero", 10, 5);
    auto dispatch = run_until(runtime, 1220, 4500);
    CHECK(has_trace(dispatch, "MonsterSpecial", "attack_broadcast", "RM_FLYAXE"));
    CHECK(has_trace(dispatch, "MonsterSpecial", "struck", "SM_STRUCK"));
  }

  {
    auto config = base_config("StickHide");
    config.monsters.push_back(make_monster("Herb", 85));
    config.spawns.push_back(make_spawn("Herb", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto herb_id = spawned_actor_id(spawn_dispatch);
    CHECK(herb_id.has_value());
    auto hidden = runtime.legacy_monster_snapshot("0", *herb_id);
    CHECK(hidden.has_value());
    CHECK(hidden->hide_mode);
    enter(runtime, 7, "Hero", 10, 13);
    auto dispatch = run_until(runtime, 1020, 1800);
    auto visible = runtime.legacy_monster_snapshot("0", *herb_id);
    CHECK(visible.has_value());
    CHECK(!visible->hide_mode);
    CHECK(has_trace(dispatch, "MonsterSpecial", "dig_up", "RM_DIGUP"));
    CHECK(!runtime.find_legacy_event("0", 10, 10,
                                     mir2::LegacyEventType::digout_zombi).has_value());
  }

  {
    auto config = base_config("DigOutZombi");
    config.monsters.push_back(make_monster("DigOutZombi", 95));
    config.spawns.push_back(make_spawn("DigOutZombi", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto zombie_id = spawned_actor_id(spawn_dispatch);
    CHECK(zombie_id.has_value());
    auto hidden = runtime.legacy_monster_snapshot("0", *zombie_id);
    CHECK(hidden.has_value());
    CHECK(hidden->hide_mode);
    CHECK(hidden->dig_up_range == 3);
    enter(runtime, 7, "Hero", 13, 10);
    auto dispatch = run_until(runtime, 1020, 1800);
    auto visible = runtime.legacy_monster_snapshot("0", *zombie_id);
    CHECK(visible.has_value());
    CHECK(!visible->hide_mode);
    CHECK(has_trace(dispatch, "MonsterSpecial", "dig_up", "RM_DIGUP"));
    auto event = runtime.find_legacy_event("0", 10, 10,
                                           mir2::LegacyEventType::digout_zombi);
    CHECK(event.has_value());
    CHECK(!event->blocks_walk);
    static_cast<void>(runtime.run_legacy_event_manager(
        event->open_start_ms + event->continue_ms + 1ULL));
    CHECK(!runtime.find_legacy_event("0", 10, 10,
                                     mir2::LegacyEventType::digout_zombi).has_value());
  }

  {
    auto config = base_config("Summon");
    config.monsters.push_back(make_monster("BeeQueen", 103));
    config.monsters.push_back(make_monster("SpiderHouse", 116));
    config.spawns.push_back(make_spawn("BeeQueen", 10, 10));
    config.spawns.push_back(make_spawn("SpiderHouse", 14, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto queen_id = spawned_actor_id(spawn_dispatch);
    const auto house_id = spawned_actor_id(spawn_dispatch, "SpiderHouse");
    CHECK(queen_id.has_value());
    CHECK(house_id.has_value());
    enter(runtime, 7, "Hero", 10, 9);
    auto dispatch = run_until(runtime, 1220, 3000);
    CHECK(has_trace(dispatch, "MonsterSpecial", "summon_child", "__Bee"));
    CHECK(has_trace(dispatch, "MonsterSpecial", "summon_child", "__Spider"));
    const auto queen = runtime.legacy_monster_snapshot("0", *queen_id);
    const auto house = runtime.legacy_monster_snapshot("0", *house_id);
    CHECK(queen.has_value());
    CHECK(queen->child_actor_count >= 1);
    CHECK(house.has_value());
    CHECK(house->child_actor_count >= 1);
  }

  {
    auto config = base_config("CentipedeKing");
    config.monsters.push_back(make_monster("CentipedeKing", 107));
    config.spawns.push_back(make_spawn("CentipedeKing", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto centipede_id = spawned_actor_id(spawn_dispatch);
    CHECK(centipede_id.has_value());
    enter(runtime, 7, "Hero", 13, 10);
    auto dispatch = run_until(runtime, 1220, 15000);
    const auto visible = runtime.legacy_monster_snapshot("0", *centipede_id);
    CHECK(visible.has_value());
    CHECK(!visible->hide_mode);
    CHECK(has_trace_value(dispatch, "MonsterSpecial", "dig_up", 107, "RM_DIGUP"));
    CHECK(has_trace(dispatch, "MonsterSpecial", "centipede_power"));
  }

  {
    auto config = base_config("SculKing");
    config.monsters.push_back(make_monster("SculKing", 102));
    config.spawns.push_back(make_spawn("SculKing", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto spawn_dispatch = spawn_legacy_groups(runtime, config.spawns.size());
    const auto scul_id = spawned_actor_id(spawn_dispatch);
    CHECK(scul_id.has_value());
    enter(runtime, 7, "Hero", 11, 10);
    auto dispatch = run_until(runtime, 1220, 2200);
    const auto visible = runtime.legacy_monster_snapshot("0", *scul_id);
    CHECK(visible.has_value());
    CHECK(!visible->hide_mode);
    CHECK(has_trace_value(dispatch, "MonsterSpecial", "dig_up", 102, "RM_DIGUP"));
  }

  {
    auto config = base_config("Guard");
    auto guard = make_monster("ArcherGuard", 112);
    guard.attack_speed_ms = 200;
    config.monsters.push_back(guard);
    config.spawns.push_back(make_spawn("ArcherGuard", 10, 10));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(spawn_legacy_groups(runtime, config.spawns.size()));
    enter(runtime, 7, "BlueHero", 10, 5);
    auto calm = run_until(runtime, 1220, 2200);
    CHECK(!has_trace(calm, "MonsterSpecial", "attack_broadcast", "RM_FLYAXE"));

    enter(runtime, 8, "RedHero", 11, 5, 200, 200);
    auto hostile = run_until(runtime, 2220, 3600);
    CHECK(has_trace_value(hostile, "MonsterSpecial", "attack_broadcast", 112, "RM_FLYAXE"));
  }

  return 0;
}

bool has_trace_value(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
                     std::string_view action, std::int32_t value,
                     std::string_view label = {}) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == stage && trace.action == action &&
                              trace.value == value &&
                              (label.empty() || trace.label == label);
                     });
}
