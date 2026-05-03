#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
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

bool has_notice(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                const std::string& needle) {
  for (const auto& packet : find_packets(dispatch, mir2::kSmHear, session_id)) {
    if (mir2::legacy_decode_string(packet.body).find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
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

mir2::LogicCommand make_hit_command(std::uint64_t session_id, std::uint8_t dir, std::int32_t x,
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
  mir2::MapConfig map{"0", "ShieldMap", {}, 0, 0, 10, 10};
  map.allow_pk = true;
  map.fight_zone = true;
  config.maps.push_back(map);
  config.magics.push_back(
      mir2::MagicConfig{7, "Crippling Hex", 5, 1, 0, true, false, 0, 0, false, 2, 60, 20, 100, 0});
  config.magics.push_back(
      mir2::MagicConfig{8, "Holy Shield", 4, 0, 0, true, false, 0, 0, false, 0, 80, 20, 0, 8});

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
  hero.magics[0].magic_id = 8;
  hero.attack_mode = 0;

  mir2::LogicCommand hero_enter;
  hero_enter.kind = mir2::LogicCommandKind::enter_world;
  hero_enter.session_id = 80;
  hero_enter.account_id = "guest";
  hero_enter.character_name = "Hero";
  hero_enter.map_id = "0";
  hero_enter.x = 10;
  hero_enter.y = 10;
  hero_enter.character = hero;
  static_cast<void>(runtime.route_logic_command(hero_enter));

  const auto hero_login = runtime.tick();
  const auto hero_map = find_packet(hero_login, mir2::kSmNewMap, 80);
  if (!hero_map.has_value()) {
    return fail(1);
  }
  const auto hero_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(hero_map->message.recog));

  mir2::CharacterRecord rival = hero;
  rival.account_id = "guest2";
  rival.character_name = "Rival";
  rival.x = 11;
  rival.y = 10;
  rival.ability.dc = mir2::make_word(5, 5);
  rival.ability.mp = 10;
  rival.ability.max_mp = 10;
  rival.magics[0].magic_id = 7;
  rival.attack_mode = 0;

  mir2::LogicCommand rival_enter;
  rival_enter.kind = mir2::LogicCommandKind::enter_world;
  rival_enter.session_id = 81;
  rival_enter.account_id = "guest2";
  rival_enter.character_name = "Rival";
  rival_enter.map_id = "0";
  rival_enter.x = 11;
  rival_enter.y = 10;
  rival_enter.character = rival;
  static_cast<void>(runtime.route_logic_command(rival_enter));

  const auto rival_login = runtime.tick();
  const auto rival_map = find_packet(rival_login, mir2::kSmNewMap, 81);
  if (!rival_map.has_value()) {
    return fail(2);
  }

  static_cast<void>(runtime.route_logic_command(make_spell_command(80, hero_actor_id, 2, 10, 10, 8)));
  const auto shield_dispatch = runtime.tick();
  const auto hero_shield_hpmp = find_packet(shield_dispatch, mir2::kSmHealthSpellChanged, 80);
  if (!hero_shield_hpmp.has_value()) {
    return fail(3);
  }
  if (hero_shield_hpmp->message.recog != static_cast<std::int32_t>(hero_actor_id) ||
      hero_shield_hpmp->message.param != 20 || hero_shield_hpmp->message.tag != 16 ||
      hero_shield_hpmp->message.series != 20) {
    return fail(4);
  }
  if (!has_notice(shield_dispatch, 80, "Holy Shield surrounds you") ||
      !has_notice(shield_dispatch, 81, "Hero is surrounded by Holy Shield")) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(make_hit_command(81, 6, 10, 10)));
  const auto first_hit_dispatch = runtime.tick();
  const auto hero_absorb_hpmp = find_packet(first_hit_dispatch, mir2::kSmHealthSpellChanged, 80);
  if (!hero_absorb_hpmp.has_value()) {
    return fail(6);
  }
  if (find_packet(first_hit_dispatch, mir2::kSmStruck, 80).has_value() ||
      hero_absorb_hpmp->message.param != 20 || hero_absorb_hpmp->message.tag != 16) {
    return fail(7);
  }

  static_cast<void>(runtime.route_logic_command(make_spell_command(81, hero_actor_id, 6, 10, 10, 7)));
  const auto hex_dispatch = runtime.tick();
  const auto hero_hex_hpmp = find_packet(hex_dispatch, mir2::kSmHealthSpellChanged, 80);
  if (!hero_hex_hpmp.has_value()) {
    return fail(8);
  }
  if (find_packet(hex_dispatch, mir2::kSmStruck, 80).has_value() ||
      hero_hex_hpmp->message.param != 20 || hero_hex_hpmp->message.tag != 16) {
    return fail(9);
  }

  const auto dot_1 = runtime.tick();
  const auto hero_dot_absorb = find_packet(dot_1, mir2::kSmHealthSpellChanged, 80);
  if (!hero_dot_absorb.has_value()) {
    return fail(10);
  }
  if (find_packet(dot_1, mir2::kSmStruck, 80).has_value() ||
      hero_dot_absorb->message.param != 20 || hero_dot_absorb->message.tag != 16) {
    return fail(11);
  }
  if (!has_notice(dot_1, 80, "Your Holy Shield shatters") ||
      !has_notice(dot_1, 81, "Hero's Holy Shield shatters")) {
    return fail(12);
  }

  const auto dot_2 = runtime.tick();
  const auto hero_dot_struck = find_packet(dot_2, mir2::kSmStruck, 80);
  if (!hero_dot_struck.has_value()) {
    return fail(13);
  }
  if (hero_dot_struck->message.recog != static_cast<std::int32_t>(hero_actor_id) ||
      hero_dot_struck->message.param != 18 || hero_dot_struck->message.tag != 20 ||
      hero_dot_struck->message.series != 2) {
    return fail(14);
  }

  const auto dot_3 = runtime.tick();
  const auto hero_dot_struck_2 = find_packet(dot_3, mir2::kSmStruck, 80);
  if (!hero_dot_struck_2.has_value()) {
    return fail(15);
  }
  if (hero_dot_struck_2->message.param != 16 || hero_dot_struck_2->message.tag != 20 ||
      hero_dot_struck_2->message.series != 2) {
    return fail(16);
  }

  static_cast<void>(runtime.route_logic_command(make_spell_command(80, hero_actor_id, 2, 10, 10, 8)));
  const auto shield_again_dispatch = runtime.tick();
  if (!has_notice(shield_again_dispatch, 80, "Holy Shield surrounds you") ||
      !has_notice(shield_again_dispatch, 81, "Hero is surrounded by Holy Shield")) {
    return fail(17);
  }

  bool saw_fade_self = false;
  bool saw_fade_watcher = false;
  for (int step = 0; step < 6; ++step) {
    const auto fade_dispatch = runtime.tick();
    saw_fade_self = saw_fade_self || has_notice(fade_dispatch, 80, "Your Holy Shield fades");
    saw_fade_watcher =
        saw_fade_watcher || has_notice(fade_dispatch, 81, "Hero's Holy Shield fades");
  }
  if (!saw_fade_self || !saw_fade_watcher) {
    return fail(18);
  }

  return 0;
}
