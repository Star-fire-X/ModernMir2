#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "config/config_loader.hpp"

#ifndef MIR2_CONFIG_DIR
#error "MIR2_CONFIG_DIR must be defined by CMake"
#endif

#undef assert
#define assert(expression)       \
  do {                           \
    if (!(expression)) {         \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();              \
    }                            \
  } while (false)

namespace {

const mir2::MagicConfig& find_magic(const mir2::HostConfig& config, std::int32_t id) {
  const auto it = std::find_if(config.magics.begin(), config.magics.end(),
                               [id](const mir2::MagicConfig& magic) {
                                 return magic.id == id;
                               });
  assert(it != config.magics.end());
  return *it;
}

bool has_magic(const mir2::HostConfig& config, std::int32_t id) {
  return std::any_of(config.magics.begin(), config.magics.end(),
                     [id](const mir2::MagicConfig& magic) { return magic.id == id; });
}

}  // namespace

int main() {
  const auto config = mir2::ConfigLoader{}.load(std::filesystem::path(MIR2_CONFIG_DIR));
  assert(config.magics.size() == 35);

  std::set<std::int32_t> ids;
  for (const auto& magic : config.magics) {
    assert(ids.insert(magic.id).second);
    assert(magic.legacy.legacy_present);
  }

  const auto& id1 = find_magic(config, 1);
  assert(id1.legacy.effect_type == 1);
  assert(id1.legacy.effect == 1);
  assert(id1.legacy.spell == 4);
  assert(id1.legacy.min_power == 8);
  assert(id1.legacy.max_power == 8);
  const std::array<std::int32_t, 4> expected_need_level{7, 11, 16, 16};
  const std::array<std::int32_t, 4> expected_max_train{500, 1500, 3000, 3000};
  assert(id1.legacy.need_level == expected_need_level);
  assert(id1.legacy.max_train == expected_max_train);
  assert(id1.legacy.delay_time == 600);
  assert(id1.legacy.def_spell == 1);
  assert(id1.legacy.def_min_power == 2);
  assert(id1.legacy.def_max_power == 2);
  assert(!id1.legacy.is_sword_skill);

  const auto& id2 = find_magic(config, 2);
  assert(id2.affect_players);
  assert(!id2.affect_monsters);
  assert(id2.instant_heal == 14);
  assert(id2.legacy.effect_type == 2);
  assert(id2.legacy.effect == 2);
  assert(id2.legacy.spell == 7);
  assert(id2.legacy.min_power == 14);
  assert(id2.legacy.max_power == 20);
  assert(id2.legacy.delay_time == 400);

  const auto& id5 = find_magic(config, 5);
  assert(id5.legacy.effect_type == 1);
  assert(id5.legacy.effect == 3);
  assert(id5.legacy.spell == 3);
  assert(id5.legacy.min_power == 6);
  assert(id5.legacy.max_power == 6);
  assert(id5.legacy.delay_time == 600);
  assert(id5.legacy.def_spell == 5);
  assert(id5.legacy.def_min_power == 10);
  assert(id5.legacy.def_max_power == 10);

  const auto& id31 = find_magic(config, 31);
  assert(id31.legacy.effect_type == 4);
  assert(id31.legacy.effect == 29);
  assert(id31.legacy.spell == 20);
  assert(id31.legacy.delay_time == 0);

  const auto& id33 = find_magic(config, 33);
  assert(id33.legacy.effect_type == 2);
  assert(id33.legacy.effect == 31);
  assert(id33.legacy.spell == 12);
  assert(id33.legacy.min_power == 12);
  assert(id33.legacy.max_power == 14);
  assert(id33.legacy.delay_time == 600);

  const auto& id35 = find_magic(config, 35);
  assert(id35.legacy.effect_type == 7);
  assert(id35.legacy.effect == 32);
  assert(id35.legacy.spell == 12);
  assert(id35.legacy.min_power == 14);
  assert(id35.legacy.max_power == 30);
  assert(id35.legacy.delay_time == 1000);

  const auto& id36 = find_magic(config, 36);
  assert(id36.affect_players);
  assert(!id36.affect_monsters);
  assert(id36.legacy.effect_type == 9);
  assert(id36.legacy.effect == 33);
  assert(id36.legacy.spell == 15);
  assert(id36.legacy.delay_time == 400);

  assert(!has_magic(config, 34));
  assert(!has_magic(config, 37));

  return 0;
}
