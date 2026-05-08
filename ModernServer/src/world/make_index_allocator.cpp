#include "world/make_index_allocator.hpp"

#include <algorithm>
#include <limits>

#include "protocol/legacy_types.hpp"

namespace mir2 {

namespace {

template <typename Items>
void observe_items(MakeIndexAllocator& allocator, const Items& items) {
  for (const auto& item : items) {
    allocator.observe(item);
  }
}

}  // namespace

void MakeIndexAllocator::reset(std::int32_t floor) {
  next_make_index_ = std::max(floor, kLegacyRuntimeFloor);
}

void MakeIndexAllocator::observe(std::int32_t make_index) {
  if (make_index <= 0) {
    return;
  }
  if (make_index >= std::numeric_limits<std::int32_t>::max()) {
    next_make_index_ = std::numeric_limits<std::int32_t>::max();
    return;
  }
  next_make_index_ = std::max(next_make_index_, make_index + 1);
}

void MakeIndexAllocator::observe(const LegacyUserItem& item) {
  if (!is_empty(item)) {
    observe(item.make_index);
  }
}

void MakeIndexAllocator::observe(const CharacterRecord& character) {
  observe_items(*this, character.equipped_items);
  observe_items(*this, character.bag_items);
  observe_items(*this, character.storage_items);
}

void MakeIndexAllocator::observe(const MerchantStateRecord& merchant_state) {
  observe_items(*this, merchant_state.goods);
  for (const auto& record : merchant_state.weapon_upgrades) {
    observe(record.item);
  }
}

std::int32_t MakeIndexAllocator::allocate() {
  const auto result = next_make_index_;
  if (next_make_index_ < std::numeric_limits<std::int32_t>::max()) {
    ++next_make_index_;
  }
  return result;
}

}  // namespace mir2
