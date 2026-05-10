// ============================================================
// Mir2 legacy action identifiers
// Shared Delphi-compatible SM_* and CM_* attack ids.
// ============================================================
#pragma once

#include <cstdint>

namespace mir2::legacy {

constexpr std::uint16_t kSmFireHit = 8;
constexpr std::uint16_t kSmRush = 6;
constexpr std::uint16_t kSmRushKung = 7;
constexpr std::uint16_t kSmHit = 14;
constexpr std::uint16_t kSmHeavyHit = 15;
constexpr std::uint16_t kSmBigHit = 16;
constexpr std::uint16_t kSmPowerHit = 18;
constexpr std::uint16_t kSmLongHit = 19;
constexpr std::uint16_t kSmWideHit = 24;
constexpr std::uint16_t kSmCrossHit = 35;

constexpr std::uint16_t kCmHit = 3014;
constexpr std::uint16_t kCmHeavyHit = 3015;
constexpr std::uint16_t kCmBigHit = 3016;
constexpr std::uint16_t kCmPowerHit = 3018;
constexpr std::uint16_t kCmLongHit = 3019;
constexpr std::uint16_t kCmWideHit = 3024;
constexpr std::uint16_t kCmFireHit = 3025;
constexpr std::uint16_t kCmCrossHit = 3035;

constexpr bool is_attack_sm_ident(const std::uint16_t ident) {
  switch (ident) {
    case kSmFireHit:
    case kSmHit:
    case kSmHeavyHit:
    case kSmBigHit:
    case kSmPowerHit:
    case kSmLongHit:
    case kSmWideHit:
    case kSmCrossHit:
      return true;
    default:
      return false;
  }
}

constexpr bool is_attack_cm_ident(const std::uint16_t ident) {
  switch (ident) {
    case kCmHit:
    case kCmHeavyHit:
    case kCmBigHit:
    case kCmPowerHit:
    case kCmLongHit:
    case kCmWideHit:
    case kCmFireHit:
    case kCmCrossHit:
      return true;
    default:
      return false;
  }
}

constexpr std::uint16_t cm_attack_ident_to_sm(const std::uint16_t ident) {
  switch (ident) {
    case kCmHeavyHit:
      return kSmHeavyHit;
    case kCmBigHit:
      return kSmBigHit;
    case kCmPowerHit:
      return kSmPowerHit;
    case kCmLongHit:
      return kSmLongHit;
    case kCmWideHit:
      return kSmWideHit;
    case kCmFireHit:
      return kSmFireHit;
    case kCmCrossHit:
      return kSmCrossHit;
    case kCmHit:
    default:
      return kSmHit;
  }
}

constexpr std::uint16_t sm_attack_ident_to_cm(const std::uint16_t ident) {
  switch (ident) {
    case kSmHeavyHit:
      return kCmHeavyHit;
    case kSmBigHit:
      return kCmBigHit;
    case kSmPowerHit:
      return kCmPowerHit;
    case kSmLongHit:
      return kCmLongHit;
    case kSmWideHit:
      return kCmWideHit;
    case kSmFireHit:
      return kCmFireHit;
    case kSmCrossHit:
      return kCmCrossHit;
    case kSmHit:
    default:
      return kCmHit;
  }
}

constexpr std::uint16_t normalize_attack_ident_to_sm(const std::uint16_t ident) {
  if (is_attack_sm_ident(ident)) {
    return ident;
  }
  if (is_attack_cm_ident(ident)) {
    return cm_attack_ident_to_sm(ident);
  }
  return kSmHit;
}

}  // namespace mir2::legacy
