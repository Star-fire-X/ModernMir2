#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mir2::client {

constexpr int kCharacterSelectSlotCount = 2;
constexpr int kCharacterSelectSelectedFrameCount = 16;
constexpr int kCharacterSelectFreezeFrameCount = 13;
constexpr int kCharacterSelectEffectFrameCount = 14;
constexpr std::uint64_t kCharacterSelectIdleFrameMs = 300;
constexpr std::uint64_t kCharacterSelectUnfreezeFrameMs = 120;
constexpr std::uint64_t kCharacterSelectFreezeFrameMs = 50;
constexpr std::uint64_t kCharacterSelectEffectFrameMs = 110;

enum class CharacterSelectPoseKind {
  idle,
  frozen,
  unfreezing,
  freezing,
};

struct CharacterSelectPose {
  CharacterSelectPoseKind kind{CharacterSelectPoseKind::frozen};
  int body_frame{0};
  int effect_frame{0};
  bool draw_effect{false};
};

struct CharacterSelectSlotState {
  bool valid{false};
  bool selected{false};
  bool freeze_state{true};
  bool unfreezing{false};
  bool freezing{false};
  int ani_index{0};
  int eff_index{0};
  int dark_level{0};
  std::uint64_t frame_time_ms{0};
  std::uint64_t effect_time_ms{0};
  std::uint64_t idle_time_ms{0};
  std::uint64_t dark_time_ms{0};
};

class CharacterSelectVisualState {
 public:
  void reset(int valid_count, int selected_index, std::uint64_t now_ms) {
    valid_count = std::clamp(valid_count, 0, kCharacterSelectSlotCount);
    if (selected_index < 0 || selected_index >= valid_count) {
      selected_index = valid_count == 0 ? -1 : 0;
    }

    for (int index = 0; index < kCharacterSelectSlotCount; ++index) {
      auto& slot = slots_[static_cast<std::size_t>(index)];
      slot = CharacterSelectSlotState{};
      slot.valid = index < valid_count;
      slot.selected = slot.valid && index == selected_index;
      slot.freeze_state = slot.valid && !slot.selected;
      slot.frame_time_ms = now_ms;
      slot.effect_time_ms = now_ms;
      slot.idle_time_ms = now_ms;
      slot.dark_time_ms = now_ms;
    }
  }

  [[nodiscard]] bool select_slot(int index, int valid_count, std::uint64_t now_ms) {
    valid_count = std::clamp(valid_count, 0, kCharacterSelectSlotCount);
    if (index < 0 || index >= valid_count) {
      return false;
    }

    auto changed = false;
    for (int slot_index = 0; slot_index < kCharacterSelectSlotCount; ++slot_index) {
      auto& slot = slots_[static_cast<std::size_t>(slot_index)];
      slot.valid = slot_index < valid_count;
      if (!slot.valid) {
        slot = CharacterSelectSlotState{};
        continue;
      }

      if (slot_index == index) {
        if (slot.selected && !slot.freeze_state && !slot.unfreezing && !slot.freezing) {
          continue;
        }
        if (!slot.selected || slot.freeze_state || slot.freezing) {
          changed = true;
        }
        slot.selected = true;
        slot.unfreezing = true;
        slot.freezing = false;
        slot.ani_index = 0;
        slot.eff_index = 0;
        slot.dark_level = 0;
        slot.frame_time_ms = now_ms;
        slot.effect_time_ms = now_ms;
        slot.idle_time_ms = now_ms;
        slot.dark_time_ms = now_ms;
        continue;
      }

      if (slot.selected || slot.unfreezing) {
        changed = true;
      }
      slot.selected = false;
      slot.unfreezing = false;
      slot.ani_index = 0;
      slot.eff_index = 0;
      slot.effect_time_ms = now_ms;
      if (!slot.freeze_state) {
        slot.freezing = true;
        slot.frame_time_ms = now_ms;
      } else {
        slot.freezing = false;
      }
    }
    return changed;
  }

  void update(std::uint64_t now_ms) {
    for (auto& slot : slots_) {
      if (!slot.valid) {
        continue;
      }

      if (slot.unfreezing) {
        if (elapsed(now_ms, slot.frame_time_ms) > kCharacterSelectUnfreezeFrameMs) {
          slot.frame_time_ms = now_ms;
          ++slot.ani_index;
          if (slot.ani_index >= kCharacterSelectFreezeFrameCount) {
            slot.unfreezing = false;
            slot.freeze_state = false;
            slot.ani_index = 0;
            slot.idle_time_ms = now_ms;
          }
        }
        if (elapsed(now_ms, slot.effect_time_ms) > kCharacterSelectEffectFrameMs) {
          slot.effect_time_ms = now_ms;
          slot.eff_index = (slot.eff_index + 1) % kCharacterSelectEffectFrameCount;
        }
        continue;
      }

      if (slot.freezing) {
        if (elapsed(now_ms, slot.frame_time_ms) > kCharacterSelectFreezeFrameMs) {
          slot.frame_time_ms = now_ms;
          ++slot.ani_index;
          if (slot.ani_index >= kCharacterSelectFreezeFrameCount) {
            slot.freezing = false;
            slot.freeze_state = true;
            slot.ani_index = 0;
          }
        }
        continue;
      }

      if (slot.selected && !slot.freeze_state) {
        if (elapsed(now_ms, slot.idle_time_ms) > kCharacterSelectIdleFrameMs) {
          slot.idle_time_ms = now_ms;
          slot.ani_index = (slot.ani_index + 1) % kCharacterSelectSelectedFrameCount;
        }
        if (slot.dark_level > 0 && elapsed(now_ms, slot.dark_time_ms) > 25U) {
          slot.dark_time_ms = now_ms;
          --slot.dark_level;
        }
      }
    }
  }

  [[nodiscard]] bool can_delete(int index, int valid_count) const {
    valid_count = std::clamp(valid_count, 0, kCharacterSelectSlotCount);
    if (index < 0 || index >= valid_count) {
      return false;
    }
    const auto& slot = slots_[static_cast<std::size_t>(index)];
    return slot.valid && slot.selected && !slot.freeze_state && !slot.unfreezing && !slot.freezing;
  }

  [[nodiscard]] CharacterSelectPose pose_for(int index) const {
    if (index < 0 || index >= kCharacterSelectSlotCount) {
      return {};
    }
    const auto& slot = slots_[static_cast<std::size_t>(index)];
    if (!slot.valid) {
      return {};
    }
    if (slot.unfreezing) {
      return CharacterSelectPose{CharacterSelectPoseKind::unfreezing,
                                 std::clamp(slot.ani_index, 0,
                                            kCharacterSelectFreezeFrameCount - 1),
                                 slot.eff_index % kCharacterSelectEffectFrameCount, true};
    }
    if (slot.freezing) {
      const auto frame =
          kCharacterSelectFreezeFrameCount - std::clamp(slot.ani_index, 0,
                                                        kCharacterSelectFreezeFrameCount - 1) -
          1;
      return CharacterSelectPose{CharacterSelectPoseKind::freezing, frame, 0, false};
    }
    if (slot.freeze_state) {
      return CharacterSelectPose{CharacterSelectPoseKind::frozen, 0, 0, false};
    }
    return CharacterSelectPose{CharacterSelectPoseKind::idle,
                               slot.ani_index % kCharacterSelectSelectedFrameCount, 0, false};
  }

  [[nodiscard]] const CharacterSelectSlotState& slot(int index) const {
    return slots_[static_cast<std::size_t>(index)];
  }

 private:
  static std::uint64_t elapsed(std::uint64_t now_ms, std::uint64_t then_ms) {
    return now_ms >= then_ms ? now_ms - then_ms : 0;
  }

  std::array<CharacterSelectSlotState, kCharacterSelectSlotCount> slots_{};
};

}  // namespace mir2::client
