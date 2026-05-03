#include <algorithm>
#include <cassert>

#include "config/models.hpp"
#include "world/legacy_item_rules.hpp"

namespace {

bool any_desc(const mir2::LegacyUserItem& item) {
  return std::any_of(item.desc.begin(), item.desc.end(), [](std::uint8_t value) {
    return value != 0;
  });
}

}  // namespace

int main() {
  mir2::ItemConfig weapon;
  weapon.id = 1;
  weapon.name = "Blade";
  weapon.std_mode = 5;
  weapon.dura_max = 1000;

  mir2::LegacyUserItem weapon_drop;
  weapon_drop.index = 1;
  weapon_drop.make_index = 7001;
  weapon_drop.dura = 1000;
  weapon_drop.dura_max = 1000;
  mir2::LegacyRandom weapon_random(1);
  mir2::legacy_random_upgrade_monster_drop_item(weapon, weapon_drop, weapon_random);
  assert(!any_desc(weapon_drop));
  assert(weapon_drop.dura == 5000);
  assert(weapon_drop.dura_max == 5000);

  mir2::ItemConfig ring_of_unknown;
  ring_of_unknown.id = 2;
  ring_of_unknown.name = "RingOfUnknown";
  ring_of_unknown.std_mode = 22;
  ring_of_unknown.shape = 130;
  ring_of_unknown.dura_max = 1000;

  mir2::LegacyUserItem unknown_drop;
  unknown_drop.index = 2;
  unknown_drop.make_index = 7002;
  unknown_drop.dura = 1000;
  unknown_drop.dura_max = 1000;
  mir2::LegacyRandom unknown_random(1);
  mir2::legacy_random_set_unknown_monster_drop_item(ring_of_unknown, unknown_drop,
                                                    unknown_random);
  assert(unknown_drop.desc[8] == 1);
  assert(any_desc(unknown_drop));

  return 0;
}
