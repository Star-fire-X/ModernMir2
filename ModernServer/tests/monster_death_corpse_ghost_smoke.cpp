#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/map_actor.hpp"

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

std::optional<mir2::DecodedLegacyGamePacket> find_packet_by_body(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident, const std::string& body) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident &&
        mir2::legacy_decode_string(decoded->body) == body) {
      return decoded;
    }
  }
  return std::nullopt;
}

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

bool has_process_monster_action(const mir2::RuntimeDispatch& dispatch,
                                std::uint64_t actor_id,
                                const std::string& action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessMonsters" && trace.actor_id == actor_id &&
        trace.action == action) {
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
  mail.exp_reward = 7;
  mail.walk_speed_ms = 200;
  mail.attack_speed_ms = 200;
  mail.monster_ai_profile = mir2::MonsterAiProfile::basic;
  mail.monster_drop_gold = 10;
  mail.respawn_ms = 20;
  mail.legacy_spawn_group = false;
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
  hero.ability.dc = mir2::make_word(5, 5);
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
  constexpr std::uint64_t monster_id = 300;
  constexpr std::uint64_t player_id = 1;
  constexpr std::uint64_t session_id = 10;

  mir2::LogicBudgetConfig budgets;
  budgets.tick_ms = 20;
  mir2::MapActor map(mir2::MapConfig{"0", "CorpseGhost", {}, 0, 0, 20, 20},
                     budgets, {}, {});
  map.enqueue_mail(make_monster(monster_id));
  static_cast<void>(map.tick(1, 0));
  static_cast<void>(map.legacy_spawn_player(make_player(player_id, session_id), 1, 0, true));

  const auto attack = make_attack(player_id, session_id, monster_id);
  assert(map.enqueue_legacy_player_command(attack, 20));
  const auto kill_dispatch = map.legacy_process_player(player_id, 2, 20, false);
  assert(find_packet(kill_dispatch, mir2::kSmDeath).has_value());
  assert(find_packet(kill_dispatch, mir2::kSmWinExp).has_value());
  assert(find_packet_by_body(kill_dispatch, mir2::kSmItemShow, "Gold").has_value());

  auto corpse = map.legacy_monster_snapshot(monster_id);
  assert(corpse.has_value());
  assert(corpse->hp == 0);
  assert(corpse->death_time_ms == 20);
  assert(corpse->death_settled);
  assert(!corpse->ghosted);
  assert(map.legacy_can_spawn_monster(10, 9));

  const auto wait_dispatch = map.legacy_process_monster(monster_id, 3, 1000, 0, 0);
  assert(has_process_monster_action(wait_dispatch, monster_id, "death_wait"));
  assert(map.legacy_monster_snapshot(monster_id).has_value());

  const auto ghost_dispatch = map.legacy_process_monster(monster_id, 4, 180022, 0, 0);
  assert(has_process_monster_action(ghost_dispatch, monster_id, "ghost"));
  assert(find_packet_by_recog(ghost_dispatch, mir2::kSmDisappear,
                              static_cast<std::int32_t>(monster_id)).has_value());
  assert(!map.legacy_monster_snapshot(monster_id).has_value());

  const auto respawn_dispatch = map.tick(5, 180040);
  assert(find_packet_by_recog(respawn_dispatch, mir2::kSmTurn,
                              static_cast<std::int32_t>(monster_id)).has_value());
  const auto respawn = map.legacy_monster_snapshot(monster_id);
  assert(respawn.has_value());
  assert(respawn->hp == 1);

  return 0;
}
