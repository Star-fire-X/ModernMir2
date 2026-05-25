#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "config/models.hpp"
#include "world/legacy_random.hpp"
#include "world/legacy_skill_formula.hpp"

#undef assert
#define assert(expression)       \
  do {                           \
    if (!(expression)) {         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();              \
    }                            \
  } while (false)

int main() {
  assert(mir2::delphi_round(2.5) == 2);
  assert(mir2::delphi_round(3.5) == 4);
  assert(mir2::delphi_round(-2.5) == -2);
  assert(mir2::delphi_round(-3.5) == -4);
  assert(mir2::delphi_round(2.49) == 2);
  assert(mir2::delphi_round(2.51) == 3);

  mir2::LegacyMagicDefinition magic;
  magic.spell = 10;
  magic.max_train_level = 3;
  magic.def_spell = 2;
  assert(mir2::legacy_spell_point(magic, 1) == 7);
  assert(mir2::legacy_spell_point(magic, 3) == 12);

  magic.min_power = 5;
  magic.max_power = 9;
  assert(mir2::legacy_mpow(magic, 3) == 8);
  assert(mir2::legacy_mpow(magic, 0) == 5);

  magic.def_min_power = 2;
  magic.def_max_power = 5;
  assert(mir2::legacy_power(magic, 1, 10, 2) == 9);
  assert(mir2::legacy_power(magic, 3, 10, 0) == 12);

  magic.def_min_power = 0;
  magic.def_max_power = 1;
  assert(mir2::legacy_power13(magic, 1, 30, 0) == 20);
  assert(mir2::legacy_power13(magic, 3, 30, 0) == 30);

  assert(mir2::legacy_attack_power_from_rolls(10, 5, 0, 0, 3) == 13);
  assert(mir2::legacy_attack_power_from_rolls(10, 5, 2, 0, 0) == 15);
  assert(mir2::legacy_attack_power_from_rolls(10, 5, 2, 1, 3) == 13);
  assert(mir2::legacy_attack_power_from_rolls(10, 5, -2, 0, 5) == 10);
  assert(mir2::legacy_attack_power_from_rolls(10, 5, -2, 1, 3) == 13);

  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, false, 0, false, 0) == 16);
  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, true, 3, false, 0) == 19);
  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, false, 0, true, 1) == 4);
  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, true, 3, true, 1) == 5);
  assert(mir2::legacy_mag_struck_damage(2, 5, 5, 0, true, 3, false, 0) == 3);

  assert(!mir2::legacy_anti_magic_pass(1, 0));
  assert(mir2::legacy_anti_magic_pass(1, 1));
  assert(mir2::legacy_anti_magic_pass(9, 9));
  assert(!mir2::legacy_anti_magic_pass(10, 9));

  assert(mir2::legacy_delay_ms_to_ticks(600, 20) == 30);
  assert(mir2::legacy_delay_ms_to_ticks(601, 20) == 31);
  assert(mir2::legacy_delay_ms_to_ticks(0, 20) == 0);
  assert(mir2::legacy_delay_ms_to_ticks(600, 0) == 0);

  mir2::LegacyRandom rng(1);
  const std::array<std::int32_t, 8> ranges{10, 10, 100, 5, 3, 1000, 0, 1};
  const std::array<std::int32_t, 8> expected{0, 8, 20, 1, 2, 318, 0, 0};
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    const auto before = rng.state();
    assert(rng.random(ranges[index]) == expected[index]);
    if (ranges[index] <= 0) {
      assert(rng.state() == before);
    }
  }

  assert(mir2::legacy_is_sword_skill(3));
  assert(!mir2::legacy_is_sword_skill(4));
  assert(mir2::legacy_is_sword_skill(7));
  assert(mir2::legacy_is_sword_skill(12));
  assert(mir2::legacy_is_sword_skill(25));
  assert(mir2::legacy_is_sword_skill(26));
  assert(mir2::legacy_is_sword_skill(27));
  assert(mir2::legacy_is_sword_skill(34));
  assert(!mir2::legacy_is_sword_skill(1));

  return 0;
}
