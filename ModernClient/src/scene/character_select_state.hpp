/**
 * @file character_select_state.hpp
 * @brief 角色选择界面状态管理 —— 选角动画的状态机和帧推进逻辑
 *
 * @details 管理角色选择界面中角色预览动画的完整状态：
 *          - 每个槽位（最多 2 个角色）有独立的状态机
 *          - 支持四种状态：冻结（freeze）、解冻中（unfreezing）、
 *            冻结中（freezing）、空闲（idle）
 *          - 解冻/冻结动画使用 ChrSel.wil 中的帧序列
 *          - 选中角色有特效动画叠加
 *
 * 状态转换逻辑：
 * - 未被选中的角色 → 冻结状态（单帧静态）
 * - 点击选中角色 → 解冻动画（13 帧）→ 空闲动画（16 帧循环）
 * - 取消选中 → 冻结动画（13 帧倒放）→ 冻结状态
 *
 * 动画参数：
 * - 解冻帧间隔：120ms/帧
 * - 冻结帧间隔：50ms/帧
 * - 空闲帧间隔：300ms/帧
 * - 特效帧间隔：110ms/帧
 * - 暗化恢复间隔：25ms/级
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mir2::client {

/// 角色选择界面最大槽位数（对应经典传奇的 2 个角色槽位）
constexpr int kCharacterSelectSlotCount = 2;
/// 角色选中后空闲动画帧数（ChrSel.wil 每职业 16 帧）
constexpr int kCharacterSelectSelectedFrameCount = 16;
/// 角色冻结动画帧数（每职业 13 帧）
constexpr int kCharacterSelectFreezeFrameCount = 13;
/// 角色选中特效动画帧数
constexpr int kCharacterSelectEffectFrameCount = 14;
/// 空闲动画帧间隔（毫秒）
constexpr std::uint64_t kCharacterSelectIdleFrameMs = 300;
/// 解冻动画帧间隔（毫秒）
constexpr std::uint64_t kCharacterSelectUnfreezeFrameMs = 120;
/// 冻结动画帧间隔（毫秒）
constexpr std::uint64_t kCharacterSelectFreezeFrameMs = 50;
/// 特效动画帧间隔（毫秒）
constexpr std::uint64_t kCharacterSelectEffectFrameMs = 110;

/**
 * @enum CharacterSelectPoseKind
 * @brief 角色选择界面的动画姿态类型
 */
enum class CharacterSelectPoseKind {
  idle,       ///< 空闲循环动画（角色被选中后播放）
  frozen,     ///< 冻结状态（单帧静态，角色未被选中）
  unfreezing, ///< 解冻过渡动画（从冻结变为空闲）
  freezing,   ///< 冻结过渡动画（从空闲变为冻结）
};

/**
 * @struct CharacterSelectPose
 * @brief 角色选择界面的渲染姿态 —— 包含当前帧索引和特效信息
 */
struct CharacterSelectPose {
  CharacterSelectPoseKind kind{CharacterSelectPoseKind::frozen};  ///< 当前动画类型
  int body_frame{0};       ///< 身体精灵帧索引（在 ChrSel.wil 中的偏移）
  int effect_frame{0};     ///< 特效帧索引
  bool draw_effect{false}; ///< 是否绘制特效（仅在 unfreezing 阶段绘制）
};

/**
 * @struct CharacterSelectSlotState
 * @brief 单个角色槽位的完整动画状态
 */
struct CharacterSelectSlotState {
  bool valid{false};           ///< 该槽位是否有角色
  bool selected{false};        ///< 该槽位是否被选中
  bool freeze_state{true};     ///< 是否处于冻结状态
  bool unfreezing{false};      ///< 是否正在解冻
  bool freezing{false};        ///< 是否正在冻结
  int ani_index{0};            ///< 身体动画帧序号
  int eff_index{0};            ///< 特效动画帧序号
  int dark_level{0};           ///< 暗化级别（用于解冻后的渐变恢复）
  std::uint64_t frame_time_ms{0};   ///< 上次帧切换时间戳
  std::uint64_t effect_time_ms{0};  ///< 上次特效帧切换时间戳
  std::uint64_t idle_time_ms{0};    ///< 上次空闲帧切换时间戳
  std::uint64_t dark_time_ms{0};    ///< 上次暗化恢复时间戳
};

/**
 * @class CharacterSelectVisualState
 * @brief 角色选择界面视觉状态管理器
 *
 * @details 管理所有角色槽位的动画状态机。提供：
 *          - reset()：初始化/重置所有槽位状态
 *          - select_slot()：切换选中槽位（触发解冻/冻结动画）
 *          - update()：每帧推进动画帧
 *          - pose_for()：获取指定槽位的当前渲染姿态
 *          - can_delete()：检查指定槽位是否可删除（需已选中且不在过渡动画中）
 */
class CharacterSelectVisualState {
 public:
  /**
   * @brief 重置所有槽位状态
   *
   * @param valid_count 有效角色数量（0-2）
   * @param selected_index 初始选中索引（-1 表示无选中）
   * @param now_ms 当前时间戳（毫秒）
   */
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

  /**
   * @brief 选择指定槽位（触发动画过渡）
   *
   * @details 选中索引的角色触发解冻动画（frozen → unfreezing → idle），
   *          之前选中的角色触发冻结动画（idle → freezing → frozen）。
   *
   * @param index 要选中的槽位索引
   * @param valid_count 当前有效角色数量
   * @param now_ms 当前时间戳
   * @return 如果状态发生了变化返回 true
   */
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

  /**
   * @brief 推进所有槽位的动画帧
   *
   * @details 根据各槽位的当前状态和帧间隔常数，在时间到达时推进帧序号。
   *          状态机转换：
   *          - unfreezing：ani_index 从 0 递增到 kCharacterSelectFreezeFrameCount-1，
   *            完成后变为 idle；特效帧持续循环
   *          - freezing：ani_index 从 0 递增到 kCharacterSelectFreezeFrameCount-1，
   *            完成后变为 frozen
   *          - idle（selected 且非 freeze_state）：ani_index 循环 0-15
   *
   * @param now_ms 当前时间戳
   */
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

  /**
   * @brief 检查指定槽位是否可被删除
   *
   * @details 只有已选中且不在过渡动画中的角色才能被删除。
   *
   * @param index 槽位索引
   * @param valid_count 有效角色数量
   * @return 如果可以删除返回 true
   */
  [[nodiscard]] bool can_delete(int index, int valid_count) const {
    valid_count = std::clamp(valid_count, 0, kCharacterSelectSlotCount);
    if (index < 0 || index >= valid_count) {
      return false;
    }
    const auto& slot = slots_[static_cast<std::size_t>(index)];
    return slot.valid && slot.selected && !slot.freeze_state && !slot.unfreezing && !slot.freezing;
  }

  /**
   * @brief 获取指定槽位的当前渲染姿态
   *
   * @param index 槽位索引
   * @return 包含帧索引和特效信息的姿态结构
   */
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

  /// 获取指定槽位的原始状态（用于测试）
  [[nodiscard]] const CharacterSelectSlotState& slot(int index) const {
    return slots_[static_cast<std::size_t>(index)];
  }

 private:
  /// 安全的时间差计算（防止 now_ms < then_ms 时溢出）
  static std::uint64_t elapsed(std::uint64_t now_ms, std::uint64_t then_ms) {
    return now_ms >= then_ms ? now_ms - then_ms : 0;
  }

  std::array<CharacterSelectSlotState, kCharacterSelectSlotCount> slots_{};
};

}  // namespace mir2::client
