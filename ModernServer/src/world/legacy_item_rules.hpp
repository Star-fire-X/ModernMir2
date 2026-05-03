#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

constexpr std::int32_t kLegacyItemUnableTakeOff = 0x02;
constexpr std::int32_t kLegacyItemNeverTakeOff = 0x04;

std::int32_t legacy_resolve_slot_from_std_mode(std::int32_t std_mode);
bool legacy_item_fits_slot(const ItemConfig& item_config, std::int32_t slot);
bool legacy_slot_uses_hand_weight(std::size_t slot);
bool legacy_item_can_take_off(const ItemConfig* item_config, const LegacyUserItem& item);
bool legacy_item_is_consumable(const ItemConfig& item_config);
bool legacy_item_is_magic_book(const ItemConfig& item_config);
bool legacy_item_is_scroll(const ItemConfig& item_config);
bool legacy_item_is_unbind_bundle(const ItemConfig& item_config);
std::string legacy_scroll_kind(const ItemConfig& item_config);
ItemConfig legacy_upgraded_item_config(const ItemConfig& item_config,
                                       const LegacyUserItem& user_item);
void legacy_random_upgrade_monster_drop_item(const ItemConfig& item_config,
                                             LegacyUserItem& user_item,
                                             LegacyRandom& random);
void legacy_random_set_unknown_monster_drop_item(const ItemConfig& item_config,
                                                 LegacyUserItem& user_item,
                                                 LegacyRandom& random);
bool legacy_can_take_on_item(const CharacterRecord& character,
                             const ItemConfig& item_config,
                             const LegacyUserItem& user_item,
                             std::int32_t slot,
                             std::int32_t current_wear_weight,
                             std::int32_t current_hand_weight,
                             std::int32_t old_slot_weight,
                             std::string* reason = nullptr);

}  // namespace mir2
