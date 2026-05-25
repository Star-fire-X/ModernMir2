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

std::vector<mir2::LegacyRuntimeTrace> spawn_traces(const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::LegacyRuntimeTrace> traces;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "MonsterSpawn" && trace.action == "spawned") {
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

mir2::HostConfig base_config(std::uint32_t zen_time_ms) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 9;
  config.maps.push_back(mir2::MapConfig{"0", "Zen", {}, 0, 0, 40, 40});

  mir2::MonsterDefConfig oma;
  oma.name = "Oma";
  oma.hp = 1;
  oma.dc = 1;
  oma.exp = 1;
  oma.ai_profile = mir2::MonsterAiProfile::basic;
  config.monsters.push_back(oma);

  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "Oma";
  spawn.x = 10;
  spawn.y = 9;
  spawn.area = 0;
  spawn.count = 1;
  spawn.zen_time_ms = zen_time_ms;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);
  return config;
}

mir2::CharacterRecord make_hero(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord hero;
  hero.account_id = name;
  hero.character_name = std::move(name);
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(30, 30);
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

void queue_enter(mir2::LogicRuntime& runtime, std::uint64_t session_id, std::string name,
                 std::int32_t x, std::int32_t y) {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = session_id;
  enter.account_id = name;
  enter.character_name = name;
  enter.map_id = "0";
  enter.x = x;
  enter.y = y;
  enter.character = make_hero(std::move(name), x, y);
  static_cast<void>(runtime.route_logic_command(enter));
}

void queue_attack(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                  std::uint64_t target_actor_id) {
  mir2::LogicCommand attack;
  attack.kind = mir2::LogicCommandKind::attack;
  attack.session_id = session_id;
  attack.target_actor_id = target_actor_id;
  attack.x = 10;
  attack.y = 9;
  attack.game_message.ident = mir2::kCmHit;
  static_cast<void>(runtime.route_logic_command(attack));
}

}  // namespace

int main() {
  {
    mir2::LogicRuntime runtime(base_config(200));
    runtime.initialize();

    const auto first = runtime.tick(1000);
    assert(spawn_traces(first).empty());
    const auto first_spawn_dispatch = runtime.tick(1201);
    const auto first_spawns = spawn_traces(first_spawn_dispatch);
    assert(first_spawns.size() == 1);

    queue_enter(runtime, 7, "Hero", 10, 10);
    assert(find_packet(runtime.tick(1220), mir2::kSmNewMap).has_value());
    queue_attack(runtime, 7, first_spawns.front().actor_id);
    assert(find_packet(runtime.tick(1240), mir2::kSmDeath).has_value());

    assert(spawn_traces(runtime.tick(1401)).empty());
    assert(spawn_traces(runtime.tick(1402)).size() == 1);
  }

  {
    mir2::LogicRuntime runtime(base_config(500));
    runtime.initialize();

    const auto first = runtime.tick(1000);
    assert(spawn_traces(first).empty());
    const auto first_spawn_dispatch = runtime.tick(1201);
    const auto first_spawns = spawn_traces(first_spawn_dispatch);
    assert(first_spawns.size() == 1);

    assert(spawn_traces(runtime.tick(1402)).empty());

    queue_enter(runtime, 7, "Hero", 10, 10);
    assert(find_packet(runtime.tick(1420), mir2::kSmNewMap).has_value());
    queue_attack(runtime, 7, first_spawns.front().actor_id);
    assert(find_packet(runtime.tick(1440), mir2::kSmDeath).has_value());

    assert(spawn_traces(runtime.tick(1603)).empty());
    assert(spawn_traces(runtime.tick(1804)).size() == 1);
  }

  {
    auto config = base_config(500);
    config.runtime.legacy_user_full_count = 0;
    config.runtime.legacy_zen_fast_step = 1;
    mir2::LogicRuntime runtime(config);
    runtime.initialize();

    const auto first = runtime.tick(1000);
    assert(spawn_traces(first).empty());
    const auto first_spawn_dispatch = runtime.tick(1201);
    const auto first_spawns = spawn_traces(first_spawn_dispatch);
    assert(first_spawns.size() == 1);

    for (std::int32_t i = 0; i < 6; ++i) {
      queue_enter(runtime, static_cast<std::uint64_t>(20 + i), "Hero" + std::to_string(i),
                  10 + i, 12);
    }
    assert(find_packet(runtime.tick(1220), mir2::kSmNewMap).has_value());

    queue_attack(runtime, 20, first_spawns.front().actor_id);
    assert(find_packet(runtime.tick(1240), mir2::kSmDeath).has_value());
    assert(spawn_traces(runtime.tick(1402)).size() == 1);
  }

  return 0;
}
