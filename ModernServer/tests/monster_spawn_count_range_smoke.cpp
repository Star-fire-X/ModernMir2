#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::vector<mir2::LegacyRuntimeTrace> spawn_traces(const mir2::RuntimeDispatch& dispatch,
                                                   const std::string& action = "spawned") {
  std::vector<mir2::LegacyRuntimeTrace> traces;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterSpawn" && trace.action == action) {
      traces.push_back(trace);
    }
  }
  return traces;
}

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

mir2::SpawnConfig make_group_spawn(std::string name, std::int32_t x, std::int32_t y,
                                   std::int32_t area, std::int32_t count,
                                   std::int32_t small_zen_rate = 0) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.area = area;
  spawn.count = count;
  spawn.zen_time_ms = 200;
  spawn.small_zen_rate = small_zen_rate;
  spawn.legacy_group = true;
  return spawn;
}

mir2::CharacterRecord make_hero() {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 30;
  hero.y = 30;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(0, 30);
  hero.ability.mc = mir2::make_word(0, 30);
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 50;
  hero.ability.max_mp = 50;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 1;
  hero.magics[0].level = 1;
  return hero;
}

void enter_hero(mir2::LogicRuntime& runtime, std::uint64_t now_ms) {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 30;
  enter.y = 30;
  enter.character = make_hero();
  static_cast<void>(runtime.route_logic_command(enter));
  const auto dispatch = runtime.tick(now_ms);
  assert(find_packet(dispatch, mir2::kSmNewMap).has_value());
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 7;
    config.maps.push_back(mir2::MapConfig{"0", "SpawnRange", {}, 0, 0, 100, 100});
    config.monsters.push_back(mir2::MonsterDefConfig{"Oma", 81, 0, 0, 1, false, 0, 10, 8});
    config.magics.push_back(mir2::MagicConfig{1, "Fireball", 0, 40});
    config.spawns.push_back(make_group_spawn("Oma", 30, 30, 8, 5));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    const auto first = runtime.tick(1000);
    assert(spawn_traces(first).empty());
    const auto first_spawn_dispatch = runtime.tick(1201);
    const auto spawned = spawn_traces(first_spawn_dispatch);
    assert(spawned.size() == 5);
    for (const auto& trace : spawned) {
      assert(trace.value >= 22 && trace.value <= 38);
      assert(trace.damage >= 22 && trace.damage <= 38);
    }

    enter_hero(runtime, 1220);
    mir2::LogicCommand spell;
    spell.kind = mir2::LogicCommandKind::spell;
    spell.session_id = 7;
    spell.target_actor_id = spawned.front().actor_id;
    spell.x = spawned.front().value;
    spell.y = spawned.front().damage;
    spell.game_message.ident = mir2::kCmSpell;
    spell.game_message.tag = 1;
    static_cast<void>(runtime.route_logic_command(spell));
    const auto kill = runtime.tick(1240);
    assert(find_packet(kill, mir2::kSmDeath).has_value());

    const auto refill = runtime.tick(1402);
    const auto refill_spawns = spawn_traces(refill);
    assert(refill_spawns.size() == 1);
  }

  {
    mir2::HostConfig config;
    config.runtime.legacy_random_seed = 17;
    config.maps.push_back(mir2::MapConfig{"0", "SmallZen", {}, 0, 0, 100, 100});
    config.monsters.push_back(mir2::MonsterDefConfig{"Oma", 81, 0, 0, 1, false, 0, 10, 8});
    config.spawns.push_back(make_group_spawn("Oma", 30, 30, 8, 5, 100));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    assert(spawn_traces(runtime.tick(1000)).empty());
    const auto dispatch = runtime.tick(1201);
    const auto spawned = spawn_traces(dispatch);
    assert(spawned.size() == 5);
    auto min_x = spawned.front().value;
    auto max_x = spawned.front().value;
    auto min_y = spawned.front().damage;
    auto max_y = spawned.front().damage;
    for (const auto& trace : spawned) {
      min_x = std::min(min_x, trace.value);
      max_x = std::max(max_x, trace.value);
      min_y = std::min(min_y, trace.damage);
      max_y = std::max(max_y, trace.damage);
    }
    assert(max_x - min_x <= 20);
    assert(max_y - min_y <= 20);
  }

  {
    mir2::HostConfig config;
    config.maps.push_back(mir2::MapConfig{"0", "Blocked", {}, 0, 0, 1, 1});
    config.spawns.push_back(make_group_spawn("BlockedMob", 50, 50, 0, 1));
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    assert(spawn_traces(runtime.tick(1000)).empty());
    const auto dispatch = runtime.tick(1201);
    assert(spawn_traces(dispatch).empty());
    assert(spawn_traces(dispatch, "blocked").size() == 1);
  }

  return 0;
}
