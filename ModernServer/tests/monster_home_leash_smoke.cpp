#include <cassert>
#include <cstdint>
#include <optional>
#include <unordered_map>

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

mir2::CharacterRecord make_hero() {
  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 25;
  hero.y = 26;
  hero.ability.level = 1;
  hero.ability.hp = 40;
  hero.ability.max_hp = 40;
  hero.ability.mp = 10;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 100;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;
  return hero;
}

}  // namespace

int main() {
  mir2::MapConfig map{"0", "LeashMap", {}, 0, 0, 40, 40};
  mir2::LogicBudgetConfig budgets;
  mir2::MapActor actor(map, budgets, {}, {});

  mir2::ActorMail monster;
  monster.kind = mir2::ActorMailKind::spawn_monster;
  monster.map_id = "0";
  monster.actor_id = 1;
  monster.name = "Oma";
  monster.x = 25;
  monster.y = 25;
  monster.max_hp = 20;
  monster.attack_power = 4;
  monster.home_x = 10;
  monster.home_y = 10;
  monster.home_area = 3;
  monster.legacy_spawn_group = true;
  monster.monster_ai_profile = mir2::MonsterAiProfile::aggressive;
  actor.enqueue_mail(monster);
  static_cast<void>(actor.tick(1, 20));

  mir2::ActorMail player;
  player.kind = mir2::ActorMailKind::spawn_player;
  player.map_id = "0";
  player.actor_id = 2;
  player.session_id = 7;
  player.character = make_hero();
  static_cast<void>(actor.legacy_spawn_player(player, 2, 40, true));

  const auto dispatch = actor.legacy_process_monster(1, 3, 251, 0, 0);
  const auto walk = find_packet(dispatch, mir2::kSmWalk);
  assert(walk.has_value());
  assert(walk->message.recog == 1);
  assert(walk->message.param == 24);
  assert(walk->message.tag == 24);
  assert(!find_packet(dispatch, mir2::kSmHit).has_value());
  assert(!find_packet(dispatch, mir2::kSmStruck).has_value());
  const auto snapshot = actor.legacy_monster_snapshot(1);
  assert(snapshot.has_value());
  assert(snapshot->target_actor_id == 0);
  assert(snapshot->target_x == 10);
  assert(snapshot->target_y == 10);
  assert(snapshot->walk_time_ms == 251);

  return 0;
}
