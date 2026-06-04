/**
 * @file legacy_audio_cue_tracker.hpp
 * @brief 旧版音效提示追踪器 —— 检测角色动作并触发对应的音效
 *
 * @details 追踪场景中每个角色的动画状态变化，在适当的时机触发
 *          对应的音效。包括：
 *          - 脚步声（行走/跑步时左右脚交替）
 *          - 武器挥动声（攻击时）
 *          - 死亡音效（角色死亡时）
 *          - 怪物普通音效（随机播放的怪物叫声）
 *
 * 音效触发逻辑：
 * - 每个角色维护一个 ActorCueState，记录动画帧变化
 * - 当角色进入特定动作帧时，根据动作类型播发对应音效
 * - 脚步声区分左脚和右脚，确保交替播放
 * - 怪物叫声通过伪随机数生成器控制频率
 *
 * @note 此模块与 Delphi 客户端的音效触发逻辑一致，
 *       音效规则定义在 legacy_sound_rules.hpp 中
 */

#pragma once

#include <cstdint>
#include <unordered_map>

#include "assets/asset_manager.hpp"
#include "game/game_state.hpp"

namespace mir2::client {

class AnimationManager;
class AudioService;

/**
 * @class LegacyAudioCueTracker
 * @brief 旧版音效提示追踪器 —— 根据角色动画状态触发音效
 */
class LegacyAudioCueTracker {
 public:
  /**
   * @struct ActorCueState
   * @brief 单个角色的音效追踪状态
   */
  struct ActorCueState {
    bool seen{false};                        ///< 是否已初始化
    bool last_dead{false};                   ///< 上一帧是否死亡
    std::uint64_t last_action_started_ms{0}; ///< 上次动作开始时间
    std::uint64_t last_move_started_ms{0};   ///< 上次移动开始时间
    int last_action_local_frame{-1};         ///< 上次动作的本地帧号
    int last_move_local_frame{-1};           ///< 上次移动的本地帧号
    bool left_foot_played{false};            ///< 左脚音效是否已播放
    bool right_foot_played{false};           ///< 右脚音效是否已播放
    bool weapon_played{false};               ///< 武器音效是否已播放
    bool death_action_active{false};         ///< 死亡动作是否激活
    bool die2_played{false};                 ///< 第二段死亡音效是否已播放
  };

  /// 重置所有追踪状态（场景切换时调用）
  void reset();
  /// 更新音效追踪（每帧调用，检测动画变化并触发音效）
  void update(const WorldViewState& world, AnimationManager& animation,
              const MapDocument* map, AudioService& audio,
              std::uint64_t now_ms);
  /// 检查是否应该播放下一个怪物普通音效（伪随机）
  bool next_monster_normal_sound_hit();

 private:
  std::unordered_map<std::uint64_t, ActorCueState> actors_{};  ///< 角色音效追踪状态表
  std::uint32_t monster_normal_rng_{0x4D495232U};  ///< 怪物音效伪随机数种子（"MIR2" 的 ASCII）
};

}  // namespace mir2::client
