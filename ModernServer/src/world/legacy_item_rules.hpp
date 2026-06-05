/**
 * @file legacy_item_rules.hpp
 * @brief 物品使用规则和验证头文件
 * @details 定义了传奇3传统物品系统的核心规则和验证函数。
 *          包括物品插槽映射、装备条件检查、特殊物品标识、
 *          升级计算、怪物掉落随机属性生成等功能。
 *          兼容传奇3原版的物品属性编码格式（packed low/high）。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_random.hpp"

namespace mir2 {

// @{ 特殊物品属性标志常量
/** @brief 物品不可解下标志（desc[7]锁定） */
constexpr std::int32_t kLegacyItemUnableTakeOff = 0x02;
/** @brief 物品永不掉落标志 */
constexpr std::int32_t kLegacyItemNeverTakeOff = 0x04;
/** @brief 物品死亡时损坏标志 */
constexpr std::int32_t kLegacyItemDieAndBreak = 0x08;
/** @brief 物品永不丢失标志 */
constexpr std::int32_t kLegacyItemNeverLose = 0x10;
// @}

// @{ 特殊戒指物品ID常量（通过shape值识别）
constexpr std::int32_t kLegacyRingTransparentItem = 111;     ///< 隐身戒指
constexpr std::int32_t kLegacyRingMakeStoneItem = 113;       ///< 护身戒指（石化）
constexpr std::int32_t kLegacyRingRevivalItem = 114;         ///< 复活戒指
constexpr std::int32_t kLegacyRingMagicShieldItem = 118;     ///< 魔法盾戒指
constexpr std::int32_t kLegacyRingManaToHealthItem = 133;    ///< 魔血戒指（MP转HP）
constexpr std::int32_t kLegacyBraceletManaToHealthItem = 134; ///< 魔血手镯（MP转HP）
constexpr std::int32_t kLegacyNecklaceManaToHealthItem = 135; ///< 魔血项链（MP转HP）
constexpr std::int32_t kLegacyRingSuckHealthItem = 136;      ///< 嗜血戒指（吸血）
constexpr std::int32_t kLegacyBraceletSuckHealthItem = 137;  ///< 嗜血手镯（吸血）
constexpr std::int32_t kLegacyNecklaceSuckHealthItem = 138;  ///< 嗜血项链（吸血）
// @}

/**
 * @brief 根据标准模式（std_mode）解析装备槽位
 * @details 映射物品的标准模式到装备槽位索引：
 *          5/6 -> 武器, 10/11 -> 衣服, 15 -> 头盔,
 *          19/20/21 -> 项链, 22/23 -> 戒指, 24/25/26 -> 手镯,
 *          30 -> 右手, 52 -> 靴子, 53 -> 护身符, 54 -> 腰带
 * @param std_mode 物品的标准模式
 * @return 装备槽位索引，无法识别返回 -1
 */
std::int32_t legacy_resolve_slot_from_std_mode(std::int32_t std_mode);

/**
 * @brief 检查物品是否适合放入指定槽位
 * @details 同时检查标准模式和显式声明的 equip_slot。
 *          如果物品有 equip_slot 设置（>=0），则只允许该槽位。
 * @param item_config 物品配置
 * @param slot 槽位索引
 * @return true 适合，false 不适合
 */
bool legacy_item_fits_slot(const ItemConfig& item_config, std::int32_t slot);

/**
 * @brief 判断槽位是否使用手部重量
 * @details 武器和右手槽位使用手部重量（hand_weight），
 *          其他槽位使用穿戴重量（wear_weight）。
 * @param slot 槽位索引
 * @return true 使用手部重量，false 使用穿戴重量
 */
bool legacy_slot_uses_hand_weight(std::size_t slot);

/**
 * @brief 判断物品是否可解下
 * @details 检查物品属性标志（item_desc）和 desc[7]。
 *          某些std_mode的物品如果desc[7]非零则不可解下。
 * @param item_config 物品配置（可为nullptr）
 * @param item 用户物品数据
 * @return true 可解下，false 不可解下
 */
bool legacy_item_can_take_off(const ItemConfig* item_config, const LegacyUserItem& item);

/**
 * @brief 判断物品是否为消耗品
 * @details 根据 HP/MP 恢复量判断，hp_add > 0 或 mp_add > 0 即为消耗品。
 * @param item_config 物品配置
 * @return true 是消耗品，false 不是
 */
bool legacy_item_is_consumable(const ItemConfig& item_config);

/**
 * @brief 判断物品是否为技能书
 * @details std_mode == 4 的物品为技能书。
 * @param item_config 物品配置
 * @return true 是技能书，false 不是
 */
bool legacy_item_is_magic_book(const ItemConfig& item_config);

/**
 * @brief 判断物品是否为卷轴
 * @details std_mode == 31 的物品为卷轴。
 * @param item_config 物品配置
 * @return true 是卷轴，false 不是
 */
bool legacy_item_is_scroll(const ItemConfig& item_config);

/**
 * @brief 判断物品是否为可解绑的包裹
 * @details 如果物品有 unbind_item（解绑后获得的物品）且 unbind_count > 0，则为解绑包裹。
 * @param item_config 物品配置
 * @return true 是可解绑包裹，false 不是
 */
bool legacy_item_is_unbind_bundle(const ItemConfig& item_config);

/**
 * @brief 判断物品是否为武器升级材料
 * @details 特定的 std_mode（19-24, 26, 52-54）用于武器升级。
 * @param item_config 物品配置
 * @return true 是升级材料，false 不是
 */
bool legacy_is_upgrade_weapon_stuff(const ItemConfig& item_config);

/**
 * @brief 判断物品是否为祝福油
 * @details std_mode == 3 && shape == 4，或者 scroll_kind 为 "blessed_oil"。
 * @param item_config 物品配置
 * @return true 是祝福油，false 不是
 */
bool legacy_is_blessed_oil(const ItemConfig& item_config);

/**
 * @brief 获取卷轴类型
 * @details 根据 scroll_kind 配置或名称启发式判断卷轴类型：
 *          名称含 "random" -> random，含 "escape" -> escape，
 *          含 "town"/"return"/"recall" -> town
 *          无匹配时默认 town。
 * @param item_config 物品配置
 * @return 卷轴类型名称字符串
 */
std::string legacy_scroll_kind(const ItemConfig& item_config);

/**
 * @brief 计算物品升级后的配置属性
 * @details 根据用户物品的 desc[] 数组中的升级加成，叠加到基础物品配置上。
 *          不同的 std_mode 对应不同的属性加成规则：
 *          - 武器（5,6）：DC/MC/SC/AC/MAC/special_pwr
 *          - 衣服（10,11）：AC/MAC/DC/MC/SC
 *          - 饰品（15,19-26）：AC/MAC/DC/MC/SC/need/need_level
 * @param item_config 基础物品配置
 * @param user_item 用户物品数据（含升级信息）
 * @return 升级后的物品配置
 */
ItemConfig legacy_upgraded_item_config(const ItemConfig& item_config,
                                       const LegacyUserItem& user_item);

/**
 * @brief 为怪物掉落物品随机生成升级属性
 * @details 模拟传奇3怪物掉落时物品的随机升级机制。
 *          不同类型的物品有不同的升级概率和属性范围：
 *          - 武器：概率获得DC/MC/SC加成，幸运值、特殊属性等
 *          - 衣服/头盔/项链/手镯/戒指：概率获得AC/MAC/DC/MC/SC加成
 *          - 各类型均有耐久度加成
 *          升级概率从1/15到1/60不等，通过多次随机判定实现。
 * @param item_config 物品配置
 * @param user_item 用户物品数据（输出参数，升级属性将被修改）
 * @param random 随机数生成器
 */
void legacy_random_upgrade_monster_drop_item(const ItemConfig& item_config,
                                             LegacyUserItem& user_item,
                                             LegacyRandom& random);

/**
 * @brief 为"未知物品"设置随机属性
 * @details 处理特殊类型未知物品（shape 130-132）的随机属性生成。
 *          这些物品在掉落时随机生成属性，属性数量和强度取决于多次随机结果。
 *          如果总加成值(sum)达标，还会生成特殊需求属性（need/need_level）。
 *          对应传奇3中的"神秘装备"系列：神秘戒指(130)、神秘手镯(131)、神秘头盔(132)。
 * @param item_config 物品配置
 * @param user_item 用户物品数据（输出参数）
 * @param random 随机数生成器
 */
void legacy_random_set_unknown_monster_drop_item(const ItemConfig& item_config,
                                                 LegacyUserItem& user_item,
                                                 LegacyRandom& random);

/**
 * @brief 检查角色是否能穿上指定物品
 * @details 全面检查装备可行性：
 *          1. 物品是否适合该槽位
 *          2. 职业、性别限制
 *          3. 等级限制
 *          4. 特殊需求（DC/MC/SC要求）
 *          5. 重量限制（手部重量/穿戴重量）
 * @param character 角色记录
 * @param item_config 物品配置（考虑升级加成）
 * @param user_item 用户物品数据
 * @param slot 目标装备槽位
 * @param current_wear_weight 当前穿戴重量
 * @param current_hand_weight 当前手部重量
 * @param old_slot_weight 原槽位物品重量（用于重量差计算）
 * @param reason 输出参数，存放失败原因文本
 * @return true 可以穿上，false 不能穿上
 */
bool legacy_can_take_on_item(const CharacterRecord& character,
                             const ItemConfig& item_config,
                             const LegacyUserItem& user_item,
                             std::int32_t slot,
                             std::int32_t current_wear_weight,
                             std::int32_t current_hand_weight,
                             std::int32_t old_slot_weight,
                             std::string* reason = nullptr);

}  // namespace mir2
