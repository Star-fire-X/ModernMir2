#pragma once

#include <cstdint>

#include "config/models.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

[[nodiscard]] std::int32_t delphi_round(double value);
[[nodiscard]] bool legacy_is_sword_skill(std::int32_t magic_id);
[[nodiscard]] std::int32_t legacy_spell_point(const LegacyMagicDefinition& magic,
                                              std::int32_t level);
[[nodiscard]] std::int32_t legacy_mpow(const LegacyMagicDefinition& magic,
                                       std::int32_t random_value);
[[nodiscard]] std::int32_t legacy_mpow(const LegacyMagicDefinition& magic,
                                       LegacyRandom& random);
[[nodiscard]] std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                                        std::int32_t power, std::int32_t random_value);
[[nodiscard]] std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                                        std::int32_t power, LegacyRandom& random);
[[nodiscard]] std::int32_t legacy_power13(const LegacyMagicDefinition& magic, std::int32_t level,
                                          std::int32_t power, std::int32_t random_value);
[[nodiscard]] std::int32_t legacy_attack_power_from_rolls(std::int32_t damage,
                                                          std::int32_t ranval,
                                                          std::int32_t luck,
                                                          std::int32_t gate_roll,
                                                          std::int32_t power_roll);
[[nodiscard]] std::int32_t legacy_attack_power(std::int32_t damage, std::int32_t ranval,
                                               std::int32_t luck, LegacyRandom& random);
[[nodiscard]] std::int32_t legacy_mag_struck_damage(std::int32_t damage, std::int32_t mac_min,
                                                    std::int32_t mac_max,
                                                    std::int32_t armor_random,
                                                    bool undead_target,
                                                    std::int32_t undead_power,
                                                    bool magic_bubble,
                                                    std::int32_t magic_bubble_level);
[[nodiscard]] bool legacy_anti_magic_pass(std::int32_t anti_magic, std::int32_t random10);
[[nodiscard]] std::uint64_t legacy_delay_ms_to_ticks(std::uint32_t value_ms,
                                                     std::uint32_t tick_ms);

}  // namespace mir2
