/**
 * @file legacy_character_importer.cpp
 * @brief 遗留角色数据导入器实现
 * @details 实现从旧版 Hum.DB 和 Mir.DB 二进制数据库文件中读取角色数据，
 *          解析二进制格式，转换为新的 CharacterRecord 结构并通过 Repository
 *          写入新系统 SQLite 数据库的完整流程。
 * @author mir2 Team
 * @date 2026-06-04
 */

#include "importer/legacy_character_importer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace mir2 {

namespace {

/** @brief 旧版数据库文件头大小（字节数） */
constexpr std::size_t kLegacyDbHeaderSize = 124;
/** @brief 旧版 Mir.DB 中每条角色记录的固定大小（字节数） */
constexpr std::size_t kLegacyMirRecordSize = 4937;
/** @brief 旧版 Hum.DB 中每条映射记录的固定大小（字节数） */
constexpr std::size_t kLegacyHumRecordSize = 72;

/**
 * @brief 启用 1 字节内存对齐，确保与旧版二进制数据库布局完全一致
 *
 * 以下所有结构体均使用 #pragma pack(push, 1) 强制 1 字节对齐，
 * 以保证 memcpy 直接从二进制文件中读取时字段位置与旧版 BDE 数据库格式完全匹配。
 */
#pragma pack(push, 1)

/**
 * @struct LegacyMirHuman
 * @brief 旧版 Mir.DB 中的角色基本属性结构（393 字节）
 * @details 对应旧版传奇服务端 THuman 记录，包含角色的位置、外观、属性、
 *          任务状态等基本信息。该结构是 MirRecord 的核心组成部分。
 * @note 字段排列顺序和大小必须与旧版 BDE 数据库完全一致，不可随意调整。
 */
struct LegacyMirHuman {
  std::array<char, 20> user_name{};       ///< 角色名（最大 20 字符）
  std::array<char, 20> map_name{};        ///< 当前所在地图ID（最大 20 字符）
  std::uint16_t x{0};                     ///< 当前 X 坐标
  std::uint16_t y{0};                     ///< 当前 Y 坐标
  std::uint8_t dir{0};                    ///< 当前面向方向（0-7，对应 8 方向）
  std::uint8_t hair{0};                   ///< 发型编号
  std::uint8_t hair_color_r{0};           ///< 发色红色分量
  std::uint8_t hair_color_g{0};           ///< 发色绿色分量
  std::uint8_t hair_color_b{0};           ///< 发色蓝色分量
  std::uint8_t sex{0};                    ///< 性别（0=男，1=女）
  std::uint8_t job{0};                    ///< 职业（0=战士，1=法师，2=道士）
  std::int32_t gold{0};                   ///< 当前金币数量
  std::uint8_t level{1};                  ///< 当前等级（从 1 开始）
  std::uint16_t hp{15};                   ///< 当前生命值
  std::uint16_t mp{15};                   ///< 当前魔法值
  std::uint32_t exp{0};                   ///< 当前经验值
  std::array<std::uint16_t, 12> status_arr{};  ///< 角色状态数组（如攻击、魔法、道术等基础属性）
  std::array<char, 20> home_map{};        ///< 绑定回城点地图ID
  std::uint16_t home_x{0};                ///< 绑定回城点 X 坐标
  std::uint16_t home_y{0};                ///< 绑定回城点 Y 坐标
  std::int32_t pk_point{0};               ///< PK 值（红名惩罚点数）
  std::uint8_t allow_party{0};            ///< 是否允许组队（0=不允许，1=允许）
  std::uint8_t free_gulity_count{0};      ///< 免费洗红名次数（旧版用于清除 PK 值）
  std::uint8_t attack_mode{0};            ///< 攻击模式（0=全体攻击，1=和平，2=行会，3=组队，4=善恶）
  std::uint8_t inc_health{0};             ///< 体力增强点数
  std::uint8_t inc_spell{0};              ///< 魔力增强点数
  std::uint8_t inc_healing{0};            ///< 治愈术增强点数
  std::uint8_t fight_zone_die{0};         ///< 战斗区域死亡标记
  std::array<char, 20> user_id{};         ///< 账户ID（旧版直接存储在角色记录中）
  std::uint8_t db_version{0};             ///< 数据库版本标识
  std::uint8_t bonus_apply{0};            ///< 奖励点数是否已应用标记
  std::int32_t bonus_point{0};            ///< 可分配的奖励属性点数
  std::uint32_t daily_quest{0};           ///< 每日任务数据
  std::uint8_t horse_ride{0};             ///< 骑马状态标记
  std::uint16_t cghi_use_time{0};         ///< CGHI 道具使用时间戳
  double body_luck{0};                    ///< 角色幸运值（影响爆率和强化概率等）
  std::uint8_t enable_g_recall{0};        ///< 是否允许行会召回
  std::array<std::uint8_t, 3> bytes_1{};  ///< 填充字节（用于对齐）
  std::array<std::uint8_t, 24> quest_open_index{};  ///< 已接任务索引数组
  std::array<std::uint8_t, 24> quest_fin_index{};   ///< 已完成任务索引数组
  std::array<std::uint8_t, 176> quest{};  ///< 任务标记数据（位图形式存储任务状态）
  std::uint8_t horse_race{0};             ///< 赛马数据标记
};

/**
 * @struct LegacyMirBagItem
 * @brief 旧版 Mir.DB 中的角色背包与装备结构（2360 字节）
 * @details 包含角色当前穿戴的所有装备以及背包中的物品列表。
 *          装备槽位固定 13 个（衣服、武器、右手、头盔、项链、左右手镯、
 *          左右戒指、护身符、腰带、靴子、宝石），背包 40 格。
 * @see LegacyUserItem LegacyUseMagicInfo
 */
struct LegacyMirBagItem {
  LegacyUserItem dress{};              ///< 衣服装备
  LegacyUserItem weapon{};             ///< 武器装备
  LegacyUserItem right_hand{};         ///< 右手装备（蜡烛或特殊物品）
  LegacyUserItem helmet{};             ///< 头盔装备
  LegacyUserItem necklace{};           ///< 项链装备
  LegacyUserItem arm_ring_left{};      ///< 左手镯装备
  LegacyUserItem arm_ring_right{};     ///< 右手镯装备
  LegacyUserItem ring_left{};          ///< 左戒指装备
  LegacyUserItem ring_right{};         ///< 右戒指装备
  LegacyUserItem bujuk{};              ///< 护身符装备
  LegacyUserItem belt{};               ///< 腰带装备
  LegacyUserItem boots{};              ///< 靴子装备
  LegacyUserItem charm{};              ///< 宝石装备
  std::array<LegacyUserItem, kMaxBagItems> bags{};  ///< 背包物品数组（默认 40 格）
};

/**
 * @struct LegacyMirUseMagic
 * @brief 旧版 Mir.DB 中的已学习魔法结构
 * @details 存储角色已学习的所有魔法/技能信息，包括魔法ID、等级和修炼经验值。
 * @note kMaxUserMagic 定义了最大可学习的魔法数量。
 */
struct LegacyMirUseMagic {
  std::array<LegacyUseMagicInfo, kMaxUserMagic> magics{};  ///< 已学习的魔法列表
};

/**
 * @struct LegacyMirSaveItem
 * @brief 旧版 Mir.DB 中的仓库物品结构
 * @details 存储角色存放在仓库中的所有物品。
 * @note kMaxSaveItems 定义了仓库的最大容量。
 */
struct LegacyMirSaveItem {
  std::array<LegacyUserItem, kMaxSaveItems> items{};  ///< 仓库中的物品列表
};

/**
 * @struct LegacyMirBlockData
 * @brief 旧版 Mir.DB 中的角色数据块聚合结构
 * @details 将角色基本属性、装备背包、魔法技能、仓库物品四部分数据
 *          聚合为一个整体，对应旧版数据库中的完整角色数据块。
 */
struct LegacyMirBlockData {
  LegacyMirHuman human{};              ///< 角色基本属性
  LegacyMirBagItem bag_item{};         ///< 装备与背包物品
  LegacyMirUseMagic use_magic{};       ///< 已学魔法技能
  LegacyMirSaveItem save_item{};       ///< 仓库物品
};

/**
 * @struct LegacyMirRecord
 * @brief 旧版 Mir.DB 中的完整角色记录结构（4937 字节）
 * @details 对应旧版 BDE 数据库中 Mir.DB 表的每条记录，包含删除标记、
 *          更新时间戳、主键和完整的角色数据块。
 * @note 记录总大小必须等于 kLegacyMirRecordSize（4937 字节）。
 */
struct LegacyMirRecord {
  std::uint8_t deleted{0};             ///< 删除标记（0=有效，非0=已删除）
  double update_date_time{0};          ///< 记录最后更新时间（OLE Automation 日期格式）
  std::array<char, 15> key{};          ///< 记录主键（通常为角色名的 BDE 编码形式）
  LegacyMirBlockData block{};          ///< 完整的角色数据块
};

/**
 * @struct LegacyHumRecord
 * @brief 旧版 Hum.DB 中的角色-账户映射记录结构（72 字节）
 * @details 对应旧版 BDE 数据库中 Hum.DB 表的每条记录，建立角色名与账户ID
 *          之间的映射关系。用于在 Mir.DB 中 user_id 字段为空时补充账户信息。
 * @note 记录总大小必须等于 kLegacyHumRecordSize（72 字节）。
 */
struct LegacyHumRecord {
  std::uint8_t deleted{0};             ///< 删除标记（0=有效，非0=已删除）
  std::array<std::uint8_t, 3> pad{};   ///< 填充字节
  double update_date_time{0};          ///< 记录最后更新时间（OLE Automation 日期格式）
  std::array<char, 15> key{};          ///< 记录主键
  std::uint8_t pad_after_key{};        ///< 主键后的填充字节
  std::array<std::uint8_t, 44> block{}; ///< 映射数据块（包含角色名和账户ID的短字符串编码）
};

#pragma pack(pop)

/* 编译期断言：确保结构体大小与旧版数据库格式完全一致 */
static_assert(sizeof(LegacyMirHuman) == 393);
static_assert(sizeof(LegacyMirBagItem) == 2360);
static_assert(sizeof(LegacyMirRecord) == kLegacyMirRecordSize);
static_assert(sizeof(LegacyHumRecord) == kLegacyHumRecordSize);

/**
 * @brief 以二进制方式读取整个文件内容到字节向量
 * @param path 文件路径
 * @return std::vector<std::uint8_t> 文件内容的字节数组
 * @throws std::runtime_error 如果文件无法打开
 */
std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open legacy DB: " + path.string());
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
}

/**
 * @brief 从字节数组的指定偏移量读取一个小端序 32 位整数
 * @param bytes 源字节数组
 * @param offset 读取起始偏移量
 * @return std::int32_t 读取到的整数值，如果偏移量超出范围则返回 0
 * @details 旧版数据库文件头中的记录数量等信息以小端序 Int32 格式存储。
 */
std::int32_t read_le_i32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + sizeof(std::int32_t) > bytes.size()) {
    return 0;
  }
  std::int32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

/**
 * @brief 去除字符串末尾的空字符（'\\0'）以及前后控制字符/空白符
 * @param text 原始字符串
 * @return std::string 清理后的字符串
 * @details 旧版数据库使用定长字符数组存储字符串，未使用的字节以 '\\0' 填充。
 *          本函数移除尾部的 '\\0'，同时去除前后不可见字符（ASCII <= 0x20）。
 */
std::string trim_nul(std::string text) {
  const auto nul = text.find('\0');
  if (nul != std::string::npos) {
    text.resize(nul);
  }
  while (!text.empty() && static_cast<unsigned char>(text.back()) <= 0x20) {
    text.pop_back();
  }
  const auto first = std::find_if(text.begin(), text.end(), [](unsigned char ch) {
    return ch > 0x20;
  });
  text.erase(text.begin(), first);
  return text;
}

/**
 * @brief 将定长字符数组转换为字符串并清理尾部空字符
 * @tparam N 数组大小
 * @param value 定长字符数组
 * @return std::string 清理后的字符串
 * @details 模板函数，支持任意大小的固定长度字符数组到字符串的转换。
 */
template <std::size_t N>
std::string char_array_to_string(const std::array<char, N>& value) {
  return trim_nul(std::string(value.data(), value.data() + value.size()));
}

/**
 * @brief 解析旧版数据库中的短字符串编码格式
 * @param data 编码数据的起始指针
 * @param capacity 缓冲区总容量
 * @return std::string 解码后的字符串
 * @details 旧版数据库使用长度前缀编码存储短字符串：
 *          第一个字节表示字符串长度，后续字节为字符串内容。
 *          例如：{5, 'H', 'e', 'l', 'l', 'o'} 解码为 "Hello"。
 */
std::string short_string_to_string(const std::uint8_t* data, std::size_t capacity) {
  if (capacity == 0) {
    return {};
  }
  const auto length = std::min<std::size_t>(data[0], capacity - 1);
  return trim_nul(std::string(reinterpret_cast<const char*>(data + 1),
                              reinterpret_cast<const char*>(data + 1 + length)));
}

/**
 * @brief 读取 Hum.DB 文件，建立角色名到账户ID的映射表
 * @param hum_db_path Hum.DB 文件路径
 * @return std::unordered_map<std::string, std::string> 角色名到账户ID的映射表
 * @details 遍历 Hum.DB 中的所有有效记录，解析每条记录的 block 数据块，
 *          提取角色名（第 0-15 字节）和账户ID（第 15-26 字节），
 *          两者使用短字符串编码格式存储。
 * @note 如果 Hum.DB 文件不存在、为空或无法打开，返回空映射表而非报错。
 */
std::unordered_map<std::string, std::string> read_hum_accounts(
    const std::filesystem::path& hum_db_path) {
  std::unordered_map<std::string, std::string> accounts;
  if (hum_db_path.empty() || !std::filesystem::exists(hum_db_path)) {
    return accounts;
  }

  const auto bytes = read_binary_file(hum_db_path);
  if (bytes.size() < kLegacyDbHeaderSize) {
    return accounts;
  }
  /* 从文件头偏移 104 处读取记录总数（小端序 Int32） */
  const auto max_count = std::max(read_le_i32(bytes, 104), 0);
  for (std::int32_t index = 0; index < max_count; ++index) {
    const auto offset = kLegacyDbHeaderSize + static_cast<std::size_t>(index) * kLegacyHumRecordSize;
    if (offset + kLegacyHumRecordSize > bytes.size()) {
      break;
    }
    LegacyHumRecord record;
    std::memcpy(&record, bytes.data() + offset, sizeof(record));
    if (record.deleted != 0) {
      continue;
    }
    /* 解析 block 数据块：前 15 字节为角色名，接下来 11 字节为账户ID */
    const auto* block = record.block.data();
    const auto character_name = short_string_to_string(block, 15);
    const auto account_id = short_string_to_string(block + 15, 11);
    if (!character_name.empty() && !account_id.empty()) {
      accounts[character_name] = account_id;
    }
  }
  return accounts;
}

/**
 * @brief 检测指定地图ID是否在主机配置中定义
 * @param config 主机配置指针（可为 nullptr）
 * @param map_id 要检测的地图ID
 * @return true 如果地图在配置中存在，或 config 为 nullptr
 * @details 当 config 为 nullptr 时跳过所有验证（兼容模式）。
 */
bool contains_map(const HostConfig* config, const std::string& map_id) {
  if (config == nullptr) {
    return true;
  }
  return std::any_of(config->maps.begin(), config->maps.end(), [&](const MapConfig& map) {
    return map.id == map_id;
  });
}

/**
 * @brief 从主机配置中提取所有已知物品ID的集合
 * @param config 主机配置指针（可为 nullptr）
 * @return std::set<std::int32_t> 已知物品ID集合，config 为 nullptr 时返回空集
 * @details 用于后续验证角色装备/背包中的物品是否在服务端配置范围内。
 */
std::set<std::int32_t> item_ids(const HostConfig* config) {
  std::set<std::int32_t> ids;
  if (config == nullptr) {
    return ids;
  }
  for (const auto& item : config->items) {
    ids.insert(item.id);
  }
  return ids;
}

/**
 * @brief 从主机配置中提取所有已知魔法ID的集合
 * @param config 主机配置指针（可为 nullptr）
 * @return std::set<std::int32_t> 已知魔法ID集合，config 为 nullptr 时返回空集
 * @details 用于后续验证角色已学魔法是否在服务端配置范围内。
 */
std::set<std::int32_t> magic_ids(const HostConfig* config) {
  std::set<std::int32_t> ids;
  if (config == nullptr) {
    return ids;
  }
  for (const auto& magic : config->magics) {
    ids.insert(magic.id);
  }
  return ids;
}

/**
 * @brief 向导入报告中添加一条警告记录
 * @param report 导入报告引用
 * @param character_name 触发警告的角色名
 * @param kind 警告类型标识
 * @param value 警告相关值
 */
void push_warning(LegacyCharacterImportReport& report, std::string character_name,
                  std::string kind, std::string value) {
  report.warnings.push_back(
      LegacyCharacterImportWarning{std::move(character_name), std::move(kind), std::move(value)});
}

/**
 * @brief 验证单个物品是否在已知列表中存在
 * @param item 待验证的物品
 * @param known_items 已知物品ID集合
 * @param character_name 所属角色名（用于记录警告）
 * @param report 导入报告引用（用于添加警告）
 * @details 如果物品索引为 0 或已知物品列表为空则跳过验证。
 */
void validate_item(const LegacyUserItem& item, const std::set<std::int32_t>& known_items,
                   const std::string& character_name, LegacyCharacterImportReport& report) {
  if (item.index == 0 || known_items.empty()) {
    return;
  }
  if (!known_items.contains(item.index)) {
    push_warning(report, character_name, "unknown_item", std::to_string(item.index));
  }
}

/**
 * @brief 验证单个魔法是否在已知列表中存在
 * @param magic 待验证的魔法信息
 * @param known_magics 已知魔法ID集合
 * @param character_name 所属角色名（用于记录警告）
 * @param report 导入报告引用（用于添加警告）
 * @details 如果魔法ID为 0 或已知魔法列表为空则跳过验证。
 */
void validate_magic(const LegacyUseMagicInfo& magic, const std::set<std::int32_t>& known_magics,
                    const std::string& character_name, LegacyCharacterImportReport& report) {
  if (magic.magic_id == 0 || known_magics.empty()) {
    return;
  }
  if (!known_magics.contains(magic.magic_id)) {
    push_warning(report, character_name, "unknown_magic", std::to_string(magic.magic_id));
  }
}

/**
 * @brief 将旧版 MirRecord 转换为新系统的 CharacterRecord
 * @param record 旧版角色记录
 * @param hum_accounts Hum.DB 中读取的角色-账户映射表
 * @return CharacterRecord 转换后的新系统角色记录
 * @details 转换逻辑：
 *          1. 从 LegacyMirHuman 中提取基本属性
 *          2. 按优先级获取账户ID：user_id 字段 > Hum.DB 映射 > 生成 "legacy_" 前缀ID
 *          3. 映射装备槽位（13 个固定装备位）
 *          4. 复制背包物品、仓库物品和魔法列表
 *          5. 复制任务标记数据
 * @note 当角色名为空时，使用记录的 key 字段作为备选角色名。
 *       攻击模式被限制在 0-4 范围内以保证兼容性。
 */
CharacterRecord to_character_record(const LegacyMirRecord& record,
                                    const std::unordered_map<std::string, std::string>& hum_accounts) {
  CharacterRecord character;
  const auto& human = record.block.human;
  character.character_name = char_array_to_string(human.user_name);
  if (character.character_name.empty()) {
    character.character_name = char_array_to_string(record.key);
  }
  character.account_id = char_array_to_string(human.user_id);
  if (character.account_id.empty()) {
    if (const auto it = hum_accounts.find(character.character_name); it != hum_accounts.end()) {
      character.account_id = it->second;
    }
  }
  if (character.account_id.empty() && !character.character_name.empty()) {
    character.account_id = "legacy_" + character.character_name;
  }

  character.map_id = char_array_to_string(human.map_name);
  if (character.map_id.empty()) {
    character.map_id = "0";
  }
  character.x = human.x;
  character.y = human.y;
  character.dir = human.dir;
  character.hair = human.hair;
  character.sex = human.sex;
  character.job = human.job;
  character.gold = human.gold;
  /* 限制攻击模式范围为 0-4，防止无效值 */
  character.attack_mode = static_cast<std::uint8_t>(std::clamp<std::int32_t>(human.attack_mode, 0, 4));
  character.pk_point = human.pk_point;
  character.daily_quest = human.daily_quest;
  character.body_luck = human.body_luck;
  character.ability.level = std::max<std::uint8_t>(human.level, 1);
  character.ability.hp = human.hp;
  character.ability.mp = human.mp;
  character.ability.max_hp = std::max<std::uint16_t>(human.hp, 15);
  character.ability.max_mp = std::max<std::uint16_t>(human.mp, 15);
  character.ability.exp = human.exp;
  character.ability.max_exp = std::max<std::uint32_t>(character.ability.max_exp, 100);
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;

  /* 映射 13 个固定装备槽位 */
  const auto& bag = record.block.bag_item;
  character.equipped_items[kEquipDress] = bag.dress;
  character.equipped_items[kEquipWeapon] = bag.weapon;
  character.equipped_items[kEquipRightHand] = bag.right_hand;
  character.equipped_items[kEquipNecklace] = bag.necklace;
  character.equipped_items[kEquipHelmet] = bag.helmet;
  character.equipped_items[kEquipArmRingLeft] = bag.arm_ring_left;
  character.equipped_items[kEquipArmRingRight] = bag.arm_ring_right;
  character.equipped_items[kEquipRingLeft] = bag.ring_left;
  character.equipped_items[kEquipRingRight] = bag.ring_right;
  character.equipped_items[kEquipBujuk] = bag.bujuk;
  character.equipped_items[kEquipBelt] = bag.belt;
  character.equipped_items[kEquipBoots] = bag.boots;
  character.equipped_items[kEquipCharm] = bag.charm;
  character.bag_items = bag.bags;
  character.storage_items = record.block.save_item.items;
  character.magics = record.block.use_magic.magics;
  /* 复制任务标记数据 */
  std::copy(human.quest.begin(), human.quest.end(), character.quest_marks.begin());
  std::copy(human.quest_open_index.begin(), human.quest_open_index.end(),
            character.quest_open_units.begin());
  std::copy(human.quest_fin_index.begin(), human.quest_fin_index.end(),
            character.quest_units.begin());
  return character;
}

/**
 * @brief 为导入的角色创建对应的账户记录
 * @param account_id 账户ID
 * @param password 账户密码
 * @return AccountRecord 新系统账户记录
 * @details 为新导入的角色自动创建账户，使用默认的"legacy"安全问题及答案。
 *          生日统一设置为 1970/01/01 作为占位值。
 */
AccountRecord make_import_account(const std::string& account_id, const std::string& password) {
  AccountRecord account;
  account.account_id = account_id;
  account.password = password;
  account.display_name = account_id;
  account.user_name = account_id;
  account.birthday = "1970/01/01";
  account.quiz = "legacy";
  account.answer = "legacy";
  account.quiz2 = "legacy";
  account.answer2 = "legacy";
  return account;
}

/**
 * @brief 记录单条角色的导入审计日志
 * @param repository 数据仓库引用
 * @param source_file 源数据库文件路径
 * @param record_index 在源文件中的记录索引
 * @param character 已转换的角色记录
 * @param status 导入状态（"imported"/"skipped"/"failed"）
 * @param message 状态描述信息
 * @param raw_record 原始的二进制记录数据（用于归档）
 * @details 每条角色的导入过程都会生成一条审计记录，包含源文件路径、
 *          记录索引、导入状态和原始二进制数据，便于后续追溯和调试。
 */
void record_import(Repository& repository, const std::filesystem::path& source_file,
                   std::int32_t record_index, const CharacterRecord& character,
                   std::string status, std::string message,
                   const std::vector<std::uint8_t>& raw_record) {
  LegacyImportRecord import_record;
  import_record.source_file = source_file.string();
  import_record.record_index = record_index;
  import_record.account_id = character.account_id;
  import_record.character_name = character.character_name;
  import_record.status = std::move(status);
  import_record.message = std::move(message);
  import_record.raw_record = raw_record;
  repository.record_legacy_import(import_record);
}

}  // namespace

LegacyCharacterImportReport LegacyCharacterImporter::import_characters(
    const LegacyCharacterImportOptions& options, Repository& repository) const {
  if (options.mir_db_path.empty()) {
    throw std::runtime_error("legacy Mir.DB path is required for character import.");
  }

  LegacyCharacterImportReport report;
  /* 加载 Hum.DB 中的角色-账户映射、已知物品和魔法ID集合 */
  const auto hum_accounts = read_hum_accounts(options.hum_db_path);
  const auto known_items = item_ids(options.config);
  const auto known_magics = magic_ids(options.config);
  const auto bytes = read_binary_file(options.mir_db_path);
  if (bytes.size() < kLegacyDbHeaderSize) {
    throw std::runtime_error("legacy Mir.DB is too small: " + options.mir_db_path.string());
  }

  /* 从文件头偏移 104 处读取记录总数，逐条处理 */
  const auto max_count = std::max(read_le_i32(bytes, 104), 0);
  for (std::int32_t index = 0; index < max_count; ++index) {
    const auto offset = kLegacyDbHeaderSize + static_cast<std::size_t>(index) * kLegacyMirRecordSize;
    if (offset + kLegacyMirRecordSize > bytes.size()) {
      ++report.failed;
      break;
    }
    ++report.scanned;

    /* 保存原始二进制数据用于审计日志 */
    std::vector<std::uint8_t> raw(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + kLegacyMirRecordSize));
    LegacyMirRecord record;
    std::memcpy(&record, raw.data(), sizeof(record));

    CharacterRecord character = to_character_record(record, hum_accounts);
    /* 跳过已删除或缺少关键标识的记录 */
    if (record.deleted != 0 || character.character_name.empty() || character.account_id.empty()) {
      ++report.failed;
      record_import(repository, options.mir_db_path, index, character, "failed",
                    "deleted_or_missing_identity", raw);
      continue;
    }

    /* 验证数据合法性 */
    if (!contains_map(options.config, character.map_id)) {
      push_warning(report, character.character_name, "unknown_map", character.map_id);
    }
    for (const auto& item : character.equipped_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& item : character.bag_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& item : character.storage_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& magic : character.magics) {
      validate_magic(magic, known_magics, character.character_name, report);
    }

    /* 检查重复角色 */
    const auto existing_characters = repository.list_characters(character.account_id);
    const auto duplicate = std::any_of(
        existing_characters.begin(), existing_characters.end(), [&](const CharacterRecord& existing) {
          return existing.character_name == character.character_name;
        });
    if (duplicate) {
      ++report.skipped;
      record_import(repository, options.mir_db_path, index, character, "skipped",
                    "duplicate_character", raw);
      continue;
    }

    /* 自动创建缺失的账户 */
    if (!repository.load_account(character.account_id).has_value()) {
      static_cast<void>(
          repository.create_account(make_import_account(character.account_id, options.default_password)));
    }
    /* 写入新系统的角色记录 */
    if (!repository.create_character(character)) {
      ++report.failed;
      record_import(repository, options.mir_db_path, index, character, "failed",
                    "create_character_failed", raw);
      continue;
    }

    ++report.imported;
    record_import(repository, options.mir_db_path, index, character, "imported", {}, raw);
  }

  return report;
}

}  // namespace mir2
