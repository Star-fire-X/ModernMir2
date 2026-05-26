#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "config/models.hpp"
#include "protocol/legacy_types.hpp"
#include "storage/repository.hpp"
#include "world/game_object.hpp"
#include "world/legacy_map_environment.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_bag_invariants_smoke failed at " << stage << '\n';
  return 1;
}

mir2::LegacyUserItem item(std::int32_t make_index, std::uint16_t index,
                          std::uint16_t dura = 1, std::uint16_t dura_max = 1) {
  mir2::LegacyUserItem result;
  result.make_index = make_index;
  result.index = index;
  result.dura = dura;
  result.dura_max = dura_max;
  result.desc[0] = static_cast<std::uint8_t>(make_index & 0xff);
  result.prefix[0] = 'P';
  return result;
}

mir2::CharacterRecord character(std::string name) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.ability.max_hp = 15;
  record.ability.max_mp = 10;
  record.ability.max_exp = 100;
  record.ability.max_weight = 1000;
  record.ability.max_wear_weight = 200;
  record.ability.max_hand_weight = 200;
  return record;
}

std::unordered_map<std::int32_t, mir2::ItemConfig> item_configs() {
  return {{1, mir2::ItemConfig{1, "Ruby", 1, 10, 0, 1, 1, 1, -1, 0, 0}},
          {2, mir2::ItemConfig{2, "Sword", 3, 100, 5, 1, 1, 1000, 1, 0, 0}},
          {3, mir2::ItemConfig{3, "Token", 1, 1, 10, 0, 1, 1, -1, 0, 0}}};
}

std::vector<std::int32_t> make_indices(const std::array<mir2::LegacyUserItem, mir2::kMaxBagItems>& items) {
  std::vector<std::int32_t> result;
  for (const auto& entry : items) {
    if (!mir2::is_empty(entry)) {
      result.push_back(entry.make_index);
    }
  }
  return result;
}

template <std::size_t N>
bool compact_prefix(const std::array<mir2::LegacyUserItem, N>& items) {
  bool saw_empty = false;
  for (const auto& entry : items) {
    if (mir2::is_empty(entry)) {
      saw_empty = true;
    } else if (saw_empty) {
      return false;
    }
  }
  return true;
}

template <std::size_t N>
bool same_items(const std::array<mir2::LegacyUserItem, N>& lhs,
                const std::array<mir2::LegacyUserItem, N>& rhs) {
  return std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(mir2::LegacyUserItem)) == 0;
}

bool unique_make_indices(const mir2::CharacterRecord& record) {
  std::set<std::int32_t> seen;
  auto add = [&](const mir2::LegacyUserItem& entry) {
    return mir2::is_empty(entry) || seen.insert(entry.make_index).second;
  };
  return std::all_of(record.bag_items.begin(), record.bag_items.end(), add) &&
         std::all_of(record.equipped_items.begin(), record.equipped_items.end(), add) &&
         std::all_of(record.storage_items.begin(), record.storage_items.end(), add);
}

bool capacity_and_compact_invariants() {
  auto record = character("Capacity");
  for (std::size_t slot = 0; slot < record.bag_items.size(); ++slot) {
    record.bag_items[slot] = item(static_cast<std::int32_t>(1000 + slot), 1);
  }

  mir2::Player player(1, 7, record);
  const auto configs = item_configs();
  if (player.has_free_bag_slot() || player.can_add_bag_item(item(2000, 1), configs) ||
      player.add_bag_item(item(2000, 1))) {
    return false;
  }

  const auto removed = player.remove_bag_item_at(10);
  if (!removed.has_value() || removed->make_index != 1010 || !compact_prefix(player.character().bag_items)) {
    return false;
  }
  if (!player.add_bag_item(item(2000, 1))) {
    return false;
  }
  auto expected = std::vector<std::int32_t>{};
  expected.reserve(mir2::kMaxBagItems);
  for (std::int32_t make_index = 1000; make_index < 1046; ++make_index) {
    if (make_index != 1010) {
      expected.push_back(make_index);
    }
  }
  expected.push_back(2000);
  return make_indices(player.character().bag_items) == expected && compact_prefix(player.character().bag_items);
}

bool failed_delete_does_not_mutate() {
  auto record = character("DeleteFail");
  record.bag_items[0] = item(3001, 1);
  record.bag_items[1] = item(3002, 2);
  mir2::Player player(2, 8, record);
  const auto configs = item_configs();
  const auto before = player.character().bag_items;

  if (player.remove_bag_item(3001, "Wrong Name", configs).has_value()) {
    return false;
  }
  return same_items(player.character().bag_items, before) && compact_prefix(player.character().bag_items);
}

bool storage_remove_compacts_before_next_add() {
  auto record = character("Storage");
  record.storage_items[0] = item(4001, 1);
  record.storage_items[1] = item(4002, 2);
  record.storage_items[2] = item(4003, 3);
  mir2::Player player(3, 9, record);
  const auto configs = item_configs();

  const auto removed = player.remove_storage_item(4002, "Sword", configs);
  if (!removed.has_value() || !compact_prefix(player.character().storage_items)) {
    return false;
  }
  if (!player.add_storage_item(item(4004, 1))) {
    return false;
  }
  return player.character().storage_items[0].make_index == 4001 &&
         player.character().storage_items[1].make_index == 4003 &&
         player.character().storage_items[2].make_index == 4004 &&
         compact_prefix(player.character().storage_items);
}

bool repository_round_trips_item_containers() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_legacy_bag_invariants_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  auto record = character("RoundTrip");
  record.equipped_items[mir2::kEquipWeapon] = item(5001, 2, 600, 1000);
  record.bag_items[0] = item(5002, 1, 2, 3);
  record.bag_items[2] = item(5003, 3, 4, 5);
  record.storage_items[0] = item(5004, 1, 6, 7);
  record.storage_items[3] = item(5005, 2, 8, 9);
  if (!repository.create_character(record) || !repository.save_character(record)) {
    return false;
  }

  const auto loaded = repository.load_character(record.account_id, record.character_name);
  if (!loaded.has_value()) {
    return false;
  }
  return loaded->equipped_items[mir2::kEquipWeapon].make_index == 5001 &&
         loaded->bag_items[0].make_index == 5002 && loaded->bag_items[2].make_index == 5003 &&
         loaded->bag_items[2].dura_max == 5 && loaded->storage_items[0].make_index == 5004 &&
         loaded->storage_items[3].make_index == 5005 && loaded->storage_items[3].prefix[0] == 'P';
}

bool make_index_uniqueness_scan_covers_all_item_containers() {
  auto record = character("Unique");
  record.equipped_items[mir2::kEquipWeapon] = item(6001, 2);
  record.bag_items[0] = item(6002, 1);
  record.storage_items[0] = item(6003, 3);
  if (!unique_make_indices(record)) {
    return false;
  }
  record.storage_items[1] = item(6002, 3);
  return !unique_make_indices(record);
}

bool add_gold_clamps_to_legacy_bag_gold() {
  auto record = character("GoldCap");
  record.gold = mir2::kLegacyBagGold - 1;
  mir2::Player player(4, 10, record);
  player.add_gold(100);
  if (player.character().gold != mir2::kLegacyBagGold) {
    return false;
  }
  player.add_gold(-mir2::kLegacyBagGold - 100);
  return player.character().gold == 0;
}

}  // namespace

int main() {
  if (!capacity_and_compact_invariants()) {
    return fail("capacity and compact invariants");
  }
  if (!failed_delete_does_not_mutate()) {
    return fail("failed delete does not mutate");
  }
  if (!storage_remove_compacts_before_next_add()) {
    return fail("storage remove compacts before next add");
  }
  if (!repository_round_trips_item_containers()) {
    return fail("repository item container roundtrip");
  }
  if (!make_index_uniqueness_scan_covers_all_item_containers()) {
    return fail("make index uniqueness scan");
  }
  if (!add_gold_clamps_to_legacy_bag_gold()) {
    return fail("add gold clamps");
  }
  return 0;
}
