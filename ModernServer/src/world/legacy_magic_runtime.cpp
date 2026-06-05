/**
 * @file legacy_magic_runtime.cpp
 * @brief 魔法/技能运行时系统实现
 * @details 实现了技能书学习、技能训练升级和特殊能力检查功能。
 *          兼容传奇3原版的技能系统机制，包括技能书名称匹配、
 *          训练值递增、等级阈值升级、特殊能力解锁等。
 */

#include "world/legacy_magic_runtime.hpp"

#include <algorithm>
#include <string>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 生成比较键：小写并去除首尾空格
 * @param value 原始字符串
 * @return 标准化后的比较键
 */
std::string compare_key(std::string_view value) {
  return util::lower_copy(util::trim(std::string(value)));
}

/**
 * @brief 获取技能等级索引（限制在0-3范围内）
 * @param user_magic 用户技能信息
 * @return 等级索引 0-3
 */
std::int32_t magic_level_index(const LegacyUseMagicInfo& user_magic) {
  return std::clamp<std::int32_t>(user_magic.level, 0, 3);
}

}  // namespace

/**
 * @brief 判断技能是否为仅源技能
 * @details magic_id 34-37 对应刺杀剑术系列的四个技能等级，
 *          这些技能不能通过技能书学习，只能通过其他方式（如GM命令）获得。
 * @param magic_id 技能ID
 * @return true 仅源技能，false 非仅源技能
 */
bool legacy_magic_source_only(std::int32_t magic_id) {
  return magic_id >= 34 && magic_id <= 37;
}

/**
 * @brief 根据技能书名称查找技能配置
 * @details 遍历所有技能配置，匹配条件：
 *          1. legacy_present 为 true（兼容传奇3的技能）
 *          2. 技能名称非空
 *          3. 名称与技能书名称匹配（不区分大小写、忽略首尾空格）
 *          如果有多个同名技能（如不同等级的同一个技能），返回ID最小的。
 * @param magic_configs 技能配置映射表（key = magic_id）
 * @param book_name 技能书名称
 * @return 技能配置指针，未找到返回 nullptr
 */
const MagicConfig* legacy_find_magic_by_book_name(
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
    std::string_view book_name) {
  const auto wanted = compare_key(book_name);
  if (wanted.empty()) {
    return nullptr;
  }
  const MagicConfig* best = nullptr;
  for (const auto& [magic_id, magic] : magic_configs) {
    // 过滤无效和非传奇兼容的技能
    if (!magic.legacy.legacy_present || magic.name.empty()) {
      continue;
    }
    if (compare_key(magic.name) != wanted) {
      continue;
    }
    // 返回ID最小的（通常是基础等级）
    if (best == nullptr || magic_id < best->id) {
      best = &magic;
    }
  }
  return best;
}

/**
 * @brief 获取技能书阅读结果的状态名称
 * @param status 状态枚举值
 * @return 状态名称的C风格字符串
 */
const char* legacy_read_book_status_name(LegacyReadBookStatus status) {
  switch (status) {
    case LegacyReadBookStatus::learned:
      return "learned";
    case LegacyReadBookStatus::invalid_item:
      return "invalid_item";
    case LegacyReadBookStatus::unknown_magic:
      return "unknown_magic";
    case LegacyReadBookStatus::source_only:
      return "source_only";
    case LegacyReadBookStatus::duplicate:
      return "duplicate";
    case LegacyReadBookStatus::job_mismatch:
      return "job_mismatch";
    case LegacyReadBookStatus::level_too_low:
      return "level_too_low";
    case LegacyReadBookStatus::no_slot:
      return "no_slot";
  }
  return "unknown";
}

/**
 * @brief 阅读技能书（学习技能）
 * @details 完整的学习验证流程：
 *
 *          1. 基础验证：确认物品是技能书（std_mode == 4）
 *          2. 查找技能：通过书名在配置中查找对应技能
 *          3. 源技能检查：magic_id 34-37 不能通过书学习
 *          4. 重复检查：玩家是否已学会该技能
 *          5. 职业检查：技能是否有职业要求且与玩家匹配
 *          6. 等级检查：玩家等级是否达到技能需求等级（need_level[0]）
 *          7. 槽位检查：玩家技能栏是否有空位
 *
 *          所有条件满足后，调用 player.add_legacy_magic() 添加技能。
 *
 * @param player 玩家对象（输出参数，可能被修改）
 * @param book 技能书物品配置
 * @param magic_configs 所有技能配置的映射表
 * @return 学习结果，包含状态和技能ID
 */
LegacyReadBookResult legacy_read_magic_book(
    Player& player, const ItemConfig& book,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  // 1. 验证物品类型
  if (book.std_mode != 4) {
    return {LegacyReadBookStatus::invalid_item, 0};
  }

  // 2. 查找技能配置
  const auto* magic = legacy_find_magic_by_book_name(magic_configs, book.name);
  if (magic == nullptr || !magic->legacy.legacy_present) {
    return {LegacyReadBookStatus::unknown_magic, 0};
  }

  const auto magic_id = magic->id;
  // 3. 检查是否为仅源技能
  if (legacy_magic_source_only(magic_id)) {
    return {LegacyReadBookStatus::source_only, magic_id};
  }
  // 4. 检查重复
  if (player.learned_magic(magic_id) != nullptr) {
    return {LegacyReadBookStatus::duplicate, magic_id};
  }
  // 5. 检查职业匹配（job=99 表示全职业可用）
  if (magic->legacy.job != 99 && magic->legacy.job != player.character().job) {
    return {LegacyReadBookStatus::job_mismatch, magic_id};
  }
  // 6. 检查等级要求
  if (player.character().ability.level < magic->legacy.need_level[0]) {
    return {LegacyReadBookStatus::level_too_low, magic_id};
  }
  // 7. 尝试添加技能到技能栏
  if (!player.add_legacy_magic(magic_id, '\0', 0, 0)) {
    return {LegacyReadBookStatus::no_slot, magic_id};
  }
  return {LegacyReadBookStatus::learned, magic_id};
}

/**
 * @brief 训练技能
 * @details 技能训练的核心逻辑：
 *
 *          1. 前置条件检查：
 *             - 技能配置必须有效（legacy_present）
 *             - 技能ID不能为0
 *             - 技能等级不能已满（level < 3）
 *
 *          2. 等级需求检查：
 *             - 当前技能等级对应的 need_level 必须 <= 玩家等级
 *
 *          3. 训练值增加：
 *             - fixed_train_amount > 0：使用固定值
 *             - fixed_train_amount == 0：随机1-3点
 *
 *          4. 升级检查：
 *             - 当前训练值 >= 阈值（max_train[level]）且
 *             - 未达到最大可训练等级（max_train_level）
 *             - 升级后调用 legacy_check_magic_special_ability()
 *
 * @param player 玩家对象（输出参数，用于检查等级和设置特殊能力）
 * @param user_magic 用户技能信息（输出参数，训练值和等级可能被修改）
 * @param magic 技能配置
 * @param random 随机数生成器
 * @param fixed_train_amount 固定训练增加值，0表示随机
 * @return 训练结果
 */
LegacyMagicTrainResult legacy_train_magic(Player& player, LegacyUseMagicInfo& user_magic,
                                          const MagicConfig& magic, LegacyRandom& random,
                                          std::int32_t fixed_train_amount) {
  LegacyMagicTrainResult result;
  result.magic_id = user_magic.magic_id;
  result.level = user_magic.level;
  result.cur_train = user_magic.cur_train;

  // 前置条件检查
  if (!magic.legacy.legacy_present || user_magic.magic_id == 0 || user_magic.level >= 3) {
    return result;
  }

  // 等级需求检查
  const auto level = magic_level_index(user_magic);
  if (player.character().ability.level < magic.legacy.need_level[level]) {
    return result;
  }

  // 增加训练值
  result.train_amount = fixed_train_amount > 0 ? fixed_train_amount : 1 + random.random(3);
  user_magic.cur_train += result.train_amount;
  result.trained = true;

  // 检查是否需要升级
  const auto max_train_level = std::clamp(magic.legacy.max_train_level, 0, 3);
  const auto threshold = std::max(magic.legacy.max_train[level], 0);
  if (user_magic.level < max_train_level && threshold > 0 && user_magic.cur_train >= threshold) {
    user_magic.cur_train -= threshold;
    ++user_magic.level;
    result.leveled_up = true;
    legacy_check_magic_special_ability(player, user_magic);
  }

  result.level = user_magic.level;
  result.cur_train = user_magic.cur_train;
  return result;
}

/**
 * @brief 检查技能特殊能力
 * @details 当技能升级时调用此函数检查是否解锁特殊被动能力。
 *          当前规则：
 *          - 心灵启示（magic_id=28）达到2级时，解锁查看目标血量百分比的能力
 *
 *          后续可在此函数中添加更多技能的特殊能力逻辑。
 * @param player 玩家对象（输出参数，可能设置特殊能力）
 * @param user_magic 用户技能信息
 */
void legacy_check_magic_special_ability(Player& player, const LegacyUseMagicInfo& user_magic) {
  // 心灵启示（magic_id=28）达到2级：解锁查看目标血量
  if (user_magic.magic_id == 28 && user_magic.level >= 2) {
    player.set_legacy_see_health_gauge(true);
  }
}

}  // namespace mir2
