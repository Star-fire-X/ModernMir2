#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
                                                         std::uint16_t ident,
                                                         std::int32_t recog) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.recog == recog) {
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
  hero.x = 12;
  hero.y = 12;
  hero.ability.level = 1;
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 50;
  hero.ability.max_mp = 50;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "MonsterBase", {}, 0, 0, 100, 100});

  mir2::MonsterDefConfig monster;
  monster.name = "TemplateMob";
  monster.race_server = 81;
  monster.race_image = 12;
  monster.appearance = 34;
  monster.level = 5;
  monster.cool_eye = 6;
  monster.exp = 77;
  monster.hp = 88;
  monster.mp = 9;
  monster.ac = 1;
  monster.mac = 5;
  monster.dc = 2;
  monster.dc_max = 7;
  monster.mc = 3;
  monster.sc = 4;
  monster.agility = 8;
  monster.accurate = 6;
  monster.walk_speed_ms = 100;
  monster.walk_step = 2;
  monster.walk_wait_ms = 11;
  monster.attack_speed_ms = 50;
  config.monsters.push_back(monster);

  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = "TemplateMob";
  spawn.x = 10;
  spawn.y = 10;
  spawn.area = 0;
  spawn.count = 1;
  spawn.zen_time_ms = 200;
  spawn.legacy_group = true;
  config.spawns.push_back(spawn);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto spawn_dispatch = runtime.tick(1000);
  const auto spawned = spawn_traces(spawn_dispatch);
  assert(spawned.size() == 1);
  const auto monster_id = spawned.front().actor_id;

  const auto snapshot = runtime.legacy_monster_snapshot("0", monster_id);
  assert(snapshot.has_value());
  assert(snapshot->name == "TemplateMob");
  assert(snapshot->x == spawned.front().value);
  assert(snapshot->y == spawned.front().damage);
  assert(snapshot->level == 5);
  assert(snapshot->hp == 88);
  assert(snapshot->max_hp == 88);
  assert(snapshot->mp == 9);
  assert(snapshot->max_mp == 9);
  assert(snapshot->dc_min == 2);
  assert(snapshot->dc_max == 7);
  assert(snapshot->attack_power == 7);
  assert(snapshot->defense == 1);
  assert(snapshot->magic_defense == 5);
  assert(snapshot->mc == 3);
  assert(snapshot->sc == 4);
  assert(snapshot->exp_reward == 77);
  assert(snapshot->race_server == 81);
  assert(snapshot->race_image == 12);
  assert(snapshot->appearance == 34);
  assert(snapshot->cool_eye == 6);
  assert(snapshot->speed_point == 8);
  assert(snapshot->accuracy_point == 6);
  assert(snapshot->walk_speed_ms == 200);
  assert(snapshot->walk_step == 2);
  assert(snapshot->walk_wait_ms == 11);
  assert(snapshot->attack_speed_ms == 200);

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 12;
  enter.y = 12;
  enter.character = make_hero();
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick(1020);
  const auto monster_turn =
      find_packet(login_dispatch, mir2::kSmTurn, static_cast<std::int32_t>(monster_id));
  assert(monster_turn.has_value());
  assert(monster_turn->message.param == snapshot->x);
  assert(monster_turn->message.tag == snapshot->y);

  return 0;
}
