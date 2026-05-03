#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::vector<mir2::DecodedLegacyGamePacket> find_packets(const mir2::RuntimeDispatch& dispatch,
                                                        std::uint16_t ident) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  const auto packets = find_packets(dispatch, ident);
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

}  // namespace

int main() {
  auto fail = [](int stage) { return stage; };

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "DebuffMap", {}, 0, 0, 10, 10});
  config.runtime.legacy_random_seed = 11;
  config.spawns.push_back(
      mir2::SpawnConfig{"0", "monster", "Venom Target", 10, 9, 30000, 1, 11, 3, 0, 0, 20});
  config.magics.push_back(
      mir2::MagicConfig{4, "Poison Mist", 5, 1, 0, false, true, 0, 0, false, 3, 60, 20, 100});

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
  hero.ability.mc = mir2::make_word(0, 2);
  hero.ability.hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.exp = 40;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 4;
  hero.magics[0].level = 1;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 50;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  const auto new_map = find_packet(login_dispatch, mir2::kSmNewMap);
  if (!new_map.has_value()) {
    return fail(1);
  }
  const auto player_actor_id =
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(new_map->message.recog));

  static_cast<void>(runtime.route_logic_command(make_spell_command(50, 1, 0, 10, 9, 4)));
  const auto spell_dispatch = runtime.tick();
  const auto initial_struck = find_packet(spell_dispatch, mir2::kSmStruck);
  const auto hpmp = find_packet(spell_dispatch, mir2::kSmHealthSpellChanged);
  if (!initial_struck.has_value() || !hpmp.has_value() ||
      find_packet(spell_dispatch, mir2::kSmWalk).has_value() ||
      find_packet(spell_dispatch, mir2::kSmHit).has_value()) {
    return fail(2);
  }
  if (initial_struck->message.recog != 1 || initial_struck->message.param != 9 ||
      initial_struck->message.tag != 11 || initial_struck->message.series != 2 ||
      hpmp->message.recog != static_cast<std::int32_t>(player_actor_id) ||
      hpmp->message.param != 15 || hpmp->message.tag != 5 || hpmp->message.series != 15) {
    return fail(3);
  }

  const auto dot_dispatch_1 = runtime.tick();
  const auto dot_dispatch_2 = runtime.tick();
  const auto dot_dispatch_3 = runtime.tick();

  const auto dot_struck_1 = find_packet(dot_dispatch_1, mir2::kSmStruck);
  const auto dot_struck_2 = find_packet(dot_dispatch_2, mir2::kSmStruck);
  const auto dot_death = find_packet(dot_dispatch_3, mir2::kSmDeath);
  const auto dot_win_exp = find_packet(dot_dispatch_3, mir2::kSmWinExp);
  if (!dot_struck_1.has_value() || !dot_struck_2.has_value() || !dot_death.has_value() ||
      !dot_win_exp.has_value()) {
    return fail(4);
  }

  if (dot_struck_1->message.recog != 1 || dot_struck_1->message.param != 6 ||
      dot_struck_1->message.tag != 11 || dot_struck_1->message.series != 3 ||
      dot_struck_2->message.recog != 1 || dot_struck_2->message.param != 3 ||
      dot_struck_2->message.tag != 11 || dot_struck_2->message.series != 3 ||
      dot_death->message.recog != 1 || dot_death->message.param != 10 ||
      dot_death->message.tag != 9 || dot_win_exp->message.recog != 60 ||
      dot_win_exp->message.param != 20) {
    return fail(5);
  }

  if (find_packet(dot_dispatch_1, mir2::kSmWalk).has_value() ||
      find_packet(dot_dispatch_2, mir2::kSmWalk).has_value() ||
      find_packet(dot_dispatch_3, mir2::kSmWalk).has_value() ||
      find_packet(dot_dispatch_1, mir2::kSmHit).has_value() ||
      find_packet(dot_dispatch_2, mir2::kSmHit).has_value() ||
      find_packet(dot_dispatch_3, mir2::kSmHit).has_value()) {
    return fail(6);
  }

  return 0;
}
