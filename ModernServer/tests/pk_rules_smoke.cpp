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

mir2::LogicCommand make_move_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
                                     std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::walk;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmWalk;
  return command;
}

mir2::LogicCommand make_attack_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
                                       std::int32_t y, std::uint64_t target_actor_id = 0) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.target_actor_id = target_actor_id;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  mir2::MapConfig map;
  map.id = "0";
  map.title = "PkRulesMap";
  map.home_x = 10;
  map.home_y = 10;
  map.allow_pk = false;
  map.safe_zones.push_back(mir2::MapZoneConfig{10, 9, 1, 2});
  config.maps.push_back(map);
  config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "TownGuard", 10, 8, 0, 1, 8, 4, 0, 0, 12});

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
  hero.ability.dc = mir2::make_word(0, 8);
  hero.ability.hp = 15;
  hero.ability.max_hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::LogicCommand hero_enter;
  hero_enter.kind = mir2::LogicCommandKind::enter_world;
  hero_enter.session_id = 30;
  hero_enter.account_id = "guest";
  hero_enter.character_name = "Hero";
  hero_enter.map_id = "0";
  hero_enter.x = 10;
  hero_enter.y = 10;
  hero_enter.character = hero;
  static_cast<void>(runtime.route_logic_command(hero_enter));

  const auto hero_login = runtime.tick();
  const auto hero_map = find_packet(hero_login, mir2::kSmNewMap);
  const auto hero_area = find_packet(hero_login, mir2::kSmAreaState);
  if (!hero_map.has_value() || !hero_area.has_value() || hero_area->message.recog != 1) {
    return fail(1);
  }

  mir2::CharacterRecord rival = hero;
  rival.account_id = "guest2";
  rival.character_name = "Rival";
  rival.x = 10;
  rival.y = 9;

  mir2::LogicCommand rival_enter;
  rival_enter.kind = mir2::LogicCommandKind::enter_world;
  rival_enter.session_id = 31;
  rival_enter.account_id = "guest2";
  rival_enter.character_name = "Rival";
  rival_enter.map_id = "0";
  rival_enter.x = 10;
  rival_enter.y = 9;
  rival_enter.character = rival;
  static_cast<void>(runtime.route_logic_command(rival_enter));

  const auto rival_login = runtime.tick();
  const auto rival_area = find_packet(rival_login, mir2::kSmAreaState);
  if (!rival_area.has_value() || rival_area->message.recog != 1) {
    return fail(2);
  }

  for (int attempt = 0; attempt < 6; ++attempt) {
    const auto safe_tick = runtime.tick();
    if (find_packet(safe_tick, mir2::kSmHit).has_value() ||
        find_packet(safe_tick, mir2::kSmStruck).has_value()) {
      return fail(3);
    }
  }

  static_cast<void>(runtime.route_logic_command(make_move_command(30, 2, 11, 10)));
  const auto hero_leave_safe = runtime.tick();
  const auto hero_area_leave = find_packet(hero_leave_safe, mir2::kSmAreaState);
  if (!hero_area_leave.has_value() || hero_area_leave->message.recog != 0) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(make_move_command(31, 2, 11, 9)));
  const auto rival_leave_safe = runtime.tick();
  const auto rival_area_leave = find_packet(rival_leave_safe, mir2::kSmAreaState);
  if (!rival_area_leave.has_value() || rival_area_leave->message.recog != 0) {
    return fail(5);
  }

  const auto hero_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(hero_map->message.recog));
  const auto rival_actor_id = hero_actor_id + 1;

  static_cast<void>(runtime.route_logic_command(make_attack_command(30, 0, 11, 9, rival_actor_id)));
  const auto blocked_pk = runtime.tick();
  const auto notice = find_packet(blocked_pk, mir2::kSmHear);
  if (find_packet(blocked_pk, mir2::kSmStruck).has_value() ||
      find_packet(blocked_pk, mir2::kSmDeath).has_value()) {
    return fail(6);
  }
  if (!notice.has_value() ||
      mir2::legacy_decode_string(notice->body).find("PK") == std::string::npos) {
    return fail(7);
  }

  return 0;
}
