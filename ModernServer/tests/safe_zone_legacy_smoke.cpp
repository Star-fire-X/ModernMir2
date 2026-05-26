#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
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

bool has_hear_text(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                   std::string_view expected) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmHear) {
      continue;
    }
    if (mir2::legacy_decode_string(decoded->body).find(expected) != std::string::npos) {
      return true;
    }
  }
  return false;
}

mir2::CharacterRecord make_character(std::string name, int x, int y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 20;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.dc = mir2::make_word(0, 8);
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  static_cast<void>(runtime.route_logic_command(command));
}

mir2::LogicCommand make_attack(std::uint64_t session_id, std::int32_t x, std::int32_t y,
                               std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

void assert_safe_zone_attack_blocked(const mir2::MapConfig& map, int hero_x, int hero_y, int rival_x,
                                     int rival_y) {
  mir2::HostConfig config;
  config.maps.push_back(map);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  enter(runtime, 1, make_character("Hero", hero_x, hero_y));
  const auto hero_login = runtime.tick(1000);
  const auto hero_map = find_packet(hero_login, 1, mir2::kSmNewMap);
  assert(hero_map.has_value());

  enter(runtime, 2, make_character("Rival", rival_x, rival_y));
  const auto rival_login = runtime.tick(2000);
  const auto rival_map = find_packet(rival_login, 2, mir2::kSmNewMap);
  assert(rival_map.has_value());

  static_cast<void>(runtime.tick(6000));
  static_cast<void>(runtime.route_logic_command(
      make_attack(1, rival_x, rival_y, static_cast<std::uint64_t>(rival_map->message.recog))));
  const auto attack_dispatch = runtime.tick(7000);
  assert(has_hear_text(attack_dispatch, 1, "Safe zone"));
}

}  // namespace

int main() {
  mir2::HostConfig start_point_config;
  mir2::MapConfig start_point_map;
  start_point_map.id = "0";
  start_point_map.title = "StartPointSafe";
  start_point_map.width = 100;
  start_point_map.height = 100;
  start_point_map.allow_pk = true;
  start_point_map.safe_zones.push_back(mir2::MapZoneConfig{20, 20, 21, 21});
  start_point_config.maps.push_back(start_point_map);
  mir2::LogicRuntime start_point_runtime(start_point_config);
  start_point_runtime.initialize();
  enter(start_point_runtime, 1, make_character("Hero", 20, 20));
  auto dispatch = start_point_runtime.tick(1000);
  auto area = find_packet(dispatch, 1, mir2::kSmAreaState);
  assert(area.has_value() && (area->message.recog & 1) == 0);
  assert_safe_zone_attack_blocked(start_point_map, 20, 20, 20, 21);

  mir2::MapConfig badman_map;
  badman_map.id = "0";
  badman_map.title = "BadmanSafe";
  badman_map.width = 100;
  badman_map.height = 100;
  badman_map.allow_pk = true;
  badman_map.badman_zones.push_back(mir2::MapZoneConfig{50, 50, 21, 21});
  assert_safe_zone_attack_blocked(badman_map, 60, 60, 60, 61);

  mir2::MapConfig full_safe;
  full_safe.id = "0";
  full_safe.title = "FullSafe";
  full_safe.width = 100;
  full_safe.height = 100;
  full_safe.law_full = true;
  full_safe.allow_pk = false;
  mir2::HostConfig full_safe_config;
  full_safe_config.maps.push_back(full_safe);
  mir2::LogicRuntime full_safe_runtime(full_safe_config);
  full_safe_runtime.initialize();
  enter(full_safe_runtime, 2, make_character("Lawful", 90, 90));
  const auto full_safe_dispatch = full_safe_runtime.tick(1000);
  area = find_packet(full_safe_dispatch, 2, mir2::kSmAreaState);
  assert(area.has_value() && (area->message.recog & 1) != 0);

  mir2::HostConfig guild_war_config;
  mir2::MapConfig guild_war_map;
  guild_war_map.id = "0";
  guild_war_map.title = "GuildWar";
  guild_war_map.width = 100;
  guild_war_map.height = 100;
  guild_war_map.allow_pk = true;
  guild_war_map.fight3_zone = true;
  guild_war_config.maps.push_back(guild_war_map);
  mir2::LogicRuntime guild_war_runtime(guild_war_config);
  guild_war_runtime.initialize();
  enter(guild_war_runtime, 3, make_character("Warrior", 40, 40));
  const auto guild_war_dispatch = guild_war_runtime.tick(1000);
  area = find_packet(guild_war_dispatch, 3, mir2::kSmAreaState);
  assert(area.has_value());
  assert((area->message.recog & 1) == 0);
  assert((area->message.recog & 2) != 0);
  assert((area->message.recog & 4) != 0);
  return 0;
}
