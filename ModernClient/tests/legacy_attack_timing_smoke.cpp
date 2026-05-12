#include "game/game_state.hpp"

#include <cassert>

int main() {
  using namespace mir2::client;

  WorldViewState world;
  ActorState self;
  self.actor_id = 1;
  world.latest_hit_ms = 1000;

  world.self_ability_detail.level = 0;
  world.self_ability_detail.speed = 0;
  world.attack_slow = false;
  assert(!can_next_hit(world, self, 2400));
  assert(can_next_hit(world, self, 2401));

  world.self_ability_detail.level = 30;
  world.self_ability_detail.speed = 0;
  assert(!can_next_hit(world, self, 2029));
  assert(can_next_hit(world, self, 2031));

  world.self_ability_detail.level = 30;
  world.self_ability_detail.speed = 10;
  assert(!can_next_hit(world, self, 1599));
  assert(can_next_hit(world, self, 1601));

  world.self_ability_detail.level = 0;
  world.self_ability_detail.speed = 0;
  world.self_ability_detail.hand_weight = 11;
  world.self_ability_detail.max_hand_weight = 10;
  update_legacy_weight_slow(world);
  assert(world.attack_slow);
  assert(!can_next_hit(world, self, 3900));
  assert(can_next_hit(world, self, 3901));

  world.self_ability_detail.weight = 21;
  world.self_ability_detail.max_weight = 10;
  world.self_ability_detail.wear_weight = 11;
  world.self_ability_detail.max_wear_weight = 10;
  update_legacy_weight_slow(world);
  assert(world.move_slow);
  assert(world.move_slow_level == 3);
  assert(legacy_move_skip_due_to_slow(world));
  assert(legacy_move_skip_due_to_slow(world));
  assert(legacy_move_skip_due_to_slow(world));
  assert(!legacy_move_skip_due_to_slow(world));
  return 0;
}
