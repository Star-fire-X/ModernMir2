#include <cassert>

#include "world/game_object.hpp"

int main() {
  mir2::Monster hitter(1, "Oma", "0", 0, 0, 1, 10, 1, 1, 1, 0, 0, 0, 0, 1);
  hitter.record_legacy_hitter(100, 1000);
  hitter.record_legacy_hitter(200, 2000);
  assert(hitter.last_hitter_id() == 200);
  assert(hitter.exp_hitter_id() == 100);
  hitter.record_legacy_hitter(100, 3000);
  assert(hitter.exp_hitter_id() == 100);
  assert(hitter.exp_hit_time_ms() == 3000);
  hitter.expire_legacy_hitters(9001);
  assert(hitter.exp_hitter_id() == 0);
  assert(hitter.last_hitter_id() == 200);

  mir2::Monster slave(2, "OmaSlave", "0", 0, 0, 1, 100, 10, 2, 10, 0, 5, 0, 0,
                      1, 0, 0, 0, 0, 0, 0, 0, 15, 20, 1, 0, 100,
                      mir2::MonsterAiProfile::aggressive, 0, 0, 0, 0, true,
                      900, true, 114, 1, 0, 10 * 1000, 0, true);
  assert(slave.is_slave());
  assert(slave.accuracy_point() == 15);
  assert(slave.slave_exp_level() == 0);
  assert(!slave.gain_slave_exp(1));
  assert(slave.slave_exp_level() == 0);
  assert(slave.slave_exp() == 115);
  assert(slave.gain_slave_exp(1));
  assert(slave.slave_exp_level() == 1);
  assert(slave.slave_exp() == 1);
  assert(slave.max_hp() == 115);
  assert(slave.dc_max() == 12);
  assert(slave.accuracy_point() == 15);

  assert(slave.gain_slave_exp(10000));
  assert(slave.slave_exp_level() == 2);
  assert(slave.gain_slave_exp(10000));
  assert(slave.slave_exp_level() == 3);
  assert(!slave.gain_slave_exp(10000));
  assert(slave.slave_exp_level() == 3);

  mir2::Monster skeleton(3, "__WhiteSkeleton", "0", 0, 0, 1, 100, 10, 2, 10, 0,
                         5, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 20, 1, 0, 100,
                         mir2::MonsterAiProfile::aggressive, 0, 0, 0, 0, true,
                         900, true, 0, 1, 1, 10 * 1000, 0, true);
  assert(skeleton.max_hp() == 140);
  assert(skeleton.dc_max() == 11);
  assert(skeleton.accuracy_point() == 15);

  return 0;
}
