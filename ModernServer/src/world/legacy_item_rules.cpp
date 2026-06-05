/**
 * @file legacy_item_rules.cpp
 * @brief 物品使用规则和验证实现
 * @details 实现了传奇3传统物品系统的核心规则，包括：
 *          - 物品槽位映射与验证
 *          - 装备条件检查（职业、性别、等级、属性需求、重量）
 *          - 物品升级属性计算
 *          - 怪物掉落物品随机属性生成
 *          - 特殊"未知物品"属性生成
 *          使用 packed low/high 编码兼容传奇3的16位属性存储格式。
 */

#include "world/legacy_item_rules.hpp"

#include <algorithm>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数和常量
 */
namespace {

/**
 * @brief 从16位打包值中提取低8位
 * @param value 打包的16位值
 * @return 低8位的整数值
 */
std::int32_t packed_low(std::uint16_t value) {
  return static_cast<std::int32_t>(value & 0xffu);
}

/**
 * @brief 从16位打包值中提取高8位
 * @param value 打包的16位值
 * @return 高8位的整数值
 */
std::int32_t packed_high(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

/**
 * @brief 将两个8位值打包为16位
 * @details 兼容传奇3的属性存储格式，将低位和高位打包到一个uint16_t中。
 *          例如DC、MC、SC、AC、MAC等属性都使用此格式存储最小值和最大值。
 * @param low 低位值（最小值），自动限制在0-255
 * @param high 高位值（最大值），自动限制在0-255
 * @return 打包后的16位值
 */
std::uint16_t pack_range(std::int32_t low, std::int32_t high) {
  return static_cast<std::uint16_t>((std::clamp(high, 0, 255) << 8) |
                                    std::clamp(low, 0, 255));
}

/**
 * @brief 向打包值的高8位增加delta
 * @param packed 原打包值
 * @param delta 增量
 * @return 修改后的打包值
 */
std::uint16_t add_high(std::uint16_t packed, std::int32_t delta) {
  return pack_range(packed_low(packed), packed_high(packed) + delta);
}

/**
 * @brief 向打包值的低8位增加delta
 * @param packed 原打包值
 * @param delta 增量
 * @return 修改后的打包值
 */
std::uint16_t add_low(std::uint16_t packed, std::int32_t delta) {
  return pack_range(packed_low(packed) + delta, packed_high(packed));
}

/**
 * @brief 检查角色职业是否符合物品要求
 * @param required_job 要求的职业（-1表示不限）
 * @param character_job 角色职业
 * @return true 符合，false 不符合
 */
bool matches_legacy_job(std::int32_t required_job, std::uint8_t character_job) {
  return required_job < 0 || required_job == static_cast<std::int32_t>(character_job);
}

/**
 * @brief 检查角色性别是否符合物品要求
 * @param required_sex 要求的性别（-1表示不限）
 * @param character_sex 角色性别
 * @return true 符合，false 不符合
 */
bool matches_legacy_sex(std::int32_t required_sex, std::uint8_t character_sex) {
  return required_sex < 0 || required_sex == static_cast<std::int32_t>(character_sex);
}

/**
 * @brief 检查服装（std_mode 10/11）的性别限制
 * @details std_mode=10 为男装，std_mode=11 为女装。
 * @param item_config 物品配置
 * @param character_sex 角色性别（0=男, 1=女）
 * @return true 符合性别要求，false 不符合
 */
bool matches_legacy_dress_sex(const ItemConfig& item_config, std::uint8_t character_sex) {
  if (item_config.std_mode == 10) {
    return character_sex == 0;
  }
  if (item_config.std_mode == 11) {
    return character_sex == 1;
  }
  return true;
}

/**
 * @brief 将整数值限制在0-255范围内
 * @param value 原始值
 * @return 限制后的uint8_t值
 */
std::uint8_t clamp_legacy_desc(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

/**
 * @brief 将耐久度限制在0-65000范围内
 * @param value 原始值
 * @return 限制后的uint16_t值
 */
std::uint16_t clamp_legacy_dura(std::int32_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0, 65000));
}

/**
 * @brief 随机升级判定
 * @details 进行 count 次独立判定，每次有 1/range 概率增加1点升级值。
 *          一旦某次判定失败，后续不再继续判定。
 *          这是传奇3原版物品升级的随机算法。
 * @param count 判定次数上限
 * @param range 概率分母（1/range 概率成功）
 * @param random 随机数生成器
 * @return 升级点数
 */
std::int32_t legacy_get_upgrade(std::int32_t count, std::int32_t range,
                                LegacyRandom& random) {
  std::int32_t result = 0;
  for (std::int32_t index = 0; index < count; ++index) {
    if (random.random(range) == 0) {
      ++result;
    } else {
      break;
    }
  }
  return result;
}

/**
 * @brief 增加物品耐久度
 * @details 同时增加当前耐久和最大耐久。
 * @param item 用户物品（输出参数）
 * @param amount 增加量
 */
void add_legacy_dura(LegacyUserItem& item, std::int32_t amount) {
  item.dura_max = clamp_legacy_dura(static_cast<std::int32_t>(item.dura_max) + amount);
  item.dura = clamp_legacy_dura(static_cast<std::int32_t>(item.dura) + amount);
}

}  // namespace

/**
 * @brief 根据标准模式解析装备槽位
 * @details 映射传奇3的 std_mode 值到统一的装备槽位索引。
 *          支持识别武器、衣服、头盔、项链、戒指、手镯、右手、靴子、护身符、腰带。
 * @param std_mode 物品的标准模式
 * @return 槽位索引（参见 kEquip* 常量），无法识别返回 -1
 */
std::int32_t legacy_resolve_slot_from_std_mode(std::int32_t std_mode) {
  switch (std_mode) {
    case 5:
    case 6:
      return static_cast<std::int32_t>(kEquipWeapon);
    case 10:
    case 11:
      return static_cast<std::int32_t>(kEquipDress);
    case 15:
      return static_cast<std::int32_t>(kEquipHelmet);
    case 19:
    case 20:
    case 21:
      return static_cast<std::int32_t>(kEquipNecklace);
    case 22:
    case 23:
      return static_cast<std::int32_t>(kEquipRingLeft);
    case 24:
    case 26:
      return static_cast<std::int32_t>(kEquipArmRingRight);
    case 25:
      return static_cast<std::int32_t>(kEquipBujuk);
    case 30:
      return static_cast<std::int32_t>(kEquipRightHand);
    case 52:
      return static_cast<std::int32_t>(kEquipBoots);
    case 53:
      return static_cast<std::int32_t>(kEquipCharm);
    case 54:
      return static_cast<std::int32_t>(kEquipBelt);
    default:
      return -1;
  }
}

/**
 * @brief 检查物品是否适合指定槽位
 * @details 如果物品显式设置了 equip_slot（>=0），则严格匹配该槽位。
 *          否则根据 std_mode 判断哪些槽位可以放入该物品。
 *          部分std_mode支持多个槽位（如戒指可左右互换，手镯可左右互换）。
 * @param item_config 物品配置
 * @param slot 槽位索引
 * @return true 适合，false 不适合
 */
bool legacy_item_fits_slot(const ItemConfig& item_config, std::int32_t slot) {
  if (slot < 0 || slot >= static_cast<std::int32_t>(kMaxEquipSlots)) {
    return false;
  }
  // 如果物品显式指定了 equip_slot，只匹配该槽位
  if (item_config.equip_slot >= 0) {
    return item_config.equip_slot == slot;
  }

  // 根据 std_mode 判断槽位匹配
  switch (slot) {
    case static_cast<std::int32_t>(kEquipDress):
      return item_config.std_mode == 10 || item_config.std_mode == 11;
    case static_cast<std::int32_t>(kEquipWeapon):
      return item_config.std_mode == 5 || item_config.std_mode == 6;
    case static_cast<std::int32_t>(kEquipRightHand):
      return item_config.std_mode == 30;
    case static_cast<std::int32_t>(kEquipNecklace):
      return item_config.std_mode == 19 || item_config.std_mode == 20 ||
             item_config.std_mode == 21;
    case static_cast<std::int32_t>(kEquipHelmet):
      return item_config.std_mode == 15;
    case static_cast<std::int32_t>(kEquipRingLeft):
    case static_cast<std::int32_t>(kEquipRingRight):
      return item_config.std_mode == 22 || item_config.std_mode == 23;
    case static_cast<std::int32_t>(kEquipArmRingRight):
      return item_config.std_mode == 24 || item_config.std_mode == 26;
    case static_cast<std::int32_t>(kEquipArmRingLeft):
      return item_config.std_mode == 24 || item_config.std_mode == 25 ||
             item_config.std_mode == 26;
    case static_cast<std::int32_t>(kEquipBujuk):
      return item_config.std_mode == 25;
    case static_cast<std::int32_t>(kEquipBelt):
      return item_config.std_mode == 54;
    case static_cast<std::int32_t>(kEquipBoots):
      return item_config.std_mode == 52;
    case static_cast<std::int32_t>(kEquipCharm):
      return item_config.std_mode == 53;
    default:
      return false;
  }
}

/**
 * @brief 判断槽位是否使用手部重量
 * @details 武器（kEquipWeapon）和右手（kEquipRightHand）使用手部重量。
 *          手部重量限制独立于穿戴重量，通常较小。
 * @param slot 槽位索引
 * @return true 使用手部重量，false 使用穿戴重量
 */
bool legacy_slot_uses_hand_weight(std::size_t slot) {
  return slot == kEquipWeapon || slot == kEquipRightHand;
}

/**
 * @brief 匿名命名空间，延续定义内部辅助函数
 */
namespace {

/**
 * @brief 检查特定 std_mode 下 desc[7] 是否锁定装备
 * @details 对于头盔(15)、项链(19-21)、戒指(22-23)、手镯(24,26)、
 *          靴子(52)、护身符(53)、腰带(54)，如果 desc[7] 非零则锁定。
 *          desc[7] 在传奇3中用于标记"诅咒"或"特殊绑定"状态。
 * @param item_config 物品配置
 * @return true 受 desc[7] 锁定影响，false 不受影响
 */
bool desc7_locks_takeoff(const ItemConfig& item_config) {
  switch (item_config.std_mode) {
    case 15:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
    case 52:
    case 53:
    case 54:
      return true;
    default:
      return false;
  }
}

}  // namespace

/**
 * @brief 判断物品是否可解下
 * @details 检查逻辑：
 *          1. 空物品可解下
 *          2. 无配置信息的物品可解下
 *          3. 受 desc[7] 锁定且 desc[7] != 0 时不可解下
 *          4. 检查 item_desc 标志位：kLegacyItemUnableTakeOff 或 kLegacyItemNeverTakeOff
 * @param item_config 物品配置（可为nullptr）
 * @param item 用户物品数据
 * @return true 可解下，false 不可解下
 */
bool legacy_item_can_take_off(const ItemConfig* item_config, const LegacyUserItem& item) {
  if (is_empty(item)) {
    return true;
  }
  if (item_config == nullptr) {
    return true;
  }
  // 检查 desc[7] 锁定（头盔、项链、戒指等特殊装备）
  if (desc7_locks_takeoff(*item_config) && item.desc[7] != 0) {
    return false;
  }
  return (item_config->item_desc & (kLegacyItemUnableTakeOff | kLegacyItemNeverTakeOff)) == 0;
}

/**
 * @brief 判断物品是否为消耗品
 * @details 消耗品包括药水、食物等可恢复HP/MP的物品。
 * @param item_config 物品配置
 * @return true 是消耗品，false 不是
 */
bool legacy_item_is_consumable(const ItemConfig& item_config) {
  return item_config.hp_add > 0 || item_config.mp_add > 0;
}

/**
 * @brief 判断物品是否为技能书
 * @param item_config 物品配置
 * @return true 是技能书，false 不是
 */
bool legacy_item_is_magic_book(const ItemConfig& item_config) {
  return item_config.std_mode == 4;
}

/**
 * @brief 判断物品是否为卷轴
 * @param item_config 物品配置
 * @return true 是卷轴，false 不是
 */
bool legacy_item_is_scroll(const ItemConfig& item_config) {
  return item_config.std_mode == 31;
}

/**
 * @brief 判断物品是否为可解绑包裹
 * @param item_config 物品配置
 * @return true 是可解绑包裹，false 不是
 */
bool legacy_item_is_unbind_bundle(const ItemConfig& item_config) {
  return !item_config.unbind_item.empty() && item_config.unbind_count > 0;
}

/**
 * @brief 判断物品是否为武器升级材料
 * @details 包括黑铁矿石、首饰等可用于武器升级的材料。
 *          对应的 std_mode 为 19-24, 26, 52-54。
 * @param item_config 物品配置
 * @return true 是升级材料，false 不是
 */
bool legacy_is_upgrade_weapon_stuff(const ItemConfig& item_config) {
  switch (item_config.std_mode) {
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
    case 52:
    case 53:
    case 54:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 判断物品是否为祝福油
 * @param item_config 物品配置
 * @return true 是祝福油，false 不是
 */
bool legacy_is_blessed_oil(const ItemConfig& item_config) {
  // 方法1：std_mode=3 && shape=4（传奇3原版祝福油的标识）
  if (item_config.std_mode == 3 && item_config.shape == 4) {
    return true;
  }
  // 方法2：scroll_kind 字段标记为 "blessed_oil"
  return !item_config.scroll_kind.empty() &&
         util::lower_copy(item_config.scroll_kind) == "blessed_oil";
}

/**
 * @brief 获取卷轴类型
 * @details 优先级：
 *          1. 使用 item_config.scroll_kind 字段（如果非空）
 *          2. 根据物品名称启发式判断：
 *             - 含"random" -> random（随机传送）
 *             - 含"escape" -> escape（回城/逃脱）
 *             - 含"town"/"return"/"recall" -> town（城镇传送）
 *          3. 根据 shape 值判断：
 *             - shape=1 -> random（随机传送）
 *             - shape=2 -> escape（回城卷轴）
 *          4. 默认 -> town
 * @param item_config 物品配置
 * @return 卷轴类型名称："random"、"escape"、"town"
 */
std::string legacy_scroll_kind(const ItemConfig& item_config) {
  // 优先使用配置中指定的 scroll_kind
  if (!item_config.scroll_kind.empty()) {
    return util::lower_copy(item_config.scroll_kind);
  }
  // 根据名称启发式判断
  const auto name = util::lower_copy(item_config.name);
  if (name.find("random") != std::string::npos) {
    return "random";
  }
  if (name.find("escape") != std::string::npos) {
    return "escape";
  }
  if (name.find("town") != std::string::npos || name.find("return") != std::string::npos ||
      name.find("recall") != std::string::npos) {
    return "town";
  }
  // 根据 shape 值判断
  if (item_config.shape == 1) {
    return "random";
  }
  if (item_config.shape == 2) {
    return "escape";
  }
  return "town";
}

/**
 * @brief 计算物品升级后的配置属性
 * @details 将用户物品 desc[] 中的升级加成应用到基础配置上。
 *          不同的 std_mode 处理方式不同：
 *
 *          武器(5,6)：
 *          - desc[0] -> DC高位+  desc[1] -> MC高位+
 *          - desc[2] -> SC高位+  desc[3] -> AC低位+
 *          - desc[5] -> AC高位+  desc[4] -> MAC低位+
 *          - desc[6] -> MAC高位+ desc[7] 1-10 -> special_pwr
 *          - desc[10] != 0 -> 设置 item_desc 0x01（幸运）
 *
 *          衣服(10,11)：
 *          - desc[0] -> AC高位+  desc[1] -> MAC高位+
 *          - desc[2] -> DC高位+  desc[3] -> MC高位+
 *          - desc[4] -> SC高位+
 *
 *          饰品(15,19-26)：
 *          - desc[0-4] -> AC/MAC/DC/MC/SC 高位+
 *          - desc[5] > 0 -> need = desc[5]
 *          - desc[6] > 0 -> need_level = desc[6]
 *
 * @param item_config 基础物品配置
 * @param user_item 用户物品数据（含 desc 升级信息）
 * @return 升级后的物品配置
 */
ItemConfig legacy_upgraded_item_config(const ItemConfig& item_config,
                                        const LegacyUserItem& user_item) {
  auto upgraded = item_config;
  switch (item_config.std_mode) {
    case 5:
    case 6:
      // 武器升级：DC/MC/SC/AC/MAC + special_pwr
      upgraded.dc = add_high(upgraded.dc, user_item.desc[0]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[1]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[2]);
      upgraded.ac = add_low(upgraded.ac, user_item.desc[3]);
      upgraded.ac = add_high(upgraded.ac, user_item.desc[5]);
      upgraded.mac = add_low(upgraded.mac, user_item.desc[4]);
      upgraded.mac = add_high(upgraded.mac, user_item.desc[6]);
      if (user_item.desc[7] >= 1 && user_item.desc[7] <= 10) {
        upgraded.special_pwr = user_item.desc[7];
      }
      if (user_item.desc[10] != 0) {
        upgraded.item_desc |= 0x01;
      }
      break;
    case 10:
    case 11:
      // 衣服升级：AC/MAC/DC/MC/SC
      upgraded.ac = add_high(upgraded.ac, user_item.desc[0]);
      upgraded.mac = add_high(upgraded.mac, user_item.desc[1]);
      upgraded.dc = add_high(upgraded.dc, user_item.desc[2]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[3]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[4]);
      break;
    case 15:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
      // 饰品升级：AC/MAC/DC/MC/SC + need/need_level
      upgraded.ac = add_high(upgraded.ac, user_item.desc[0]);
      upgraded.mac = add_high(upgraded.mac, user_item.desc[1]);
      upgraded.dc = add_high(upgraded.dc, user_item.desc[2]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[3]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[4]);
      if (user_item.desc[5] > 0) {
        upgraded.need = user_item.desc[5];
      }
      if (user_item.desc[6] > 0) {
        upgraded.need_level = user_item.desc[6];
      }
      break;
    default:
      break;
  }
  return upgraded;
}

/**
 * @brief 为怪物掉落物品随机生成升级属性
 * @details 模拟传奇3的核心掉落升级机制。不同 std_mode 的物品有不同
 *          的升级概率和属性分配。算法特征：
 *
 *          武器(5,6)：
 *          - DC: 12次判定，1/15概率，12次全失败则无
 *          - MC/SC: 类似，概率1/15
 *          - AC: 12次判定，1/24概率
 *          - MAC: 12次判定，1/12概率
 *          - special_pwr: 12次判定，1/15概率
 *          - 耐久: 12次判定，1/3~1/6概率
 *
 *          衣服(10,11)：
 *          - AC/MAC: 6次判定，1/15概率
 *          - DC/MC/SC: 6次判定，1/20概率
 *
 *          饰品(15,19-26)：各有不同的概率组合
 *
 * @param item_config 物品配置
 * @param user_item 用户物品数据（输出参数，desc[] 数组被修改）
 * @param random 随机数生成器
 */
void legacy_random_upgrade_monster_drop_item(const ItemConfig& item_config,
                                             LegacyUserItem& user_item,
                                             LegacyRandom& random) {
  std::int32_t up = 0;
  switch (item_config.std_mode) {
    case 5:
    case 6:
      // 武器 - DC升级
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      // 武器 - MAC升级（特殊槽位desc[6]）
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(20) == 0) {
        const auto incp = (1 + up) / 3;
        if (incp > 0) {
          user_item.desc[6] = clamp_legacy_desc(random.random(3) != 0 ? incp : 10 + incp);
        }
      }
      // 武器 - MC升级
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      // 武器 - SC升级
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      // 武器 - AC升级
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(24) == 0) {
        user_item.desc[5] = clamp_legacy_desc(1 + (up / 2));
      }
      // 武器 - 耐久度升级
      up = legacy_get_upgrade(12, 12, random);
      if (random.random(3) < 2) {
        add_legacy_dura(user_item, (1 + up) * 2000);
      }
      // 武器 - 特殊属性升级
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(10) == 0) {
        user_item.desc[7] = clamp_legacy_desc(1 + (up / 2));
      }
      break;
    case 10:
    case 11:
      // 衣服
      up = legacy_get_upgrade(6, 15, random);
      if (random.random(30) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 15, random);
      if (random.random(30) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 10, random);
      if (random.random(8) < 6) { add_legacy_dura(user_item, (1 + up) * 2000); }
      break;
    case 19:
      // 项链类型19
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 10, random);
      if (random.random(4) < 3) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    case 20:
    case 21:
    case 24:
      // 项链类型20/21, 手镯类型24
      up = legacy_get_upgrade(6, 30, random);
      if (random.random(60) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 30, random);
      if (random.random(60) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(20) < 15) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    case 26:
      // 手镯类型26
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(20) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(20) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(20) < 15) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    case 22:
      // 戒指类型22
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    case 23:
      // 戒指类型23
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    case 15:
      // 头盔
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) { user_item.desc[0] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[1] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[2] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[3] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) { user_item.desc[4] = clamp_legacy_desc(1 + up); }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) { add_legacy_dura(user_item, (1 + up) * 1000); }
      break;
    default:
      break;
  }
}

/**
 * @brief 为"未知物品"设置随机属性
 * @details 处理传奇3中的神秘装备系列：
 *          - 神秘戒指（shape=130, 类型22/23）
 *          - 神秘手镯（shape=131, 类型24/26）
 *          - 神秘头盔（shape=132, 类型15）
 *
 *          算法特点：
 *          1. 使用多层随机判定，每次判定的概率不同
 *          2. 属性强度取决于多次随机叠加
 *          3. 总属性值(sum)达标后自动生成特殊需求条件：
 *             - 高防御 -> 需要等级25+防御*3
 *             - 高攻击 -> 需要攻击力35+攻击*4
 *             - 等等
 *          4. 有概率获得"已鉴定"标记(desc[7]=1)
 *          5. desc[8] 标记为神秘装备(1)
 *
 * @param item_config 物品配置
 * @param user_item 用户物品数据（输出参数）
 * @param random 随机数生成器
 */
void legacy_random_set_unknown_monster_drop_item(const ItemConfig& item_config,
                                                 LegacyUserItem& user_item,
                                                 LegacyRandom& random) {
  constexpr std::int32_t kRingOfUnknown = 130;
  constexpr std::int32_t kBraceletOfUnknown = 131;
  constexpr std::int32_t kHelmetOfUnknown = 132;
  // 仅处理神秘装备（shape=130/131/132）
  if (item_config.shape != kRingOfUnknown && item_config.shape != kBraceletOfUnknown &&
      item_config.shape != kHelmetOfUnknown) {
    return;
  }

  std::int32_t up = 0;
  std::int32_t sum = 0;

  // 神秘头盔（std_mode=15）
  if (item_config.std_mode == 15) {
    up = legacy_get_upgrade(4, 3, random) + legacy_get_upgrade(4, 8, random) +
         legacy_get_upgrade(4, 20, random);
    if (up > 0) { user_item.desc[0] = clamp_legacy_desc(up); }
    sum += up;
    up = legacy_get_upgrade(4, 3, random) + legacy_get_upgrade(4, 8, random) +
         legacy_get_upgrade(4, 20, random);
    if (up > 0) { user_item.desc[1] = clamp_legacy_desc(up); }
    sum += up;
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 15, random) + legacy_get_upgrade(3, 30, random);
      if (up > 0) { user_item.desc[index] = clamp_legacy_desc(up); }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) { add_legacy_dura(user_item, (1 + up) * 1000); }
    if (random.random(30) == 0) { user_item.desc[7] = 1; }
    user_item.desc[8] = 1; // 标记为神秘装备
    if (sum >= 3) {
      if (user_item.desc[0] >= 5) {
        user_item.desc[5] = 1; // 需要防御
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[0] * 3);
      } else if (user_item.desc[2] >= 2) {
        user_item.desc[5] = 1; // 需要攻击
        user_item.desc[6] = clamp_legacy_desc(35 + user_item.desc[2] * 4);
      } else if (user_item.desc[3] >= 2) {
        user_item.desc[5] = 2; // 需要魔法
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 2) {
        user_item.desc[5] = 3; // 需要道术
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
    return;
  }

  // 神秘戒指（std_mode=22/23）
  if (item_config.std_mode == 22 || item_config.std_mode == 23) {
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 4, random) + legacy_get_upgrade(3, 8, random) +
           legacy_get_upgrade(6, 20, random);
      if (up > 0) { user_item.desc[index] = clamp_legacy_desc(up); }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) { add_legacy_dura(user_item, (1 + up) * 1000); }
    if (random.random(30) == 0) { user_item.desc[7] = 1; }
    user_item.desc[8] = 1;
    if (sum >= 3) {
      if (user_item.desc[2] >= 3) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[2] * 3);
      } else if (user_item.desc[3] >= 3) {
        user_item.desc[5] = 2;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 3) {
        user_item.desc[5] = 3;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
    return;
  }

  // 神秘手镯（std_mode=24/26）
  if (item_config.std_mode == 24 || item_config.std_mode == 26) {
    up = legacy_get_upgrade(3, 5, random) + legacy_get_upgrade(5, 20, random);
    if (up > 0) { user_item.desc[0] = clamp_legacy_desc(up); }
    sum += up;
    up = legacy_get_upgrade(3, 5, random) + legacy_get_upgrade(5, 20, random);
    if (up > 0) { user_item.desc[1] = clamp_legacy_desc(up); }
    sum += up;
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 15, random) + legacy_get_upgrade(5, 30, random);
      if (up > 0) { user_item.desc[index] = clamp_legacy_desc(up); }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) { add_legacy_dura(user_item, (1 + up) * 1000); }
    if (random.random(30) == 0) { user_item.desc[7] = 1; }
    user_item.desc[8] = 1;
    if (sum >= 2) {
      if (user_item.desc[0] >= 3) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[0] * 3);
      } else if (user_item.desc[2] >= 2) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(30 + user_item.desc[2] * 3);
      } else if (user_item.desc[3] >= 2) {
        user_item.desc[5] = 2;
        user_item.desc[6] = clamp_legacy_desc(20 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 2) {
        user_item.desc[5] = 3;
        user_item.desc[6] = clamp_legacy_desc(20 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
  }
}

/**
 * @brief 检查角色是否能穿上指定物品
 * @details 完整的装备可行性检查流程：
 *
 *          1. 槽位匹配：物品是否适合该装备槽位
 *          2. 职业检查：物品是否有职业要求且角色符合
 *          3. 性别检查：物品是否有性别要求且角色符合（含服装特殊性别逻辑）
 *          4. 等级检查：物品需要等级 > 角色等级时禁止
 *          5. 属性需求检查：need=1/2/3 时检查角色DC/MC/SC是否达标
 *          6. 重量检查：
 *             - 手部重量：武器和右手使用
 *             - 穿戴重量：其他槽位使用（计算新旧物品重量差）
 *
 * @param character 角色记录
 * @param item_config 物品配置（自动考虑升级加成）
 * @param user_item 用户物品数据
 * @param slot 目标槽位
 * @param current_wear_weight 当前总穿戴重量
 * @param current_hand_weight 当前手部重量（当前未实际使用，仅占位）
 * @param old_slot_weight 原槽位物品重量（用于计算重量变化）
 * @param reason 输出参数，失败时存放原因字符串
 * @return true 可以穿上，false 不能穿上
 */
bool legacy_can_take_on_item(const CharacterRecord& character,
                             const ItemConfig& item_config,
                             const LegacyUserItem& user_item,
                             std::int32_t slot,
                             std::int32_t current_wear_weight,
                             std::int32_t current_hand_weight,
                             std::int32_t old_slot_weight,
                             std::string* reason) {
  // 当前未使用 hand_weight，抑制未使用变量警告
  static_cast<void>(current_hand_weight);

  // 1. 检查槽位匹配
  if (!legacy_item_fits_slot(item_config, slot)) {
    if (reason != nullptr) {
      *reason = "slot";
    }
    return false;
  }

  // 2. 计算升级后的属性用于后续检查
  const auto upgraded = legacy_upgraded_item_config(item_config, user_item);

  // 3. 职业检查
  if (!matches_legacy_job(upgraded.job, character.job)) {
    if (reason != nullptr) {
      *reason = "job";
    }
    return false;
  }

  // 4. 性别检查（含服装的特殊性别逻辑）
  if (!matches_legacy_dress_sex(upgraded, character.sex) ||
      !matches_legacy_sex(upgraded.sex, character.sex)) {
    if (reason != nullptr) {
      *reason = "sex";
    }
    return false;
  }

  // 5. 等级检查（仅当 need==0 且 need_level>0 时检查纯等级需求）
  if (upgraded.need == 0 && upgraded.need_level > 0 &&
      character.ability.level < upgraded.need_level) {
    if (reason != nullptr) {
      *reason = "level";
    }
    return false;
  }

  // 6. 属性需求检查（need=1 表示需要DC, need=2 表示需要MC, need=3 表示需要SC）
  if (upgraded.need >= 1 && upgraded.need <= 3 && upgraded.need_level > 0) {
    const auto current = [&]() {
      switch (upgraded.need) {
        case 1:
          return packed_high(character.ability.dc);
        case 2:
          return packed_high(character.ability.mc);
        case 3:
          return packed_high(character.ability.sc);
        default:
          return 0;
      }
    }();
    if (current < upgraded.need_level) {
      if (reason != nullptr) {
        *reason = "need";
      }
      return false;
    }
  }

  // 7. 重量检查
  const auto new_weight = std::max(upgraded.weight, 0);
  if (legacy_slot_uses_hand_weight(static_cast<std::size_t>(slot))) {
    // 手部重量：武器和右手
    if (new_weight > static_cast<std::int32_t>(character.ability.max_hand_weight)) {
      if (reason != nullptr) {
        *reason = "hand_weight";
      }
      return false;
    }
  } else {
    // 穿戴重量：其他槽位
    if (current_wear_weight - old_slot_weight + new_weight >
             static_cast<std::int32_t>(character.ability.max_wear_weight)) {
      if (reason != nullptr) {
        *reason = "wear_weight";
      }
      return false;
    }
  }
  return true;
}

}  // namespace mir2
