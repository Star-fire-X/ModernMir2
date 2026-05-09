#include <cstdint>
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

mir2::LogicCommand make_turn_command(std::uint64_t session_id, std::uint8_t dir,
                                     std::int32_t x, std::int32_t y) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::turn;
  command.session_id = session_id;
  command.dir = dir;
  command.x = x;
  command.y = y;
  command.game_message.ident = mir2::kCmTurn;
  return command;
}

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  mir2::MapConfig map{"0", "BuffMap", {}, 0, 0, 10, 10};
  map.allow_pk = false;
  config.maps.push_back(map);
  config.magics.push_back(
      mir2::MagicConfig{105, "Regeneration", 4, 0, 0, true, false, 2, 2, false, 0, 1000, 20, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.hp = 15;
  hero.ability.max_hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 105;

  mir2::LogicCommand hero_enter;
  hero_enter.kind = mir2::LogicCommandKind::enter_world;
  hero_enter.session_id = 60;
  hero_enter.account_id = "guest";
  hero_enter.character_name = "Hero";
  hero_enter.map_id = "0";
  hero_enter.x = 10;
  hero_enter.y = 10;
  hero_enter.character = hero;
  static_cast<void>(runtime.route_logic_command(hero_enter));

  const auto hero_login = runtime.tick();
  const auto hero_map = find_packet(hero_login, mir2::kSmNewMap, 60);
  if (!hero_map.has_value()) {
    return fail(1);
  }
  const auto hero_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(hero_map->message.recog));

  mir2::CharacterRecord ally = hero;
  ally.account_id = "guest2";
  ally.character_name = "Ally";
  ally.x = 11;
  ally.y = 10;
  ally.ability.hp = 5;

  mir2::LogicCommand ally_enter;
  ally_enter.kind = mir2::LogicCommandKind::enter_world;
  ally_enter.session_id = 61;
  ally_enter.account_id = "guest2";
  ally_enter.character_name = "Ally";
  ally_enter.map_id = "0";
  ally_enter.x = 11;
  ally_enter.y = 10;
  ally_enter.character = ally;
  static_cast<void>(runtime.route_logic_command(ally_enter));

  const auto ally_login = runtime.tick();
  const auto ally_map = find_packet(ally_login, mir2::kSmNewMap, 61);
  if (!ally_map.has_value()) {
    return fail(2);
  }
  const auto ally_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(ally_map->message.recog));

  static_cast<void>(runtime.route_logic_command(make_spell_command(60, ally_actor_id, 2, 11, 10, 105)));
  const auto buff_dispatch = runtime.tick();
  const auto hero_hpmp = find_packet(buff_dispatch, mir2::kSmHealthSpellChanged, 60);
  const auto ally_hpmp = find_packet(buff_dispatch, mir2::kSmHealthSpellChanged, 61);
  if (!hero_hpmp.has_value() || !ally_hpmp.has_value()) {
    return fail(3);
  }
  if (hero_hpmp->message.recog != static_cast<std::int32_t>(hero_actor_id) ||
      hero_hpmp->message.param != 15 || hero_hpmp->message.tag != 6 ||
      hero_hpmp->message.series != 15) {
    return fail(4);
  }
  if (ally_hpmp->message.recog != static_cast<std::int32_t>(ally_actor_id) ||
      ally_hpmp->message.param != 7 || ally_hpmp->message.tag != 10 ||
      ally_hpmp->message.series != 15) {
    return fail(5);
  }

  std::vector<std::int32_t> ally_hot_values;
  bool saw_hot_struck = false;
  for (int step = 0; step < 12 && ally_hot_values.size() < 3; ++step) {
    static_cast<void>(runtime.route_logic_command(make_turn_command(61, 2, 11, 10)));
    const auto hot_dispatch = runtime.tick();
    if (const auto ally_hot = find_packet(hot_dispatch, mir2::kSmHealthSpellChanged, 61);
        ally_hot.has_value()) {
      ally_hot_values.push_back(ally_hot->message.param);
    }
    saw_hot_struck = saw_hot_struck ||
                     find_packet(hot_dispatch, mir2::kSmStruck, 61).has_value();
  }
  if (ally_hot_values.size() != 3) {
    return fail(6);
  }
  if (ally_hot_values[0] != 9 || ally_hot_values[1] != 11 ||
      ally_hot_values[2] != 13) {
    return fail(7);
  }

  if (find_packet(buff_dispatch, mir2::kSmStruck, 61).has_value() || saw_hot_struck) {
    return fail(8);
  }

  return 0;
}
