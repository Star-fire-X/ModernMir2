#include <iostream>

#include "world/game_object.hpp"

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ":"       \
                << __LINE__ << "\n";                                          \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main() {
  mir2::Monster hitter(1, "Oma", "0", 0, 0, 1, 10, 1, 1, 1, 0, 0, 0, 0, 1);
  hitter.record_legacy_hitter(100, 1000);
  hitter.record_legacy_hitter(200, 2000);
  CHECK(hitter.last_hitter_id() == 200);
  CHECK(hitter.exp_hitter_id() == 100);
  hitter.record_legacy_hitter(100, 3000);
  CHECK(hitter.exp_hitter_id() == 100);
  CHECK(hitter.exp_hit_time_ms() == 3000);
  hitter.expire_legacy_hitters(9001);
  CHECK(hitter.exp_hitter_id() == 0);
  CHECK(hitter.last_hitter_id() == 100);

  mir2::Monster slave(2, "OmaSlave", "0", 0, 0, 1, 100, 10, 2, 10, 0, 5, 0, 0,
                      1, 0, 0, 0, 0, 0, 0, 0, 15, 20, 1, 0, 100,
                      mir2::MonsterAiProfile::aggressive, 0, 0, 0, 0, true,
                      900, true, 114, 1, 0, 10 * 1000, 0, true);
  CHECK(slave.is_slave());
  CHECK(slave.accuracy_point() == 15);
  CHECK(slave.slave_exp_level() == 0);
  CHECK(!slave.gain_slave_exp(1));
  CHECK(slave.slave_exp_level() == 0);
  CHECK(slave.slave_exp() == 115);
  CHECK(slave.gain_slave_exp(1));
  CHECK(slave.slave_exp_level() == 1);
  CHECK(slave.slave_exp() == 1);
  CHECK(slave.max_hp() == 115);
  CHECK(slave.dc_max() == 12);
  CHECK(slave.accuracy_point() == 15);

  CHECK(slave.gain_slave_exp(10000));
  CHECK(slave.slave_exp_level() == 2);
  CHECK(slave.gain_slave_exp(10000));
  CHECK(slave.slave_exp_level() == 3);
  CHECK(!slave.gain_slave_exp(10000));
  CHECK(slave.slave_exp_level() == 3);

  mir2::Monster skeleton(3, "__WhiteSkeleton", "0", 0, 0, 1, 100, 10, 2, 10, 0,
                         5, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 20, 1, 0, 100,
                         mir2::MonsterAiProfile::aggressive, 0, 0, 0, 0, true,
                         900, true, 0, 1, 1, 10 * 1000, 0, true);
  CHECK(skeleton.max_hp() == 140);
  CHECK(skeleton.dc_max() == 11);
  CHECK(skeleton.accuracy_point() == 15);

  return 0;
}
