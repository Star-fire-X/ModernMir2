/**
 * @file game_object.cpp
 * @brief 游戏对象核心实现文件
 * @details 该文件实现了 mir2 命名空间下所有游戏实体类的核心逻辑，包括：
 *          - 匿名命名空间中的各类辅助函数（物品查询、数值钳位、状态位操作等）
 *          - MapContext 上下文方法（数据包发送、审计、持久化、跨地图通信）
 *          - GameObject 基类（构造函数、邮件处理、tick 处理、位置管理）
 *          - LegacyBuffContainer Buff 容器（激活/刷新、查询、清除、过期处理）
 *          - Player 玩家类（核心部分，涵盖背包/装备/仓库/金币管理、战斗属性计算、
 *            伤害/治疗/状态效果、限速防作弊、Buff/毒药、特殊攻击、生命周期管理）
 *          - Monster 怪物类（属性管理、AI 调度、战斗/仇恨、状态效果、宠物系统）
 *          - Npc NPC/商人类（服务类型判断、商品管理、价格管理、武器升级）
 *          - EventObject 事件对象类（火墙、圣言帷幕等地图事件）
 *
 *         该文件是游戏服务器的核心数据层，处理所有游戏实体的状态变更逻辑。
 *         所有方法实现均与头文件 game_object.hpp 中的声明对应。
 *
 * @note 该文件约 3078 行，是项目中最大的源文件之一。
 *       逻辑分组按：辅助函数 -> 上下文 -> 基类 -> Buff容器 -> Player -> Monster -> Npc -> EventObject
 * @see game_object.hpp
 */

#include "world/game_object.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#include "util/string_utils.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/legacy_map_environment.hpp"
#include "world/legacy_skill_formula.hpp"

namespace mir2 {

namespace {

/** ==================== 辅助函数：物品查询与基础工具 ==================== */

/**
 * @brief 从物品配置表中查找指定索引的物品配置
 * @param item_configs 物品配置表（索引 -> 配置的映射）
 * @param item_index 物品索引
 * @return 找到则返回配置指针，否则返回 nullptr
 */
const ItemConfig* find_item_config(const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   std::int32_t item_index) {
  const auto it = item_configs.find(item_index);
  return it != item_configs.end() ? &it->second : nullptr;
}

/**
 * @brief 获取物品的显示名称
 * @param item 用户物品
 * @param item_configs 物品配置表
 * @return 物品名称字符串，如果配置中无名称则返回 "Item <index>"
 */
std::string item_name(const LegacyUserItem& item,
                      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr &&
                                                               !config->name.empty()) {
    return config->name;
  }
  return "Item " + std::to_string(item.index);
}

/**
 * @brief 获取物品的重量
 * @param item 用户物品
 * @param item_configs 物品配置表
 * @return 物品重量（非负整数），找不到配置则返回 0
 */
std::int32_t item_weight(const LegacyUserItem& item,
                         const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return std::max(config->weight, 0);
  }
  return 0;
}

/**
 * @brief 检查物品是否匹配指定的 make_index 和名称
 * @param item 待匹配的物品
 * @param make_index 要匹配的制造索引
 * @param expected_name 要匹配的名称（空字符串表示忽略名称检查）
 * @param item_configs 物品配置表
 * @return 匹配返回 true，否则 false（空物品也返回 false）
 * @note 名称匹配时不区分大小写
 */
bool matches_item(const LegacyUserItem& item, std::int32_t make_index, std::string_view expected_name,
                  const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item) || item.make_index != make_index) {
    return false;
  }
  return expected_name.empty() ||
         util::lower_copy(item_name(item, item_configs)) == util::lower_copy(expected_name);
}

/**
 * @brief 压缩物品数组，将空物品移除并移到末尾
 * @tparam N 数组大小
 * @param items 物品数组（将被原地修改）
 * @details 使用双指针法：将所有非空物品紧凑排列在数组前端，
 *          剩余位置填充为空物品。保持物品的相对顺序不变。
 */
template <std::size_t N>
void compact_legacy_items(std::array<LegacyUserItem, N>& items) {
  std::size_t write_index = 0;
  for (std::size_t read_index = 0; read_index < items.size(); ++read_index) {
    if (is_empty(items[read_index])) {
      continue;
    }
    if (write_index != read_index) {
      items[write_index] = items[read_index];
    }
    ++write_index;
  }
  for (; write_index < items.size(); ++write_index) {
    items[write_index] = LegacyUserItem{};
  }
}

/** ==================== 辅助函数：数值钳位与范围打包 ==================== */

/**
 * @brief 将 int32_t 值钳位到 uint16_t 范围 [0, 65535]
 */
std::uint16_t clamp_u16(std::int32_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0, 65535));
}

/**
 * @brief 将 int32_t 值钳位到 uint8_t 范围 [0, 255]
 */
std::uint8_t clamp_u8(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

/**
 * @brief 将 int32_t 值钳位到奴隶等级范围 [0, 6]
 */
std::uint8_t clamp_slave_level(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 6));
}

/**
 * @brief 从打包的 uint16_t 中提取低 8 位（最小值部分）
 * @param value 打包值（低字节为最小值，高字节为最大值）
 * @return 最小值
 */
std::int32_t packed_min(std::uint16_t value) { return static_cast<std::int32_t>(value & 0xffu); }

/**
 * @brief 从打包的 uint16_t 中提取高 8 位（最大值部分）
 * @param value 打包值（低字节为最小值，高字节为最大值）
 * @return 最大值
 */
std::int32_t packed_max(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

/**
 * @brief 计算打包范围的平均值
 * @param value 打包的[min, max]值
 * @return (min + max(max, min)) / 2
 */
std::int32_t packed_average(std::uint16_t value) {
  return (packed_min(value) + std::max(packed_min(value), packed_max(value))) / 2;
}

/**
 * @brief 将两个打包的范围值相加，并钳位到 [0, 255] 的字节范围
 * @param lhs 左侧打包值
 * @param rhs 右侧打包值
 * @return 相加后重新打包的 uint16_t
 * @details 最小值部分直接相加，最大值部分取各自区间宽度的和，
 *          最终高字节和低字节分别钳位到 0-255。
 */
std::uint16_t add_packed_range(std::uint16_t lhs, std::uint16_t rhs) {
  const auto low = packed_min(lhs) + packed_min(rhs);
  const auto high = std::max(packed_max(lhs), packed_min(lhs)) +
                    std::max(packed_max(rhs), packed_min(rhs));
  return static_cast<std::uint16_t>((std::clamp(high, 0, 255) << 8) |
                                    std::clamp(low, 0, 255));
}

/** ==================== 辅助函数：装备形状匹配与经验表 ==================== */

/**
 * @brief 检查物品形状是否为"魔法转生命"类型
 * @param shape 物品形状值
 * @return 如果是戒指/手镯/项链的魔法转生命形状则返回 true
 */
bool legacy_mana_to_health_shape(std::int32_t shape) {
  return shape == kLegacyRingManaToHealthItem ||
         shape == kLegacyBraceletManaToHealthItem ||
         shape == kLegacyNecklaceManaToHealthItem;
}

/**
 * @brief 检查物品形状是否为"吸血"类型
 * @param shape 物品形状值
 * @return 如果是戒指/手镯/项链的吸血形状则返回 true
 */
bool legacy_suck_health_shape(std::int32_t shape) {
  return shape == kLegacyRingSuckHealthItem ||
         shape == kLegacyBraceletSuckHealthItem ||
         shape == kLegacyNecklaceSuckHealthItem;
}

/**
 * @brief 检查特殊装备形状是否匹配指定的装备槽位
 * @param slot 装备槽位
 * @param shape 物品形状值
 * @return 形状与槽位匹配返回 true
 * @details 用于验证"魔法转生命"和"吸血"类装备是否正确穿戴在对应的槽位上
 */
bool legacy_shape_matches_slot(std::size_t slot, std::int32_t shape) {
  switch (shape) {
    case kLegacyRingManaToHealthItem:
    case kLegacyRingSuckHealthItem:
      return slot == kEquipRingLeft || slot == kEquipRingRight;
    case kLegacyBraceletManaToHealthItem:
    case kLegacyBraceletSuckHealthItem:
      return slot == kEquipArmRingLeft || slot == kEquipArmRingRight;
    case kLegacyNecklaceManaToHealthItem:
    case kLegacyNecklaceSuckHealthItem:
      return slot == kEquipNecklace;
    default:
      return false;
  }
}

/**
 * @brief 根据当前等级获取升到下一级所需的经验值
 * @param level 当前等级（0-60+）
 * @return 下一级所需经验值
 * @details 使用 Mir2 经典经验表（kNeedExps），60 级以后固定为 15 亿经验。
 *          1 级对应索引 0，以此类推。等级为 0 时也返回 1 级经验值。
 * @note 该经验表与传奇系列游戏的经典升级曲线一致，前期增长平缓，后期急剧增长。
 */
std::uint32_t next_level_exp(std::uint8_t level) {
  static constexpr std::array<std::uint32_t, 61> kNeedExps{
      100,       200,       300,       400,       600,       900,       1200,
      1700,      2500,      6000,      8000,      10000,     15000,     30000,
      40000,     50000,     70000,     100000,    120000,    140000,    250000,
      300000,    350000,    400000,    500000,    700000,    1000000,   1400000,
      1800000,   2000000,   2400000,   2800000,   3200000,   3600000,   4000000,
      4800000,   5600000,   8200000,   9000000,   12000000,  16000000,  30000000,
      50000000,  80000000,  120000000, 160000000, 200000000, 250000000, 300000000,
      350000000, 400000000, 480000000, 560000000, 640000000, 740000000, 840000000,
      950000000, 1070000000, 1200000000, 1500000000, 1500000000};
  const auto index = std::clamp<std::size_t>(level == 0 ? 0 : static_cast<std::size_t>(level - 1),
                                             0, kNeedExps.size() - 1);
  return kNeedExps[index];
}

/** ==================== 辅助函数：位标记操作 ==================== */

/**
 * @brief 从位标记数组中读取指定索引的位值（1-based 索引）
 * @tparam N 数组大小
 * @param marks 位标记数组（每个元素存储 8 个位标记）
 * @param index 标记索引（1-based，从 1 开始计数）
 * @return 0 或 1，索引无效时返回 0
 * @details 使用大端位序（MSB first），索引 1 对应第一个字节的最高位(0x80)
 */
template <std::size_t N>
std::uint8_t legacy_bit_mark(const std::array<std::uint8_t, N>& marks,
                             std::int32_t index) {
  if (index <= 0) {
    return 0;
  }
  const auto zero_based = index - 1;
  const auto byte_index = static_cast<std::size_t>(zero_based / 8);
  if (byte_index >= marks.size()) {
    return 0;
  }
  const auto bit = static_cast<std::uint8_t>(0x80U >> (zero_based % 8));
  return (marks[byte_index] & bit) != 0 ? 1 : 0;
}

/**
 * @brief 设置位标记数组中指定索引的位值（1-based 索引）
 * @tparam N 数组大小
 * @param marks 位标记数组（将被修改）
 * @param index 标记索引（1-based）
 * @param value 要设置的值（0 清除，非 0 设置）
 * @return 操作成功返回 true，索引无效返回 false
 */
template <std::size_t N>
bool set_legacy_bit_mark(std::array<std::uint8_t, N>& marks,
                         std::int32_t index, std::uint8_t value) {
  if (index <= 0) {
    return false;
  }
  const auto zero_based = index - 1;
  const auto byte_index = static_cast<std::size_t>(zero_based / 8);
  if (byte_index >= marks.size()) {
    return false;
  }
  const auto bit = static_cast<std::uint8_t>(0x80U >> (zero_based % 8));
  if (value == 0) {
    marks[byte_index] = static_cast<std::uint8_t>(marks[byte_index] & ~bit);
  } else {
    marks[byte_index] = static_cast<std::uint8_t>(marks[byte_index] | bit);
  }
  return true;
}

/** ==================== 辅助函数：外观特征与服务解析 ==================== */

/**
 * @brief 根据性别和装备物品解析角色外观特征值
 * @param sex 性别（0=男, 1=女）
 * @param item 装备物品
 * @param item_configs 物品配置表
 * @return 外观特征值（shape * 2 + sex，钳位到 0-255）
 * @details 用于计算角色穿戴装备后的外观显示，衣服和武器的 shape 决定显示外观
 */
std::uint8_t resolve_shape_feature(std::uint8_t sex, const LegacyUserItem& item,
                                   const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item)) {
    return sex;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return static_cast<std::uint8_t>(std::clamp(config->shape * 2 + sex, 0, 255));
  }
  return sex;
}

/**
 * @brief 将服务类型字符串转换为小写形式
 * @param service 原始服务类型字符串
 * @return 小写化后的服务类型字符串
 * @details 用于规范化 NPC 服务类型比较，确保大小写不敏感匹配
 */
std::string normalize_service(std::string service) {
  std::transform(service.begin(), service.end(), service.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return service;
}

/** ==================== 辅助函数：状态效果与 NPC 功能检测 ==================== */

/**
 * @brief 判断状态效果是否为负面效果
 * @param effect 定时状态效果
 * @return 如果有 DOT 伤害或减速则返回 true
 */
bool is_negative_effect(const TimedStatusEffect& effect) {
  return effect.damage_per_tick > 0 || effect.slow_percent > 0;
}

/**
 * @brief 判断状态效果是否具有实际作用负载
 * @param effect 定时状态效果
 * @return 如果有 DOT 伤害/HOT 治疗/减速/护盾中任意一项则返回 true
 */
bool effect_has_payload(const TimedStatusEffect& effect) {
  return effect.damage_per_tick > 0 || effect.heal_per_tick > 0 || effect.slow_percent > 0 ||
         effect.shield_points > 0;
}

/**
 * @brief 创建字符串的小写副本（仅 ASCII 字符）
 * @param value 源字符串
 * @return 小写化后的新字符串
 */
std::string lower_ascii_copy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

/**
 * @brief 检测 NPC 对话框是否支持武器升级功能
 * @param sections NPC 对话框段落列表
 * @return 如果找到 @upgradenow 或 @getbackupgnow 命令则返回 true
 */
bool dialog_supports_weapon_upgrade(const std::vector<NpcDialogSectionConfig>& sections) {
  for (const auto& section : sections) {
    const auto action = lower_ascii_copy(section.action);
    if (action == "@upgradenow" || action == "@getbackupgnow") {
      return true;
    }
    const auto text = lower_ascii_copy(section.text);
    if (text.find("@upgradenow") != std::string::npos ||
        text.find("@getbackupgnow") != std::string::npos) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 检测是否为特殊名称的奴隶怪物
 * @param name 怪物名称
 * @return 如果是白骷髅/精灵/精灵战士则返回 true
 * @details 特殊名称的奴隶在升级时使用不同的属性倍率计算
 */
bool legacy_special_slave_name(std::string_view name) {
  const auto lowered = lower_ascii_copy(name);
  return lowered == "__whiteskeleton" || lowered == "__elf" || lowered == "__elfwarrior";
}

/** ==================== 辅助函数：时间单位转换 ==================== */

/**
 * @brief 将 tick 数转换为毫秒数
 * @param ticks tick 数量
 * @param tick_ms 每个 tick 对应的毫秒数
 * @return 毫秒数
 */
std::uint64_t ticks_to_ms(std::uint64_t ticks, std::uint32_t tick_ms) {
  return ticks * static_cast<std::uint64_t>(std::max<std::uint32_t>(tick_ms, 1));
}

/**
 * @brief 将毫秒数转换为 tick 数（向上取整）
 * @param value_ms 毫秒数
 * @param tick_ms 每个 tick 对应的毫秒数
 * @return tick 数（至少为 1）
 */
std::uint64_t ms_to_ticks(std::uint32_t value_ms, std::uint32_t tick_ms) {
  const auto tick = std::max<std::uint32_t>(tick_ms, 1);
  return std::max<std::uint64_t>(
      1, (static_cast<std::uint64_t>(value_ms) + static_cast<std::uint64_t>(tick) - 1) /
             static_cast<std::uint64_t>(tick));
}

/** ==================== 常量定义：状态位索引 ==================== */

constexpr std::int32_t kPoisonDecHealth = 0;       ///< 减血毒状态位
constexpr std::int32_t kPoisonDamageArmor = 1;     ///< 损装备毒状态位
constexpr std::int32_t kPoisonDontMove = 4;         ///< 定身毒状态位
constexpr std::int32_t kPoisonStone = 5;            ///< 石化状态位
constexpr std::int32_t kStateTransparent = 8;       ///< 透明/隐身状态位
constexpr std::int32_t kStateDefenceUp = 9;         ///< 物理防御提升状态位
constexpr std::int32_t kStateMagicDefenceUp = 10;   ///< 魔法防御提升状态位
constexpr std::int32_t kStateBubbleDefenceUp = 11;  ///< 气泡防御（魔法盾）状态位
constexpr std::int32_t kLegacyHealingCap = 300;     ///< 传统 HP/MP 自动恢复累积上限

/** ==================== 常量定义：怪物种族 ID（服务端） ==================== */
/** @note 这些常量对应 Mir2 服务的怪物种族标识，用于决定怪物 AI 行为特性 */

constexpr std::int32_t kRcSpitSpider = 82;           ///< 吐网蜘蛛
constexpr std::int32_t kRcKillingHerb = 85;          ///< 食人草（隐藏、挖掘模式）
constexpr std::int32_t kRcDualAxeSkeleton = 87;      ///< 双斧骷髅（连锁射击 2 次）
constexpr std::int32_t kRcBigKudeki = 90;            ///< 大角蝇
constexpr std::int32_t kRcMagCowFaceMon = 91;        ///< 魔法牛魔怪
constexpr std::int32_t kRcThornDark = 93;            ///< 暗黑荆棘（连锁射击 3 次）
constexpr std::int32_t kRcDigOutZombi = 95;          ///< 挖出僵尸（隐藏、地上地下切换）
constexpr std::int32_t kRcBeeQueen = 103;            ///< 蜂后（召唤小蜜蜂）
constexpr std::int32_t kRcArcherMon = 104;           ///< 弓箭手怪物（连锁射击 6 次）
constexpr std::int32_t kRcGasMoth = 105;             ///< 毒蛾
constexpr std::int32_t kRcGasDung = 106;             ///< 毒粪
constexpr std::int32_t kRcCentipedeKing = 107;       ///< 蜈蚣王（隐藏、挖掘模式）
constexpr std::int32_t kRcArcherGuard = 112;         ///< 弓箭守卫
constexpr std::int32_t kRcSpiderHouse = 116;         ///< 蜘蛛巢（召唤小蜘蛛）
constexpr std::int32_t kRcHighRiskSpider = 118;      ///< 高危蜘蛛
constexpr std::int32_t kRcBigPoisonSpider = 119;     ///< 大毒蜘蛛
constexpr std::int32_t kRcScultureKing = 102;        ///< 骷髅王（有随从）
constexpr std::int32_t kRcScultureKingNoFollower = 122; ///< 骷髅王（无随从）
constexpr std::int32_t kRcToxicGhost = 127;          ///< 剧毒幽灵
constexpr std::int32_t kRcDoorGuard = 11;            ///< 门卫（中立守卫）
constexpr std::int32_t kRcArcherPolice = 20;         ///< 弓箭卫士
constexpr std::int32_t kRcCastleDoor = 110;          ///< 城堡门
constexpr std::int32_t kRcWall = 111;                ///< 城墙

/** ==================== 辅助函数：伤害修正与状态位操作 ==================== */

/**
 * @brief 将伤害值修正为 120% 版本（Mir2 经典公式）
 * @param amount 原始伤害值
 * @return 修正后的伤害值
 * @details 公式：(amount * 6 + 2) / 5，相当于 +20% 伤害后取整
 *          用于"损装备毒"效果下的伤害加成
 */
std::int32_t round_damage_120(std::int32_t amount) {
  return amount > 0 ? (amount * 6 + 2) / 5 : amount;
}

/**
 * @brief 设置传统状态位中的指定位
 * @param status 状态位整数值（将被修改）
 * @param index 位索引（0-31，0 为最高位）
 * @param active true=置位，false=清除
 * @details 使用大端位序：索引 0 对应 0x80000000（最高位），
 *          索引 31 对应 0x00000001（最低位）
 */
void set_legacy_status_bit(std::int32_t& status, std::int32_t index, bool active) {
  const auto mask = 0x80000000u >> static_cast<std::uint32_t>(std::clamp(index, 0, 31));
  auto bits = static_cast<std::uint32_t>(status);
  if (active) {
    bits |= mask;
  } else {
    bits &= ~mask;
  }
  status = static_cast<std::int32_t>(bits);
}

/**
 * @brief 清除所有临时性传统状态位（死亡/登出时调用）
 * @param status 状态位整数值（将被修改）
 * @details 清除包括：减血毒、损装备毒、定身、石化、透明、防御提升、魔防提升、气泡防御
 *          保留永久性状态位（如职业、性别等）
 */
void clear_transient_legacy_status_bits(std::int32_t& status) {
  set_legacy_status_bit(status, kPoisonDecHealth, false);
  set_legacy_status_bit(status, kPoisonDamageArmor, false);
  set_legacy_status_bit(status, kPoisonDontMove, false);
  set_legacy_status_bit(status, kPoisonStone, false);
  set_legacy_status_bit(status, kStateTransparent, false);
  set_legacy_status_bit(status, kStateDefenceUp, false);
  set_legacy_status_bit(status, kStateMagicDefenceUp, false);
  set_legacy_status_bit(status, kStateBubbleDefenceUp, false);
}

/** ==================== 辅助函数：Buff 类型转换与创建 ==================== */

/**
 * @brief 将传统状态位索引转换为 LegacyBuffKind 枚举值
 * @param status_bit 状态位索引
 * @return 对应的 Buff 类型，如果不是已知的 Buff 类型则返回 std::nullopt
 * @see LegacyBuffKind
 */
std::optional<LegacyBuffKind> legacy_buff_kind_from_status_bit(std::int32_t status_bit) {
  switch (status_bit) {
    case kPoisonDecHealth:
      return LegacyBuffKind::poison_dechealth;
    case kPoisonDamageArmor:
      return LegacyBuffKind::poison_damage_armor;
    case kPoisonDontMove:
      return LegacyBuffKind::poison_dont_move;
    case kPoisonStone:
      return LegacyBuffKind::poison_stone;
    case kStateTransparent:
      return LegacyBuffKind::transparent;
    case kStateDefenceUp:
      return LegacyBuffKind::defence_up;
    case kStateMagicDefenceUp:
      return LegacyBuffKind::magic_defence_up;
    case kStateBubbleDefenceUp:
      return LegacyBuffKind::bubble_defence_up;
    default:
      return std::nullopt;
  }
}

/**
 * @brief 创建传统 Buff 状态对象
 * @param kind Buff 类型
 * @param expire_tick 过期 tick
 * @param next_tick 下次触发 tick（默认为 0）
 * @param tick_interval 触发间隔（默认为 1）
 * @param level 效果等级（默认为 0）
 * @param source_actor_id 来源角色 ID（默认为 0）
 * @return 初始化完成的 LegacyBuffState 对象
 * @details 自动推导 status_bit、affects_ability、negative、clear_on_death 字段
 * @note dc_up（攻击提升）的 status_bit 设为 -1，因为它没有对应的传统状态位
 */
LegacyBuffState make_legacy_buff(LegacyBuffKind kind, std::uint64_t expire_tick,
                                 std::uint64_t next_tick = 0,
                                 std::uint64_t tick_interval = 1,
                                 std::int32_t level = 0,
                                 std::uint64_t source_actor_id = 0) {
  const auto status_bit =
      kind == LegacyBuffKind::dc_up ? -1 : static_cast<std::int32_t>(kind);
  return LegacyBuffState{kind,
                         expire_tick,
                         next_tick,
                         tick_interval,
                         level,
                         source_actor_id,
                         status_bit,
                         kind == LegacyBuffKind::defence_up ||
                             kind == LegacyBuffKind::magic_defence_up ||
                             kind == LegacyBuffKind::dc_up,
                         kind == LegacyBuffKind::poison_dechealth ||
                             kind == LegacyBuffKind::poison_damage_armor ||
                             kind == LegacyBuffKind::poison_dont_move ||
                             kind == LegacyBuffKind::poison_stone,
                         true};
}

/**
 * @brief 根据 Buff 状态列表清除对应的玩家状态位
 * @param status 玩家状态位（将被修改）
 * @param states Buff 状态列表
 * @details 遍历 Buff 列表，对每个具有有效 status_bit 的 Buff 清除其对应的状态位
 */
void clear_player_status_bits(std::int32_t& status, const std::vector<LegacyBuffState>& states) {
  for (const auto& state : states) {
    if (state.status_bit < 0) {
      continue;
    }
    set_legacy_status_bit(status, state.status_bit, false);
  }
}

}  // namespace

/** ==================== MapContext：地图上下文方法实现 ==================== */

/**
 * @brief 向指定会话发送网络数据包
 * @param session_id 目标会话 ID
 * @param packet 要发送的 LegacyPacket
 */
void MapContext::send_packet(std::uint64_t session_id, LegacyPacket packet) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->session_events.push_back(SessionEvent{
      SessionEventKind::send_packet, "game_gateway", session_id, {}, std::move(packet), {}});
}

/**
 * @brief 发送审计事件
 * @param category 事件类别
 * @param message 事件描述
 */
void MapContext::emit_audit(std::string category, std::string message) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->audit_events.push_back(AuditEvent{std::move(category), std::move(message), map_id});
}

/**
 * @brief 请求持久化存储操作
 * @param request 持久化请求（如保存角色、物品等）
 */
void MapContext::request_persist(PersistRequest request) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->persist_requests.push_back(std::move(request));
}

/**
 * @brief 发送跨地图邮件（用于地图间角色通信）
 * @param mail 要发送的邮件
 * @details 邮件会被投递到目标地图的消息队列中，由目标地图在下帧处理
 */
void MapContext::post_cross_map_mail(ActorMail mail) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->cross_map_mails.push_back(std::move(mail));
}

/** ==================== GameObject：基类构造函数与方法实现 ==================== */

GameObject::GameObject(std::uint64_t id, GameObjectKind kind, std::string name, std::string map_id,
                       std::int32_t x, std::int32_t y)
    : id_(id), kind_(kind), name_(std::move(name)), map_id_(std::move(map_id)), x_(x), y_(y) {}

/** ==================== LegacyBuffContainer：Buff 容器方法实现 ==================== */

/**
 * @brief 激活或刷新一个 Buff
 * @param state 待激活的 Buff 状态
 * @param current_tick 当前逻辑 tick
 * @return 如果 Buff 被新激活（非刷新）返回 true，如果只是刷新延长则返回 false
 * @details 如果相同类型的 Buff 已存在，则合并效果（取更长的过期时间、更高的等级等）。
 *          如果传入的 Buff 已经过期则直接返回 false。
 */
bool LegacyBuffContainer::activate_or_refresh(LegacyBuffState state,
                                               std::uint64_t current_tick) {
  if (state.expire_tick == 0 || state.expire_tick <= current_tick) {
    return false;
  }
  state.tick_interval = std::max<std::uint64_t>(state.tick_interval, 1);
  if (state.next_tick != 0) {
    state.next_tick = std::max(state.next_tick, current_tick + state.tick_interval);
  }
  auto* current = get(state.kind);
  if (current == nullptr) {
    states_.push_back(state);
    return true;
  }
  const auto changed = current->expire_tick == 0 || state.expire_tick > current->expire_tick;
  current->expire_tick = std::max(current->expire_tick, state.expire_tick);
  if (state.next_tick != 0 && current->next_tick == 0) {
    current->next_tick = state.next_tick;
  }
  current->tick_interval = state.tick_interval;
  current->level = std::max(current->level, state.level);
  if (state.source_actor_id != 0) {
    current->source_actor_id = state.source_actor_id;
  }
  current->status_bit = state.status_bit;
  current->affects_ability = state.affects_ability;
  current->negative = state.negative;
  current->clear_on_death = state.clear_on_death;
  return changed;
}

/**
 * @brief 检查指定类型的 Buff 是否在有效期内
 * @param kind Buff 类型
 * @param current_tick 当前逻辑 tick
 * @return 如果 Buff 存在且未过期则返回 true
 */
bool LegacyBuffContainer::active(LegacyBuffKind kind, std::uint64_t current_tick) const {
  const auto* state = get(kind);
  return state != nullptr && state->expire_tick != 0 && current_tick <= state->expire_tick;
}

/**
 * @brief 检查是否拥有指定类型的 Buff（不论是否过期）
 * @param kind Buff 类型
 * @return 如果存在该类型的 Buff 记录则返回 true
 */
bool LegacyBuffContainer::has(LegacyBuffKind kind) const { return get(kind) != nullptr; }

/**
 * @brief 清除指定类型的所有 Buff
 * @param kind 要清除的 Buff 类型
 * @return 如果实际清除了至少一个 Buff 则返回 true
 */
bool LegacyBuffContainer::clear(LegacyBuffKind kind) {
  const auto before = states_.size();
  states_.erase(std::remove_if(states_.begin(), states_.end(),
                               [&](const LegacyBuffState& state) {
                                 return state.kind == kind;
                               }),
                states_.end());
  return states_.size() != before;
}

/**
 * @brief 按清除策略批量清除 Buff
 * @param policy 清除策略（登出/死亡/离开地图）
 * @return 清除结果，包含状态位和属性变化信息
 * @details 不同策略影响不同 Buff：
 *          - logout：清除所有 Buff
 *          - death：仅清除 clear_on_death 标记为 true 的 Buff
 *          - leave_map：仅清除透明、定身、石化 Buff
 */
LegacyBuffClearResult LegacyBuffContainer::clear_by_policy(LegacyBuffClearPolicy policy) {
  LegacyBuffClearResult result;
  states_.erase(std::remove_if(states_.begin(), states_.end(),
                               [&](const LegacyBuffState& state) {
                                 const auto clear = policy == LegacyBuffClearPolicy::logout ||
                                                    (policy == LegacyBuffClearPolicy::death &&
                                                     state.clear_on_death) ||
                                                    (policy == LegacyBuffClearPolicy::leave_map &&
                                                     (state.kind == LegacyBuffKind::transparent ||
                                                      state.kind == LegacyBuffKind::poison_dont_move ||
                                                      state.kind == LegacyBuffKind::poison_stone));
                                 if (clear) {
                                   result.status_changed = true;
                                   result.ability_changed =
                                       result.ability_changed || state.affects_ability;
                                 }
                                 return clear;
                               }),
                states_.end());
  return result;
}

/**
 * @brief 检查指定类型的 Buff 是否到了触发 tick
 * @param kind Buff 类型
 * @param current_tick 当前逻辑 tick
 * @return 如果 Buff 存在且到达触发时间则返回可变的 Buff 指针，否则返回 nullptr
 * @details 用于 DOT（持续伤害）类 Buff 的定时触发检测
 */
LegacyBuffState* LegacyBuffContainer::tick_due(LegacyBuffKind kind,
                                               std::uint64_t current_tick) {
  auto* state = get(kind);
  if (state == nullptr || state->next_tick == 0 || state->expire_tick == 0 ||
      current_tick < state->next_tick || state->next_tick > state->expire_tick) {
    return nullptr;
  }
  return state;
}

/**
 * @brief 获取所有已过期的 Buff 并从容器中移除
 * @param current_tick 当前逻辑 tick
 * @return 已过期的 Buff 状态列表
 * @details 遍历所有 Buff，将 expire_tick 小于 current_tick 的 Buff 移除并返回
 */
std::vector<LegacyBuffState> LegacyBuffContainer::expire_due(std::uint64_t current_tick) {
  std::vector<LegacyBuffState> expired;
  for (auto it = states_.begin(); it != states_.end();) {
    if (it->expire_tick != 0 && current_tick > it->expire_tick) {
      expired.push_back(*it);
      it = states_.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

/**
 * @brief 获取指定类型 Buff 的剩余 tick 数
 * @param kind Buff 类型
 * @param current_tick 当前逻辑 tick
 * @return 剩余 tick 数，如果 Buff 不存在或已过期则返回 0
 */
std::uint64_t LegacyBuffContainer::remaining_ticks(LegacyBuffKind kind,
                                                   std::uint64_t current_tick) const {
  const auto* state = get(kind);
  if (state == nullptr || state->expire_tick <= current_tick) {
    return 0;
  }
  return state->expire_tick - current_tick;
}

/**
 * @brief 按类型查找 Buff（const 版本）
 * @param kind Buff 类型
 * @return 找到返回 const 指针，否则返回 nullptr
 */
const LegacyBuffState* LegacyBuffContainer::get(LegacyBuffKind kind) const {
  const auto it = std::find_if(states_.begin(), states_.end(),
                               [&](const LegacyBuffState& state) {
                                 return state.kind == kind;
                               });
  return it == states_.end() ? nullptr : &*it;
}

/**
 * @brief 按类型查找 Buff（可变版本）
 * @param kind Buff 类型
 * @return 找到返回 mutable 指针，否则返回 nullptr
 */
LegacyBuffState* LegacyBuffContainer::get(LegacyBuffKind kind) {
  const auto it = std::find_if(states_.begin(), states_.end(),
                               [&](const LegacyBuffState& state) {
                                 return state.kind == kind;
                               });
  return it == states_.end() ? nullptr : &*it;
}

/**
 * @brief 基类邮件处理：移动/跑步时更新位置
 * @param mail 收到的邮件
 * @param context 地图上下文（本例中未使用）
 */
void GameObject::on_mail(const ActorMail& mail, MapContext&) {
  if (mail.kind == ActorMailKind::move || mail.kind == ActorMailKind::run) {
    set_position(mail.x, mail.y);
  }
}

/**
 * @brief 基类 tick 处理：默认每 10 tick 调度一次
 */
void GameObject::on_tick(MapContext& context) { set_next_due_tick(context.tick + 10); }

/**
 * @brief 设置游戏对象位置
 */
void GameObject::set_position(std::int32_t x, std::int32_t y) {
  x_ = x;
  y_ = y;
}

/**
 * @brief 设置下次调度到期 tick
 */
void GameObject::set_next_due_tick(std::uint64_t next_due_tick) { next_due_tick_ = next_due_tick; }

/** ==================== Player：玩家构造函数与基础查询 ==================== */

/**
 * @brief 构造 Player 实例
 * @param id 角色唯一标识
 * @param session_id 网关会话 ID（用于网络通信）
 * @param character 角色完整数据记录（包含属性、背包、装备等）
 * @details 初始化时清除临时状态位，确保经验上限值正确。
 *          如果 max_exp 为 0 或 100（默认值），则按等级重新计算。
 */
Player::Player(std::uint64_t id, std::uint64_t session_id, CharacterRecord character)
    : GameObject(id, GameObjectKind::player, character.character_name, character.map_id, character.x,
                 character.y),
      session_id_(session_id),
      character_(std::move(character)),
      base_ability_(character_.ability) {
  run_time_ms_ = 0;
  clear_transient_legacy_status_bits(character_.status);
  if (character_.ability.max_exp == 0 || character_.ability.max_exp == 100) {
    character_.ability.max_exp = next_level_exp(character_.ability.level);
    base_ability_.max_exp = character_.ability.max_exp;
  }
}

/**
 * @brief 创建玩家快照（用于传输和保存）
 * @return 包含当前完整位置信息的角色记录副本
 * @note 快照包含当前地图 ID 和坐标，与持久化快照不同
 */
CharacterRecord Player::snapshot() const {
  CharacterRecord snapshot = character_;
  snapshot.map_id = map_id();
  snapshot.x = x();
  snapshot.y = y();
  snapshot.dir = character_.dir;
  return snapshot;
}

/**
 * @brief 创建持久化快照（仅保存基础属性和经验/等级信息）
 * @return 适合存储到数据库的角色记录
 * @details 与普通快照不同，持久化快照使用 base_ability_ 作为基础，
 *          只保留等级、经验、HP、MP 等关键属性，不包含装备加成属性。
 *          重量字段全部归零，因为持久化时不保存装备加成后的重量。
 */
CharacterRecord Player::persistent_snapshot() const {
  CharacterRecord snapshot = this->snapshot();
  auto ability = base_ability_;
  ability.level = character_.ability.level;
  ability.exp = character_.ability.exp;
  ability.max_exp = character_.ability.max_exp;
  ability.hp = character_.ability.hp;
  ability.mp = character_.ability.mp;
  ability.weight = 0;
  ability.wear_weight = 0;
  ability.hand_weight = 0;
  snapshot.ability = ability;
  return snapshot;
}

/**
 * @brief 检查玩家是否已死亡
 * @return HP 为 0 时返回 true
 */
bool Player::is_dead() const { return character_.ability.hp == 0; }

/**
 * @brief 检查背包是否有空闲格子
 * @return 背包中是否存在空物品位
 */
bool Player::has_free_bag_slot() const {
  return std::any_of(character_.bag_items.begin(), character_.bag_items.end(),
                     [](const LegacyUserItem& item) { return is_empty(item); });
}

/**
 * @brief 检查仓库是否有空闲格子
 * @return 仓库中是否存在空物品位
 */
bool Player::has_free_storage_slot() const {
  return std::any_of(character_.storage_items.begin(), character_.storage_items.end(),
                     [](const LegacyUserItem& item) { return is_empty(item); });
}

/**
 * @brief 检查背包能否添加指定物品（考虑重量上限）
 * @param item 要添加的物品
 * @param item_configs 物品配置表
 * @return 是否有空闲格子且总重量不超过负重上限
 */
bool Player::can_add_bag_item(
    const LegacyUserItem& item, const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  if (!has_free_bag_slot()) {
    return false;
  }

  std::int32_t total_weight = item_weight(item, item_configs);
  for (const auto& bag_item : character_.bag_items) {
    total_weight += item_weight(bag_item, item_configs);
  }
  return total_weight <= std::max<std::int32_t>(character_.ability.max_weight, 0);
}

/**
 * @brief 在背包中查找匹配的物品（可修改指针）
 * @param make_index 物品制造索引
 * @param expected_name 预期物品名称（空字符串忽略名称检查）
 * @param item_configs 物品配置表
 * @return 找到返回物品指针，否则返回 nullptr
 */
LegacyUserItem* Player::bag_item_mutable(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      return &item;
    }
  }
  return nullptr;
}

/**
 * @brief 在背包中查找匹配的物品（const 版本）
 * @param make_index 物品制造索引
 * @param expected_name 预期物品名称（空字符串忽略名称检查）
 * @param item_configs 物品配置表
 * @return 找到返回 const 物品指针，否则返回 nullptr
 */
const LegacyUserItem* Player::bag_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  for (const auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      return &item;
    }
  }
  return nullptr;
}

/**
 * @brief 查找匹配物品在背包中的索引位置
 * @param make_index 物品制造索引
 * @param expected_name 预期物品名称（空字符串忽略名称检查）
 * @param item_configs 物品配置表
 * @return 找到返回索引，否则返回 std::nullopt
 */
std::optional<std::size_t> Player::bag_item_index(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  for (std::size_t index = 0; index < character_.bag_items.size(); ++index) {
    if (matches_item(character_.bag_items[index], make_index, expected_name, item_configs)) {
      return index;
    }
  }
  return std::nullopt;
}

/**
 * @brief 在仓库中查找匹配的物品（const 版本）
 * @param make_index 物品制造索引
 * @param expected_name 预期物品名称
 * @param item_configs 物品配置表
 * @return 找到返回 const 物品指针，否则返回 nullptr
 */
const LegacyUserItem* Player::storage_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  for (const auto& item : character_.storage_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      return &item;
    }
  }
  return nullptr;
}

/**
 * @brief 获取指定槽位的已装备物品（const 版本）
 * @param slot 装备槽位索引
 * @return 如果槽位有效返回物品指针，否则返回 nullptr
 */
const LegacyUserItem* Player::equipped_item(std::size_t slot) const {
  if (slot >= character_.equipped_items.size()) {
    return nullptr;
  }
  return &character_.equipped_items[slot];
}

/**
 * @brief 获取指定槽位的已装备物品（可变版本）
 * @param slot 装备槽位索引
 * @return 如果槽位有效返回可变物品指针，否则返回 nullptr
 */
LegacyUserItem* Player::equipped_item_mutable(std::size_t slot) {
  if (slot >= character_.equipped_items.size()) {
    return nullptr;
  }
  return &character_.equipped_items[slot];
}

/**
 * @brief 查找已学习的魔法（const 版本）
 * @param magic_id 魔法 ID
 * @return 找到返回魔法信息指针，否则返回 nullptr
 */
const LegacyUseMagicInfo* Player::learned_magic(std::int32_t magic_id) const {
  for (const auto& magic : character_.magics) {
    if (!is_empty(magic) && magic.magic_id == magic_id) {
      return &magic;
    }
  }
  return nullptr;
}

/**
 * @brief 查找已学习的魔法（可变版本）
 * @param magic_id 魔法 ID
 * @return 找到返回可变魔法信息指针，否则返回 nullptr
 */
LegacyUseMagicInfo* Player::learned_magic_mutable(std::int32_t magic_id) {
  for (auto& magic : character_.magics) {
    if (!is_empty(magic) && magic.magic_id == magic_id) {
      return &magic;
    }
  }
  return nullptr;
}

/** ==================== Player：魔法管理 ==================== */

/**
 * @brief 为玩家添加一个新的魔法
 * @param magic_id 魔法 ID（需在 1-65535 范围内）
 * @param key 快捷键
 * @param level 魔法等级
 * @param cur_train 当前训练值
 * @return 添加成功返回 true，已学习过或魔法数组满则返回 false
 * @note 同一个魔法不可重复学习
 */
bool Player::add_legacy_magic(std::int32_t magic_id, char key, std::uint8_t level,
                              std::int32_t cur_train) {
  if (magic_id <= 0 || magic_id > 65535 || learned_magic(magic_id) != nullptr) {
    return false;
  }
  for (auto& magic : character_.magics) {
    if (is_empty(magic)) {
      magic.magic_id = static_cast<std::uint16_t>(magic_id);
      magic.key = key;
      magic.level = level;
      magic.cur_train = std::max(cur_train, 0);
      return true;
    }
  }
  return false;
}

/**
 * @brief 移除玩家已学习的魔法
 * @param magic_id 要移除的魔法 ID
 * @return 移除成功返回 true，未找到该魔法则返回 false
 * @details 移除后后续魔法向前移位，同时清除该魔法的经验代次记录
 */
bool Player::remove_legacy_magic(std::int32_t magic_id) {
  for (std::size_t index = 0; index < character_.magics.size(); ++index) {
    if (is_empty(character_.magics[index]) || character_.magics[index].magic_id != magic_id) {
      continue;
    }
    for (std::size_t move_index = index; move_index + 1 < character_.magics.size(); ++move_index) {
      character_.magics[move_index] = character_.magics[move_index + 1];
    }
    character_.magics.back() = LegacyUseMagicInfo{};
    legacy_magic_lvexp_generations_.erase(magic_id);
    return true;
  }
  return false;
}

/** ==================== Player：金币、PK 值与属性查询 ==================== */

/**
 * @brief 检查玩家是否拥有足够的金币
 * @param amount 需要花费的金币数量
 * @return 如果数量大于 0 且玩家持有足够金币则返回 true
 */
bool Player::can_spend_gold(std::int32_t amount) const {
  return amount > 0 && character_.gold >= amount;
}

/**
 * @brief 计算玩家 PK 等级（罪恶值等级）
 * @return PK 等级 = PK 点 / 100
 */
std::int32_t Player::pk_level() const { return std::max(character_.pk_point, 0) / 100; }

/**
 * @brief 计算玩家身体幸运等级
 * @return 幸运等级（范围 -10 ~ 5），每 5000 点为一个等级
 */
std::int32_t Player::body_luck_level() const {
  return std::clamp(static_cast<std::int32_t>(std::trunc(character_.body_luck / 5000.0)), -10, 5);
}

/** ==================== Player：任务标记与脚本参数 ==================== */

/**
 * @brief 读取任务标记（1-based 位索引）
 * @param index 标记索引（从 1 开始）
 * @return 0 或 1
 */
std::uint8_t Player::quest_mark(std::int32_t index) const {
  return legacy_bit_mark(character_.quest_marks, index);
}

/**
 * @brief 读取任务开放单元标记
 */
std::uint8_t Player::quest_open_unit(std::int32_t index) const {
  return legacy_bit_mark(character_.quest_open_units, index);
}

/**
 * @brief 读取任务单元标记
 */
std::uint8_t Player::quest_unit(std::int32_t index) const {
  return legacy_bit_mark(character_.quest_units, index);
}

/**
 * @brief 读取脚本参数
 * @param index 参数索引（从 0 开始）
 * @return 参数值，索引越界返回 0
 */
std::int32_t Player::script_param(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.script_params.size()) {
    return 0;
  }
  return character_.script_params[static_cast<std::size_t>(index)];
}

/**
 * @brief 读取脚本骰子参数
 * @param index 参数索引（从 0 开始）
 * @return 参数值，索引越界返回 0
 */
std::int32_t Player::script_dice_param(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= script_dice_params_.size()) {
    return 0;
  }
  return script_dice_params_[static_cast<std::size_t>(index)];
}

/** ==================== Player：奴隶/宠物管理 ==================== */

/**
 * @brief 添加奴隶角色 ID 到列表
 * @param actor_id 角色 ID
 * @details 去重添加，已存在的 ID 不会重复添加
 */
void Player::add_slave_actor_id(std::uint64_t actor_id) {
  if (actor_id == 0 ||
      std::find(slave_actor_ids_.begin(), slave_actor_ids_.end(), actor_id) !=
          slave_actor_ids_.end()) {
    return;
  }
  slave_actor_ids_.push_back(actor_id);
}

/**
 * @brief 从列表中移除指定的奴隶角色 ID
 */
void Player::remove_slave_actor_id(std::uint64_t actor_id) {
  slave_actor_ids_.erase(std::remove(slave_actor_ids_.begin(), slave_actor_ids_.end(), actor_id),
                         slave_actor_ids_.end());
}

/**
 * @brief 清理不在存活集合中的奴隶 ID
 * @param live_slave_ids 当前存活的奴隶 ID 集合
 */
void Player::prune_slave_actor_ids(const std::unordered_set<std::uint64_t>& live_slave_ids) {
  slave_actor_ids_.erase(
      std::remove_if(slave_actor_ids_.begin(), slave_actor_ids_.end(),
                     [&](std::uint64_t actor_id) { return !live_slave_ids.contains(actor_id); }),
      slave_actor_ids_.end());
}

/** ==================== Player：物品移除/添加操作 ==================== */

/**
 * @brief 从背包中移除匹配的物品（按 make_index 和名称）
 * @param make_index 物品制造索引
 * @param expected_name 预期名称（空字符串忽略名称）
 * @param item_configs 物品配置表
 * @return 移除成功返回被移除的物品，未找到则返回 std::nullopt
 * @details 移除后自动压缩背包数组，将空物品移到末尾
 */
std::optional<LegacyUserItem> Player::remove_bag_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      const auto removed = item;
      item = LegacyUserItem{};
      compact_legacy_items(character_.bag_items);
      return removed;
    }
  }
  return std::nullopt;
}

/**
 * @brief 从背包中移除指定槽位的物品
 * @param slot 槽位索引（从 0 开始）
 * @return 移除成功返回物品，槽位无效或为空则返回 std::nullopt
 * @details 移除后自动压缩背包数组
 */
std::optional<LegacyUserItem> Player::remove_bag_item_at(std::size_t slot) {
  if (slot >= character_.bag_items.size() || is_empty(character_.bag_items[slot])) {
    return std::nullopt;
  }
  const auto removed = character_.bag_items[slot];
  character_.bag_items[slot] = LegacyUserItem{};
  compact_legacy_items(character_.bag_items);
  return removed;
}

/**
 * @brief 从仓库中移除匹配的物品
 * @param make_index 物品制造索引
 * @param expected_name 预期名称
 * @param item_configs 物品配置表
 * @return 移除成功返回物品，未找到返回 std::nullopt
 */
std::optional<LegacyUserItem> Player::remove_storage_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.storage_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      const auto removed = item;
      item = LegacyUserItem{};
      compact_legacy_items(character_.storage_items);
      return removed;
    }
  }
  return std::nullopt;
}

/**
 * @brief 从指定装备槽位移除匹配的物品
 * @param slot 装备槽位索引
 * @param make_index 物品制造索引
 * @param expected_name 预期名称
 * @param item_configs 物品配置表
 * @return 移除成功返回物品，未匹配返回 std::nullopt
 * @note 装备移除后不会压缩数组（装备槽位大小固定）
 */
std::optional<LegacyUserItem> Player::remove_equipped_item(
    std::size_t slot, std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (slot >= character_.equipped_items.size()) {
    return std::nullopt;
  }
  auto& item = character_.equipped_items[slot];
  if (!matches_item(item, make_index, expected_name, item_configs)) {
    return std::nullopt;
  }
  const auto removed = item;
  item = LegacyUserItem{};
  return removed;
}

/**
 * @brief 将物品添加到背包的空闲格子
 * @param item 要添加的物品
 * @return 添加成功返回 true，背包已满返回 false
 */
bool Player::add_bag_item(const LegacyUserItem& item) {
  for (auto& bag_item : character_.bag_items) {
    if (is_empty(bag_item)) {
      bag_item = item;
      return true;
    }
  }
  return false;
}

/**
 * @brief 将物品添加到仓库的空闲格子
 * @param item 要添加的物品
 * @return 添加成功返回 true，仓库已满返回 false
 */
bool Player::add_storage_item(const LegacyUserItem& item) {
  for (auto& storage_item : character_.storage_items) {
    if (is_empty(storage_item)) {
      storage_item = item;
      return true;
    }
  }
  return false;
}

/** ==================== Player：战斗属性计算 ==================== */

/**
 * @brief 计算玩家的近战攻击力
 * @return 取 DC 最小值和（最大值 + DC_up 加成）中较大的值，最低为 1
 */
std::int32_t Player::melee_power() const {
  return std::max(1, std::max(packed_min(character_.ability.dc),
                              packed_max(character_.ability.dc) + legacy_dc_up_bonus()));
}

/**
 * @brief 获取 DC_up Buff 的等级加成值
 * @return Buff 等级，无 Buff 则返回 0
 */
std::int32_t Player::legacy_dc_up_bonus() const {
  const auto* buff = legacy_buffs_.get(LegacyBuffKind::dc_up);
  return buff != nullptr ? std::max(buff->level, 0) : 0;
}

/**
 * @brief 计算玩家的法术强度
 * @param base_power 基础法术强度
 * @return 基础强度 + 魔法/道术平均值加成，最低为 1
 */
std::int32_t Player::spell_power(std::int32_t base_power) const {
  const auto magic_bonus =
      std::max({packed_average(character_.ability.mc), packed_average(character_.ability.sc), 0});
  return std::max(1, base_power + magic_bonus);
}

/**
 * @brief 计算玩家的物理防御值
 * @return 基础防御 + 防御 Buff 加成（等级/7 + 2）
 */
std::int32_t Player::physical_defense() const {
  auto defense = std::max(0, packed_average(character_.ability.ac));
  if (legacy_buffs_.has(LegacyBuffKind::defence_up)) {
    defense += 2 + static_cast<std::int32_t>(character_.ability.level) / 7;
  }
  return defense;
}

/**
 * @brief 计算玩家的魔法防御值
 * @return 基础魔防 + 魔防 Buff 加成（等级/7 + 2）
 */
std::int32_t Player::magic_defense() const {
  auto defense = std::max(0, packed_average(character_.ability.mac));
  if (legacy_buffs_.has(LegacyBuffKind::magic_defence_up)) {
    defense += 2 + static_cast<std::int32_t>(character_.ability.level) / 7;
  }
  return defense;
}

/**
 * @brief 计算当前所有未过期的护盾吸收值总和
 * @param current_tick 当前逻辑 tick
 * @return 总护盾值
 */
std::int32_t Player::current_shield_points(std::uint64_t current_tick) const {
  std::int32_t total = 0;
  for (const auto& effect : status_effects_) {
    if (effect.shield_points > 0 && current_tick <= effect.expire_tick) {
      total += effect.shield_points;
    }
  }
  return total;
}

/**
 * @brief 检查是否有活跃的护盾
 * @param current_tick 当前逻辑 tick
 * @return 护盾值 > 0 返回 true
 */
bool Player::has_active_shield(std::uint64_t current_tick) const {
  return current_shield_points(current_tick) > 0;
}

/**
 * @brief 计算当前减速效果百分比
 * @param current_tick 当前逻辑 tick
 * @return 减速百分比（取所有负面效果中的最大值）
 */
std::int32_t Player::current_slow_percent(std::uint64_t current_tick) const {
  std::int32_t slow_percent = 0;
  for (const auto& effect : status_effects_) {
    if (current_tick <= effect.expire_tick && is_negative_effect(effect)) {
      slow_percent = std::max(slow_percent, effect.slow_percent);
    }
  }
  return slow_percent;
}

/**
 * @brief 检查是否到达可以移动的时间点
 * @param current_tick 当前逻辑 tick
 * @return 当前 tick >= 下次允许移动的 tick
 */
bool Player::can_move_at(std::uint64_t current_tick) const { return current_tick >= next_move_tick_; }

/** ==================== Player：限速系统（反加速作弊） ==================== */

/**
 * @brief 检查移动请求是否允许（限速/反加速）
 * @param current_tick 当前逻辑 tick
 * @param tick_ms 每个 tick 的毫秒数
 * @return 包含是否允许移动和是否应断开连接的结果
 * @details Mir2 经典反加速算法：
 *          1. 如果上次行走间隔 < 600ms，记录超限
 *          2. 连续超限 < 4 次且累积超限 < 6 次时允许移动
 *          3. 超过阈值后标记加速嫌疑，累计 8 次后要求断开连接
 */
LegacyMoveThrottleResult Player::begin_move_attempt(std::uint64_t current_tick,
                                                    std::uint32_t tick_ms) {
  if (latest_walk_tick_ != 0 &&
      ticks_to_ms(current_tick > latest_walk_tick_ ? current_tick - latest_walk_tick_ : 0, tick_ms) <
          600) {
    ++walk_time_over_count_;
    ++walk_time_over_sum_;
  } else {
    walk_time_over_count_ = 0;
    if (walk_time_over_sum_ > 0) {
      --walk_time_over_sum_;
    }
  }

  latest_walk_tick_ = current_tick;
  if (walk_time_over_count_ < 4 && walk_time_over_sum_ < 6) {
    return {};
  }

  ++speed_hack_timer_over_count_;
  return LegacyMoveThrottleResult{false, speed_hack_timer_over_count_ > 8};
}

/**
 * @brief 检查技能释放请求是否允许（限速/反加速）
 * @param now_ms 当前时间（毫秒）
 * @param delay_time_ms 技能延迟时间
 * @param sword_skill 是否为剑术技能
 * @return 包含是否允许释放、是否应断开连接和超限次数
 * @details 普通技能延迟 = delay_time_ms + 800ms，剑术技能无额外延迟。
 *          连续超限 2 次后触发加速检测，8 次后要求断开。
 */
LegacySpellThrottleResult Player::begin_spell_attempt(std::uint64_t now_ms,
                                                      std::int32_t delay_time_ms,
                                                      bool sword_skill) {
  if (now_ms - latest_spell_time_ms_ > static_cast<std::uint64_t>(latest_spell_delay_ms_)) {
    spell_time_over_count_ = 0;
  } else {
    ++spell_time_over_count_;
  }

  if (spell_time_over_count_ < 2) {
    latest_spell_delay_ms_ = sword_skill ? 0 : std::max(delay_time_ms, 0) + 800;
    latest_spell_time_ms_ = now_ms;
    return LegacySpellThrottleResult{true, false, spell_time_over_count_};
  }

  if (sword_skill) {
    spell_time_over_count_ = 0;
    return LegacySpellThrottleResult{false, false, spell_time_over_count_};
  }

  latest_spell_time_ms_ = now_ms;
  ++spell_speed_hack_timer_over_count_;
  return LegacySpellThrottleResult{false, spell_speed_hack_timer_over_count_ > 8,
                                   spell_time_over_count_};
}

/**
 * @brief 检查攻击请求是否允许（基于攻击速度的限速/反加速）
 * @param now_ms 当前时间（毫秒）
 * @return 包含是否允许攻击、是否应断开连接和超限次数
 * @details 攻击间隔由 hit_speed 决定（公式：900 - hit_speed * 60）。
 *          连续超限 >= 4 次或累积超限 >= 6 次触发加速检测。
 */
LegacyAttackThrottleResult Player::begin_attack_attempt(std::uint64_t now_ms) {
  const auto interval_ms = legacy_server_attack_interval_ms(legacy_hit_speed_);
  const auto elapsed_ms =
      now_ms > latest_hit_time_ms_ ? now_ms - latest_hit_time_ms_ : 0;
  if (latest_hit_time_ms_ != 0 && interval_ms > 0 &&
      elapsed_ms < static_cast<std::uint64_t>(interval_ms)) {
    ++hit_time_over_count_;
    if (hit_time_over_count_ >= 4) {
      ++hit_speed_hack_timer_over_count_;
      return LegacyAttackThrottleResult{false, hit_speed_hack_timer_over_count_ > 8,
                                        hit_time_over_count_};
    }
    ++hit_time_over_sum_;
    if (hit_time_over_sum_ >= 6) {
      ++hit_speed_hack_timer_over_count_;
      return LegacyAttackThrottleResult{false, hit_speed_hack_timer_over_count_ > 8,
                                        hit_time_over_count_};
    }
  } else {
    hit_time_over_count_ = 0;
    if (hit_time_over_sum_ > 0) {
      --hit_time_over_sum_;
    }
  }

  latest_hit_time_ms_ = now_ms;
  return LegacyAttackThrottleResult{true, false, hit_time_over_count_};
}

/**
 * @brief 重置移动限速计数（用于安全区/传送后复位）
 */
void Player::reset_move_throttle() {
  walk_time_over_count_ = 0;
  walk_time_over_sum_ = 0;
}

/** ==================== Player：伤害/治疗/护盾结算 ==================== */

/**
 * @brief 对玩家应用伤害
 * @param amount 原始伤害值
 * @param current_tick 当前逻辑 tick
 * @return 详细的伤害结算结果（HP/MP 伤害、吸收量、护盾状态）
 * @details 伤害吸收优先级（按顺序）：
 *          1. 状态效果护盾（如护身气功）
 *          2. 魔法护盾（护身戒指，消耗 MP 代替 HP，转换率 1.5x）
 *          3. 直接扣除 HP
 * @note 如果处于"损装备毒"状态，伤害会放大 120%
 */
DamageResult Player::apply_damage(std::int32_t amount, std::uint64_t current_tick) {
  DamageResult result;
  if (amount <= 0 || character_.ability.hp == 0) {
    return result;
  }

  if (legacy_buffs_.active(LegacyBuffKind::poison_damage_armor, current_tick)) {
    amount = round_damage_120(amount);
  }

  const auto had_active_shield = has_active_shield(current_tick);
  auto remaining = amount;
  for (auto& effect : status_effects_) {
    if (remaining <= 0) {
      break;
    }
    if (effect.shield_points <= 0 || current_tick > effect.expire_tick) {
      continue;
    }
    const auto absorbed = std::min(remaining, effect.shield_points);
    effect.shield_points -= absorbed;
    remaining -= absorbed;
    result.absorbed_damage += absorbed;
    if (result.shield_name.empty()) {
      result.shield_name = effect.effect_name;
    }
  }

  // 魔法护盾（护身戒指）：用 MP 替代 HP 承受伤害，转换率 150%
  if (remaining > 0 && legacy_equipment_specials_.magic_shield && character_.ability.mp > 0) {
    auto spdam = delphi_round(static_cast<double>(remaining) * 1.5);
    const auto before_mp = static_cast<std::int32_t>(character_.ability.mp);
    if (before_mp >= spdam) {
      character_.ability.mp = clamp_u16(before_mp - spdam);
      result.mp_damage += spdam;
      result.absorbed_damage += remaining;
      remaining = 0;
    } else {
      spdam -= before_mp;
      character_.ability.mp = 0;
      result.mp_damage += before_mp;
      const auto hp_remaining = delphi_round(static_cast<double>(spdam) / 1.5);
      result.absorbed_damage += std::max(0, remaining - hp_remaining);
      remaining = hp_remaining;
    }
  }

  // 直接扣除 HP
  if (remaining > 0) {
    const auto before = static_cast<std::int32_t>(character_.ability.hp);
    character_.ability.hp = clamp_u16(before - remaining);
    result.hp_damage = before - static_cast<std::int32_t>(character_.ability.hp);
  }
  result.shield_broken =
      had_active_shield && result.absorbed_damage > 0 && !has_active_shield(current_tick);
  return result;
}

/**
 * @brief 对玩家应用治疗（HP 恢复）
 * @param amount 治疗量
 * @return 实际恢复的 HP 值（不超过 max_hp）
 */
std::int32_t Player::apply_heal(std::int32_t amount) {
  if (amount <= 0 || character_.ability.hp >= character_.ability.max_hp) {
    return 0;
  }
  const auto before = static_cast<std::int32_t>(character_.ability.hp);
  character_.ability.hp = clamp_u16(std::min(before + amount,
                                             static_cast<std::int32_t>(character_.ability.max_hp)));
  return static_cast<std::int32_t>(character_.ability.hp) - before;
}

/**
 * @brief 对玩家应用魔法恢复（MP 恢复）
 * @param amount 恢复量
 * @return 实际恢复的 MP 值（不超过 max_mp）
 */
std::int32_t Player::apply_spell(std::int32_t amount) {
  if (amount <= 0 || character_.ability.mp >= character_.ability.max_mp) {
    return 0;
  }
  const auto before = static_cast<std::int32_t>(character_.ability.mp);
  character_.ability.mp = clamp_u16(std::min(before + amount,
                                             static_cast<std::int32_t>(character_.ability.max_mp)));
  return static_cast<std::int32_t>(character_.ability.mp) - before;
}

/** ==================== Player：HP/MP 自动恢复系统（传统机制） ==================== */

/**
 * @brief 排队等待逐步恢复的 HP/MP/治疗量
 * @param hp 等待恢复的 HP 量
 * @param mp 等待恢复的 MP 量
 * @param healing 额外治疗量
 * @param current_tick 当前逻辑 tick
 * @param tick_interval 每次恢复的 tick 间隔
 * @details 累积值上限为 kLegacyHealingCap（300），每次 tick 实际恢复量
 *          受等级影响：基础 5 + 等级/10。用于实现吃药/打坐等持续恢复效果。
 */
void Player::queue_legacy_health_spell(std::int32_t hp, std::int32_t mp,
                                       std::int32_t healing,
                                       std::uint64_t current_tick,
                                       std::uint64_t tick_interval) {
  if (character_.ability.hp == 0) {
    return;
  }
  if (hp > 0 && character_.ability.hp < character_.ability.max_hp) {
    legacy_inc_health_ =
        std::min(kLegacyHealingCap, legacy_inc_health_ + std::max(hp, 0));
  }
  if (mp > 0 && character_.ability.mp < character_.ability.max_mp) {
    legacy_inc_spell_ =
        std::min(kLegacyHealingCap, legacy_inc_spell_ + std::max(mp, 0));
  }
  if (healing > 0 && character_.ability.hp < character_.ability.max_hp) {
    legacy_inc_healing_ =
        std::min(kLegacyHealingCap, legacy_inc_healing_ + std::max(healing, 0));
  }
  if (legacy_inc_health_ <= 0 && legacy_inc_spell_ <= 0 && legacy_inc_healing_ <= 0) {
    return;
  }
  legacy_health_spell_tick_interval_ = std::max<std::uint64_t>(tick_interval, 1);
  if (legacy_next_health_spell_tick_ == 0 ||
      current_tick >= legacy_next_health_spell_tick_) {
    legacy_next_health_spell_tick_ = current_tick + legacy_health_spell_tick_interval_;
  }
}

/**
 * @brief 排队等待治疗（简化的 queue_legacy_health_spell）
 * @param amount 治疗量
 * @param current_tick 当前逻辑 tick
 * @param tick_interval 恢复间隔
 */
void Player::queue_legacy_healing(std::int32_t amount, std::uint64_t current_tick,
                                  std::uint64_t tick_interval) {
  queue_legacy_health_spell(0, 0, amount, current_tick, tick_interval);
}

/**
 * @brief 检查是否有待处理的治疗量
 * @return 待处理治疗量 > 0 返回 true
 */
bool Player::legacy_healing_pending() const { return legacy_inc_healing_ > 0; }

/**
 * @brief 执行一次 HP/MP 自动恢复 tick
 * @param current_tick 当前逻辑 tick
 * @return 本次恢复的 HP/MP 量和变化状态
 * @details 每次恢复：HP = max(1, 5 + 等级/10)，MP = 同上，治疗 = 5
 *          HP/MP 满时自动清零等待队列
 */
LegacyHealthSpellTickResult Player::tick_legacy_health_spell(std::uint64_t current_tick) {
  LegacyHealthSpellTickResult result;
  if (character_.ability.hp == 0 ||
      (legacy_inc_health_ <= 0 && legacy_inc_spell_ <= 0 && legacy_inc_healing_ <= 0)) {
    legacy_next_health_spell_tick_ = current_tick;
    return result;
  }
  if (legacy_next_health_spell_tick_ == 0 ||
      current_tick < legacy_next_health_spell_tick_) {
    return result;
  }

  const auto per_health =
      std::max<std::int32_t>(1, 5 + static_cast<std::int32_t>(character_.ability.level) / 10);
  const auto per_spell = per_health;
  constexpr std::int32_t kLegacyPerHealing = 5;

  auto hp_amount = std::min(legacy_inc_health_, per_health);
  if (hp_amount > 0) {
    const auto healed = apply_heal(hp_amount);
    result.hp += healed;
    legacy_inc_health_ -= hp_amount;
  }

  auto mp_amount = std::min(legacy_inc_spell_, per_spell);
  if (mp_amount > 0) {
    const auto restored = apply_spell(mp_amount);
    result.mp += restored;
    legacy_inc_spell_ -= mp_amount;
  }

  auto healing_amount = std::min(legacy_inc_healing_, kLegacyPerHealing);
  if (healing_amount > 0) {
    const auto healed = apply_heal(healing_amount);
    result.hp += healed;
    legacy_inc_healing_ -= healing_amount;
  }

  if (character_.ability.hp >= character_.ability.max_hp) {
    legacy_inc_health_ = 0;
    legacy_inc_healing_ = 0;
  }
  if (character_.ability.mp >= character_.ability.max_mp) {
    legacy_inc_spell_ = 0;
  }
  if (legacy_inc_health_ <= 0 && legacy_inc_spell_ <= 0 && legacy_inc_healing_ <= 0) {
    legacy_next_health_spell_tick_ = 0;
  } else {
    legacy_next_health_spell_tick_ +=
        std::max<std::uint64_t>(legacy_health_spell_tick_interval_, 1);
  }
  result.changed = result.hp > 0 || result.mp > 0;
  return result;
}

/** ==================== Player：MP 消耗与特殊能力 ==================== */

/**
 * @brief 消耗玩家的魔法值（MP）
 * @param amount 消耗量
 * @return 消耗成功返回 true，MP 不足或参数无效返回 false
 */
bool Player::spend_mp(std::int32_t amount) {
  if (amount < 0) {
    return false;
  }
  if (static_cast<std::int32_t>(character_.ability.mp) < amount) {
    return false;
  }
  character_.ability.mp = clamp_u16(static_cast<std::int32_t>(character_.ability.mp) - amount);
  return true;
}

/**
 * @brief 检查复活戒指是否可用（60 秒冷却）
 * @param now_ms 当前时间（毫秒）
 * @return 装备了复活戒指且冷却已过则返回 true
 */
bool Player::legacy_revival_available(std::uint64_t now_ms) const {
  return legacy_equipment_specials_.revival &&
         now_ms > latest_legacy_revival_time_ms_ + 60ULL * 1000ULL;
}

/**
 * @brief 标记复活戒指已使用
 * @param now_ms 当前时间（毫秒）
 */
void Player::mark_legacy_revival(std::uint64_t now_ms) {
  latest_legacy_revival_time_ms_ = now_ms;
}

/**
 * @brief 应用吸血效果（基于造成伤害的比例恢复 HP）
 * @param damage 造成的伤害值
 * @return 实际恢复的 HP 量
 * @details 使用累积器避免舍入误差：
 *          每次累加 (damage / 100) * suck_rate，累积 >= 2 时消耗恢复
 */
std::int32_t Player::apply_legacy_suck_health(std::int32_t damage) {
  if (damage <= 0 || legacy_equipment_specials_.suck_health_rate <= 0 ||
      character_.ability.hp == 0) {
    return 0;
  }
  legacy_suck_health_accumulator_ +=
      static_cast<double>(damage) / 100.0 *
      static_cast<double>(legacy_equipment_specials_.suck_health_rate);
  if (legacy_suck_health_accumulator_ < 2.0) {
    return 0;
  }
  const auto amount = static_cast<std::int32_t>(legacy_suck_health_accumulator_);
  legacy_suck_health_accumulator_ -= static_cast<double>(amount);
  return apply_heal(amount);
}

/** ==================== Player：经验值获取与升级 ==================== */

/**
 * @brief 玩家获得经验值
 * @param amount 经验值量
 * @return 包含实际获得经验、显示经验和是否升级的结果
 * @details 如果经验达到升级要求则自动升级，属性加成：
 *          - max_hp += 5，max_mp += 3
 *          经验获得上限 60000/次
 */
ExperienceResult Player::gain_experience(std::int32_t amount) {
  ExperienceResult result;
  result.gained = std::clamp(amount, 0, 60000);
  if (result.gained <= 0) {
    result.display_exp = static_cast<std::int32_t>(character_.ability.exp);
    return result;
  }

  character_.ability.exp += static_cast<std::uint32_t>(result.gained);
  result.display_exp = static_cast<std::int32_t>(character_.ability.exp);

  if (character_.ability.max_exp > 0 && character_.ability.exp >= character_.ability.max_exp) {
    character_.ability.exp -= character_.ability.max_exp;
    character_.ability.level = clamp_u8(static_cast<std::int32_t>(character_.ability.level) + 1);
    base_ability_.level = character_.ability.level;
    base_ability_.max_hp = clamp_u16(static_cast<std::int32_t>(base_ability_.max_hp) + 5);
    base_ability_.max_mp = clamp_u16(static_cast<std::int32_t>(base_ability_.max_mp) + 3);
    base_ability_.max_exp = next_level_exp(character_.ability.level);
    character_.ability.max_hp = base_ability_.max_hp;
    character_.ability.max_mp = base_ability_.max_mp;
    character_.ability.max_exp = base_ability_.max_exp;
    result.leveled_up = true;
  }
  base_ability_.level = character_.ability.level;
  base_ability_.exp = character_.ability.exp;
  base_ability_.max_exp = character_.ability.max_exp;

  return result;
}

/** ==================== Player：状态效果管理 ==================== */

/**
 * @brief 添加一个定时状态效果（DOT/HOT/护盾/减速）
 * @param effect 要添加的效果（通过移动语义传入）
 * @details 自动设置 tick_interval（最小为 1）和 next_tick，
 *          效果必须具有实际负载且过期时间不为 0
 */
void Player::add_status_effect(TimedStatusEffect effect) {
  if (!effect_has_payload(effect) || effect.expire_tick == 0) {
    return;
  }
  effect.tick_interval = std::max<std::uint64_t>(effect.tick_interval, 1);
  effect.next_tick = std::max(effect.next_tick, effect.tick_interval);
  status_effects_.push_back(std::move(effect));
}

/** ==================== Player：毒药/Buff 激活管理 ==================== */

/**
 * @brief 对玩家施加传统毒药效果
 * @param poison_kind 毒药类型（状态位索引）
 * @param duration_ticks 持续 tick 数
 * @param poison_level 毒药等级
 * @param poison_tick_interval 伤害触发间隔
 * @param source_actor_id 施毒者 ID
 * @param current_tick 当前逻辑 tick
 * @return 毒药是否被新激活（刷新已有毒药返回 false）
 * @note 仅支持减血毒、损装备毒、定身毒、石化四种毒药
 */
bool Player::apply_legacy_poison(std::int32_t poison_kind, std::uint64_t duration_ticks,
                                 std::int32_t poison_level,
                                 std::uint64_t poison_tick_interval,
                                 std::uint64_t source_actor_id,
                                 std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto kind = legacy_buff_kind_from_status_bit(poison_kind);
  if (!kind.has_value() ||
      (*kind != LegacyBuffKind::poison_dechealth &&
       *kind != LegacyBuffKind::poison_damage_armor &&
       *kind != LegacyBuffKind::poison_stone &&
       *kind != LegacyBuffKind::poison_dont_move)) {
    return false;
  }
  const auto new_expire_tick = current_tick + duration_ticks;
  const auto tick_interval = std::max<std::uint64_t>(poison_tick_interval, 1);
  const auto next_tick =
      *kind == LegacyBuffKind::poison_dechealth ? current_tick + tick_interval : 0;
  const auto changed = legacy_buffs_.activate_or_refresh(
      make_legacy_buff(*kind, new_expire_tick, next_tick, tick_interval,
                       std::max(poison_level, 0), source_actor_id),
      current_tick);
  set_legacy_status_bit(character_.status, poison_kind, true);
  return changed;
}

/**
 * @brief 激活物理防御提升 Buff
 * @param duration_ticks 持续 tick 数
 * @param current_tick 当前逻辑 tick
 * @return 是否新激活（刷新已有返回 false）
 */
bool Player::activate_legacy_defence_up(std::uint64_t duration_ticks,
                                        std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  const auto changed = legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::defence_up, expire_tick), current_tick);
  set_legacy_status_bit(character_.status, kStateDefenceUp, true);
  return changed;
}

/**
 * @brief 激活魔法防御提升 Buff
 * @param duration_ticks 持续 tick 数
 * @param current_tick 当前逻辑 tick
 * @return 是否新激活（刷新已有返回 false）
 */
bool Player::activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                              std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  const auto changed = legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::magic_defence_up, expire_tick), current_tick);
  set_legacy_status_bit(character_.status, kStateMagicDefenceUp, true);
  return changed;
}

/**
 * @brief 激活攻击力提升 Buff（DC up）
 * @param duration_ticks 持续 tick 数
 * @param current_tick 当前逻辑 tick
 * @param bonus 攻击力加成值
 * @return 是否新激活（刷新已有返回 false）
 * @note DC up Buff 没有对应的传统状态位（status_bit = -1）
 */
bool Player::activate_legacy_dc_up(std::uint64_t duration_ticks,
                                   std::uint64_t current_tick,
                                   std::int32_t bonus) {
  if (duration_ticks == 0 || bonus <= 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  return legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::dc_up, expire_tick, 0, 1, bonus), current_tick);
}

/**
 * @brief 检查玩家是否处于透明/隐身状态
 * @param current_tick 当前逻辑 tick
 * @return 如果 alive 且（装备透明或 Buff 透明）则返回 true
 */
bool Player::legacy_transparent_active(std::uint64_t current_tick) const {
  return character_.ability.hp > 0 &&
         (legacy_equipment_specials_.equipment_transparent ||
          legacy_buffs_.active(LegacyBuffKind::transparent, current_tick));
}

/**
 * @brief 激活透明/隐身 Buff
 * @param duration_ticks 持续 tick 数
 * @param current_tick 当前逻辑 tick
 * @return 激活成功返回 true，已处于透明状态则返回 false
 */
bool Player::activate_legacy_transparent(std::uint64_t duration_ticks,
                                         std::uint64_t current_tick) {
  if (duration_ticks == 0 || legacy_transparent_active(current_tick)) {
    return false;
  }
  static_cast<void>(legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::transparent, current_tick + duration_ticks),
      current_tick));
  set_legacy_status_bit(character_.status, kStateTransparent, true);
  return true;
}

/**
 * @brief 清除透明/隐身 Buff
 * @param current_tick 当前逻辑 tick
 * @return 清除成功返回 true，无透明 Buff 则返回 false
 * @note 如果装备有透明效果，清除后仍保持状态位
 */
bool Player::clear_legacy_transparent(std::uint64_t current_tick) {
  if (!legacy_buffs_.active(LegacyBuffKind::transparent, current_tick)) {
    return false;
  }
  static_cast<void>(legacy_buffs_.clear(LegacyBuffKind::transparent));
  set_legacy_status_bit(character_.status, kStateTransparent,
                        legacy_equipment_specials_.equipment_transparent);
  return true;
}

/**
 * @brief 检查损装备毒是否激活
 */
bool Player::legacy_poison_damage_armor_active(std::uint64_t current_tick) const {
  return legacy_buffs_.active(LegacyBuffKind::poison_damage_armor, current_tick);
}

/**
 * @brief 清除所有负面状态效果（DOT/减速等）
 * @param current_tick 当前逻辑 tick
 * @return 清除的数量
 * @details 如果有任何清除，同时重置移动限制（允许立即移动）
 */
std::size_t Player::clear_negative_status_effects(std::uint64_t current_tick) {
  const auto before = status_effects_.size();
  status_effects_.erase(
      std::remove_if(status_effects_.begin(), status_effects_.end(),
                     [](const TimedStatusEffect& effect) { return is_negative_effect(effect); }),
      status_effects_.end());
  if (status_effects_.size() != before) {
    next_move_tick_ = std::min(next_move_tick_, current_tick);
  }
  return before - status_effects_.size();
}

/**
 * @brief 清除所有负面传统 Buff（四种毒药）
 * @param current_tick 当前逻辑 tick
 * @return 清除的毒药种类数量
 * @details 清除减血毒、损装备毒、定身毒、石化，如果有定身/石化被清除，
 *          则允许立即移动
 */
std::size_t Player::clear_negative_legacy_buffs(std::uint64_t current_tick) {
  std::size_t cleared = 0;
  auto movement_released = false;
  for (const auto kind : {LegacyBuffKind::poison_dechealth,
                          LegacyBuffKind::poison_damage_armor,
                          LegacyBuffKind::poison_dont_move,
                          LegacyBuffKind::poison_stone}) {
    if (legacy_buffs_.clear(kind)) {
      ++cleared;
      movement_released = movement_released ||
                          kind == LegacyBuffKind::poison_dont_move ||
                          kind == LegacyBuffKind::poison_stone;
      set_legacy_status_bit(character_.status, static_cast<std::int32_t>(kind), false);
    }
  }
  if (movement_released) {
    next_move_tick_ = std::min(next_move_tick_, current_tick);
  }
  return cleared;
}

/** ==================== Player：Buff 批量清除与传输 ==================== */

/**
 * @brief 死亡时清除 Buff
 * @return 包含状态位和属性变化的结果
 * @details 清除所有标记为 clear_on_death 的 Buff，并重置临时状态位
 */
StatusTickResult Player::clear_legacy_buffs_on_death(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::death);
  if (cleared.status_changed) {
    clear_transient_legacy_status_bits(character_.status);
    result.legacy_status_changed = true;
  }
  result.ability_changed = cleared.ability_changed;
  return result;
}

/**
 * @brief 离开地图时清除 Buff
 * @return 清除结果
 * @details 清除透明、定身、石化三种 Buff（跨地图时不应带走的限制状态）
 */
StatusTickResult Player::clear_legacy_buffs_on_leave_map(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::leave_map);
  if (cleared.status_changed) {
    set_legacy_status_bit(character_.status, kStateTransparent, false);
    set_legacy_status_bit(character_.status, kPoisonDontMove, false);
    set_legacy_status_bit(character_.status, kPoisonStone, false);
    result.legacy_status_changed = true;
  }
  result.ability_changed = cleared.ability_changed;
  return result;
}

/**
 * @brief 登出时清除所有 Buff
 * @return 清除结果
 */
StatusTickResult Player::clear_legacy_buffs_on_logout(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::logout);
  if (cleared.status_changed) {
    clear_transient_legacy_status_bits(character_.status);
    result.legacy_status_changed = true;
  }
  result.ability_changed = cleared.ability_changed;
  return result;
}

/**
 * @brief 获取所有需要跨地图传输的 Buff 列表
 * @param current_tick 当前逻辑 tick
 * @return Buff 传输状态列表（已过滤过期的）
 */
std::vector<LegacyBuffTransferState> Player::legacy_buffs_for_transfer(
    std::uint64_t current_tick) const {
  std::vector<LegacyBuffTransferState> result;
  for (const auto& state : legacy_buffs_.snapshot()) {
    if (state.expire_tick == 0 || state.expire_tick < current_tick) {
      continue;
    }
    result.push_back(LegacyBuffTransferState{
        static_cast<std::int32_t>(state.kind),
        state.expire_tick,
        state.next_tick,
        state.tick_interval,
        state.level,
        state.source_actor_id});
  }
  return result;
}

/**
 * @brief 从传输数据恢复 Buff 状态（跨地图后调用）
 * @param states 传输的 Buff 状态列表
 * @param current_tick 当前逻辑 tick
 * @details 只恢复已知的 Buff 类型，过滤已过期的 Buff，恢复后同步设置状态位
 */
void Player::restore_legacy_buffs_from_transfer(
    const std::vector<LegacyBuffTransferState>& states,
    std::uint64_t current_tick) {
  for (const auto& transfer : states) {
    const auto kind = static_cast<LegacyBuffKind>(transfer.kind);
    switch (kind) {
      case LegacyBuffKind::poison_dechealth:
      case LegacyBuffKind::poison_damage_armor:
      case LegacyBuffKind::poison_dont_move:
      case LegacyBuffKind::poison_stone:
      case LegacyBuffKind::transparent:
      case LegacyBuffKind::defence_up:
      case LegacyBuffKind::magic_defence_up:
      case LegacyBuffKind::bubble_defence_up:
      case LegacyBuffKind::dc_up:
        break;
      default:
        continue;
    }
    if (transfer.expire_tick == 0 || transfer.expire_tick < current_tick) {
      continue;
    }
    auto state = make_legacy_buff(kind, transfer.expire_tick, transfer.next_tick,
                                  transfer.tick_interval, transfer.level,
                                  transfer.source_actor_id);
    if (legacy_buffs_.activate_or_refresh(state, current_tick) && state.status_bit >= 0) {
      set_legacy_status_bit(character_.status, state.status_bit, true);
    }
  }
}

/** ==================== Player：状态效果统一 Tick 处理 ==================== */

/**
 * @brief 统一处理所有定时状态效果和 Buff 的 tick
 * @param current_tick 当前逻辑 tick
 * @return 汇总的状态变化结果（伤害/治疗/护盾/属性变化等）
 * @details 处理顺序：
 *          1. 遍历 status_effects_，触发到期的 DOT/HOT
 *          2. 清理过期的效果
 *          3. 处理过期的 legacy Buffs（更新状态位）
 *          4. 检查武器技能是否过期
 *          5. 处理减血毒（poison_dechealth）的定时伤害
 * @note 如果伤害导致 HP=0，同时有等待恢复的 HP/MP，会延迟恢复 tick
 */
StatusTickResult Player::tick_status_effects(std::uint64_t current_tick) {
  StatusTickResult result;
  for (auto it = status_effects_.begin(); it != status_effects_.end();) {
    while (current_tick >= it->next_tick && it->next_tick <= it->expire_tick) {
      if (it->damage_per_tick > 0) {
        const auto applied = apply_damage(it->damage_per_tick, current_tick);
        result.damage += applied.hp_damage;
        result.absorbed_damage += applied.absorbed_damage;
        result.shield_broken = result.shield_broken || applied.shield_broken;
        if (result.shield_name.empty() && !applied.shield_name.empty()) {
          result.shield_name = applied.shield_name;
        }
        if (applied.hp_damage > 0 && it->source_actor_id != 0) {
          result.source_actor_id = it->source_actor_id;
        }
      }
      if (it->heal_per_tick > 0) {
        result.heal += apply_heal(it->heal_per_tick);
      }
      it->next_tick += std::max<std::uint64_t>(it->tick_interval, 1);
      if (character_.ability.hp == 0) {
        break;
      }
    }

    if (current_tick > it->expire_tick && it->shield_points > 0) {
      result.shield_expired = true;
      if (result.shield_name.empty()) {
        result.shield_name = it->effect_name;
      }
    }

    if (current_tick > it->expire_tick &&
        ((!effect_has_payload(*it)) || (it->damage_per_tick <= 0 && it->heal_per_tick <= 0) ||
         it->next_tick > it->expire_tick)) {
      it = status_effects_.erase(it);
    } else {
      ++it;
    }
  }

  const auto expired_buffs = legacy_buffs_.expire_due(current_tick);
  if (!expired_buffs.empty()) {
    clear_player_status_bits(character_.status, expired_buffs);
    if (legacy_equipment_specials_.equipment_transparent) {
      set_legacy_status_bit(character_.status, kStateTransparent, true);
    }
    result.legacy_status_changed = true;
    result.ability_changed =
        std::any_of(expired_buffs.begin(), expired_buffs.end(),
                    [](const LegacyBuffState& state) { return state.affects_ability; });
  }
  if (legacy_prepared_sword_expire_tick_ != 0 && current_tick > legacy_prepared_sword_expire_tick_) {
    clear_legacy_sword_skill();
  }
  // 处理减血毒的定时伤害
  while (character_.ability.hp > 0) {
    auto* poison = legacy_buffs_.tick_due(LegacyBuffKind::poison_dechealth, current_tick);
    if (poison == nullptr) {
      break;
    }
    const auto applied = apply_damage(1 + poison->level, current_tick);
    result.damage += applied.hp_damage;
    result.absorbed_damage += applied.absorbed_damage;
    result.shield_broken = result.shield_broken || applied.shield_broken;
    if (result.shield_name.empty() && !applied.shield_name.empty()) {
      result.shield_name = applied.shield_name;
    }
    if (applied.hp_damage > 0 && poison->source_actor_id != 0) {
      result.source_actor_id = poison->source_actor_id;
    }
    if (applied.hp_damage > 0 &&
        (legacy_inc_health_ > 0 || legacy_inc_spell_ > 0 || legacy_inc_healing_ > 0)) {
      legacy_next_health_spell_tick_ =
          current_tick + std::max<std::uint64_t>(legacy_health_spell_tick_interval_, 1);
    }
    poison->next_tick += std::max<std::uint64_t>(poison->tick_interval, 1);
    if (character_.ability.hp == 0) {
      break;
    }
  }
  return result;
}

/** ==================== Player：移动/装备/消耗品操作 ==================== */

/**
 * @brief 消耗一次移动操作，计算下次允许移动的时间
 * @param current_tick 当前逻辑 tick
 * @param running 是否为跑步模式（当前未区分，均为 250ms）
 * @param tick_ms 每个 tick 的毫秒数
 * @details 移动间隔基础为 250ms（跑步和走路相同），
 *          受减速效果影响：interval = base * (100 + slow%) / 100
 */
void Player::consume_move_action(std::uint64_t current_tick, bool running, std::uint32_t tick_ms) {
  const auto slow_percent = std::max(current_slow_percent(current_tick), 0);
  const auto base_interval = ms_to_ticks(running ? 250U : 250U, tick_ms);
  const auto interval = std::max<std::uint64_t>(
      1, (base_interval * static_cast<std::uint64_t>(100 + slow_percent) + 99) / 100);
  next_move_tick_ = current_tick + interval;
}

/**
 * @brief 将 HP 和 MP 恢复到最大值
 */
void Player::restore_full_vitals() {
  character_.ability.hp = character_.ability.max_hp;
  character_.ability.mp = character_.ability.max_mp;
}

/**
 * @brief 在指定装备槽位放置物品
 * @param slot 装备槽位
 * @param item 要装备的物品
 */
void Player::equip_item(std::size_t slot, const LegacyUserItem& item) {
  if (slot >= character_.equipped_items.size()) {
    return;
  }
  character_.equipped_items[slot] = item;
}

/**
 * @brief 应用消耗品效果（直接恢复 HP/MP）
 * @param item_config 消耗品配置
 * @details 根据配置的 hp_add/mp_add 值直接增减 HP/MP，然后钳位到最大值
 */
void Player::apply_consumable(const ItemConfig& item_config) {
  character_.ability.hp =
      clamp_u16(static_cast<std::int32_t>(character_.ability.hp) + item_config.hp_add);
  character_.ability.mp =
      clamp_u16(static_cast<std::int32_t>(character_.ability.mp) + item_config.mp_add);
  character_.ability.hp = std::min(character_.ability.hp, character_.ability.max_hp);
  character_.ability.mp = std::min(character_.ability.mp, character_.ability.max_mp);
}

/** ==================== Player：金币管理 ==================== */

/**
 * @brief 增加金币（不超过背包上限 kLegacyBagGold）
 * @param amount 增加量
 */
void Player::add_gold(std::int32_t amount) {
  const auto next = static_cast<std::int64_t>(character_.gold) + amount;
  character_.gold =
      static_cast<std::int32_t>(std::clamp<std::int64_t>(next, 0, kLegacyBagGold));
}

/**
 * @brief 花费金币
 * @param amount 花费量（必须为正数）
 */
void Player::spend_gold(std::int32_t amount) {
  if (amount > 0) {
    character_.gold = std::max(0, character_.gold - amount);
  }
}

/** ==================== Player：属性设置器 ==================== */

/**
 * @brief 设置玩家等级
 * @param level 目标等级（钳位到 1-255）
 * @param max_level 最大允许等级
 * @details 同时更新 base_ability_ 中的等级和经验上限
 */
void Player::set_legacy_level(std::int32_t level, std::int32_t max_level) {
  const auto clamped = std::clamp(level, 1, std::max(max_level, 1));
  character_.ability.level = static_cast<std::uint8_t>(std::clamp(clamped, 1, 255));
  character_.ability.max_exp = next_level_exp(character_.ability.level);
  base_ability_.level = character_.ability.level;
  base_ability_.max_exp = character_.ability.max_exp;
}

/**
 * @brief 设置玩家经验值
 * @param exp 经验值（最小为 0）
 */
void Player::set_legacy_exp(std::int32_t exp) {
  character_.ability.exp = static_cast<std::uint32_t>(std::max(exp, 0));
  base_ability_.exp = character_.ability.exp;
}

/**
 * @brief 设置 PK 值
 * @param value PK 值（最小为 0）
 */
void Player::set_pk_point(std::int32_t value) {
  character_.pk_point = std::max(value, 0);
}

/**
 * @brief 设置身体幸运值
 */
void Player::set_body_luck_value(double value) {
  character_.body_luck = value;
}

/**
 * @brief 设置发型
 */
void Player::set_hair(std::int32_t value) {
  character_.hair = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

/**
 * @brief 设置职业（0=战士, 1=法师, 2=道士）
 */
void Player::set_job(std::int32_t value) {
  character_.job = static_cast<std::uint8_t>(std::clamp(value, 0, 2));
}

/**
 * @brief 切换性别（0<->1）
 */
void Player::toggle_sex() {
  character_.sex = character_.sex == 0 ? 1 : 0;
}

/**
 * @brief 设置名字颜色
 */
void Player::set_legacy_name_color(std::int32_t value) {
  legacy_name_color_ = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

/**
 * @brief 标记出生物品已发放
 */
void Player::mark_birth_items_granted() {
  character_.birth_items_granted = true;
}

/**
 * @brief 设置公会成员信息
 */
void Player::set_guild_membership(std::string guild_name, std::string guild_title) {
  character_.guild_name = std::move(guild_name);
  character_.guild_title = std::move(guild_title);
}

/**
 * @brief 清除公会成员信息
 */
void Player::clear_guild_membership() {
  character_.guild_name.clear();
  character_.guild_title.clear();
}

/** ==================== Player：任务/脚本参数设置 ==================== */

/**
 * @brief 设置任务标记位
 * @param index 位索引
 * @param value 要设置的位值（0 或 1）
 * @return 设置成功返回 true
 */
bool Player::set_quest_mark(std::int32_t index, std::uint8_t value) {
  return set_legacy_bit_mark(character_.quest_marks, index, value);
}

/**
 * @brief 设置任务开启单元标记位
 * @param index 位索引
 * @param value 要设置的位值（0 或 1）
 * @return 设置成功返回 true
 */
bool Player::set_quest_open_unit(std::int32_t index, std::uint8_t value) {
  return set_legacy_bit_mark(character_.quest_open_units, index, value);
}

/**
 * @brief 设置任务单元标记位
 * @param index 位索引
 * @param value 要设置的位值（0 或 1）
 * @return 设置成功返回 true
 */
bool Player::set_quest_unit(std::int32_t index, std::uint8_t value) {
  return set_legacy_bit_mark(character_.quest_units, index, value);
}

/**
 * @brief 设置脚本参数
 * @return 索引有效返回 true，越界返回 false
 */
bool Player::set_script_param(std::int32_t index, std::int32_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.script_params.size()) {
    return false;
  }
  character_.script_params[static_cast<std::size_t>(index)] = value;
  return true;
}

/**
 * @brief 设置脚本骰子参数
 * @return 索引有效返回 true，越界返回 false
 */
bool Player::set_script_dice_param(std::int32_t index, std::int32_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= script_dice_params_.size()) {
    return false;
  }
  script_dice_params_[static_cast<std::size_t>(index)] = value;
  return true;
}

/**
 * @brief 设置玩家的每日任务 ID
 * @param value 每日任务 ID
 */
void Player::set_daily_quest(std::uint32_t value) {
  character_.daily_quest = value;
}

/** ==================== Player：装备属性刷新（核心方法） ==================== */

/**
 * @brief 重新计算所有衍生属性（装备变更后调用）
 * @param item_configs 物品配置表
 * @details 这是玩家属性计算的核心方法，在装备变更后重新计算所有面板属性。
 *          处理内容包括：
 *          1. 遍历所有已装备物品，累加各部位的属性加成
 *          2. 处理特殊装备效果（std_mode 分支逻辑）：
 *             - mode 5/6：幸运/诅咒/命中/攻击速度
 *             - mode 19：特殊幸运/诅咒
 *             - mode 20/24：准确/敏捷
 *             - mode 52：负重/防御/魔防
 *             - mode 21：攻击速度调整
 *             - mode 53：HP/MP 上限增加
 *             - mode 23：毒物抗性
 *          3. 收集装备特殊属性（透明/石化/复活/魔法护盾/吸血/魔法转生命）
 *          4. 处理套装效果（三件魔法转生命装备同时穿戴时额外+50）
 *          5. 计算背包总重量和穿戴重量
 *          6. 更新角色外观特征（衣服/武器 shape）
 * @note 这是一个高开销方法，仅在装备变更时调用
 * @see LegacyEquipmentSpecials
 */
void Player::refresh_derived_state(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  base_ability_.level = character_.ability.level;
  base_ability_.exp = character_.ability.exp;
  base_ability_.max_exp = character_.ability.max_exp > 0
                              ? character_.ability.max_exp
                              : next_level_exp(character_.ability.level);
  const auto current_hp = character_.ability.hp;
  const auto current_mp = character_.ability.mp;
  auto derived = base_ability_;
  LegacyEquipmentSpecials specials;
  auto mana_to_health_ring = false;
  auto mana_to_health_bracelet = false;
  auto mana_to_health_necklace = false;

  // Lambda：遍历每件已装备物品，累加其属性加成
  auto add_equipment_stats = [&](std::size_t slot, const LegacyUserItem& item) {
    if (is_empty(item) || item.dura == 0) {
      return;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      return;
    }
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    switch (upgraded.std_mode) {
      case 5:
      case 6:
        accuracy_point_ += packed_max(upgraded.ac);
        if (slot == kEquipWeapon) {
          const auto mac_high = packed_max(upgraded.mac);
          legacy_hit_speed_ += mac_high > 10 ? mac_high - 10 : -mac_high;
        }
        specials.luck += packed_min(upgraded.ac);
        specials.unluck += packed_min(upgraded.mac);
        break;
      case 19:
        specials.unluck += packed_min(upgraded.mac);
        specials.luck += packed_max(upgraded.mac);
        break;
      case 20:
      case 24:
        accuracy_point_ += packed_max(upgraded.ac);
        speed_point_ += packed_max(upgraded.mac);
        break;
      case 52:
        if (upgraded.eff_type1 == 1) {
          derived.max_hand_weight = clamp_u8(
              static_cast<std::int32_t>(derived.max_hand_weight) + upgraded.eff_value1);
        } else if (upgraded.eff_type1 == 2) {
          derived.max_wear_weight = clamp_u8(
              static_cast<std::int32_t>(derived.max_wear_weight) + upgraded.eff_value1);
        }
        if (upgraded.eff_type2 == 1) {
          derived.max_hand_weight = clamp_u8(
              static_cast<std::int32_t>(derived.max_hand_weight) + upgraded.eff_value2);
        } else if (upgraded.eff_type2 == 2) {
          derived.max_wear_weight = clamp_u8(
              static_cast<std::int32_t>(derived.max_wear_weight) + upgraded.eff_value2);
        }
        derived.ac = add_packed_range(derived.ac, upgraded.ac);
        derived.mac = add_packed_range(derived.mac, upgraded.mac);
        break;
      case 21:
        legacy_hit_speed_ += packed_min(upgraded.ac);
        legacy_hit_speed_ -= packed_min(upgraded.mac);
        break;
      case 53:
        break;
      case 23:
        specials.anti_poison += packed_max(upgraded.ac);
        legacy_hit_speed_ += packed_min(upgraded.ac);
        legacy_hit_speed_ -= packed_min(upgraded.mac);
        break;
      default:
        derived.ac = add_packed_range(derived.ac, upgraded.ac);
        derived.mac = add_packed_range(derived.mac, upgraded.mac);
        break;
    }
    derived.dc = add_packed_range(derived.dc, upgraded.dc);
    derived.mc = add_packed_range(derived.mc, upgraded.mc);
    derived.sc = add_packed_range(derived.sc, upgraded.sc);
    if (upgraded.std_mode == 53) {
      derived.max_hp = clamp_u16(static_cast<std::int32_t>(derived.max_hp) + upgraded.hp_add);
      derived.max_mp = clamp_u16(static_cast<std::int32_t>(derived.max_mp) + upgraded.mp_add);
    }
    specials.undead_power += std::max(upgraded.undead, 0);
    if (slot == kEquipWeapon) {
      if (config->special_pwr <= -1 && config->special_pwr >= -50) {
        specials.undead_power += -config->special_pwr;
      } else if (config->special_pwr <= -51 && config->special_pwr >= -100) {
        specials.undead_power += config->special_pwr + 50;
      }
    }
    // 检测特殊戒指效果
    if ((slot == kEquipRingLeft || slot == kEquipRingRight) &&
        config->shape == kLegacyRingTransparentItem) {
      specials.equipment_transparent = true;
    }
    if ((slot == kEquipRingLeft || slot == kEquipRingRight) &&
        config->shape == kLegacyRingMakeStoneItem) {
      specials.make_stone = true;
    }
    if ((slot == kEquipRingLeft || slot == kEquipRingRight) &&
        config->shape == kLegacyRingRevivalItem) {
      specials.revival = true;
    }
    if ((slot == kEquipRingLeft || slot == kEquipRingRight) &&
        config->shape == kLegacyRingMagicShieldItem) {
      specials.magic_shield = true;
    }
    // 检测魔法转生命和吸血装备
    if (legacy_shape_matches_slot(slot, config->shape)) {
      if (legacy_mana_to_health_shape(config->shape)) {
        specials.mana_to_health += std::max(config->ani_count, 0);
        mana_to_health_ring = mana_to_health_ring ||
                              config->shape == kLegacyRingManaToHealthItem;
        mana_to_health_bracelet =
            mana_to_health_bracelet ||
            config->shape == kLegacyBraceletManaToHealthItem;
        mana_to_health_necklace =
            mana_to_health_necklace ||
            config->shape == kLegacyNecklaceManaToHealthItem;
      } else if (legacy_suck_health_shape(config->shape)) {
        specials.suck_health_rate += std::max(config->ani_count, 0);
      }
    }
  };

  accuracy_point_ = base_ability_.reserved1 > 0 ? base_ability_.reserved1 : 10;
  speed_point_ = base_ability_.exp_count > 0 ? base_ability_.exp_count : 10;
  legacy_hit_speed_ = 0;
  for (std::size_t slot = 0; slot < character_.equipped_items.size(); ++slot) {
    add_equipment_stats(slot, character_.equipped_items[slot]);
  }
  // 套装效果：同时佩戴戒指+手镯+项链时额外 +50 转换量
  if (mana_to_health_ring && mana_to_health_bracelet && mana_to_health_necklace) {
    specials.mana_to_health += 50;
  }
  // 应用魔法转生命效果
  if (specials.mana_to_health > 0 && derived.max_mp > 1) {
    const auto convert =
        std::min(specials.mana_to_health, static_cast<std::int32_t>(derived.max_mp) - 1);
    derived.max_mp = clamp_u16(static_cast<std::int32_t>(derived.max_mp) - convert);
    derived.max_hp = clamp_u16(static_cast<std::int32_t>(derived.max_hp) + convert);
    specials.mana_to_health = convert;
  } else if (specials.mana_to_health > 0) {
    specials.mana_to_health = 0;
  }
  legacy_equipment_specials_ = specials;
  legacy_suck_health_accumulator_ = 0.0;

  // 计算背包总重量
  std::int32_t bag_weight = 0;
  for (const auto& item : character_.bag_items) {
    bag_weight += item_weight(item, item_configs);
  }

  // 计算穿戴重量（区分手部重量和身体重量）
  std::int32_t wear_weight = 0;
  std::int32_t hand_weight = 0;
  for (std::size_t slot = 0; slot < character_.equipped_items.size(); ++slot) {
    const auto& item = character_.equipped_items[slot];
    const auto weight = !is_empty(item) && item.dura > 0 ? item_weight(item, item_configs) : 0;
    if (legacy_slot_uses_hand_weight(slot)) {
      hand_weight += weight;
    } else {
      wear_weight += weight;
    }
  }

  derived.hp = std::min(current_hp, derived.max_hp);
  derived.mp = std::min(current_mp, derived.max_mp);
  derived.weight = clamp_u16(bag_weight);
  derived.wear_weight = clamp_u8(wear_weight);
  derived.hand_weight = clamp_u8(hand_weight);
  derived.reserved1 = clamp_u8(accuracy_point_);
  derived.exp_count = clamp_u8(speed_point_);
  character_.ability = derived;
  set_legacy_status_bit(character_.status, kStateTransparent,
                        legacy_equipment_specials_.equipment_transparent ||
                            legacy_buffs_.has(LegacyBuffKind::transparent));

  // 更新角色外观特征（衣服/武器/发型的显示索引）
  const auto dress_feature =
      resolve_shape_feature(character_.sex, character_.equipped_items[kEquipDress], item_configs);
  const auto weapon_feature = resolve_shape_feature(character_.sex,
                                                    character_.equipped_items[kEquipWeapon],
                                                    item_configs);
  const auto face_feature =
      static_cast<std::uint8_t>(std::clamp(character_.hair * 2 + character_.sex, 0, 255));
  character_.feature = make_feature(0, dress_feature, weapon_feature, face_feature);
}

/** ==================== Player：死亡/复活/PK 管理 ==================== */

/**
 * @brief 标记玩家死亡
 * @param now_ms 当前时间（毫秒）
 * @return Buff 清除结果
 * @details 设置 HP=0，记录死亡时间，清除所有特殊攻击状态
 *          （剑术蓄力、重击、长距/范围/十字攻击），
 *          将移动限制设置为最大值（禁止移动）
 */
StatusTickResult Player::mark_dead(std::uint64_t now_ms) {
  if (character_.ability.hp != 0) {
    character_.ability.hp = 0;
  }
  if (character_.death_time_ms == 0) {
    character_.death_time_ms = now_ms;
  }
  auto result = clear_legacy_buffs_on_death(0);
  clear_legacy_sword_skill();
  legacy_power_hit_ready_ = false;
  legacy_power_hit_count_ = 0;
  legacy_power_hit_point_count_ = 0;
  legacy_power_hit_level_ = -1;
  legacy_long_hit_enabled_ = false;
  legacy_wide_hit_enabled_ = false;
  legacy_cross_hit_enabled_ = false;
  next_move_tick_ = std::numeric_limits<std::uint64_t>::max();
  return result;
}

/**
 * @brief 在指定位置复活玩家
 * @param map_id 复活目标地图
 * @param x, y 复活坐标
 * @param hp 复活后 HP（至少为 1）
 * @param mp 复活后 MP
 * @details 重置各种死亡标记和限制，恢复正常运行状态
 */
void Player::revive_at(std::string map_id, std::int32_t x, std::int32_t y,
                       std::uint16_t hp, std::uint16_t mp) {
  character_.map_id = std::move(map_id);
  character_.x = x;
  character_.y = y;
  character_.ability.hp = std::min<std::uint16_t>(std::max<std::uint16_t>(hp, 1),
                                                  character_.ability.max_hp);
  character_.ability.mp = std::min<std::uint16_t>(mp, character_.ability.max_mp);
  character_.death_time_ms = 0;
  ghost_ = false;
  legacy_death_drop_settled_ = false;
  ghost_time_ms_ = 0;
  legacy_state_ = LegacyPlayerState::running;
  next_move_tick_ = 0;
  set_position(x, y);
}

/**
 * @brief 增加 PK 值
 * @param amount 增加量（自动钳位到非负）
 */
void Player::inc_pk_point(std::int32_t amount) {
  character_.pk_point = std::max(0, character_.pk_point + amount);
}

/**
 * @brief 增加身体幸运值（有范围限制）
 * @param amount 增加量（正或负）
 * @details 幸运上限 25000，诅咒下限 -50000
 */
void Player::add_body_luck(double amount) {
  if ((amount > 0.0 && character_.body_luck < 25000.0) ||
      (amount < 0.0 && character_.body_luck > -50000.0)) {
    character_.body_luck += amount;
  }
}

/**
 * @brief 记录攻击过该玩家的角色（用于 PK 值正当防卫判断）
 * @param actor_id 攻击者角色 ID
 * @param now_ms 当前时间（毫秒）
 * @details 1 分钟内攻击过该玩家的角色被击杀不计 PK 值。
 *          列表中同一角色只保留最新记录，超过 1 分钟的记录被清理。
 */
void Player::record_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) {
  if (actor_id == 0 || actor_id == id()) {
    return;
  }
  const auto expire_before = now_ms >= 60000 ? now_ms - 60000 : 0;
  pk_hiters_.erase(std::remove_if(pk_hiters_.begin(), pk_hiters_.end(),
                                  [&](const PkHiterInfo& entry) {
                                    return entry.hit_time_ms < expire_before ||
                                           entry.actor_id == actor_id;
                                  }),
                   pk_hiters_.end());
  pk_hiters_.push_back(PkHiterInfo{actor_id, now_ms});
}

/**
 * @brief 检查指定角色最近是否攻击过该玩家
 * @param actor_id 角色 ID
 * @param now_ms 当前时间（毫秒）
 * @return 如果 1 分钟内有攻击记录则返回 true
 */
bool Player::has_recent_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) const {
  if (actor_id == 0) {
    return false;
  }
  const auto expire_before = now_ms >= 60000 ? now_ms - 60000 : 0;
  return std::any_of(pk_hiters_.begin(), pk_hiters_.end(), [&](const PkHiterInfo& entry) {
    return entry.actor_id == actor_id && entry.hit_time_ms >= expire_before;
  });
}

/** ==================== Player：生命周期与状态管理 ==================== */

/**
 * @brief 检查玩家的运行周期是否到期
 * @param now_ms 当前时间（毫秒）
 * @return 运行间隔到期返回 true
 */
bool Player::legacy_due(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - run_time_ms_ >
         static_cast<std::int64_t>(run_next_tick_ms_);
}

/** ==================== Player：魔法盾（气泡防御）管理 ==================== */

/**
 * @brief 检查魔法盾（气泡防御）是否激活
 * @param current_tick 当前逻辑 tick
 * @return 魔法盾激活返回 true
 */
bool Player::legacy_magic_bubble_active(std::uint64_t current_tick) const {
  return legacy_buffs_.active(LegacyBuffKind::bubble_defence_up, current_tick);
}

/**
 * @brief 获取魔法盾等级
 */
std::int32_t Player::legacy_magic_bubble_level() const {
  const auto* bubble = legacy_buffs_.get(LegacyBuffKind::bubble_defence_up);
  return bubble != nullptr ? bubble->level : 0;
}

/**
 * @brief 检查麻痹效果（石化毒）是否激活
 * @param current_tick 当前逻辑 tick
 */
bool Player::legacy_poison_stone_active(std::uint64_t current_tick) const {
  return legacy_buffs_.active(LegacyBuffKind::poison_stone, current_tick);
}

/**
 * @brief 激活魔法盾（气泡防御）
 * @param level Buff 等级
 * @param current_tick 当前逻辑 tick
 * @param expire_tick 过期 tick
 * @return 激活成功返回 true（已有魔法盾时返回 false）
 */
bool Player::activate_legacy_magic_bubble(std::int32_t level, std::uint64_t current_tick,
                                          std::uint64_t expire_tick) {
  if (legacy_magic_bubble_active(current_tick)) {
    return false;
  }
  static_cast<void>(legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::bubble_defence_up, expire_tick, 0, 1,
                       std::max(level, 0)),
      current_tick));
  set_legacy_status_bit(character_.status, kStateBubbleDefenceUp, true);
  return true;
}

/**
 * @brief 减少魔法盾的剩余持续时间
 * @param current_tick 当前逻辑 tick
 * @param ticks 要减少的 tick 数
 * @details 用于受到攻击时消耗魔法盾持续时间
 */
void Player::damage_legacy_magic_bubble(std::uint64_t current_tick, std::uint64_t ticks) {
  if (!legacy_magic_bubble_active(current_tick)) {
    static_cast<void>(legacy_buffs_.clear(LegacyBuffKind::bubble_defence_up));
    set_legacy_status_bit(character_.status, kStateBubbleDefenceUp, false);
    return;
  }
  auto* bubble = legacy_buffs_.get(LegacyBuffKind::bubble_defence_up);
  if (bubble == nullptr) {
    return;
  }
  const auto minimum_expire = current_tick + 1;
  if (bubble->expire_tick > minimum_expire + ticks) {
    bubble->expire_tick -= ticks;
  } else {
    bubble->expire_tick = minimum_expire;
  }
}

/** ==================== Player：剑术技能系统 ==================== */

/**
 * @brief 蓄力一个剑术技能
 * @param magic_id 魔法 ID
 * @param expire_tick 蓄力过期时间
 */
void Player::prepare_legacy_sword_skill(std::int32_t magic_id, std::uint64_t expire_tick) {
  legacy_prepared_sword_magic_id_ = magic_id;
  legacy_prepared_sword_expire_tick_ = expire_tick;
}

/**
 * @brief 获取蓄力中的剑术技能 ID
 * @param current_tick 当前逻辑 tick
 * @return 技能 ID（过期或未蓄力返回 0）
 */
std::int32_t Player::pending_legacy_sword_skill(std::uint64_t current_tick) const {
  if (legacy_prepared_sword_magic_id_ == 0 ||
      (legacy_prepared_sword_expire_tick_ != 0 &&
       current_tick > legacy_prepared_sword_expire_tick_)) {
    return 0;
  }
  return legacy_prepared_sword_magic_id_;
}

/**
 * @brief 消耗蓄力中的剑术技能并返回其 ID
 * @param current_tick 当前逻辑 tick
 * @return 技能 ID（已过期返回 0）
 */
std::int32_t Player::consume_legacy_sword_skill(std::uint64_t current_tick) {
  if (legacy_prepared_sword_magic_id_ == 0 ||
      (legacy_prepared_sword_expire_tick_ != 0 && current_tick > legacy_prepared_sword_expire_tick_)) {
    clear_legacy_sword_skill();
    return 0;
  }
  const auto magic_id = legacy_prepared_sword_magic_id_;
  clear_legacy_sword_skill();
  return magic_id;
}

/**
 * @brief 清除蓄力中的剑术技能
 */
void Player::clear_legacy_sword_skill() {
  legacy_prepared_sword_magic_id_ = 0;
  legacy_prepared_sword_expire_tick_ = 0;
}

/** ==================== Player：重击（PowerHit）系统 ==================== */

/**
 * @brief 消耗一次重击（如果就绪）
 * @return 消耗成功返回 true
 */
bool Player::consume_legacy_power_hit() {
  if (!legacy_power_hit_ready_) {
    return false;
  }
  legacy_power_hit_ready_ = false;
  return true;
}

/**
 * @brief 检查重击计数器是否匹配指定等级
 */
bool Player::legacy_power_hit_counter_matches(std::int32_t level) const {
  return legacy_power_hit_level_ == level && legacy_power_hit_count_ > 0;
}

/**
 * @brief 重置重击计数器
 * @param level 技能等级
 * @param random_point 随机触发点
 * @details 总次数 = max(1, 7 - level)，随机触发点决定在第几次攻击时触发重击
 */
void Player::reset_legacy_power_hit_counter(std::int32_t level,
                                            std::int32_t random_point) {
  const auto count = std::max(1, 7 - level);
  legacy_power_hit_level_ = level;
  legacy_power_hit_count_ = count;
  legacy_power_hit_point_count_ = std::clamp(random_point, 0, count - 1);
}

/**
 * @brief 推进重击计数器（每次攻击后调用）
 * @return 当计数达到触发点时设置 ready 状态并返回 true
 */
bool Player::advance_legacy_power_hit_counter() {
  if (legacy_power_hit_count_ <= 0) {
    return false;
  }
  --legacy_power_hit_count_;
  if (legacy_power_hit_point_count_ == legacy_power_hit_count_) {
    legacy_power_hit_ready_ = true;
    return true;
  }
  return false;
}

/**
 * @brief 检查重击计数器是否已耗尽
 */
bool Player::legacy_power_hit_counter_expired() const {
  return legacy_power_hit_count_ <= 0;
}

/** ==================== Player：烈火/冲刺/物品更换冷却管理 ==================== */

/**
 * @brief 检查烈火剑法是否就绪（10 秒冷却）
 */
bool Player::legacy_fire_hit_ready(std::uint64_t now_ms) const {
  return legacy_latest_fire_hit_time_ms_ == 0 ||
         now_ms - legacy_latest_fire_hit_time_ms_ > 10000;
}

/**
 * @brief 标记烈火剑法已使用（记录时间戳用于 10 秒冷却）
 * @param now_ms 当前时间（毫秒）
 */
void Player::mark_legacy_fire_hit(std::uint64_t now_ms) {
  legacy_latest_fire_hit_time_ms_ = now_ms;
}

/**
 * @brief 检查冲刺是否就绪（3 秒冷却）
 */
bool Player::legacy_rush_ready(std::uint64_t now_ms) const {
  return legacy_latest_rush_time_ms_ == 0 ||
         now_ms - legacy_latest_rush_time_ms_ > 3000;
}

/**
 * @brief 标记冲刺已使用（记录时间戳用于 3 秒冷却）
 * @param now_ms 当前时间（毫秒）
 */
void Player::mark_legacy_rush(std::uint64_t now_ms) {
  legacy_latest_rush_time_ms_ = now_ms;
}

/**
 * @brief 检查更换物品是否就绪（3 秒冷却，防刷装备）
 */
bool Player::legacy_item_change_ready(std::uint64_t now_ms) const {
  return legacy_item_change_time_ms_ == 0 || now_ms > legacy_item_change_time_ms_ + 3000;
}

/**
 * @brief 标记更换物品时间（记录时间戳用于 3 秒冷却）
 * @param now_ms 当前时间（毫秒）
 */
void Player::mark_legacy_item_change(std::uint64_t now_ms) {
  legacy_item_change_time_ms_ = now_ms;
}

/** ==================== Player：开天斩系统 ==================== */

/**
 * @brief 检查开天斩是否处于激活状态
 * @param current_tick 当前逻辑 tick
 * @return 未过期返回 true
 */
bool Player::legacy_open_health_active(std::uint64_t current_tick) const {
  return legacy_open_health_expire_tick_ != 0 && current_tick <= legacy_open_health_expire_tick_;
}

/**
 * @brief 激活开天斩效果
 * @param expire_tick 过期 tick（取较大值，防止缩短已有持续时间）
 */
void Player::activate_legacy_open_health(std::uint64_t expire_tick) {
  legacy_open_health_expire_tick_ = std::max(legacy_open_health_expire_tick_, expire_tick);
}

/** ==================== Player：魔法等级经验代次 ==================== */

/**
 * @brief 获取魔法等级经验代次
 * @param magic_id 魔法 ID
 * @return 代次数（未设置返回 0）
 */
std::uint32_t Player::legacy_magic_lvexp_generation(std::int32_t magic_id) const {
  const auto it = legacy_magic_lvexp_generations_.find(magic_id);
  return it == legacy_magic_lvexp_generations_.end() ? 0 : it->second;
}

/**
 * @brief 推进魔法等级经验代次（防止客户端缓存导致数据显示错误）
 * @return 新的代次数
 */
std::uint32_t Player::advance_legacy_magic_lvexp_generation(std::int32_t magic_id) {
  auto& generation = legacy_magic_lvexp_generations_[magic_id];
  ++generation;
  if (generation == 0) {
    generation = 1;
  }
  return generation;
}

/** ==================== Player：生命周期标记 ==================== */

/**
 * @brief 设置玩家的生命周期状态
 * @param state 新的生命周期状态
 */
void Player::set_legacy_state(LegacyPlayerState state) { legacy_state_ = state; }

/**
 * @brief 标记登录通知已完成
 * @param now_ms 当前时间
 * @details 状态转为 initialize_pending，准备初始化阶段
 */
void Player::mark_legacy_notice_done(std::uint64_t now_ms) {
  login_sign_ = true;
  legacy_state_ = LegacyPlayerState::initialize_pending;
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

/**
 * @brief 标记初始化已完成（进入正常运行状态）
 * @param now_ms 当前时间
 */
void Player::mark_legacy_initialize_done(std::uint64_t now_ms) {
  login_sign_ = true;
  ready_run_ = true;
  legacy_state_ = LegacyPlayerState::running;
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
  if (last_save_time_ms_ == 0) {
    last_save_time_ms_ = now_ms;
  }
}

/**
 * @brief 更新玩家的运行时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Player::mark_legacy_running_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

/**
 * @brief 标记玩家数据已自动保存
 * @param now_ms 保存完成的时间（毫秒）
 */
void Player::mark_legacy_autosaved(std::uint64_t now_ms) { last_save_time_ms_ = now_ms; }

/**
 * @brief 标记玩家进入鬼魂状态（尸体过期）
 */
void Player::mark_legacy_ghost(std::uint64_t now_ms) {
  ghost_ = true;
  ghost_time_ms_ = now_ms;
  legacy_state_ = LegacyPlayerState::ghost;
}

/**
 * @brief 标记玩家为已关闭（最终状态）
 */
void Player::mark_legacy_closed() {
  ghost_ = true;
  legacy_state_ = LegacyPlayerState::closed;
}

/**
 * @brief 回拨运行时间（用于时间校准）
 */
void Player::rewind_legacy_run_time(std::uint64_t delta_ms) {
  run_time_ms_ -= static_cast<std::int64_t>(delta_ms);
}

/** ==================== Player：命令队列 ==================== */

/**
 * @brief 将邮件作为命令加入队列尾
 * @param mail 要排队处理的邮件
 * @param now_ms 当前时间
 * @details 当邮件从帧外到达时，先放入队列，下一帧再处理。
 *          序列号自动递增保证 FIFO 顺序。
 */
void Player::enqueue_legacy_command(ActorMail mail, std::uint64_t now_ms) {
  legacy_inbox_.push_back(LegacyQueuedCommand{std::move(mail), now_ms, ++legacy_command_sequence_});
}

/**
 * @brief 从队列头弹出命令
 * @return 队列非空则返回命令，空队列返回 nullopt
 */
std::optional<LegacyQueuedCommand> Player::pop_legacy_command() {
  if (legacy_inbox_.empty()) {
    return std::nullopt;
  }
  auto command = std::move(legacy_inbox_.front());
  legacy_inbox_.pop_front();
  return command;
}

/**
 * @brief 获取队列中所有邮件的会话序列号列表
 */
std::vector<std::uint64_t> Player::legacy_inbox_session_sequences() const {
  std::vector<std::uint64_t> sequences;
  sequences.reserve(legacy_inbox_.size());
  for (const auto& command : legacy_inbox_) {
    sequences.push_back(command.mail.session_seq);
  }
  return sequences;
}

/** ==================== Player：邮件/虚拟方法重写 ==================== */

/**
 * @brief 处理玩家邮件（重写基类方法）
 * @param mail 收到的邮件
 * @param context 地图上下文
 * @details 处理移动/转向/攻击/技能等操作邮件，必要时清除透明状态
 */
void Player::on_mail(const ActorMail& mail, MapContext& context) {
  GameObject::on_mail(mail, context);
  switch (mail.kind) {
    case ActorMailKind::turn:
      character_.dir = mail.dir;
      break;
    case ActorMailKind::move:
    case ActorMailKind::run:
      character_.x = mail.x;
      character_.y = mail.y;
      character_.dir = mail.dir;
      static_cast<void>(clear_legacy_transparent(context.tick));
      break;
    case ActorMailKind::attack:
    case ActorMailKind::spell:
      character_.dir = mail.dir;
      static_cast<void>(clear_legacy_transparent(context.tick));
      break;
    default:
      break;
  }
}

/**
 * @brief 玩家 tick 处理（自动保存 + 调度）
 * @param context 地图上下文
 * @details 每 500 tick 自动保存一次角色快照
 */
void Player::on_tick(MapContext& context) {
  if (context.tick % 500 == 0) {
    PersistRequest request;
    request.kind = PersistRequestKind::save_character;
    request.account_id = character_.account_id;
    request.character_name = character_.character_name;
    request.character = snapshot();
    context.request_persist(std::move(request));
  }
  set_next_due_tick(context.tick + 1);
}

/** ==================== Monster：怪物构造函数 ==================== */

/**
 * @brief 构造 Monster 实例
 * @details 初始化所有怪物属性，然后根据 race_server（服务端种族）配置特殊行为：
 *          - 连锁射击类（双斧骷髅/暗黑荆棘/弓箭手）：设置射击次数
 *          - 隐藏/挖掘类（食人草/挖出僵尸/蜈蚣王/骷髅王）：设置隐藏模式和挖掘范围
 *          - 召唤类（蜂后/蜘蛛巢）：设置召唤怪物名称、上限和延迟
 *          - 粘附类（守卫/城墙）：固定位置模式
 *          - 其他（蜘蛛/毒蛾等）：设置搜索频率
 *          如果是宠物（is_slave），调用 apply_slave_level_abilities 计算加成属性
 *
 * @note 搜索频率规则：aggressive 默认 1500ms，其余默认 3000ms，特殊种族可覆盖
 */
Monster::Monster(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
                 std::int32_t level, std::int32_t max_hp, std::int32_t attack_power,
                 std::int32_t dc_min, std::int32_t dc_max, std::int32_t defense,
                 std::int32_t magic_defense, std::int32_t mc, std::int32_t sc,
                 std::int32_t exp_reward,
                 std::int32_t life_attrib, std::int32_t max_mp,
                 std::int32_t race_server, std::int32_t race_image,
                 std::int32_t appearance, std::int32_t cool_eye,
                 std::int32_t speed, std::int32_t accuracy,
                 std::int32_t walk_speed_ms, std::int32_t walk_step,
                 std::int32_t walk_wait_ms, std::int32_t attack_speed_ms,
                 MonsterAiProfile ai_profile, std::uint64_t search_rate_ms,
                 std::int32_t home_x, std::int32_t home_y,
                 std::int32_t home_area, bool legacy_spawn_group,
                 std::uint64_t master_actor_id, bool is_slave,
                 std::int32_t slave_exp, std::int32_t slave_make_level,
                 std::int32_t slave_exp_level,
                 std::uint64_t master_royalty_time_ms,
                 std::uint64_t slave_life_time_ms,
                 bool no_item,
                 bool tameable,
                 std::vector<LegacyUserItem> drop_items, std::int32_t drop_gold)
    : GameObject(id, GameObjectKind::monster, std::move(name), std::move(map_id), x, y),
      level_(std::max(level, 1)),
      hp_(std::max(max_hp, 1)),
      max_hp_(std::max(max_hp, 1)),
      mp_(std::max(max_mp, 0)),
      max_mp_(std::max(max_mp, 0)),
      attack_power_(std::max(dc_max > 0 ? dc_max : attack_power, 1)),
      dc_min_(std::max(dc_min, 0)),
      dc_max_(std::max({dc_max, dc_min_, attack_power_, 1})),
      defense_(std::max(defense, 0)),
      magic_defense_(std::max(magic_defense, 0)),
      mc_(std::max(mc, 0)),
      sc_(std::max(sc, 0)),
      exp_reward_(std::max(exp_reward, 1)),
      life_attrib_(std::max(life_attrib, 0)),
      race_server_(race_server),
      race_image_(race_image),
      appearance_(appearance),
      cool_eye_(cool_eye),
      speed_point_(speed),
      accuracy_point_(accuracy),
      walk_speed_ms_(std::max(walk_speed_ms, 1)),
      walk_step_(std::max(walk_step, 1)),
      walk_wait_ms_(std::max(walk_wait_ms, 0)),
      attack_speed_ms_(std::max(attack_speed_ms, 1)),
      ai_profile_(ai_profile),
      search_rate_ms_(search_rate_ms != 0
                          ? search_rate_ms
                          : (ai_profile == MonsterAiProfile::aggressive ? 1500 : 3000)),
      home_x_(home_x),
      home_y_(home_y),
      home_area_(std::max(home_area, 0)),
      legacy_spawn_group_(legacy_spawn_group),
      master_actor_id_(master_actor_id),
      is_slave_(is_slave || master_actor_id != 0),
      slave_exp_(std::max(slave_exp, 0)),
      slave_make_level_(std::max(slave_make_level, 0)),
      slave_exp_level_(std::clamp(slave_exp_level, 0, 6)),
      master_royalty_time_ms_(master_royalty_time_ms),
      slave_life_time_ms_(slave_life_time_ms),
      no_item_(no_item || is_slave || master_actor_id != 0),
      tameable_(tameable),
      base_max_hp_(std::max(max_hp, 1)),
      base_dc_max_(std::max({dc_max, dc_min, attack_power, 1})),
      base_magic_defense_(std::max(magic_defense, 0)),
      drop_items_(std::move(drop_items)),
      drop_gold_(std::max(drop_gold, 0)) {
  // 根据服务端种族配置特殊行为
  switch (race_server_) {
    case kRcDualAxeSkeleton:
      chain_shot_count_ = 2;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcThornDark:
      chain_shot_count_ = 3;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcArcherMon:
      chain_shot_count_ = 6;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcKillingHerb:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 4;
      dig_down_range_ = 4;
      run_next_tick_ms_ = 250;
      break;
    case kRcDigOutZombi:
      hide_mode_ = true;
      dig_up_range_ = 3;
      search_rate_ms_ = search_rate_ms != 0 ? search_rate_ms : 2500;
      break;
    case kRcCentipedeKing:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 4;
      dig_down_range_ = 6;
      run_next_tick_ms_ = 250;
      break;
    case kRcBeeQueen:
      stick_mode_ = true;
      summon_monster_name_ = "__Bee";
      summon_limit_ = 15;
      summon_delay_ms_ = 500;
      run_next_tick_ms_ = 250;
      break;
    case kRcSpiderHouse:
      stick_mode_ = true;
      summon_monster_name_ = "__Spider";
      summon_limit_ = 15;
      summon_delay_ms_ = 500;
      run_next_tick_ms_ = 250;
      break;
    case kRcScultureKing:
    case kRcScultureKingNoFollower:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 2;
      dig_down_range_ = 8;
      run_next_tick_ms_ = 250;
      break;
    case kRcDoorGuard:
    case kRcArcherGuard:
    case kRcArcherPolice:
    case kRcCastleDoor:
    case kRcWall:
      stick_mode_ = true;
      run_next_tick_ms_ = 250;
      break;
    case kRcSpitSpider:
    case kRcHighRiskSpider:
    case kRcBigPoisonSpider:
    case kRcBigKudeki:
    case kRcGasMoth:
    case kRcGasDung:
    case kRcToxicGhost:
    case kRcMagCowFaceMon:
      search_rate_ms_ = search_rate_ms != 0 ? search_rate_ms : 1500;
      break;
    default:
      break;
  }
  if (is_slave_) {
    no_item_ = true;
    apply_slave_level_abilities();
  }
}

/** ==================== Monster：基础查询与快照 ==================== */

/**
 * @brief 检查怪物是否已死亡
 * @return HP 小于等于 0 返回 true
 */
bool Monster::is_dead() const { return hp_ <= 0; }

/**
 * @brief 创建怪物完整状态快照
 * @return 包含所有运行时状态的 MonsterSnapshot
 */
MonsterSnapshot Monster::snapshot() const {
  return MonsterSnapshot{.id = id(),
                         .name = name(),
                         .map_id = map_id(),
                         .x = x(),
                         .y = y(),
                         .dir = dir(),
                         .level = level_,
                         .hp = hp_,
                         .max_hp = max_hp_,
                         .mp = mp_,
                         .max_mp = max_mp_,
                         .dc_min = dc_min_,
                         .dc_max = dc_max_,
                         .attack_power = attack_power_,
                         .defense = defense_,
                         .magic_defense = magic_defense_,
                         .mc = mc_,
                         .sc = sc_,
                         .exp_reward = exp_reward_,
                         .life_attrib = life_attrib_,
                         .race_server = race_server_,
                         .race_image = race_image_,
                         .appearance = appearance_,
                         .cool_eye = cool_eye_,
                         .speed_point = speed_point_,
                         .accuracy_point = accuracy_point_,
                         .walk_speed_ms = walk_speed_ms_,
                         .walk_step = walk_step_,
                         .walk_wait_ms = walk_wait_ms_,
                         .attack_speed_ms = attack_speed_ms_,
                         .legacy_run_time_ms = run_time_ms_,
                         .legacy_run_next_tick_ms = run_next_tick_ms_,
                         .legacy_search_time_ms = search_time_ms_,
                         .legacy_search_rate_ms = search_rate_ms_,
                         .legacy_visible_actor_ids = legacy_visible_actor_ids_,
                         .target_actor_id = aggro_target_id_,
                         .target_focus_time_ms = target_focus_time_ms_,
                         .target_x = target_x_,
                         .target_y = target_y_,
                         .walk_time_ms = walk_time_ms_,
                         .hit_time_ms = hit_time_ms_,
                         .search_enemy_time_ms = search_enemy_time_ms_,
                         .think_time_ms = think_time_ms_,
                         .last_hitter_id = last_hitter_id_,
                         .last_hit_time_ms = last_hit_time_ms_,
                         .exp_hitter_id = exp_hitter_id_,
                         .exp_hit_time_ms = exp_hit_time_ms_,
                         .death_time_ms = death_time_ms_,
                         .ghost_time_ms = ghost_time_ms_,
                         .walk_wait_mode = walk_wait_mode_,
                         .dup_mode = dup_mode_,
                         .ghosted = ghosted_,
                         .death_settled = death_settled_,
                         .chain_shot = chain_shot_,
                         .chain_shot_count = chain_shot_count_,
                         .hide_mode = hide_mode_,
                         .stick_mode = stick_mode_,
                         .dig_up_range = dig_up_range_,
                         .dig_down_range = dig_down_range_,
                         .appear_time_ms = appear_time_ms_,
                         .child_actor_count = child_actor_ids_.size(),
                         .summon_limit = summon_limit_,
                         .master_actor_id = master_actor_id_,
                         .is_slave = is_slave_,
                         .slave_exp = slave_exp_,
                         .slave_make_level = slave_make_level_,
                         .slave_exp_level = slave_exp_level_,
                         .master_royalty_time_ms = master_royalty_time_ms_,
                         .slave_life_time_ms = slave_life_time_ms_,
                         .no_item = no_item_,
                         .tameable = tameable_};
}

/** ==================== Monster：定时检查 ==================== */

/**
 * @brief 检查怪物的运行时间是否到期
 * @param now_ms 当前时间（毫秒）
 * @return 到期返回 true
 */
bool Monster::legacy_due(std::uint64_t now_ms) const {
  return run_next_tick_ms_ == 0 ||
         static_cast<std::int64_t>(now_ms) - run_time_ms_ >
             static_cast<std::int64_t>(run_next_tick_ms_);
}

/**
 * @brief 检查是否到了搜索时间
 */
bool Monster::legacy_search_due(std::uint64_t now_ms) const {
  return now_ms > search_time_ms_ + search_rate_ms_;
}

/**
 * @brief 检查是否到了行走时间
 */
bool Monster::legacy_walk_due(std::uint64_t now_ms) const {
  return legacy_walk_due_by_walk_time(now_ms);
}

/**
 * @brief 检查是否到了攻击时间
 */
bool Monster::legacy_attack_due(std::uint64_t now_ms) const {
  return legacy_attack_due_by_hit_time(now_ms);
}

/**
 * @brief 根据行走时间戳判断是否到了下次行走的时间
 */
bool Monster::legacy_walk_due_by_walk_time(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(walk_time_ms_) >
         static_cast<std::int64_t>(std::max(walk_speed_ms_, 1));
}

/**
 * @brief 根据攻击时间戳判断是否到了下次攻击的时间
 */
bool Monster::legacy_attack_due_by_hit_time(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(hit_time_ms_) >
         static_cast<std::int64_t>(std::max(attack_speed_ms_, 1));
}

/**
 * @brief 检查行走等待时间是否已过
 */
bool Monster::legacy_walk_wait_elapsed(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) -
             static_cast<std::int64_t>(walk_wait_cur_time_ms_) >
         static_cast<std::int64_t>(std::max(walk_wait_ms_, 0));
}

/** ==================== Monster：圣言术/疯狂状态 ==================== */

/**
 * @brief 检查怪物是否处于被圣言术捕捉状态
 * @details 圣言术是道士的克制不死系技能，被捕捉的怪物无法行动
 */
bool Monster::legacy_holy_seize_active(std::uint64_t now_ms) const {
  return legacy_holy_seize_until_ms_ != 0 && now_ms <= legacy_holy_seize_until_ms_;
}

/**
 * @brief 检查怪物是否处于疯狂状态
 */
bool Monster::legacy_crazy_active(std::uint64_t now_ms) const {
  return legacy_crazy_until_ms_ != 0 && now_ms <= legacy_crazy_until_ms_;
}

/**
 * @brief 对怪物施加圣言术效果
 * @param duration_ms 持续时间（毫秒）
 * @param now_ms 当前时间
 * @details 被圣言捕捉的怪物会失去目标和移动能力
 */
void Monster::make_legacy_holy_seize(std::uint64_t duration_ms, std::uint64_t now_ms) {
  legacy_holy_seize_until_ms_ =
      std::max(legacy_holy_seize_until_ms_, now_ms + std::max<std::uint64_t>(duration_ms, 1));
  lose_target();
  clear_target_xy();
}

/**
 * @brief 解除圣言术效果
 */
void Monster::break_legacy_holy_seize() {
  legacy_holy_seize_until_ms_ = 0;
}

/**
 * @brief 使怪物进入疯狂状态（如被诱惑之光影响）
 */
void Monster::make_legacy_crazy(std::uint64_t duration_ms, std::uint64_t now_ms) {
  legacy_crazy_until_ms_ =
      std::max(legacy_crazy_until_ms_, now_ms + std::max<std::uint64_t>(duration_ms, 1));
  lose_target();
  clear_target_xy();
}

/**
 * @brief 解除疯狂状态
 */
void Monster::break_legacy_crazy() {
  legacy_crazy_until_ms_ = 0;
}

/** ==================== Monster：伤害/状态效果/Buff ==================== */

/**
 * @brief 对怪物应用伤害（不含时间戳版本）
 * @param amount 伤害量
 * @param attacker_id 攻击者 ID
 * @return 实际造成的伤害值
 */
std::int32_t Monster::apply_damage(std::int32_t amount, std::uint64_t attacker_id) {
  return apply_damage(amount, attacker_id, 0);
}

/**
 * @brief 对怪物应用伤害（含时间戳版本）
 * @param amount 伤害量
 * @param attacker_id 攻击者 ID
 * @param now_ms 当前时间（为 0 时不触发死亡标记和仇恨超时）
 * @return 实际造成的伤害值
 * @details 损装备毒状态下伤害放大 120%。HP 降到 0 时自动标记死亡并清除 Buff。
 *          攻击者 ID 非 0 时记录仇恨。
 */
std::int32_t Monster::apply_damage(std::int32_t amount, std::uint64_t attacker_id,
                                   std::uint64_t now_ms) {
  if (amount <= 0 || hp_ <= 0) {
    return 0;
  }
  if (legacy_buffs_.has(LegacyBuffKind::poison_damage_armor)) {
    amount = round_damage_120(amount);
  }
  const auto before = hp_;
  hp_ = std::max(0, hp_ - amount);
  if (attacker_id != 0) {
    record_legacy_hitter(attacker_id, now_ms);
  }
  if (hp_ == 0 && now_ms != 0) {
    static_cast<void>(mark_legacy_death(now_ms));
  }
  return before - hp_;
}

/**
 * @brief 为怪物添加定时状态效果
 * @param effect 要添加的效果
 * @details 怪物仅支持 DOT 伤害和减速效果，不支持治疗和护盾
 */
void Monster::add_status_effect(TimedStatusEffect effect) {
  if ((effect.damage_per_tick <= 0 && effect.slow_percent <= 0) || effect.expire_tick == 0) {
    return;
  }
  effect.tick_interval = std::max<std::uint64_t>(effect.tick_interval, 1);
  effect.next_tick = std::max<std::uint64_t>(effect.next_tick, effect.tick_interval);
  status_effects_.push_back(std::move(effect));
}

/**
 * @brief 对怪物施加传统毒药效果
 * @param poison_kind 毒药类型
 * @param duration_ticks 持续 tick 数
 * @param poison_level 毒药等级
 * @param poison_tick_interval 触发间隔
 * @param source_actor_id 施毒者 ID
 * @param current_tick 当前逻辑 tick
 * @return 毒药是否新激活
 * @note 施毒时自动记录攻击者（用于仇恨计算）
 */
bool Monster::apply_legacy_poison(std::int32_t poison_kind, std::uint64_t duration_ticks,
                                  std::int32_t poison_level,
                                  std::uint64_t poison_tick_interval,
                                  std::uint64_t source_actor_id,
                                  std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto kind = legacy_buff_kind_from_status_bit(poison_kind);
  if (!kind.has_value() ||
      (*kind != LegacyBuffKind::poison_dechealth &&
       *kind != LegacyBuffKind::poison_damage_armor &&
       *kind != LegacyBuffKind::poison_stone &&
       *kind != LegacyBuffKind::poison_dont_move)) {
    return false;
  }
  const auto new_expire_tick = current_tick + duration_ticks;
  const auto tick_interval = std::max<std::uint64_t>(poison_tick_interval, 1);
  const auto next_tick =
      *kind == LegacyBuffKind::poison_dechealth ? current_tick + tick_interval : 0;
  const auto changed = legacy_buffs_.activate_or_refresh(
      make_legacy_buff(*kind, new_expire_tick, next_tick, tick_interval,
                       std::max(poison_level, 0), source_actor_id),
      current_tick);
  if (source_actor_id != 0) {
    record_legacy_hitter(source_actor_id, 0);
  }
  return changed;
}

/**
 * @brief 获取怪物 DC_up 攻击加成
 */
std::int32_t Monster::legacy_dc_up_bonus() const {
  const auto* buff = legacy_buffs_.get(LegacyBuffKind::dc_up);
  return buff != nullptr ? std::max(buff->level, 0) : 0;
}

/**
 * @brief 计算怪物的物理防御（含 Buff 加成）
 */
std::int32_t Monster::physical_defense() const {
  auto defense = defense_;
  if (legacy_buffs_.has(LegacyBuffKind::defence_up)) {
    defense += 2 + level_ / 7;
  }
  return std::max(defense, 0);
}

/**
 * @brief 计算怪物的魔法防御（含 Buff 加成）
 */
std::int32_t Monster::magical_defense() const {
  auto defense = magic_defense_;
  if (legacy_buffs_.has(LegacyBuffKind::magic_defence_up)) {
    defense += 2 + level_ / 7;
  }
  return std::max(defense, 0);
}

/**
 * @brief 激活怪物物理防御提升 Buff
 */
bool Monster::activate_legacy_defence_up(std::uint64_t duration_ticks,
                                         std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  return legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::defence_up, expire_tick), current_tick);
}

/**
 * @brief 激活怪物魔法防御提升 Buff
 */
bool Monster::activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                               std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  return legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::magic_defence_up, expire_tick), current_tick);
}

/**
 * @brief 激活怪物攻击力提升 Buff
 */
bool Monster::activate_legacy_dc_up(std::uint64_t duration_ticks,
                                    std::uint64_t current_tick,
                                    std::int32_t bonus) {
  if (duration_ticks == 0 || bonus <= 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  return legacy_buffs_.activate_or_refresh(
      make_legacy_buff(LegacyBuffKind::dc_up, expire_tick, 0, 1, bonus), current_tick);
}

/** ==================== Monster：Buff 清除 ==================== */

/**
 * @brief 怪物死亡时清除 Buff
 * @return 状态和属性变化结果
 */
StatusTickResult Monster::clear_legacy_buffs_on_death(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::death);
  result.legacy_status_changed = cleared.status_changed;
  result.ability_changed = cleared.ability_changed;
  return result;
}

/**
 * @brief 怪物离开地图时清除 Buff
 * @return 状态和属性变化结果
 */
StatusTickResult Monster::clear_legacy_buffs_on_leave_map(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::leave_map);
  result.legacy_status_changed = cleared.status_changed;
  result.ability_changed = cleared.ability_changed;
  return result;
}

/**
 * @brief 怪物登出时清除 Buff
 * @return 状态和属性变化结果
 */
StatusTickResult Monster::clear_legacy_buffs_on_logout(std::uint64_t) {
  StatusTickResult result;
  const auto cleared = legacy_buffs_.clear_by_policy(LegacyBuffClearPolicy::logout);
  result.legacy_status_changed = cleared.status_changed;
  result.ability_changed = cleared.ability_changed;
  return result;
}

/** ==================== Monster：状态效果统一 Tick 处理 ==================== */

/**
 * @brief 统一处理怪物所有定时状态效果
 * @param current_tick 当前逻辑 tick
 * @return 伤害和状态变化汇总
 * @details 依次处理：status_effects_ 的 DOT 伤害、过期 Buff 清理、减血毒伤害
 */
StatusTickResult Monster::tick_status_effects(std::uint64_t current_tick) {
  StatusTickResult result;
  for (auto it = status_effects_.begin(); it != status_effects_.end();) {
    while (it->damage_per_tick > 0 && hp_ > 0 && current_tick >= it->next_tick &&
           it->next_tick <= it->expire_tick) {
      const auto applied = apply_damage(it->damage_per_tick, it->source_actor_id);
      result.damage += applied;
      if (applied > 0 && it->source_actor_id != 0) {
        result.source_actor_id = it->source_actor_id;
      }
      it->next_tick += std::max<std::uint64_t>(it->tick_interval, 1);
    }

    if (current_tick > it->expire_tick &&
        (it->damage_per_tick <= 0 || it->next_tick > it->expire_tick)) {
      it = status_effects_.erase(it);
    } else {
      ++it;
    }
  }
  const auto expired_buffs = legacy_buffs_.expire_due(current_tick);
  result.legacy_status_changed = result.legacy_status_changed || !expired_buffs.empty();
  result.ability_changed =
      std::any_of(expired_buffs.begin(), expired_buffs.end(),
                  [](const LegacyBuffState& state) { return state.affects_ability; });
  while (hp_ > 0) {
    auto* poison = legacy_buffs_.tick_due(LegacyBuffKind::poison_dechealth, current_tick);
    if (poison == nullptr) {
      break;
    }
    const auto applied = apply_damage(1 + poison->level, 0);
    result.damage += applied;
    if (applied > 0 && poison->source_actor_id != 0) {
      result.source_actor_id = poison->source_actor_id;
    }
    poison->next_tick += std::max<std::uint64_t>(poison->tick_interval, 1);
    if (hp_ == 0) {
      break;
    }
  }
  return result;
}

/**
 * @brief 获取下一个需要处理的状态效果 tick
 * @return 最近的 tick 值，无待处理效果则返回 0
 */
std::uint64_t Monster::next_status_tick() const {
  std::uint64_t next_tick = 0;
  for (const auto& effect : status_effects_) {
    if (effect.damage_per_tick <= 0 || effect.next_tick > effect.expire_tick) {
      continue;
    }
    if (next_tick == 0 || effect.next_tick < next_tick) {
      next_tick = effect.next_tick;
    }
  }
  if (const auto* poison = legacy_buffs_.get(LegacyBuffKind::poison_dechealth);
      poison != nullptr && poison->next_tick != 0 && poison->next_tick <= poison->expire_tick &&
      (next_tick == 0 || poison->next_tick < next_tick)) {
    next_tick = poison->next_tick;
  }
  return next_tick;
}

/**
 * @brief 获取怪物当前减速百分比
 */
std::int32_t Monster::current_slow_percent(std::uint64_t current_tick) const {
  std::int32_t slow_percent = 0;
  for (const auto& effect : status_effects_) {
    if (current_tick <= effect.expire_tick) {
      slow_percent = std::max(slow_percent, effect.slow_percent);
    }
  }
  return slow_percent;
}

bool Monster::legacy_open_health_active(std::uint64_t current_tick) const {
  return legacy_open_health_expire_tick_ != 0 && current_tick <= legacy_open_health_expire_tick_;
}

std::uint64_t Monster::legacy_open_health_expire_tick() const {
  return legacy_open_health_expire_tick_;
}

void Monster::activate_legacy_open_health(std::uint64_t expire_tick) {
  legacy_open_health_expire_tick_ = std::max(legacy_open_health_expire_tick_, expire_tick);
}

const std::vector<std::uint64_t>& Monster::child_actor_ids() const { return child_actor_ids_; }

const std::string& Monster::summon_monster_name() const { return summon_monster_name_; }

std::uint64_t Monster::summon_delay_ms() const { return summon_delay_ms_; }

std::uint64_t Monster::master_royalty_time_ms() const { return master_royalty_time_ms_; }

std::uint64_t Monster::target_focus_time_ms() const { return target_focus_time_ms_; }

std::uint64_t Monster::search_enemy_time_ms() const { return search_enemy_time_ms_; }

const std::vector<std::uint64_t>& Monster::legacy_visible_actor_ids() const {
  return legacy_visible_actor_ids_;
}

/** ==================== Monster：运行时标记 ==================== */

/**
 * @brief 更新怪物的运行时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_run_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

/**
 * @brief 更新怪物的搜索时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_search_time(std::uint64_t now_ms) { search_time_ms_ = now_ms; }

/**
 * @brief 刷新可见角色列表（合并新扫描结果与旧列表）
 * @param scanned_actor_ids 新扫描到的角色 ID 列表
 * @details 保持原有可见角色的顺序，再追加新角色
 */
void Monster::refresh_legacy_visible_actor_ids(
    const std::vector<std::uint64_t>& scanned_actor_ids) {
  std::vector<std::uint64_t> refreshed;
  refreshed.reserve(scanned_actor_ids.size());
  for (const auto actor_id : legacy_visible_actor_ids_) {
    if (std::find(scanned_actor_ids.begin(), scanned_actor_ids.end(), actor_id) !=
            scanned_actor_ids.end() &&
        std::find(refreshed.begin(), refreshed.end(), actor_id) == refreshed.end()) {
      refreshed.push_back(actor_id);
    }
  }
  for (const auto actor_id : scanned_actor_ids) {
    if (std::find(refreshed.begin(), refreshed.end(), actor_id) == refreshed.end()) {
      refreshed.push_back(actor_id);
    }
  }
  legacy_visible_actor_ids_ = std::move(refreshed);
}

/**
 * @brief 更新怪物的攻击时间戳（同步攻击与命中时间）
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_attack_time(std::uint64_t now_ms) {
  attack_time_ms_ = now_ms;
  hit_time_ms_ = now_ms;
}

/**
 * @brief 更新怪物的行走时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_walk_time(std::uint64_t now_ms) { walk_time_ms_ = now_ms; }

/**
 * @brief 更新怪物的命中时间戳（同步攻击与命中时间）
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_hit_time(std::uint64_t now_ms) {
  attack_time_ms_ = now_ms;
  hit_time_ms_ = now_ms;
}

/**
 * @brief 更新怪物的搜索敌人时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_search_enemy_time(std::uint64_t now_ms) { search_enemy_time_ms_ = now_ms; }

/**
 * @brief 更新怪物的 AI 思考时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_think_time(std::uint64_t now_ms) { think_time_ms_ = now_ms; }

/**
 * @brief 更新怪物的鬼魂化时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Monster::mark_legacy_ghost_time(std::uint64_t now_ms) { ghost_time_ms_ = now_ms; }

/** ==================== Monster：仇恨系统 ==================== */

/**
 * @brief 记录攻击者（用于仇恨计算和经验归属）
 * @param attacker_id 攻击者 ID
 * @param now_ms 攻击时间
 * @param exp_hitter 是否记录为经验归属者
 * @details 经验归属者有效期 6 秒，最后攻击者有效期 30 秒。
 *          只有第一个攻击者或连续攻击者才能获得经验归属权。
 */
void Monster::record_legacy_hitter(std::uint64_t attacker_id, std::uint64_t now_ms,
                                   bool exp_hitter) {
  if (attacker_id == 0) {
    return;
  }
  if (now_ms != 0 && exp_hitter_id_ != 0 && exp_hit_time_ms_ != 0 &&
      now_ms > exp_hit_time_ms_ + 6000ULL) {
    clear_exp_hitter();
  }
  if (now_ms != 0 && last_hitter_id_ != 0 && last_hit_time_ms_ != 0 &&
      now_ms > last_hit_time_ms_ + 30000ULL) {
    clear_last_hitter();
  }
  last_hitter_id_ = attacker_id;
  last_hit_time_ms_ = now_ms;
  if (exp_hitter && (exp_hitter_id_ == 0 || exp_hitter_id_ == attacker_id)) {
    exp_hitter_id_ = attacker_id;
    exp_hit_time_ms_ = now_ms;
  }
}

/**
 * @brief 清除最后攻击者记录
 */
void Monster::clear_last_hitter() {
  last_hitter_id_ = 0;
  last_hit_time_ms_ = 0;
}

/**
 * @brief 清除经验归属者记录
 */
void Monster::clear_exp_hitter() {
  exp_hitter_id_ = 0;
  exp_hit_time_ms_ = 0;
}

/**
 * @brief 清除所有攻击者记录（最后攻击者和经验归属者）
 */
void Monster::clear_legacy_hitters() {
  clear_last_hitter();
  clear_exp_hitter();
}

/**
 * @brief 过期清理无用的攻击者记录
 * @param now_ms 当前时间
 * @details 经验归属超过 6 秒清除，最后攻击者超过 30 秒清除。
 *          如果怪物已死亡或鬼魂化，直接清除所有记录。
 */
void Monster::expire_legacy_hitters(std::uint64_t now_ms) {
  if (is_dead() || legacy_ghosted()) {
    clear_legacy_hitters();
    return;
  }
  if (exp_hitter_id_ != 0 && exp_hit_time_ms_ != 0 && now_ms > exp_hit_time_ms_ + 6000ULL) {
    clear_exp_hitter();
  }
  if (last_hitter_id_ != 0 && last_hit_time_ms_ != 0 && now_ms > last_hit_time_ms_ + 30000ULL) {
    clear_last_hitter();
  }
}

/** ==================== Monster：死亡/幽灵状态 ==================== */

/**
 * @brief 标记怪物死亡
 * @param now_ms 当前时间（毫秒）
 * @return Buff 清除结果
 * @details 清除所有 Buff，重置仇恨目标和行走等待状态
 */
StatusTickResult Monster::mark_legacy_death(std::uint64_t now_ms) {
  hp_ = 0;
  if (death_time_ms_ == 0) {
    death_time_ms_ = now_ms;
  }
  auto result = clear_legacy_buffs_on_death(0);
  aggro_target_id_ = 0;
  target_focus_time_ms_ = 0;
  clear_target_xy();
  walk_wait_mode_ = false;
  walk_wait_cur_time_ms_ = 0;
  return result;
}

/**
 * @brief 标记怪物进入鬼魂状态（尸体消失）
 * @param now_ms 当前时间
 */
void Monster::mark_legacy_ghost(std::uint64_t now_ms) {
  ghosted_ = true;
  ghost_time_ms_ = now_ms;
}

/**
 * @brief 检查死亡是否达到转为鬼魂的时间
 * @param now_ms 当前时间
 * @param corpse_ms 尸体保留时间（毫秒）
 * @return 如果尸体已过期且未鬼魂化则返回 true
 */
bool Monster::death_due_for_ghost(std::uint64_t now_ms, std::uint64_t corpse_ms) const {
  return is_dead() && !ghosted_ && death_time_ms_ != 0 &&
         static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(death_time_ms_) >
             static_cast<std::int64_t>(corpse_ms);
}

/**
 * @brief 标记死亡结算已完成
 * @details 用于防止重复结算死亡掉落和经验分配
 */
void Monster::mark_death_settled() { death_settled_ = true; }

/** ==================== Monster：行为设置 ==================== */

/**
 * @brief 设置连锁射击的剩余次数
 * @param value 剩余次数（不能为负数）
 */
void Monster::set_chain_shot(std::int32_t value) {
  chain_shot_ = std::max(value, 0);
}

/**
 * @brief 递增连锁射击剩余次数
 */
void Monster::increment_chain_shot() { ++chain_shot_; }

/**
 * @brief 设置连锁射击的总次数上限
 * @param value 总次数上限（不能为负数）
 */
void Monster::set_chain_shot_count(std::int32_t value) {
  chain_shot_count_ = std::max(value, 0);
}

/**
 * @brief 设置隐藏模式
 * @param value 是否启用隐藏模式
 */
void Monster::set_hide_mode(bool value) { hide_mode_ = value; }

/**
 * @brief 设置固定位置模式
 * @param value 是否启用固定位置模式
 * @details 固定位置模式下怪物不会主动移动
 */
void Monster::set_stick_mode(bool value) { stick_mode_ = value; }

/**
 * @brief 设置挖掘范围
 * @param up_range 向上挖掘范围
 * @param down_range 向下挖掘范围
 */
void Monster::set_dig_ranges(std::int32_t up_range, std::int32_t down_range) {
  dig_up_range_ = std::max(up_range, 0);
  dig_down_range_ = std::max(down_range, 0);
}

/**
 * @brief 设置出现时间戳
 * @param now_ms 出现的时间（毫秒）
 */
void Monster::set_appear_time_ms(std::uint64_t now_ms) { appear_time_ms_ = now_ms; }

/** ==================== Monster：子角色/召唤管理 ==================== */

/**
 * @brief 添加子角色 ID（如分身、召唤物）
 */
void Monster::add_child_actor_id(std::uint64_t actor_id) {
  if (actor_id != 0) {
    child_actor_ids_.push_back(actor_id);
  }
}

/**
 * @brief 清理不在存活集合中的子角色 ID
 */
void Monster::prune_child_actor_ids(const std::unordered_set<std::uint64_t>& live_child_ids) {
  child_actor_ids_.erase(
      std::remove_if(child_actor_ids_.begin(), child_actor_ids_.end(),
                     [&](std::uint64_t actor_id) {
                       return live_child_ids.find(actor_id) == live_child_ids.end();
                     }),
      child_actor_ids_.end());
}

/**
 * @brief 设置召唤信息
 * @param monster_name 召唤的怪物名称
 * @param limit 召唤数量上限
 * @param delay_ms 召唤间隔（毫秒）
 */
void Monster::set_summon(std::string monster_name, std::int32_t limit,
                         std::uint64_t delay_ms) {
  summon_monster_name_ = std::move(monster_name);
  summon_limit_ = std::max(limit, 0);
  summon_delay_ms_ = delay_ms;
}

/** ==================== Monster：宠物（奴隶）系统 ==================== */

/**
 * @brief 设置主人角色 ID
 * @param actor_id 主人 ID
 */
void Monster::set_master_actor_id(std::uint64_t actor_id) {
  master_actor_id_ = actor_id;
  is_slave_ = actor_id != 0;
  if (is_slave_) {
    no_item_ = true;
  }
}

/**
 * @brief 完整配置怪物作为宠物的各项参数
 * @param master_actor_id 主人 ID
 * @param slave_exp 宠物经验
 * @param slave_make_level 宠物制造等级
 * @param slave_exp_level 宠物经验等级
 * @param master_royalty_time_ms 主人忠诚时间
 * @param slave_life_time_ms 宠物存活时间
 * @param no_item 是否掉落物品
 * @details 配置后自动应用宠物等级能力加成
 */
void Monster::configure_slave(std::uint64_t master_actor_id, std::int32_t slave_exp,
                              std::int32_t slave_make_level, std::int32_t slave_exp_level,
                              std::uint64_t master_royalty_time_ms,
                              std::uint64_t slave_life_time_ms, bool no_item) {
  master_actor_id_ = master_actor_id;
  is_slave_ = master_actor_id != 0;
  slave_exp_ = std::max(slave_exp, 0);
  slave_make_level_ = std::max(slave_make_level, 0);
  slave_exp_level_ = std::clamp(slave_exp_level, 0, 6);
  master_royalty_time_ms_ = master_royalty_time_ms;
  slave_life_time_ms_ = slave_life_time_ms;
  no_item_ = no_item || is_slave_;
  apply_slave_level_abilities();
}

void Monster::set_master_royalty_time_ms(std::uint64_t value) {
  master_royalty_time_ms_ = value;
}

void Monster::set_slave_life_time_ms(std::uint64_t value) { slave_life_time_ms_ = value; }

void Monster::set_no_item(bool value) { no_item_ = value; }

/**
 * @brief 设置怪物的 HP 和 MP（带钳位）
 * @param hp HP 值（钳位到 0 ~ max_hp）
 * @param mp MP 值（钳位到 0 ~ max_mp）
 */
void Monster::set_hp_mp(std::int32_t hp, std::int32_t mp) {
  hp_ = std::clamp(hp, 0, max_hp_);
  mp_ = std::clamp(mp, 0, max_mp_);
  if (hp_ == 0) {
    death_time_ms_ = death_time_ms_ == 0 ? 1 : death_time_ms_;
  }
}

/**
 * @brief 将 HP 降低到忠诚破坏临界值（HP/10，最低为 1）
 * @details 用于宠物忠诚度系统的惩罚机制
 */
void Monster::reduce_hp_to_loyalty_break_floor() {
  hp_ = std::max(1, hp_ / 10);
}

/**
 * @brief 应用宠物的等级能力加成
 * @details 根据宠物经验等级（0~6）计算并应用 HP、攻击力、魔法防御等属性加成。
 *          特殊名字宠物（如神兽、月灵等 legacy_special_slave_name）使用不同的加成公式：
 *          比例因子 = 0.3 + exp_level * 0.1，每级增加 3*factor*exp_level 攻击力、
 *          和 base_max_hp * factor * exp_level 最大 HP。
 *          普通宠物每级增加 2 点攻击力，最多增加 60*exp_level 或 15% 基底 HP（取小值）。
 */
void Monster::apply_slave_level_abilities() {
  max_hp_ = std::max(base_max_hp_, 1);
  dc_max_ = std::max(base_dc_max_, dc_min_);
  attack_power_ = std::max(dc_max_, 1);
  magic_defense_ = base_magic_defense_;
  accuracy_point_ = 15;

  const auto exp_level = std::clamp(slave_exp_level_, 0, 6);
  if (exp_level <= 0) {
    hp_ = std::clamp(hp_, 0, max_hp_);
    return;
  }

  if (legacy_special_slave_name(name())) {
    const auto factor = 0.3 + static_cast<double>(exp_level) * 0.1;
    dc_max_ += static_cast<std::int32_t>(std::lround(3.0 * factor * exp_level));
    max_hp_ += static_cast<std::int32_t>(
        std::lround(static_cast<double>(base_max_hp_) * factor)) *
               exp_level;
  } else {
    dc_max_ += 2 * exp_level;
    max_hp_ = std::min(base_max_hp_ + 60 * exp_level,
                       base_max_hp_ +
                           static_cast<std::int32_t>(
                               std::lround(static_cast<double>(base_max_hp_) * 0.15)) *
                               exp_level);
    magic_defense_ = 0;
  }
  dc_max_ = std::max(dc_max_, dc_min_);
  attack_power_ = std::max(dc_max_, 1);
  hp_ = std::clamp(hp_, 0, max_hp_);
}

/**
 * @brief 宠物获得经验（击杀怪物后调用）
 * @param slain_level 被击杀怪物的等级
 * @return 宠物是否升级
 * @details 经验值计算公式：next = 100 + level * 15 + kSlaveExpMore[exp_level]。
 *          累计经验超过升级阈值时，扣除阈值并尝试升级。升级上限受制造等级约束：
 *          cap = slave_make_level * 2 + 1，最大不超过 6 级。
 *          升级后重新应用等级能力加成。
 */
bool Monster::gain_slave_exp(std::int32_t slain_level) {
  if (!is_slave_) {
    return false;
  }
  static constexpr std::array<std::int32_t, 7> kSlaveExpMore{0, 0, 50, 100, 200, 300, 600};
  const auto exp_level = std::clamp(slave_exp_level_, 0, 6);
  const auto next = 100 + std::max(level_, 1) * 15 + kSlaveExpMore[exp_level];
  slave_exp_ += std::max(slain_level, 0);
  if (slave_exp_ <= next) {
    return false;
  }
  slave_exp_ -= next;
  const auto cap = std::max(slave_make_level_, 0) * 2 + 1;
  if (slave_exp_level_ >= cap) {
    return false;
  }
  slave_exp_level_ = std::min(slave_exp_level_ + 1, 6);
  apply_slave_level_abilities();
  return true;
}

/**
 * @brief 调度下一次 AI 处理 tick
 * @param current_tick 当前逻辑 tick
 * @details 根据减速效果计算下次 AI 调度的间隔，减速百分比以 100 为基准累加。
 *          例如：减速 50% 时 interval = (100 + 50 + 99) / 100 = 2 tick
 */
void Monster::schedule_next_ai_tick(std::uint64_t current_tick) {
  const auto slow_percent = std::max(current_slow_percent(current_tick), 0);
  const auto interval =
      static_cast<std::uint64_t>(std::max<std::int32_t>(1, (100 + slow_percent + 99) / 100));
  next_ai_tick_ = current_tick + interval;
}

void Monster::set_dir(std::uint8_t dir) { dir_ = static_cast<std::uint8_t>(dir % 8); }

/**
 * @brief 检查怪物是否在出生点范围内
 * @return 怪物当前位置是否在 home_x/y 为中心的 home_area 矩形区域内
 */
bool Monster::inside_home_area() const {
  return std::abs(x() - home_x_) <= home_area_ && std::abs(y() - home_y_) <= home_area_;
}

/**
 * @brief 选中一个目标角色
 * @param actor_id 目标角色 ID
 * @param now_ms 选中时间（毫秒，用于仇恨超时计算）
 */
void Monster::select_target(std::uint64_t actor_id, std::uint64_t now_ms) {
  aggro_target_id_ = actor_id;
  target_focus_time_ms_ = now_ms;
}

/**
 * @brief 清除当前目标
 * @details 清空目标 ID、仇恨时间戳和目标坐标
 */
void Monster::lose_target() {
  aggro_target_id_ = 0;
  target_focus_time_ms_ = 0;
  clear_target_xy();
}

/**
 * @brief 设置目标坐标（用于寻路到指定位置）
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 */
void Monster::set_target_xy(std::int32_t x, std::int32_t y) {
  target_x_ = x;
  target_y_ = y;
}

/**
 * @brief 清除目标坐标（重置为 -1）
 */
void Monster::clear_target_xy() {
  target_x_ = -1;
  target_y_ = -1;
}

/**
 * @brief 开始行走等待
 * @param now_ms 当前时间（毫秒）
 * @details 启用行走等待模式并记录开始时间，用于控制连续行走的间隔
 */
void Monster::begin_walk_wait(std::uint64_t now_ms) {
  walk_wait_mode_ = true;
  walk_wait_cur_time_ms_ = now_ms;
}

/**
 * @brief 设置行走等待模式
 * @param value 是否启用行走等待
 * @details 关闭等待模式时同时重置等待开始时间
 */
void Monster::set_walk_wait_mode(bool value) {
  walk_wait_mode_ = value;
  if (!value) {
    walk_wait_cur_time_ms_ = 0;
  }
}

/**
 * @brief 设置复制模式（用于分身类怪物）
 */
void Monster::set_dup_mode(bool value) { dup_mode_ = value; }

/**
 * @brief 重置当前行走步数
 */
void Monster::reset_walk_cur_step() { walk_cur_step_ = 0; }

/**
 * @brief 递增当前行走步数
 */
void Monster::increment_walk_cur_step() { ++walk_cur_step_; }

/**
 * @brief 初始化怪物的 AI 定时器
 * @param now_ms 当前时间（毫秒）
 * @param walk_offset_ms 行走时间偏移（用于出生后延迟首次行走）
 * @param hit_offset_ms 攻击时间偏移（用于出生后延迟首次攻击）
 * @details 搜索时间直接初始化到当前时间，行走和攻击时间根据偏移量反推
 */
void Monster::initialize_legacy_ai_timers(std::uint64_t now_ms,
                                          std::uint64_t walk_offset_ms,
                                          std::uint64_t hit_offset_ms) {
  walk_time_ms_ = now_ms >= walk_offset_ms ? now_ms - walk_offset_ms : 0;
  hit_time_ms_ = now_ms >= hit_offset_ms ? now_ms - hit_offset_ms : 0;
  attack_time_ms_ = hit_time_ms_;
  search_time_ms_ = now_ms;
  search_enemy_time_ms_ = now_ms;
}

void Monster::set_aggro_target_id(std::uint64_t actor_id) { aggro_target_id_ = actor_id; }

void Monster::clear_aggro_target() { lose_target(); }

/**
 * @brief 怪物的 tick 处理（虚拟方法重写）
 * @param context 地图上下文
 * @details 死亡时每 50 tick 检查一次是否转为鬼魂。
 *          存活时计算下次 tick 为 AI 处理 tick 和状态效果 tick 中较早的一个。
 */
void Monster::on_tick(MapContext& context) {
  if (hp_ <= 0) {
    set_next_due_tick(context.tick + 50);
    return;
  }
  auto next_due_tick = next_ai_tick_ > context.tick ? next_ai_tick_ : context.tick + 1;
  if (const auto status_tick = next_status_tick(); status_tick != 0) {
    next_due_tick = std::min(next_due_tick, status_tick > context.tick ? status_tick : context.tick + 1);
  }
  set_next_due_tick(next_due_tick);
}

/** ==================== Npc：NPC 类实现 ==================== */

/**
 * @brief 构造 Npc 实例
 * @param id 全局唯一 ID
 * @param name NPC 名称
 * @param map_id 所在地图 ID
 * @param x 出生 X 坐标
 * @param y 出生 Y 坐标
 * @param service NPC 提供的服务类型字符串（如 "buy,sell,repair,storage,guild"）
 * @param merchant_items 商人出售的物品列表
 * @param dialog_sections 对话段落配置
 * @param price_rate_percent 价格倍率百分比
 * @param merchant_key 商人唯一标识键
 * @param merchant_products 商人商品运行时配置
 * @param merchant_prices 物品价格覆盖表
 * @param deal_std_modes 允许交易的物品 StdMode 列表
 * @param weapon_upgrades 武器升级记录列表
 * @details 构造函数中自动解析服务字符串，判断是否支持购买、出售、修理、仓库等功能。
 *          buy_enabled 在 merchant_items 或 merchant_products 非空时自动启用。
 *          weapon_upgrade_enabled 在服务字符串含 "upgrade" 或对话配置中有升级选项时启用。
 */
Npc::Npc(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
         std::string service, std::vector<LegacyUserItem> merchant_items,
         std::vector<NpcDialogSectionConfig> dialog_sections,
         std::int32_t price_rate_percent, std::string merchant_key,
         std::vector<MerchantProductRuntimeConfig> merchant_products,
         std::unordered_map<std::int32_t, std::int32_t> merchant_prices,
         std::vector<std::int32_t> deal_std_modes,
         std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades)
    : GameObject(id, GameObjectKind::npc, std::move(name), std::move(map_id), x, y),
      service_(normalize_service(std::move(service))),
      merchant_key_(std::move(merchant_key)),
      merchant_items_(std::move(merchant_items)),
      weapon_upgrades_(std::move(weapon_upgrades)),
      merchant_products_(std::move(merchant_products)),
      merchant_prices_(std::move(merchant_prices)),
      deal_std_modes_(std::move(deal_std_modes)),
      dialog_sections_(std::move(dialog_sections)),
      price_rate_percent_(std::max(price_rate_percent, 0)),
      buy_enabled_(!merchant_items_.empty() || !merchant_products_.empty()),
      weapon_upgrade_enabled_(service_.find("upgrade") != std::string::npos ||
                              dialog_supports_weapon_upgrade(dialog_sections_)) {}

/**
 * @brief 检查 NPC 是否支持购买功能
 */
bool Npc::supports_buy() const { return buy_enabled_; }

/**
 * @brief 检查 NPC 是否支持出售功能
 */
bool Npc::supports_sell() const { return service_.find("sell") != std::string::npos; }

/**
 * @brief 检查 NPC 是否支持修理功能
 */
bool Npc::supports_repair() const { return service_.find("repair") != std::string::npos; }

/**
 * @brief 检查 NPC 是否支持仓库功能
 */
bool Npc::supports_storage() const { return service_.find("storage") != std::string::npos; }

/**
 * @brief 检查 NPC 是否支持行会功能
 */
bool Npc::supports_guild() const { return service_.find("guild") != std::string::npos; }

/**
 * @brief 检查 NPC 是否支持城堡管理功能
 */
bool Npc::supports_castle() const { return service_.find("castle") != std::string::npos; }

/**
 * @brief 检查 NPC 是否支持武器升级功能
 */
bool Npc::supports_weapon_upgrade() const { return weapon_upgrade_enabled_; }

/**
 * @brief 检查 NPC 的运行周期是否到期
 * @param now_ms 当前时间（毫秒）
 * @return 运行间隔到期返回 true
 */
bool Npc::legacy_due(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - run_time_ms_ >
         static_cast<std::int64_t>(run_next_tick_ms_);
}

/**
 * @brief 检查 NPC 是否到了搜索时间
 * @param now_ms 当前时间（毫秒）
 */
bool Npc::legacy_search_due(std::uint64_t now_ms) const {
  return now_ms > search_time_ms_ + search_rate_ms_;
}

const std::vector<LegacyUserItem>& Npc::merchant_items() const { return merchant_items_; }

std::vector<LegacyUserItem>& Npc::merchant_items_mutable() { return merchant_items_; }

const std::vector<LegacyWeaponUpgradeRecord>& Npc::weapon_upgrades() const {
  return weapon_upgrades_;
}

std::vector<LegacyWeaponUpgradeRecord>& Npc::weapon_upgrades_mutable() {
  return weapon_upgrades_;
}

const std::vector<MerchantProductRuntimeConfig>& Npc::merchant_products() const {
  return merchant_products_;
}

std::vector<MerchantProductRuntimeConfig>& Npc::merchant_products_mutable() {
  return merchant_products_;
}

const std::vector<NpcDialogSectionConfig>& Npc::dialog_sections() const {
  return dialog_sections_;
}

/**
 * @brief 更新 NPC 的运行时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Npc::mark_legacy_run_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

/**
 * @brief 更新 NPC 的搜索时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Npc::mark_legacy_search_time(std::uint64_t now_ms) { search_time_ms_ = now_ms; }

/**
 * @brief 更新 NPC 的鬼魂化时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Npc::mark_legacy_ghost_time(std::uint64_t now_ms) { ghost_time_ms_ = now_ms; }

/**
 * @brief 更新 NPC 的补货时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Npc::mark_legacy_refill_time(std::uint64_t now_ms) { refill_time_ms_ = now_ms; }

/**
 * @brief 更新 NPC 的验证时间戳
 * @param now_ms 当前时间（毫秒）
 */
void Npc::mark_legacy_verify_time(std::uint64_t now_ms) { verify_time_ms_ = now_ms; }

/**
 * @brief 查询 NPC 对指定物品的定价
 * @param item_id 物品 ID
 * @return 价格（未设置时返回 nullopt）
 */
std::optional<std::int32_t> Npc::merchant_price(std::int32_t item_id) const {
  const auto it = merchant_prices_.find(item_id);
  if (it == merchant_prices_.end()) {
    return std::nullopt;
  }
  return it->second;
}

/**
 * @brief 设置 NPC 对指定物品的定价
 * @param item_id 物品 ID
 * @param price 价格
 * @note item_id 或 price 小于等于 0 时静默忽略
 */
void Npc::set_merchant_price(std::int32_t item_id, std::int32_t price) {
  if (item_id <= 0 || price <= 0) {
    return;
  }
  merchant_prices_[item_id] = price;
}

/**
 * @brief 判断 NPC 是否允许交易指定 StdMode 的物品
 * @param std_mode 物品的标准模式
 */
bool Npc::deals_std_mode(std::int32_t std_mode) const {
  return std::find(deal_std_modes_.begin(), deal_std_modes_.end(), std_mode) !=
         deal_std_modes_.end();
}

/**
 * @brief 应用商人状态快照（用于恢复商人数据）
 * @param state 商人状态记录
 * @details 如果 state 中的 merchant_key 非空且与当前不匹配，则拒绝应用。
 *          成功应用后同步更新 buy_enabled 状态。
 */
void Npc::apply_merchant_state(const MerchantStateRecord& state) {
  if (!state.merchant_key.empty() && state.merchant_key != merchant_key_) {
    return;
  }
  merchant_items_ = state.goods;
  weapon_upgrades_ = state.weapon_upgrades;
  merchant_prices_ = state.prices;
  buy_enabled_ = !merchant_items_.empty() || !merchant_products_.empty();
}

/**
 * @brief 创建商人状态快照（用于保存/恢复）
 * @return 包含当前所有商人数据的 MerchantStateRecord
 */
MerchantStateRecord Npc::snapshot_merchant_state() const {
  MerchantStateRecord state;
  state.merchant_key = merchant_key_;
  state.npc_id = std::to_string(id());
  state.map_id = map_id();
  state.goods = merchant_items_;
  state.weapon_upgrades = weapon_upgrades_;
  state.prices = merchant_prices_;
  return state;
}

/**
 * @brief NPC 的 tick 处理（每 100 tick 调度一次）
 * @param context 地图上下文
 * @details NPC 不需要频繁处理，使用较长的调度间隔
 */
void Npc::on_tick(MapContext& context) { set_next_due_tick(context.tick + 100); }

/** ==================== EventObject：事件对象类实现 ==================== */

/**
 * @brief 构造 EventObject 实例
 * @param id 全局唯一 ID
 * @param name 事件对象名称
 * @param map_id 所在地图 ID
 * @param x 出生 X 坐标
 * @param y 出生 Y 坐标
 * @details 事件对象用于地图上的触发器、传送门等交互元素
 */
EventObject::EventObject(std::uint64_t id, std::string name, std::string map_id, std::int32_t x,
                         std::int32_t y)
    : GameObject(id, GameObjectKind::event_object, std::move(name), std::move(map_id), x, y) {}

/**
 * @brief 事件对象的 tick 处理（每 50 tick 调度一次）
 * @param context 地图上下文
 * @details 事件对象不需要频繁处理，使用适中的调度间隔
 */
void EventObject::on_tick(MapContext& context) { set_next_due_tick(context.tick + 50); }

/** ==================== 文件结束 ==================== */

}  // namespace mir2
