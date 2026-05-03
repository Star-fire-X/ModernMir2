#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
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
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "AoeMap", {}, 0, 0, 10, 10});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Scarecrow A", 10, 9, 30000, 1, 7, 3, 0, 0, 15});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Scarecrow B", 11, 9, 30000, 1, 7, 3, 0, 0, 15});
  config.spawns.push_back(mir2::SpawnConfig{"0", "monster", "Scarecrow C", 13, 13, 30000, 1, 7, 3, 0, 0, 15});
  config.magics.push_back(mir2::MagicConfig{3, "Meteor", 5, 6, 1, false, true});

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
  hero.ability.mc = mir2::make_word(2, 2);
  hero.ability.hp = 15;
  hero.ability.mp = 10;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.exp = 40;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  hero.magics[0].magic_id = 3;
  hero.magics[0].level = 1;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 40;
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

  static_cast<void>(runtime.route_logic_command(make_spell_command(40, 1, 0, 10, 9, 3)));
  const auto spell_dispatch = runtime.tick();

  const auto deaths = find_packets(spell_dispatch, mir2::kSmDeath);
  const auto win_exp_packets = find_packets(spell_dispatch, mir2::kSmWinExp);
  const auto hpmp_packet = find_packet(spell_dispatch, mir2::kSmHealthSpellChanged);
  const auto level_up = find_packet(spell_dispatch, mir2::kSmLevelUp);
  if (deaths.size() != 2 || win_exp_packets.size() != 2 || !hpmp_packet.has_value() ||
      level_up.has_value()) {
    return fail(2);
  }

  std::vector<std::int32_t> death_ids;
  for (const auto& packet : deaths) {
    death_ids.push_back(packet.message.recog);
  }
  std::sort(death_ids.begin(), death_ids.end());
  if (death_ids != std::vector<std::int32_t>{1, 2}) {
    return fail(3);
  }

  std::vector<std::int32_t> exp_values;
  for (const auto& packet : win_exp_packets) {
    if (packet.message.param != 15) {
      return fail(4);
    }
    exp_values.push_back(packet.message.recog);
  }
  std::sort(exp_values.begin(), exp_values.end());
  if (exp_values != std::vector<std::int32_t>{55, 70}) {
    return fail(5);
  }

  if (hpmp_packet->message.recog != static_cast<std::int32_t>(player_actor_id) ||
      hpmp_packet->message.param != 15 || hpmp_packet->message.tag != 5 ||
      hpmp_packet->message.series != 15) {
    return fail(6);
  }

  return 0;
}
