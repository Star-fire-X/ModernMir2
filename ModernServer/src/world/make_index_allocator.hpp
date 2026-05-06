#pragma once

#include <cstdint>

#include "core/messages.hpp"

namespace mir2 {

class MakeIndexAllocator {
 public:
  static constexpr std::int32_t kLegacyRuntimeFloor = 200000;

  void reset(std::int32_t floor = kLegacyRuntimeFloor);
  void observe(std::int32_t make_index);
  void observe(const LegacyUserItem& item);
  void observe(const CharacterRecord& character);
  void observe(const MerchantStateRecord& merchant_state);
  [[nodiscard]] std::int32_t allocate();
  [[nodiscard]] std::int32_t next_value() const { return next_make_index_; }

 private:
  std::int32_t next_make_index_{kLegacyRuntimeFloor};
};

}  // namespace mir2
