/**
 * @file legacy_magic_runtime.hpp
 * @brief 魔法/技能运行时系统头文件
 * @details 定义了技能书学习、技能训练升级、特殊能力检查等运行时功能。
 *          兼容传奇3原版的技能系统，包括技能书识别、学习验证、
 *          训练值增长、等级提升和特殊能力解锁。
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "config/models.hpp"
#include "world/game_object.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

/**
 * @enum LegacyReadBookStatus
 * @brief 阅读技能书结果状态枚举
 * @details 定义学习技能书时的各种可能结果。
 */
enum class LegacyReadBookStatus {
  learned,        ///< 学习成功
  invalid_item,   ///< 物品不是技能书（std_mode != 4）
  unknown_magic,  ///< 未找到对应的技能配置
  source_only,    ///< 仅作为技能源（magic_id 34-37，不可学习）
  duplicate,      ///< 已学会该技能
  job_mismatch,   ///< 职业不匹配
  level_too_low,  ///< 等级不足
  no_slot         ///< 技能栏已满
};

/**
 * @struct LegacyReadBookResult
 * @brief 阅读技能书结果
 * @details 包含学习结果状态和关联的技能ID。
 */
struct LegacyReadBookResult {
  LegacyReadBookStatus status{LegacyReadBookStatus::invalid_item}; ///< 学习结果状态
  std::int32_t magic_id{0}; ///< 关联的技能ID
};

/**
 * @struct LegacyMagicTrainResult
 * @brief 技能训练结果
 * @details 描述一次技能训练的结果，包括是否成功、是否升级、
 *          当前等级、训练值变化等详细信息。
 */
struct LegacyMagicTrainResult {
  bool trained{false};        ///< 是否成功训练（增加了训练值）
  bool leveled_up{false};     ///< 是否升级
  std::int32_t magic_id{0};   ///< 技能ID
  std::int32_t level{0};      ///< 训练后的技能等级
  std::int32_t cur_train{0};  ///< 训练后的当前训练值
  std::int32_t train_amount{0}; ///< 本次训练增加的值
};

/**
 * @brief 判断技能是否为仅源技能
 * @details magic_id 34-37 为仅源技能（如刺杀剑术等），
 *          不能通过技能书学习获得，只能通过其他方式获得。
 * @param magic_id 技能ID
 * @return true 是仅源技能，false 不是
 */
[[nodiscard]] bool legacy_magic_source_only(std::int32_t magic_id);

/**
 * @brief 根据技能书名称查找对应的技能配置
 * @details 遍历所有技能配置，匹配技能书名称（不区分大小写）。
 *          如果有多个同名技能，返回ID最小的那个。
 * @param magic_configs 技能配置映射表
 * @param book_name 技能书名称
 * @return 匹配的技能配置指针，未找到返回 nullptr
 */
[[nodiscard]] const MagicConfig* legacy_find_magic_by_book_name(
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
    std::string_view book_name);

/**
 * @brief 获取阅读技能书结果的状态名称
 * @param status 状态枚举值
 * @return 状态名称字符串
 */
[[nodiscard]] const char* legacy_read_book_status_name(LegacyReadBookStatus status);

/**
 * @brief 阅读技能书（学习技能）
 * @details 完整的学习流程：
 *          1. 验证物品是技能书（std_mode == 4）
 *          2. 通过书名查找对应的技能配置
 *          3. 检查技能是否为仅源技能
 *          4. 检查是否已学会
 *          5. 检查职业是否匹配
 *          6. 检查角色等级是否满足要求
 *          7. 尝试添加到技能栏
 * @param player 玩家对象
 * @param book 技能书物品配置
 * @param magic_configs 所有技能配置的映射表
 * @return 学习结果
 */
[[nodiscard]] LegacyReadBookResult legacy_read_magic_book(
    Player& player, const ItemConfig& book,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs);

/**
 * @brief 训练技能
 * @details 技能训练逻辑：
 *          1. 验证技能可训练（非满级、合法技能）
 *          2. 检查角色等级是否满足当前技能等级的继续训练需求
 *          3. 计算本次训练增加值（固定值或随机1-3）
 *          4. 更新当前训练值
 *          5. 检查是否达到升级阈值，升级时触发特殊能力检查
 * @param player 玩家对象
 * @param user_magic 用户技能信息（输出参数，会被修改）
 * @param magic 技能配置
 * @param random 随机数生成器
 * @param fixed_train_amount 固定训练增加值（0表示使用随机值）
 * @return 训练结果
 */
[[nodiscard]] LegacyMagicTrainResult legacy_train_magic(Player& player,
                                                        LegacyUseMagicInfo& user_magic,
                                                        const MagicConfig& magic,
                                                        LegacyRandom& random,
                                                        std::int32_t fixed_train_amount = 0);

/**
 * @brief 检查技能特殊能力
 * @details 当技能升级时调用，检查是否解锁特殊能力。
 *          当前实现：
 *          - magic_id=28（心灵启示）且等级>=2时，解锁查看目标血量能力
 * @param player 玩家对象
 * @param user_magic 用户技能信息
 */
void legacy_check_magic_special_ability(Player& player, const LegacyUseMagicInfo& user_magic);

}  // namespace mir2
