#include "game/game_state.hpp"

#include <cassert>

int main() {
  using namespace mir2::client;

  WorldViewState world;
  ActorState self;
  self.actor_id = 1;
  world.latest_hit_ms = 1000;

  assert(legacy_next_hit_delay_ms(0, 0, false) == 1400);
  assert(legacy_next_hit_delay_ms(1, 0, false) == 1386);
  assert(legacy_next_hit_delay_ms(30, 0, false) == 1030);
  assert(legacy_next_hit_delay_ms(40, 5, false) == 730);
  assert(legacy_next_hit_delay_ms(30, 10, false) == 600);
  assert(legacy_next_hit_delay_ms(40, 99, false) == 600);
  assert(legacy_next_hit_delay_ms(1, -1, false) == 1446);
  assert(legacy_next_hit_delay_ms(0, 0, true) == 2900);

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

  world.self_ability_detail.level = 1;
  world.self_ability_detail.speed = -1;
  assert(!can_next_hit(world, self, 2446));
  assert(can_next_hit(world, self, 2447));

  world.self_ability_detail.level = 40;
  world.self_ability_detail.speed = 99;
  assert(!can_next_hit(world, self, 1600));
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
