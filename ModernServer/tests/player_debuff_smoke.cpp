#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::vector<mir2::DecodedLegacyGamePacket> find_packets(const mir2::RuntimeDispatch& dispatch,
                                                        std::uint16_t ident,
                                                        std::uint64_t session_id) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident,
                                                         std::uint64_t session_id) {
  const auto packets = find_packets(dispatch, ident, session_id);
  if (packets.empty()) {
    return std::nullopt;
  }
  return packets.front();
}

mir2::LogicCommand make_spell_command(std::uint64_t session_id, std::uint64_t target_actor_id,
                                      std::uint8_t dir, std::int32_t x, std::int32_t y,
                                      std::int32_t magic_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::spell;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmSpell;
  command.game_message.tag = static_cast<std::uint16_t>(magic_id);
  return command;
}

mir2::LogicCommand make_walk_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
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

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  mir2::MapConfig map{"0", "PlayerDebuffMap", {}, 0, 0, 10, 10};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  config.magics.push_back(
      mir2::MagicConfig{6, "Purify", 4, 0, 0, true, false, 0, 0, true, 0, 0, 0});
  config.magics.push_back(
      mir2::MagicConfig{7, "Crippling Hex", 5, 1, 0, true, false, 0, 0, false, 2, 60, 20, 100});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.dir = 2;
  hero.ability.hp = 20;
  hero.ability.level = 10;
  hero.ability.max_hp = 20;
  hero.ability.mp = 20;
  hero.ability.max_mp = 20;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 7;
  hero.magics[1].magic_id = 6;
  hero.attack_mode = 0;

  mir2::LogicCommand hero_enter;
  hero_enter.kind = mir2::LogicCommandKind::enter_world;
  hero_enter.session_id = 70;
  hero_enter.account_id = "guest";
  hero_enter.character_name = "Hero";
  hero_enter.map_id = "0";
  hero_enter.x = 10;
  hero_enter.y = 10;
  hero_enter.character = hero;
  static_cast<void>(runtime.route_logic_command(hero_enter));

  const auto hero_login = runtime.tick();
  const auto hero_map = find_packet(hero_login, mir2::kSmNewMap, 70);
  if (!hero_map.has_value()) {
    return fail(1);
  }

  mir2::CharacterRecord rival = hero;
  rival.account_id = "guest2";
  rival.character_name = "Rival";
  rival.x = 11;
  rival.y = 10;
  rival.ability.mp = 10;
  rival.ability.max_mp = 10;
  rival.magics[0] = {};
  rival.magics[1] = {};
  rival.attack_mode = 0;

  mir2::LogicCommand rival_enter;
  rival_enter.kind = mir2::LogicCommandKind::enter_world;
  rival_enter.session_id = 71;
  rival_enter.account_id = "guest2";
  rival_enter.character_name = "Rival";
  rival_enter.map_id = "0";
  rival_enter.x = 11;
  rival_enter.y = 10;
  rival_enter.character = rival;
  static_cast<void>(runtime.route_logic_command(rival_enter));

  const auto rival_login = runtime.tick();
  const auto rival_map = find_packet(rival_login, mir2::kSmNewMap, 71);
  if (!rival_map.has_value()) {
    return fail(2);
  }
  const auto rival_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(rival_map->message.recog));

  static_cast<void>(runtime.route_logic_command(make_spell_command(70, rival_actor_id, 2, 11, 10, 7)));
  const auto hex_dispatch = runtime.tick();
  const auto rival_struck = find_packet(hex_dispatch, mir2::kSmStruck, 71);
  const auto rival_hpmp = find_packet(hex_dispatch, mir2::kSmHealthSpellChanged, 71);
  if (!rival_struck.has_value() || !rival_hpmp.has_value()) {
    return fail(3);
  }
  if (rival_struck->message.recog != static_cast<std::int32_t>(rival_actor_id) ||
      rival_struck->message.param != 19 || rival_struck->message.tag != 20 ||
      rival_struck->message.series != 1 || rival_hpmp->message.param != 19) {
    return fail(4);
  }

  static_cast<void>(runtime.route_logic_command(make_walk_command(71, 2, 12, 10)));
  const auto first_move_dispatch = runtime.tick();
  const auto hero_first_walk = find_packet(first_move_dispatch, mir2::kSmWalk, 70);
  const auto rival_dot_1 = find_packet(first_move_dispatch, mir2::kSmStruck, 71);
  if (!hero_first_walk.has_value() || !rival_dot_1.has_value()) {
    return fail(5);
  }
  if (hero_first_walk->message.recog != static_cast<std::int32_t>(rival_actor_id) ||
      hero_first_walk->message.param != 12 || hero_first_walk->message.tag != 10 ||
      rival_dot_1->message.param != 17 || rival_dot_1->message.tag != 20 ||
      rival_dot_1->message.series != 2) {
    return fail(6);
  }

  static_cast<void>(runtime.route_logic_command(make_walk_command(71, 2, 13, 10)));
  const auto second_move_dispatch = runtime.tick();
  const auto rival_move_fail = find_packet(second_move_dispatch, mir2::kSmMoveFail, 71);
  const auto rival_dot_2 = find_packet(second_move_dispatch, mir2::kSmStruck, 71);
  if (!rival_move_fail.has_value() || !rival_dot_2.has_value()) {
    return fail(7);
  }
  if (find_packet(second_move_dispatch, mir2::kSmWalk, 70).has_value() ||
      rival_dot_2->message.param != 15 || rival_dot_2->message.tag != 20 ||
      rival_dot_2->message.series != 2) {
    return fail(8);
  }

  static_cast<void>(runtime.route_logic_command(make_spell_command(70, rival_actor_id, 2, 12, 10, 6)));
  const auto purify_dispatch = runtime.tick();
  const auto purify_target_hpmp = find_packet(purify_dispatch, mir2::kSmHealthSpellChanged, 71);
  if (!purify_target_hpmp.has_value()) {
    return fail(9);
  }
  if (purify_target_hpmp->message.param != 15 ||
      find_packet(purify_dispatch, mir2::kSmStruck, 71).has_value()) {
    return fail(10);
  }

  static_cast<void>(runtime.route_logic_command(make_walk_command(71, 2, 13, 10)));
  const auto third_move_dispatch = runtime.tick();
  const auto hero_third_walk = find_packet(third_move_dispatch, mir2::kSmWalk, 70);
  if (!hero_third_walk.has_value()) {
    return fail(11);
  }
  if (hero_third_walk->message.recog != static_cast<std::int32_t>(rival_actor_id) ||
      hero_third_walk->message.param != 13 || hero_third_walk->message.tag != 10 ||
      find_packet(third_move_dispatch, mir2::kSmMoveFail, 71).has_value() ||
      find_packet(third_move_dispatch, mir2::kSmStruck, 71).has_value()) {
    return fail(12);
  }

  const auto settle_dispatch = runtime.tick();
  if (find_packet(settle_dispatch, mir2::kSmStruck, 71).has_value()) {
    return fail(13);
  }

  return 0;
}
