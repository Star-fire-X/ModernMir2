#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "world/map_actor.hpp"

namespace {

mir2::ActorMail make_spawn_player(std::int32_t x, std::int32_t y) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_player;
  mail.actor_id = 1;
  mail.session_id = 1;
  mail.map_id = "0";
  mail.x = x;
  mail.y = y;
  mail.character.account_id = "acct";
  mail.character.character_name = "Hero";
  mail.character.map_id = "0";
  mail.character.x = x;
  mail.character.y = y;
  mail.character.ability.hp = 20;
  mail.character.ability.max_hp = 20;
  return mail;
}

mir2::ActorMail make_spawn_castle_door(std::uint64_t actor_id = 100,
                                       std::int32_t x = 10,
                                       std::int32_t y = 10) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::spawn_monster;
  mail.actor_id = actor_id;
  mail.map_id = "0";
  mail.name = "CastleDoor";
  mail.x = x;
  mail.y = y;
  mail.level = 1;
  mail.max_hp = 100;
  mail.attack_power = 1;
  mail.dc_min = 1;
  mail.dc_max = 1;
  mail.race_server = 110;
  return mail;
}

mir2::ActorMail make_move(std::int32_t x, std::int32_t y) {
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::move;
  mail.actor_id = 1;
  mail.session_id = 1;
  mail.map_id = "0";
  mail.x = x;
  mail.y = y;
  return mail;
}

}  // namespace

int main() {
  mir2::MapActor map(mir2::MapConfig{"0", "CastleDoorOverlay", {}, 20, 20, 0, 0},
                     mir2::LogicBudgetConfig{}, {}, {});
  mir2::LegacyRandom random(1);
  map.set_legacy_random(&random);

  map.enqueue_mail(make_spawn_castle_door());
  static_cast<void>(map.tick(1, 0));
  static_cast<void>(map.legacy_spawn_player(make_spawn_player(9, 10), 1, 0, true));

  map.enqueue_mail(make_move(9, 11));
  static_cast<void>(map.tick(2, 20));
  auto hero = map.snapshot_player(1);
  assert(hero.has_value());
  assert(hero->x == 9 && hero->y == 10);

  mir2::ActorMail despawn;
  despawn.kind = mir2::ActorMailKind::despawn;
  despawn.actor_id = 100;
  despawn.map_id = "0";
  map.enqueue_mail(despawn);
  static_cast<void>(map.tick(3, 40));

  map.enqueue_mail(make_move(9, 11));
  static_cast<void>(map.tick(4, 60));
  hero = map.snapshot_player(1);
  assert(hero.has_value());
  assert(hero->x == 9 && hero->y == 11);

  mir2::MapActor overlap_map(
      mir2::MapConfig{"0", "CastleDoorOverlap", {}, 20, 20, 0, 0},
      mir2::LogicBudgetConfig{}, {}, {});
  overlap_map.set_legacy_random(&random);

  overlap_map.enqueue_mail(make_spawn_castle_door(100, 10, 10));
  overlap_map.enqueue_mail(make_spawn_castle_door(101, 11, 10));
  static_cast<void>(overlap_map.tick(1, 0));
  static_cast<void>(overlap_map.legacy_spawn_player(make_spawn_player(8, 11), 1, 0, true));

  overlap_map.enqueue_mail(make_move(9, 10));
  static_cast<void>(overlap_map.tick(2, 20));
  hero = overlap_map.snapshot_player(1);
  assert(hero.has_value());
  assert(hero->x == 8 && hero->y == 11);

  despawn.actor_id = 100;
  overlap_map.enqueue_mail(despawn);
  static_cast<void>(overlap_map.tick(3, 40));

  overlap_map.enqueue_mail(make_move(9, 10));
  static_cast<void>(overlap_map.tick(4, 60));
  hero = overlap_map.snapshot_player(1);
  assert(hero.has_value());
  assert(hero->x == 8 && hero->y == 11);

  despawn.actor_id = 101;
  overlap_map.enqueue_mail(despawn);
  static_cast<void>(overlap_map.tick(5, 80));

  overlap_map.enqueue_mail(make_move(9, 10));
  static_cast<void>(overlap_map.tick(6, 100));
  hero = overlap_map.snapshot_player(1);
  assert(hero.has_value());
  assert(hero->x == 9 && hero->y == 10);
  return 0;
}
