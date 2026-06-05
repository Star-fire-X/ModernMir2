/**
 * @file legacy_skill_formula.cpp
 * @brief 技能伤害/效果公式计算实现
 * @details 实现了传奇3传统技能系统的全部计算公式，兼容Delphi原版的算法。
 *          包括物理攻击（含幸运/诅咒系统）、魔法攻击力、魔法防御减免、
 *          技能消耗、反魔法穿透判定等核心战斗公式。
 */

#include "world/legacy_skill_formula.hpp"

#include <algorithm>
#include <cmath>

namespace mir2 {

/**
 * @brief Delphi兼容的四舍五入函数
 * @details 使用 std::nearbyint 实现，行为与Delphi的 Round 函数一致
 *          （银行家舍入法：.5时舍入到最接近的偶数）。
 * @param value 待舍入的值
 * @return 四舍五入后的整数
 */
std::int32_t delphi_round(double value) {
  return static_cast<std::int32_t>(std::nearbyint(value));
}

/**
 * @brief 判断是否为武器技能（剑法类）
 * @details 武器技能在攻击命中时有额外的触发逻辑。
 *          包含技能：基本剑术(3)、攻杀剑术(4)、刺杀剑术(7)、
 *          半月弯刀(12)、莲月(25)、怒斩天下(26)、十方斩(27)、
 *          以及魔法技能中的几个剑法相关技能(34)。
 * @param magic_id 技能ID
 * @return true 是武器技能
 */
bool legacy_is_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 3:
    case 4:
    case 7:
    case 12:
    case 25:
    case 26:
    case 27:
    case 34:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 计算技能每次使用的魔法消耗
 * @details 公式推导：
 *          - spell / (max_train_level + 1)：每级增加的基础消耗
 *          - * (level + 1)：当前等级的总倍数
 *          - + def_spell：固定的额外消耗
 *          例如：spell=10, max_train_level=2, level=1, def_spell=5
 *          -> round(10/3 * 2) + 5 = round(6.67) + 5 = 7 + 5 = 12
 * @param magic 技能定义
 * @param level 当前等级（0-2）
 * @return 每次使用的魔法消耗值
 */
std::int32_t legacy_spell_point(const LegacyMagicDefinition& magic, std::int32_t level) {
  return delphi_round(static_cast<double>(magic.spell) /
                      static_cast<double>(magic.max_train_level + 1) *
                      static_cast<double>(level + 1)) +
         magic.def_spell;
}

/**
 * @brief 计算最小魔法攻击力（非随机版本）
 * @param magic 技能定义
 * @param random_value 随机附加值（通常范围：0 ~ max_power - min_power）
 * @return 最小魔法攻击力
 */
std::int32_t legacy_mpow(const LegacyMagicDefinition& magic, std::int32_t random_value) {
  return magic.min_power + std::max(random_value, 0);
}

/**
 * @brief 计算最小魔法攻击力（随机版本）
 * @param magic 技能定义
 * @param random 随机数生成器
 * @return 最小魔法攻击力
 */
std::int32_t legacy_mpow(const LegacyMagicDefinition& magic, LegacyRandom& random) {
  return legacy_mpow(magic, random.random(magic.max_power - magic.min_power));
}

/**
 * @brief 计算魔法攻击力（非随机版本）
 * @details 标准魔法伤害公式：
 *          - power / (max_train_level + 1) * (level + 1)：等级加成部分
 *          - def_min_power：基础最小值
 *          - max(random_value, 0)：随机波动部分
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率（通常为magic.power）
 * @param random_value 随机附加值
 * @return 魔法攻击力
 */
std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                          std::int32_t power, std::int32_t random_value) {
  return delphi_round(static_cast<double>(power) /
                      static_cast<double>(magic.max_train_level + 1) *
                      static_cast<double>(level + 1)) +
         magic.def_min_power + std::max(random_value, 0);
}

/**
 * @brief 计算魔法攻击力（随机版本）
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率
 * @param random 随机数生成器
 * @return 魔法攻击力
 */
std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                          std::int32_t power, LegacyRandom& random) {
  return legacy_power(magic, level, power,
                      random.random(magic.def_max_power - magic.def_min_power));
}

/**
 * @brief 计算特殊技能攻击力（1/3分割公式）
 * @details 特殊公式，将基础功率分为1/3和2/3两部分：
 *          - p1 = power / 3（固定部分）
 *          - p2 = power - p1（等级成长部分）
 *          适用于传奇3中部分特殊技能（如某些高级魔法）。
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率
 * @param random_value 随机附加值
 * @return 特殊技能攻击力
 */
std::int32_t legacy_power13(const LegacyMagicDefinition& magic, std::int32_t level,
                            std::int32_t power, std::int32_t random_value) {
  const auto p1 = static_cast<double>(power) / 3.0;
  const auto p2 = static_cast<double>(power) - p1;
  return delphi_round(p1 + p2 / static_cast<double>(magic.max_train_level + 1) *
                               static_cast<double>(level + 1) +
                      magic.def_min_power + std::max(random_value, 0));
}

/**
 * @brief 根据多次随机掷骰结果计算物理攻击力
 * @details 传奇3物理伤害的"多掷骰"系统：
 *
 *          当 luck > 0（幸运）：
 *          - gate_roll == 0：触发"幸运一击"，直接取最大值 damage + ranval
 *          - gate_roll != 0：普通幸运，取 damage + clamp(power_roll, 0, ranval)
 *
 *          当 luck <= 0（无幸运或诅咒）：
 *          - 标准计算：damage + clamp(power_roll, 0, ranval)
 *          - luck < 0 且 gate_roll == 0：触发"诅咒一击"，取最小值 damage
 *
 * @param damage 基础伤害（武器最小伤害）
 * @param ranval 伤害范围（最大-最小）
 * @param luck 幸运值（正=幸运增加最大伤害概率，负=诅咒增加最小伤害概率）
 * @param gate_roll 门控掷骰结果（0=触发门控）
 * @param power_roll 功率掷骰结果
 * @return 最终物理伤害
 */
std::int32_t legacy_attack_power_from_rolls(std::int32_t damage, std::int32_t ranval,
                                            std::int32_t luck, std::int32_t gate_roll,
                                            std::int32_t power_roll) {
  ranval = std::max(ranval, 0);
  if (luck > 0) {
    if (gate_roll == 0) {
      return damage + ranval; // 幸运一击：最大伤害
    }
    return damage + std::clamp(power_roll, 0, ranval);
  }

  auto result = damage + std::clamp(power_roll, 0, ranval);
  if (luck < 0 && gate_roll == 0) {
    result = damage; // 诅咒一击：最小伤害
  }
  return result;
}

/**
 * @brief 计算物理攻击力（随机版本）
 * @details 运行时使用随机数生成器计算物理伤害：
 *
 *          幸运系统（luck > 0）：
 *          - 概率 = 1/(10 - min(9, luck)) 触发最大伤害
 *          - 否则在 0~ranval 间随机
 *          例如：luck=5，概率=1/5=20% 触发最大伤害
 *
 *          诅咒系统（luck < 0）：
 *          - 概率 = 1/(10 + luck) 触发最小伤害
 *          - 否则标准随机
 *          例如：luck=-3，概率=1/7≈14% 触发最小伤害
 *
 * @param damage 基础伤害
 * @param ranval 伤害范围
 * @param luck 幸运/诅咒值
 * @param random 随机数生成器
 * @return 最终物理伤害
 */
std::int32_t legacy_attack_power(std::int32_t damage, std::int32_t ranval,
                                 std::int32_t luck, LegacyRandom& random) {
  ranval = std::max(ranval, 0);
  if (luck > 0) {
    // 幸运系统：有概率打出最大伤害
    if (random.random(10 - std::min(9, luck)) == 0) {
      return damage + ranval;
    }
    return damage + random.random(ranval + 1);
  }

  auto result = damage + random.random(ranval + 1);
  if (luck < 0) {
    // 诅咒系统：有概率打出最小伤害
    if (random.random(10 - std::max(0, -luck)) == 0) {
      result = damage;
    }
  }
  return result;
}

/**
 * @brief 计算魔法攻击命中目标后的最终伤害
 * @details 魔法伤害减免计算流程：
 *
 *          1. 防御计算：
 *             armor = mac_min + clamp(armor_random, 0, mac_max - mac_min)
 *             即在最小防御和最大防御之间随机取值
 *
 *          2. 基础伤害减免：damage = max(0, damage - armor)
 *
 *          3. 对不死系加成：damage += max(undead_power, 0)
 *             道士的治愈术、超度等技能对不死系有额外伤害
 *
 *          4. 魔法盾减免：
 *             damage = round(damage / 100 * (bubble_level + 2) * 8)
 *             魔法盾等级越高，减免效果越好
 *             level 0: damage * 16% (约84%减免)
 *             level 3: damage * 40% (约60%减免)
 *
 * @param damage 原始魔法伤害
 * @param mac_min 目标最小魔防
 * @param mac_max 目标最大魔防
 * @param armor_random 魔防随机值
 * @param undead_target 是否对不死系
 * @param undead_power 对不死系额外伤害
 * @param magic_bubble 是否有魔法盾
 * @param magic_bubble_level 魔法盾等级
 * @return 最终实际伤害
 */
std::int32_t legacy_mag_struck_damage(std::int32_t damage, std::int32_t mac_min,
                                      std::int32_t mac_max, std::int32_t armor_random,
                                      bool undead_target, std::int32_t undead_power,
                                      bool magic_bubble,
                                      std::int32_t magic_bubble_level) {
  // 计算魔法防御减免
  const auto armor_range = std::max(0, mac_max - mac_min);
  const auto armor = mac_min + std::clamp(armor_random, 0, armor_range);
  damage = std::max(0, damage - armor);

  // 对不死系额外伤害
  if (undead_target) {
    damage += std::max(undead_power, 0);
  }

  // 魔法盾减免
  if (damage > 0 && magic_bubble) {
    damage = delphi_round(static_cast<double>(damage) / 100.0 *
                          static_cast<double>(magic_bubble_level + 2) * 8.0);
  }
  return damage;
}

/**
 * @brief 反魔法穿透判定
 * @details 目标的魔法躲避（AntiMagic）属性决定魔法命中率。
 *          anti_magic <= random10 时命中。
 *          例如：anti_magic=3, random10=0-9
 *          - random10=0,1,2,3 时命中（40%）
 *          - random10=4,5,6,7,8,9 时躲避（60%）
 * @param anti_magic 魔法躲避值
 * @param random10 10面骰结果（0-9）
 * @return true 命中穿透，false 被躲避
 */
bool legacy_anti_magic_pass(std::int32_t anti_magic, std::int32_t random10) {
  return anti_magic <= random10;
}

/**
 * @brief 将延迟时间（毫秒）转换为服务器滴答数
 * @details 使用向上取整除法确保延迟时间至少为1个滴答：
 *          ticks = (value_ms + tick_ms - 1) / tick_ms
 *          这是标准的 Ceil Division 实现。
 * @param value_ms 延迟时间（毫秒）
 * @param tick_ms 每个滴答的毫秒数
 * @return 对应的滴答数，0输入返回0
 */
std::uint64_t legacy_delay_ms_to_ticks(std::uint32_t value_ms, std::uint32_t tick_ms) {
  if (value_ms == 0 || tick_ms == 0) {
    return 0;
  }
  return std::max<std::uint64_t>(1, (static_cast<std::uint64_t>(value_ms) +
                                     static_cast<std::uint64_t>(tick_ms) - 1) /
                                        static_cast<std::uint64_t>(tick_ms));
}

}  // namespace mir2
