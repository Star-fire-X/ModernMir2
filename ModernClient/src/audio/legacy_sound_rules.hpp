/**
 * @file legacy_sound_rules.hpp
 * @brief 旧版音效规则 —— 根据游戏事件确定应播放的音效 ID
 *
 * @details 定义旧版传奇客户端中所有音效的选择规则，包括：
 *          - UI 点击音效（石头/玻璃/普通三种类型）
 *          - 物品相关音效（点击、使用）
 *          - 角色脚步音效（根据地砖类型和行走/跑步状态）
 *          - 人类武器挥动音效（根据武器类型）
 *          - 人类攻击额外音效（根据攻击类型和性别）
 *          - 受击音效（武器碰撞声、身体碰撞声、叫声）
 *          - 死亡音效（男女不同）
 *          - 怪物音效（根据外观选择不同的动作音效偏移）
 *          - 魔法音效（根据魔法类型和阶段：开始/发射/爆炸）
 *
 * 音效 ID 对应的实际 WAV 文件由 AudioIdMapping 提供。
 *
 * @note 所有音效规则必须与 Delphi 客户端完全一致，
 *       以确保老玩家听到相同的音效反馈
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "assets/asset_manager.hpp"
#include "audio/sound_constants.hpp"
#include "game/game_state.hpp"

namespace mir2::client {

/**
 * @enum LegacyClickSound
 * @brief UI 点击音效类型
 */
enum class LegacyClickSound {
  none,    ///< 无音效
  stone,   ///< 石头质感的点击声
  glass,   ///< 玻璃质感的点击声
  normal,  ///< 普通点击声
};

/**
 * @enum LegacyMagicSoundPhase
 * @brief 魔法音效阶段
 */
enum class LegacyMagicSoundPhase {
  start,      ///< 开始施法
  fire,       ///< 发射/飞行
  explosion,  ///< 爆炸/命中
};

/// 获取 UI 点击音效 ID
[[nodiscard]] std::optional<int> legacy_click_sound_id(LegacyClickSound sound);
/// 根据物品品质和名称获取点击音效 ID
[[nodiscard]] int item_click_sound_id(std::uint8_t std_mode, std::string_view name);
/// 根据物品品质获取使用音效 ID（如喝药声）
[[nodiscard]] std::optional<int> item_use_sound_id(std::uint8_t std_mode);
/// 根据角色所在地砖类型和行走/跑步状态获取脚步音效 ID
[[nodiscard]] int footstep_sound_id(const MapCell* cell, bool running, bool right_foot);
/// 根据武器特征值获取武器挥动音效 ID
[[nodiscard]] int human_weapon_sound_id(int weapon_feature);
/// 获取人类攻击的额外音效 ID 列表（如技能特殊音效）
[[nodiscard]] std::vector<int> legacy_human_attack_extra_sound_ids(std::uint16_t legacy_ident,
                                                                   int sex);
/// 获取受击时的武器碰撞音效 ID
[[nodiscard]] std::optional<int> human_struck_weapon_sound_id(int attacker_weapon_feature);
/// 获取受击时的身体碰撞音效 ID（根据防御者服装和攻击者武器）
[[nodiscard]] int human_struck_body_sound_id(int defender_dress_feature,
                                             int attacker_weapon_feature);
/// 获取受击时的人声叫声音效 ID（男女不同）
[[nodiscard]] int human_struck_vocal_sound_id(int sex);
/// 获取死亡音效 ID（男女不同）
[[nodiscard]] int human_die_sound_id(int sex);
/// 获取怪物音效 ID（根据外观和音效偏移类型）
[[nodiscard]] int monster_sound_id(int appearance, MonsterSoundOffset offset);
/// 获取魔法音效 ID（根据魔法序号和音效阶段）
[[nodiscard]] int magic_sound_id(int magic_serial, LegacyMagicSoundPhase phase);
/// 获取角色当前的本地动画帧号（用于音效触发判断）
[[nodiscard]] int actor_local_frame_for_sound(const ActorState& actor,
                                              const ActorRenderPose& pose);

}  // namespace mir2::client
