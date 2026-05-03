#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "config/models.hpp"
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

  mir2::LegacyMagicDefinition magic;
  magic.spell = 10;
  magic.max_train_level = 3;
  magic.def_spell = 2;
  assert(mir2::legacy_spell_point(magic, 1) == 7);

  magic.min_power = 5;
  magic.max_power = 9;
  assert(mir2::legacy_mpow(magic, 3) == 8);

  magic.def_min_power = 2;
  magic.def_max_power = 5;
  assert(mir2::legacy_power(magic, 1, 10, 2) == 9);

  magic.def_min_power = 0;
  magic.def_max_power = 1;
  assert(mir2::legacy_power13(magic, 1, 30, 0) == 20);

  assert(mir2::legacy_attack_power_from_rolls(10, 5, 0, 0, 3) == 13);
  assert(mir2::legacy_attack_power_from_rolls(10, 5, 2, 0, 0) == 15);

  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, false, 0, false, 0) == 16);
  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, true, 3, false, 0) == 19);
  assert(mir2::legacy_mag_struck_damage(20, 2, 5, 2, false, 0, true, 1) == 4);

  assert(!mir2::legacy_anti_magic_pass(1, 0));
  assert(mir2::legacy_anti_magic_pass(1, 1));

  assert(mir2::legacy_delay_ms_to_ticks(600, 20) == 30);
  assert(mir2::legacy_is_sword_skill(25));
  assert(!mir2::legacy_is_sword_skill(1));

  return 0;
}
