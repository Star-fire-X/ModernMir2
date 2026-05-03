#include <cassert>
#include <cstdint>
#include <optional>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

namespace {

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

mir2::ActorMail make_monster(std::uint64_t actor_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.map_id = "0";
  mail.actor_id = actor_id;
  mail.name = "Oma";
  mail.x = 10;
  mail.y = 9;
  mail.max_hp = 20;
  mail.attack_power = 3;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.accuracy = 20;
  mail.exp_reward = 1;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_search_rate_ms = 3000;
  mail.dir = 4;
  mail.legacy_spawn_group = true;
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
  hero.ability.dc = mir2::make_word(1, 1);
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
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

mir2::ActorMail make_attack(std::uint64_t actor_id, std::uint64_t session_id,
                            std::uint64_t target_actor_id) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::attack;
  mail.actor_id = actor_id;
  mail.session_id = session_id;
  mail.target_actor_id = target_actor_id;
  mail.x = 10;
  mail.y = 9;
  mail.game_message.ident = mir2::kCmHit;
  return mail;
}

}  // namespace

int main() {
  constexpr std::uint64_t monster_id = 200;
  constexpr std::uint64_t player_id = 1;
  constexpr std::uint64_t session_id = 10;

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapActor map(mir2::MapConfig{"0", "Retaliation", {}, 0, 0, 20, 20},
                     budgets, {}, {});
  map.enqueue_mail(make_monster(monster_id));
  static_cast<void>(map.tick(1, 0));
  static_cast<void>(map.legacy_spawn_player(make_player(player_id, session_id), 1, 0, true));

  auto before = map.legacy_monster_snapshot(monster_id);
  assert(before.has_value());
  assert(before->target_actor_id == 0);

  const auto attack = make_attack(player_id, session_id, monster_id);
  assert(map.enqueue_legacy_player_command(attack, 20));
  const auto struck_dispatch = map.legacy_process_player(player_id, 2, 20, false);
  assert(find_packet_by_recog(struck_dispatch, mir2::kSmStruck,
                              static_cast<std::int32_t>(monster_id)).has_value());

  const auto after_struck = map.legacy_monster_snapshot(monster_id);
  assert(after_struck.has_value());
  assert(after_struck->last_hitter_id == player_id);
  assert(after_struck->exp_hitter_id == player_id);
  assert(after_struck->target_actor_id == player_id);

  const auto retaliate_dispatch = map.legacy_process_monster(monster_id, 3, 1001, 0, 0);
  assert(find_packet_by_recog(retaliate_dispatch, mir2::kSmHit,
                              static_cast<std::int32_t>(monster_id)).has_value());
  assert(find_packet_by_recog(retaliate_dispatch, mir2::kSmStruck,
                              static_cast<std::int32_t>(player_id)).has_value());

  return 0;
}
