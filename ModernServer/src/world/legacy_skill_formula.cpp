#include "world/legacy_skill_formula.hpp"

#include <algorithm>
#include <cmath>

namespace mir2 {

std::int32_t delphi_round(double value) {
  return static_cast<std::int32_t>(std::nearbyint(value));
}

bool legacy_is_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 3:
    case 7:
    case 12:
    case 25:
    case 26:
    case 27:
    case 34:
      return true;
    default:
      return false;
  }
}

std::int32_t legacy_spell_point(const LegacyMagicDefinition& magic, std::int32_t level) {
  return delphi_round(static_cast<double>(magic.spell) /
                      static_cast<double>(magic.max_train_level + 1) *
                      static_cast<double>(level + 1)) +
         magic.def_spell;
}

std::int32_t legacy_mpow(const LegacyMagicDefinition& magic, std::int32_t random_value) {
  return magic.min_power + std::max(random_value, 0);
}

std::int32_t legacy_mpow(const LegacyMagicDefinition& magic, LegacyRandom& random) {
  return legacy_mpow(magic, random.random(magic.max_power - magic.min_power));
}

std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                          std::int32_t power, std::int32_t random_value) {
  return delphi_round(static_cast<double>(power) /
                      static_cast<double>(magic.max_train_level + 1) *
                      static_cast<double>(level + 1)) +
         magic.def_min_power + std::max(random_value, 0);
}

std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                          std::int32_t power, LegacyRandom& random) {
  return legacy_power(magic, level, power,
                      random.random(magic.def_max_power - magic.def_min_power));
}

std::int32_t legacy_power13(const LegacyMagicDefinition& magic, std::int32_t level,
                            std::int32_t power, std::int32_t random_value) {
  const auto p1 = static_cast<double>(power) / 3.0;
  const auto p2 = static_cast<double>(power) - p1;
  return delphi_round(p1 + p2 / static_cast<double>(magic.max_train_level + 1) *
                               static_cast<double>(level + 1) +
                      magic.def_min_power + std::max(random_value, 0));
}

std::int32_t legacy_attack_power_from_rolls(std::int32_t damage, std::int32_t ranval,
                                            std::int32_t luck, std::int32_t gate_roll,
                                            std::int32_t power_roll) {
  ranval = std::max(ranval, 0);
  if (luck > 0) {
    if (gate_roll == 0) {
      return damage + ranval;
    }
    return damage + std::clamp(power_roll, 0, ranval);
  }

  auto result = damage + std::clamp(power_roll, 0, ranval);
  if (luck < 0 && gate_roll == 0) {
    result = damage;
  }
  return result;
}

std::int32_t legacy_attack_power(std::int32_t damage, std::int32_t ranval,
                                 std::int32_t luck, LegacyRandom& random) {
  ranval = std::max(ranval, 0);
  if (luck > 0) {
    if (random.random(10 - std::min(9, luck)) == 0) {
      return damage + ranval;
    }
    return damage + random.random(ranval + 1);
  }

  auto result = damage + random.random(ranval + 1);
  if (luck < 0) {
    if (random.random(10 - std::max(0, -luck)) == 0) {
      result = damage;
    }
  }
  return result;
}

std::int32_t legacy_mag_struck_damage(std::int32_t damage, std::int32_t mac_min,
                                      std::int32_t mac_max, std::int32_t armor_random,
                                      bool undead_target, std::int32_t undead_power,
                                      bool magic_bubble,
                                      std::int32_t magic_bubble_level) {
  const auto armor_range = std::max(0, mac_max - mac_min);
  const auto armor = mac_min + std::clamp(armor_random, 0, armor_range);
  damage = std::max(0, damage - armor);
  if (undead_target) {
    damage += std::max(undead_power, 0);
  }
  if (damage > 0 && magic_bubble) {
    damage = delphi_round(static_cast<double>(damage) / 100.0 *
                          static_cast<double>(magic_bubble_level + 2) * 8.0);
  }
  return damage;
}

bool legacy_anti_magic_pass(std::int32_t anti_magic, std::int32_t random10) {
  return anti_magic <= random10;
}

std::uint64_t legacy_delay_ms_to_ticks(std::uint32_t value_ms, std::uint32_t tick_ms) {
  if (value_ms == 0 || tick_ms == 0) {
    return 0;
  }
  return std::max<std::uint64_t>(1, (static_cast<std::uint64_t>(value_ms) +
                                     static_cast<std::uint64_t>(tick_ms) - 1) /
                                        static_cast<std::uint64_t>(tick_ms));
}

}  // namespace mir2
