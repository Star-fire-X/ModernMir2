#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  return find_packet(dispatch, ident).has_value();
}

mir2::CharacterRecord make_hero(std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = x;
  hero.y = y;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(0, 8);
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

mir2::SpawnConfig make_spawn(std::string name, std::int32_t x, std::int32_t y) {
  mir2::SpawnConfig spawn;
  spawn.map_id = "0";
  spawn.actor_type = "monster";
  spawn.name = std::move(name);
  spawn.x = x;
  spawn.y = y;
  spawn.respawn_ms = 30000;
  return spawn;
}

mir2::RuntimeDispatch enter_and_tick(mir2::LogicRuntime& runtime,
                                     std::int32_t x = 10,
                                     std::int32_t y = 10) {
  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = x;
  enter.y = y;
  enter.character = make_hero(x, y);
  static_cast<void>(runtime.route_logic_command(enter));
  return runtime.tick();
}

mir2::RuntimeDispatch run_ticks(mir2::LogicRuntime& runtime, int count) {
  mir2::RuntimeDispatch last;
  for (int index = 0; index < count; ++index) {
    last = runtime.tick();
  }
  return last;
}

}  // namespace

int main() {
  {
    mir2::HostConfig config;
    config.maps.push_back(mir2::MapConfig{"0", "Passive", {}, 0, 0, 20, 20});
    mir2::MonsterDefConfig deer;
    deer.name = "Deer";
    deer.hp = 12;
    deer.dc = 3;
    deer.ai_profile = mir2::MonsterAiProfile::passive_animal;
    config.monsters.push_back(deer);
    config.spawns.push_back(make_spawn("Deer", 10, 8));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(enter_and_tick(runtime));
    for (int index = 0; index < 8; ++index) {
      const auto dispatch = runtime.tick();
      assert(!has_packet(dispatch, mir2::kSmWalk));
      assert(!has_packet(dispatch, mir2::kSmHit));
      assert(!has_packet(dispatch, mir2::kSmStruck));
    }
  }

  {
    mir2::HostConfig config;
    config.maps.push_back(mir2::MapConfig{"0", "Aggressive", {}, 0, 0, 20, 20});
    mir2::MonsterDefConfig oma;
    oma.name = "Oma";
    oma.race_server = 81;
    oma.hp = 12;
    oma.dc = 3;
    oma.walk_speed_ms = 20;
    config.monsters.push_back(oma);
    config.spawns.push_back(make_spawn("Oma", 10, 8));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(enter_and_tick(runtime));
    const auto chase = runtime.tick();
    const auto walk = find_packet(chase, mir2::kSmWalk);
    assert(walk.has_value());
    assert(walk->message.param == 10);
    assert(walk->message.tag == 9);
  }

  {
    mir2::HostConfig config;
    config.maps.push_back(mir2::MapConfig{"0", "Stationary", {}, 0, 0, 20, 20});
    mir2::MonsterDefConfig plant;
    plant.name = "Plant";
    plant.hp = 12;
    plant.dc = 3;
    plant.attack_speed_ms = 40;
    plant.ai_profile = mir2::MonsterAiProfile::stationary;
    config.monsters.push_back(plant);
    config.spawns.push_back(make_spawn("Plant", 10, 8));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(enter_and_tick(runtime));
    bool struck = false;
    bool walked = false;
    for (int index = 0; index < 8; ++index) {
      const auto dispatch = runtime.tick();
      struck = struck || has_packet(dispatch, mir2::kSmStruck);
      walked = walked || has_packet(dispatch, mir2::kSmWalk);
    }
    assert(struck);
    assert(!walked);
  }

  {
    mir2::HostConfig config;
    config.maps.push_back(mir2::MapConfig{"0", "Ranged", {}, 0, 0, 20, 20});
    mir2::MonsterDefConfig archer;
    archer.name = "Archer";
    archer.hp = 12;
    archer.dc = 3;
    archer.attack_speed_ms = 40;
    archer.ai_profile = mir2::MonsterAiProfile::ranged;
    config.monsters.push_back(archer);
    config.spawns.push_back(make_spawn("Archer", 10, 6));

    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    static_cast<void>(enter_and_tick(runtime));
    bool struck = false;
    bool walked = false;
    for (int index = 0; index < 8; ++index) {
      const auto dispatch = runtime.tick();
      struck = struck || has_packet(dispatch, mir2::kSmStruck);
      walked = walked || has_packet(dispatch, mir2::kSmWalk);
    }
    assert(struck);
    assert(!walked);
  }

  return 0;
}
