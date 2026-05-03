#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident,
                                                         std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
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

mir2::LogicCommand make_attack(std::uint64_t session_id, std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = 4;
  command.x = 10;
  command.y = 10;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

mir2::LogicCommand make_walk(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.dir = 2;
  command.x = 11;
  command.y = 10;
  command.game_message.ident = mir2::kCmWalk;
  return command;
}

mir2::LogicCommand make_revive(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::revive;
  command.session_id = session_id;
  return command;
}

mir2::CharacterRecord make_player(std::string name, std::int32_t x, std::int32_t y,
                                  std::uint16_t hp, std::uint16_t dc_high) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 20;
  character.ability.dc = mir2::make_word(dc_high, dc_high);
  character.ability.hp = hp;
  character.ability.max_hp = hp;
  character.ability.mp = 12;
  character.ability.max_mp = 12;
  character.ability.max_weight = 50;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.attack_mode = 0;
  return character;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  mir2::MapConfig map{"0", "DeathMap", {}, 20, 20, 5, 5};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  static_cast<void>(runtime.route_logic_command(make_enter(401, make_player("Hero", 10, 10, 20, 1))));
  const auto hero_login = runtime.tick();
  const auto hero_map = find_packet(hero_login, mir2::kSmNewMap, 401);
  if (!hero_map.has_value()) {
    return fail(1);
  }
  const auto hero_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(hero_map->message.recog));

  static_cast<void>(
      runtime.route_logic_command(make_enter(402, make_player("Rival", 10, 9, 40, 30))));
  static_cast<void>(runtime.tick());

  static_cast<void>(runtime.route_logic_command(make_attack(402, hero_actor_id)));
  const auto death = runtime.tick();
  if (!find_packet(death, mir2::kSmNowDeath, 401).has_value() ||
      !find_packet(death, mir2::kSmDeath, 402).has_value()) {
    return fail(2);
  }
  const auto dead_snapshot = runtime.snapshot_character_actor("Hero");
  if (!dead_snapshot.has_value() || dead_snapshot->ability.hp != 0 ||
      dead_snapshot->death_time_ms == 0) {
    return fail(3);
  }

  static_cast<void>(runtime.route_logic_command(make_walk(401)));
  const auto dead_walk = runtime.tick();
  if (find_packet(dead_walk, mir2::kSmWalk, 401).has_value()) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(make_revive(401)));
  const auto revive = runtime.tick();
  if (!find_packet(revive, mir2::kSmAlive, 401).has_value() ||
      !find_packet(revive, mir2::kSmHealthSpellChanged, 401).has_value() ||
      !find_packet(revive, mir2::kSmAbility, 401).has_value()) {
    return fail(5);
  }
  const auto alive_snapshot = runtime.snapshot_character_actor("Hero");
  if (!alive_snapshot.has_value() || alive_snapshot->death_time_ms != 0 ||
      alive_snapshot->ability.hp == 0 || alive_snapshot->map_id != "0" ||
      alive_snapshot->x != 5 || alive_snapshot->y != 5) {
    return fail(6);
  }

  return 0;
}
