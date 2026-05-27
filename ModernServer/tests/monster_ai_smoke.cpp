#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
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

std::optional<mir2::DecodedLegacyGamePacket> find_packet_at(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::uint16_t x,
    std::uint16_t y) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.param == x && decoded->message.tag == y) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_recog(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, std::int32_t recog) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        decoded->message.recog == recog) {
      return decoded;
    }
  }
  return std::nullopt;
}

template <typename T>
std::optional<T> decode_body(std::string_view body) {
  T value{};
  if (!mir2::legacy_decode_buffer(body, &value, sizeof(value))) {
    return std::nullopt;
  }
  return value;
}

mir2::LogicCommand make_attack_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
                                       std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "MonsterAIMap", {}, 0, 0, 10, 10});
  mir2::MonsterDefConfig goblin;
  goblin.name = "Goblin";
  goblin.race_image = 12;
  goblin.appearance = 3;
  goblin.hp = 8;
  goblin.dc = 4;
  goblin.dc_max = 4;
  goblin.accurate = 20;
  goblin.exp = 12;
  goblin.ai_profile = mir2::MonsterAiProfile::aggressive;
  config.monsters.push_back(goblin);
  config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "Goblin", 10, 8, 40, 1, 8, 4, 0, 0, 12});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.dir = 0;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(8, 8);
  hero.ability.hp = 15;
  hero.ability.max_hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 10;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  const auto new_map = find_packet(login_dispatch, mir2::kSmNewMap);
  const auto monster_turn = find_packet_at(login_dispatch, mir2::kSmTurn, 10, 8);
  if (!new_map.has_value() || !monster_turn.has_value() ||
      monster_turn->message.param != 10 || monster_turn->message.tag != 8) {
    return fail(1);
  }
  const auto monster_actor_id = monster_turn->message.recog;
  const auto turn_desc = decode_body<mir2::LegacyCharDesc>(monster_turn->body);
  if (!turn_desc.has_value() || turn_desc->feature == 0) {
    return fail(7);
  }
  const auto player_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map->message.recog));

  std::optional<mir2::DecodedLegacyGamePacket> monster_walk;
  for (int index = 0; index < 200; ++index) {
    const auto chase_dispatch = runtime.tick();
    monster_walk = find_packet(chase_dispatch, mir2::kSmWalk);
    if (monster_walk.has_value()) {
      break;
    }
  }
  if (!monster_walk.has_value() || monster_walk->message.recog != monster_actor_id ||
      monster_walk->message.param != 10 || monster_walk->message.tag != 9 ||
      monster_walk->message.series != 4) {
    return fail(2);
  }

  mir2::RuntimeDispatch attack_dispatch;
  std::optional<mir2::DecodedLegacyGamePacket> monster_hit;
  std::optional<mir2::DecodedLegacyGamePacket> hero_struck;
  for (int index = 0; index < 100; ++index) {
    attack_dispatch = runtime.tick();
    monster_hit = find_packet_by_recog(attack_dispatch, mir2::kSmHit, monster_actor_id);
    hero_struck = find_packet_by_recog(
        attack_dispatch, mir2::kSmStruck, static_cast<std::int32_t>(player_actor_id));
    if (monster_hit.has_value() && hero_struck.has_value()) {
      break;
    }
  }
  if (!monster_hit.has_value() || !hero_struck.has_value() ||
      monster_hit->message.recog != monster_actor_id ||
      monster_hit->message.series != 4 ||
      hero_struck->message.recog != static_cast<std::int32_t>(player_actor_id) ||
      hero_struck->message.param != 11 || hero_struck->message.tag != 15 ||
      hero_struck->message.series != 4) {
    return fail(3);
  }
  const auto struck_body = decode_body<mir2::LegacyMessageBodyWL>(hero_struck->body);
  if (!struck_body.has_value() || struck_body->ltag1 <= 0) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(make_attack_command(10, 0, 10, 9)));
  const auto kill_dispatch = runtime.tick();
  const auto monster_death = find_packet(kill_dispatch, mir2::kSmDeath);
  if (!monster_death.has_value() || monster_death->message.recog != monster_actor_id ||
      monster_death->message.param != 10 || monster_death->message.tag != 9 ||
      monster_death->message.series != 4) {
    return fail(5);
  }

  const auto corpse = runtime.legacy_monster_snapshot("0", monster_actor_id);
  if (!corpse.has_value() || corpse->hp != 0 || !corpse->death_settled ||
      corpse->ghosted) {
    return fail(6);
  }

  const auto corpse_wait_dispatch = runtime.tick(corpse->death_time_ms + 1000);
  if (find_packet_at(corpse_wait_dispatch, mir2::kSmTurn, 10, 8).has_value()) {
    return fail(8);
  }

  const auto ghost_now_ms = corpse->death_time_ms + 180001;
  const auto ghost_dispatch = runtime.tick(ghost_now_ms);
  if (runtime.legacy_monster_snapshot("0", monster_actor_id).has_value() ||
      !find_packet_by_recog(ghost_dispatch, mir2::kSmDisappear,
                            monster_actor_id).has_value()) {
    return fail(9);
  }

  std::optional<mir2::DecodedLegacyGamePacket> monster_respawn;
  for (int index = 0; index < 20; ++index) {
    const auto respawn_dispatch = runtime.tick(ghost_now_ms + 20 + index * 20);
    monster_respawn = find_packet_at(respawn_dispatch, mir2::kSmTurn, 10, 8);
    if (monster_respawn.has_value()) {
      break;
    }
  }
  if (!monster_respawn.has_value() || monster_respawn->message.recog != monster_actor_id ||
      monster_respawn->message.param != 10 || monster_respawn->message.tag != 8) {
    return fail(6);
  }

  return 0;
}
