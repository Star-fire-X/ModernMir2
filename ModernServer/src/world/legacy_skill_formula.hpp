/**
 * @file legacy_skill_formula.hpp
 * @brief 技能伤害/效果公式计算头文件
 * @details 定义了传奇3传统技能系统的各类计算公式，包括：
 *          - Delphi兼容的四舍五入函数
 *          - 武器技能判定
 *          - 技能点数消耗计算
 *          - 魔法攻击力计算（最小/最大功率）
 *          - 物理攻击力计算（含幸运/诅咒系统）
 *          - 魔法防御伤害减免计算
 *          - 反魔法穿透判定
 *          - 延迟时间转服务器滴答数
 */

#pragma once

#include <cstdint>

#include "config/models.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

/**
 * @brief Delphi兼容的四舍五入函数
 * @details 使用 std::nearbyint 实现银行家舍入法（与Delphi的Round函数兼容）。
 * @param value 待舍入的双精度值
 * @return 四舍五入后的整数值
 */
[[nodiscard]] std::int32_t delphi_round(double value);

/**
 * @brief 判断技能ID是否为武器技能（剑法类）
 * @details 传奇3中部分技能归类为武器技能，攻击命中时触发特殊效果。
 *          包括：基本剑术(3)、攻杀剑术(4)、刺杀剑术(7)、
 *          半月弯刀(12)、莲月(25)、怒斩天下(26)、十方斩(27)、
 *          以及魔法技能ID 34-37。
 * @param magic_id 技能ID
 * @return true 是武器技能，false 不是
 */
[[nodiscard]] bool legacy_is_sword_skill(std::int32_t magic_id);

/**
 * @brief 计算技能每次使用的魔法消耗（Spell Point）
 * @details 公式：round(spell / (max_train_level + 1) * (level + 1)) + def_spell
 *          随着技能等级提升，消耗的魔法值逐渐增加。
 * @param magic 技能定义
 * @param level 当前技能等级（0-2）
 * @return 魔法消耗值
 */
[[nodiscard]] std::int32_t legacy_spell_point(const LegacyMagicDefinition& magic,
                                              std::int32_t level);

/**
 * @brief 计算最小魔法攻击力（非随机版本）
 * @details 公式：min_power + max(random_value, 0)
 *          返回基础最小功率加上随机附加值。
 * @param magic 技能定义
 * @param random_value 随机附加值（0 ~ max_power - min_power）
 * @return 最小魔法攻击力
 */
[[nodiscard]] std::int32_t legacy_mpow(const LegacyMagicDefinition& magic,
                                       std::int32_t random_value);

/**
 * @brief 计算最小魔法攻击力（随机版本）
 * @details 自动生成随机值后调用非随机版本。
 * @param magic 技能定义
 * @param random 随机数生成器
 * @return 最小魔法攻击力
 */
[[nodiscard]] std::int32_t legacy_mpow(const LegacyMagicDefinition& magic,
                                       LegacyRandom& random);

/**
 * @brief 计算魔法攻击力（非随机版本）
 * @details 公式：round(power / (max_train_level + 1) * (level + 1)) + def_min_power + max(random_value, 0)
 *          技能等级越高，基础伤害越大。
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率值（magic.power）
 * @param random_value 随机附加值
 * @return 魔法攻击力
 */
[[nodiscard]] std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                                        std::int32_t power, std::int32_t random_value);

/**
 * @brief 计算魔法攻击力（随机版本）
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率值
 * @param random 随机数生成器
 * @return 魔法攻击力
 */
[[nodiscard]] std::int32_t legacy_power(const LegacyMagicDefinition& magic, std::int32_t level,
                                        std::int32_t power, LegacyRandom& random);

/**
 * @brief 计算特殊技能攻击力（1/3分割公式）
 * @details 传奇3中部分技能使用此特殊公式：
 *          p1 = power / 3, p2 = power - p1
 *          result = round(p1 + p2 / (max_train_level + 1) * (level + 1)) + def_min_power + max(random_value, 0)
 * @param magic 技能定义
 * @param level 技能等级
 * @param power 基础功率值
 * @param random_value 随机附加值
 * @return 特殊技能攻击力
 */
[[nodiscard]] std::int32_t legacy_power13(const LegacyMagicDefinition& magic, std::int32_t level,
                                          std::int32_t power, std::int32_t random_value);

/**
 * @brief 根据多次随机掷骰结果计算物理攻击力
 * @details 传奇3的物理伤害计算核心函数：
 *          - 幸运值>0：有概率触发最大伤害（gate=0时），否则在0~ranval间随机
 *          - 幸运值<0（诅咒）：gate=0时触发最小伤害
 *          - 幸运值=0：标准随机
 *
 *          此版本用于预计算（如显示面板），需要外部传入随机结果。
 * @param damage 基础伤害值
 * @param ranval 伤害上限范围
 * @param luck 幸运值（正=幸运，负=诅咒）
 * @param gate_roll 门控随机值（0=触发门控）
 * @param power_roll 功率随机值
 * @return 最终物理伤害值
 */
[[nodiscard]] std::int32_t legacy_attack_power_from_rolls(std::int32_t damage,
                                                          std::int32_t ranval,
                                                          std::int32_t luck,
                                                          std::int32_t gate_roll,
                                                          std::int32_t power_roll);

/**
 * @brief 计算物理攻击力（随机版本）
 * @details 运行时使用的物理伤害计算，内部生成随机值：
 *          - 幸运>0：1/(10-luck) 概率触发最大伤害
 *          - 幸运<0（诅咒）：1/(10+luck) 概率触发最小伤害
 * @param damage 基础伤害
 * @param ranval 伤害范围上限
 * @param luck 幸运值
 * @param random 随机数生成器
 * @return 最终物理伤害
 */
[[nodiscard]] std::int32_t legacy_attack_power(std::int32_t damage, std::int32_t ranval,
                                               std::int32_t luck, LegacyRandom& random);

/**
 * @brief 计算魔法攻击命中目标后的最终伤害（考虑魔法防御）
 * @details 伤害减免流程：
 *          1. 计算目标魔法防御值：mac_min + random(0, mac_max - mac_min)
 *          2. 基础伤害减去防御值
 *          3. 如果目标是不死系，增加对不死系的额外伤害
 *          4. 如果目标有魔法盾（Magic Bubble），按比例调整伤害
 *             公式：damage * (bubble_level + 2) * 8 / 100
 * @param damage 原始魔法伤害
 * @param mac_min 目标最小魔法防御
 * @param mac_max 目标最大魔法防御
 * @param armor_random 魔法防御随机值
 * @param undead_target 目标是否为不死系
 * @param undead_power 对不死系的额外攻击力
 * @param magic_bubble 目标是否有魔法盾
 * @param magic_bubble_level 魔法盾等级
 * @return 最终实际伤害
 */
[[nodiscard]] std::int32_t legacy_mag_struck_damage(std::int32_t damage, std::int32_t mac_min,
                                                    std::int32_t mac_max,
                                                    std::int32_t armor_random,
                                                    bool undead_target,
                                                    std::int32_t undead_power,
                                                    bool magic_bubble,
                                                    std::int32_t magic_bubble_level);

/**
 * @brief 反魔法穿透判定
 * @details 判断魔法是否穿透目标的魔法躲避（AntiMagic）属性。
 *          anti_magic <= random10 时穿透成功（即目标未能躲避）。
 *          random10 是0-9的随机值，代表10%一档的判定。
 * @param anti_magic 目标的魔法躲避值
 * @param random10 10面骰子结果（0-9）
 * @return true 魔法穿透（命中），false 被躲避
 */
[[nodiscard]] bool legacy_anti_magic_pass(std::int32_t anti_magic, std::int32_t random10);

/**
 * @brief 将延迟时间（毫秒）转换为服务器滴答数
 * @details 向上取整除法：ceil(value_ms / tick_ms)。
 *          用于将时间单位的延迟转换为服务器心跳周期数。
 * @param value_ms 延迟时间（毫秒）
 * @param tick_ms 每个服务器滴答的时间（毫秒）
 * @return 服务器滴答数，至少为1（如果value_ms > 0）
 */
[[nodiscard]] std::uint64_t legacy_delay_ms_to_ticks(std::uint32_t value_ms,
                                                     std::uint32_t tick_ms);

}  // namespace mir2
