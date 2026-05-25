#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "config/models.hpp"
#include "world/game_object.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

enum class LegacyReadBookStatus {
  learned,
  invalid_item,
  unknown_magic,
  source_only,
  duplicate,
  job_mismatch,
  level_too_low,
  no_slot
};

struct LegacyReadBookResult {
  LegacyReadBookStatus status{LegacyReadBookStatus::invalid_item};
  std::int32_t magic_id{0};
};

struct LegacyMagicTrainResult {
  bool trained{false};
  bool leveled_up{false};
  std::int32_t magic_id{0};
  std::int32_t level{0};
  std::int32_t cur_train{0};
  std::int32_t train_amount{0};
};

[[nodiscard]] bool legacy_magic_source_only(std::int32_t magic_id);
[[nodiscard]] const MagicConfig* legacy_find_magic_by_book_name(
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
    std::string_view book_name);
[[nodiscard]] const char* legacy_read_book_status_name(LegacyReadBookStatus status);
[[nodiscard]] LegacyReadBookResult legacy_read_magic_book(
    Player& player, const ItemConfig& book,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs);
[[nodiscard]] LegacyMagicTrainResult legacy_train_magic(Player& player,
                                                        LegacyUseMagicInfo& user_magic,
                                                        const MagicConfig& magic,
                                                        LegacyRandom& random,
                                                        std::int32_t fixed_train_amount = 0);
void legacy_check_magic_special_ability(Player& player, const LegacyUseMagicInfo& user_magic);

}  // namespace mir2
