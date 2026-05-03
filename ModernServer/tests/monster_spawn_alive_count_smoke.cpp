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

mir2::CharacterRecord make_hero() {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
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

mir2::LogicCommand make_enter_hero() {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = make_hero();
  return enter;
}

mir2::LogicCommand make_attack(std::uint64_t target_actor_id) {
  mir2::LogicCommand attack;
  attack.kind = mir2::LogicCommandKind::attack;
  attack.session_id = 7;
  attack.target_actor_id = target_actor_id;
  attack.x = 10;
  attack.y = 9;
  attack.game_message.ident = mir2::kCmHit;
  return attack;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 5;
  config.maps.push_back(mir2::MapConfig{"0", "AliveCount", {}, 0, 0, 20, 20});

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
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto first = runtime.tick(1000);
  const auto first_spawns = spawn_traces(first);
  assert(first_spawns.size() == 1);

  static_cast<void>(runtime.route_logic_command(make_enter_hero()));
  assert(find_packet(runtime.tick(1020), mir2::kSmNewMap).has_value());

  static_cast<void>(runtime.route_logic_command(make_attack(first_spawns.front().actor_id)));
  const auto kill = runtime.tick(1040);
  assert(find_packet(kill, mir2::kSmDeath).has_value());

  const auto refill = runtime.tick(1301);
  const auto refill_spawns = spawn_traces(refill);
  assert(refill_spawns.size() == 1);
  assert(refill_spawns.front().actor_id != first_spawns.front().actor_id);

  return 0;
}
