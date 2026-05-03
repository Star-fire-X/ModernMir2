#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

std::vector<mir2::DecodedLegacyGamePacket> packets_by_body(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, const std::string& body) {
  std::vector<mir2::DecodedLegacyGamePacket> packets;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body) == body) {
      packets.push_back(*decoded);
    }
  }
  return packets;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return true;
    }
  }
  return false;
}

mir2::ActorMail make_monster(std::uint64_t actor_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = 10;
  mail.y = 9;
  mail.max_hp = 1;
  mail.attack_power = 1;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.accuracy = 20;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_drop_gold = 4500;
  return mail;
}

mir2::ActorMail make_player(std::uint64_t actor_id, std::uint64_t session_id) {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 1;
  hero.ability.dc = mir2::make_word(40, 40);
  hero.ability.hp = 50;
  hero.ability.max_hp = 50;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.character = hero;
  mail.x = hero.x;
  mail.y = hero.y;
  return mail;
}

mir2::ActorMail make_attack(std::uint64_t player_id, std::uint64_t session_id,
                            std::uint64_t monster_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.actor_id = player_id;
  mail.session_id = session_id;
  mail.target_actor_id = monster_id;
  mail.x = 10;
  mail.y = 9;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

}  // namespace

int main() {
  constexpr std::uint64_t monster_id = 300;
  constexpr std::uint64_t player_id = 1;
  constexpr std::uint64_t session_id = 10;

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapActor map(mir2::MapConfig{"0", "GoldSplit", {}, 0, 0, 20, 20}, budgets, {}, {});
  map.enqueue_mail(make_monster(monster_id));
  static_cast<void>(map.tick(1, 0));
  static_cast<void>(map.legacy_spawn_player(make_player(player_id, session_id), 1, 0, true));

  assert(map.enqueue_legacy_player_command(make_attack(player_id, session_id, monster_id), 20));
  const auto kill = map.legacy_process_player(player_id, 2, 20, false);
  assert(has_packet(kill, mir2::kSmDeath));

  auto gold = packets_by_body(kill, mir2::kSmItemShow, "Gold");
  assert(gold.size() == 3);
  std::sort(gold.begin(), gold.end(), [](const auto& left, const auto& right) {
    if (left.message.tag != right.message.tag) {
      return left.message.tag < right.message.tag;
    }
    return left.message.param < right.message.param;
  });
  assert(gold[0].message.param == 9 && gold[0].message.tag == 8);
  assert(gold[1].message.param == 10 && gold[1].message.tag == 8);
  assert(gold[2].message.param == 11 && gold[2].message.tag == 8);

  const auto gold_2000_count = std::count_if(gold.begin(), gold.end(), [](const auto& packet) {
    return packet.message.series == 116;
  });
  const auto gold_500_count = std::count_if(gold.begin(), gold.end(), [](const auto& packet) {
    return packet.message.series == 115;
  });
  assert(gold_2000_count == 2);
  assert(gold_500_count == 1);

  return 0;
}
