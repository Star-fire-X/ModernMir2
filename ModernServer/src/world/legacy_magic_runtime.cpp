#include "world/legacy_magic_runtime.hpp"

#include <algorithm>
#include <string>

#include "util/string_utils.hpp"

namespace mir2 {

namespace {

std::string compare_key(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

std::int32_t magic_level_index(const LegacyUseMagicInfo& user_magic) {
  return std::clamp<std::int32_t>(user_magic.level, 0, 3);
}

}  // namespace

bool legacy_magic_source_only(std::int32_t magic_id) {
  return magic_id >= 34 && magic_id <= 37;
}

const MagicConfig* legacy_find_magic_by_book_name(
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
    std::string_view book_name) {
  const auto wanted = compare_key(book_name);
  if (wanted.empty()) {
    return nullptr;
  }
  const MagicConfig* best = nullptr;
  for (const auto& [magic_id, magic] : magic_configs) {
    if (!magic.legacy.legacy_present || magic.name.empty()) {
      continue;
    }
    if (compare_key(magic.name) != wanted) {
      continue;
    }
    if (best == nullptr || magic_id < best->id) {
      best = &magic;
    }
  }
  return best;
}

const char* legacy_read_book_status_name(LegacyReadBookStatus status) {
  switch (status) {
    case LegacyReadBookStatus::learned:
      return "learned";
    case LegacyReadBookStatus::invalid_item:
      return "invalid_item";
    case LegacyReadBookStatus::unknown_magic:
      return "unknown_magic";
    case LegacyReadBookStatus::source_only:
      return "source_only";
    case LegacyReadBookStatus::duplicate:
      return "duplicate";
    case LegacyReadBookStatus::job_mismatch:
      return "job_mismatch";
    case LegacyReadBookStatus::level_too_low:
      return "level_too_low";
    case LegacyReadBookStatus::no_slot:
      return "no_slot";
  }
  return "unknown";
}

LegacyReadBookResult legacy_read_magic_book(
    Player& player, const ItemConfig& book,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  if (book.std_mode != 4) {
    return {LegacyReadBookStatus::invalid_item, 0};
  }

  const auto* magic = legacy_find_magic_by_book_name(magic_configs, book.name);
  if (magic == nullptr || !magic->legacy.legacy_present) {
    return {LegacyReadBookStatus::unknown_magic, 0};
  }

  const auto magic_id = magic->id;
  if (legacy_magic_source_only(magic_id)) {
    return {LegacyReadBookStatus::source_only, magic_id};
  }
  if (player.learned_magic(magic_id) != nullptr) {
    return {LegacyReadBookStatus::duplicate, magic_id};
  }
  if (magic->legacy.job != 99 && magic->legacy.job != player.character().job) {
    return {LegacyReadBookStatus::job_mismatch, magic_id};
  }
  if (player.character().ability.level < magic->legacy.need_level[0]) {
    return {LegacyReadBookStatus::level_too_low, magic_id};
  }
  if (!player.add_legacy_magic(magic_id, '\0', 0, 0)) {
    return {LegacyReadBookStatus::no_slot, magic_id};
  }
  return {LegacyReadBookStatus::learned, magic_id};
}

LegacyMagicTrainResult legacy_train_magic(Player& player, LegacyUseMagicInfo& user_magic,
                                          const MagicConfig& magic, LegacyRandom& random,
                                          std::int32_t fixed_train_amount) {
  LegacyMagicTrainResult result;
  result.magic_id = user_magic.magic_id;
  result.level = user_magic.level;
  result.cur_train = user_magic.cur_train;

  if (!magic.legacy.legacy_present || user_magic.magic_id == 0 || user_magic.level >= 3) {
    return result;
  }

  const auto level = magic_level_index(user_magic);
  if (player.character().ability.level < magic.legacy.need_level[level]) {
    return result;
  }

  result.train_amount = fixed_train_amount > 0 ? fixed_train_amount : 1 + random.random(3);
  user_magic.cur_train += result.train_amount;
  result.trained = true;

  const auto max_train_level = std::clamp(magic.legacy.max_train_level, 0, 3);
  const auto threshold = std::max(magic.legacy.max_train[level], 0);
  if (user_magic.level < max_train_level && threshold > 0 && user_magic.cur_train >= threshold) {
    user_magic.cur_train -= threshold;
    ++user_magic.level;
    result.leveled_up = true;
    legacy_check_magic_special_ability(player, user_magic);
  }

  result.level = user_magic.level;
  result.cur_train = user_magic.cur_train;
  return result;
}

void legacy_check_magic_special_ability(Player& player, const LegacyUseMagicInfo& user_magic) {
  if (user_magic.magic_id == 28 && user_magic.level >= 2) {
    player.set_legacy_see_health_gauge(true);
  }
}

}  // namespace mir2
