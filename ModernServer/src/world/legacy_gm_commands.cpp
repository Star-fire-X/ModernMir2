/**
 * @file legacy_gm_commands.cpp
 * @brief GM（游戏管理员）命令系统实现
 * @details 实现了GM命令定义的注册、查找、管理员名单加载、
 *          用户权限判断等功能。命令定义兼容传奇3原版GM命令，
 *          包含中英文命令名、缩写别名、权限等级和实现状态标记。
 */

#include "world/legacy_gm_commands.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 标准化命令键名：转为小写
 * @param value 原始字符串
 * @return 小写后的字符串
 */
std::string legacy_command_key(std::string_view value) {
  return util::lower_copy(std::string(value));
}

/**
 * @brief 判断字符串是否仅包含ASCII字符
 * @param value 待检查字符串
 * @return true 仅包含ASCII，false 包含非ASCII字符
 */
bool is_ascii(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](const char ch) {
    return static_cast<unsigned char>(ch) < 0x80;
  });
}

/**
 * @brief 不区分大小写比较两个ASCII字符串
 * @details 仅用于ASCII字符的比较，非ASCII字符使用精确比较。
 * @param lhs 左侧字符串
 * @param rhs 右侧字符串
 * @return true 相等（忽略大小写），false 不相等
 */
bool ascii_equals_ci(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 检查别名是否匹配命令名称
 * @details ASCII命令名不区分大小写，非ASCII命令名精确匹配。
 *          这样既支持不区分大小写的英文命令，又支持精确匹配的中文命令。
 * @param command 命令名称
 * @param alias 别名
 * @return true 匹配，false 不匹配
 */
bool alias_matches(std::string_view command, std::string_view alias) {
  if (is_ascii(command) && is_ascii(alias)) {
    return ascii_equals_ci(command, alias);
  }
  return command == alias;
}

/**
 * @brief 创建GM命令定义
 * @details 便捷函数，自动将标准名称加入别名列表。
 * @param canonical_name 标准命令名称
 * @param minimum_degree 所需最低权限
 * @param implementation 实现状态
 * @param dependency 依赖模块
 * @param aliases 别名列表（不含标准名称）
 * @return 构造完成的命令定义
 */
LegacyGmCommandDefinition command(std::string canonical_name,
                                  LegacyUserDegree minimum_degree,
                                  LegacyGmCommandImplementation implementation,
                                  std::string dependency,
                                  std::vector<std::string> aliases = {}) {
  aliases.push_back(canonical_name);
  return LegacyGmCommandDefinition{std::move(canonical_name), std::move(aliases),
                                   minimum_degree, implementation,
                                   std::move(dependency)};
}

}  // namespace

/**
 * @brief 获取所有GM命令定义
 * @details 返回完整的GM命令列表，按权限等级分组：
 *
 *          普通玩家命令：
 *          - 拒绝私聊/允许私聊/拒绝/拒绝喊话/允许喊话 等聊天设置
 *          - H/HELP 帮助命令
 *          - AttackMode/Rest/Searching 等功能命令（待实现）
 *
 *          观察者命令：
 *          - !/$/# 广播命令（已实现）
 *
 *          系统操作员命令：
 *          - Move/PositionMove 传送（已实现）
 *          - Info/Human/MobLevel/MobCount 查询（已实现）
 *          - Map/Kick/Ting/SuperTing 地图与会话管理（已实现）
 *          - Shutup/ReleaseShutup/ShutupList 禁言（已实现）
 *          - GameMaster/Observer/Superman GM模式（已实现）
 *          - Level/SabukWallGold 等级/城堡（已实现）
 *          - Recall 召唤（已实现）
 *          - flag/showopen/showunit 任务（已实现）
 *
 *          管理员命令：
 *          - Attack/EnergyWave/Backstep 战斗（待实现）
 *          - Mob/MobPlace/RecallMob 怪物生成（已实现）
 *          - LuckyPoint/PKpoint/ChangeLuck/Hunger 玩家状态（已实现/待实现）
 *          - Training/DeleteSkill/ChangeJob/ChangeGender 技能与角色（已实现）
 *          - Mission/setflag/setopen/setunit 任务（已实现）
 *          - DeleteItem/Make 物品（已实现）
 *          - 更多...
 *
 *          超级管理员命令：
 *          - Make/DelGold/AddGold 物品与金币（已实现）
 *          - ReloadAdmin/ReloadNpc/ReloadGuild 重载配置（已实现/待实现）
 *          - AdjustLevel/AdjustExp/AdjustTestLevel 等级调整（已实现）
 *          - ChangeSabukLord 城堡管理（已实现）
 *          - OPTraining/OPDeleteSkill 技能管理（已实现）
 *          - 更多...
 *
 * @return 命令定义常量向量的引用
 */
const std::vector<LegacyGmCommandDefinition>& legacy_gm_command_definitions() {
  static const auto definitions = std::vector<LegacyGmCommandDefinition>{
      // === 普通玩家命令 (normal) ===
      command("拒绝私聊", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xCB\xBD\xC1\xC4", 8)}),
      command("允许私聊", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xCB\xBD\xC1\xC4", 8)}),
      command("拒绝", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8", 4)}),
      command("拒绝喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xBA\xB0\xBB\xB0", 8)}),
      command("允许喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xBA\xB0\xBB\xB0", 8)}),
      command("允许行会喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12)}),
      command("拒绝行会喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12)}),
      command("H", LegacyUserDegree::normal, LegacyGmCommandImplementation::pending,
              "help", {"HELP"}),
      command("AttackMode", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::pending, "combat"),
      command("Rest", LegacyUserDegree::normal, LegacyGmCommandImplementation::pending,
              "pet"),
      command("Searching", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::pending, "player_search"),

      // === 观察者命令 (observer) ===
      command("!", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),
      command("$", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),
      command("#", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),

      // === 系统操作员命令 (sysop) ===
      command("Move", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "map"),
      command("PositionMove", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "map", {"PMove"}),
      command("Info", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_query"),
      command("MobLevel", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "monster_query"),
      command("MobCount", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "monster_query"),
      command("Human", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_query"),
      command("Map", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "map"),
      command("Kick", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "session"),
      command("Ting", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "session"),
      command("SuperTing", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "session"),
      command("Shutup", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("ReleaseShutup", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("ShutupList", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("GameMaster", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode"),
      command("Observer", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode", {"Ob"}),
      command("Superman", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode"),
      command("Level", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_state"),
      command("SabukWallGold", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "castle"),
      command("Recall", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "map"),
      command("flag", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "quest"),
      command("showopen", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("showunit", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "quest"),

      // === 管理员命令 (admin) ===
      command("Attack", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "combat"),
      command("Mob", LegacyUserDegree::admin, LegacyGmCommandImplementation::implemented,
              "monster_spawn"),
      command("RecallMob", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "monster_spawn"),
      command("LuckyPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("彩票查询", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "lottery"),
      command("ReloadGuild", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild"),
      command("ReloadGuildAll", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild"),
      command("ReloadLineNotice", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "notice"),
      command("ReadAbuseInformation", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "chat_filter"),
      command("Backstep", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "movement"),
      command("EnergyWave", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "combat"),
      command("FreePenalty", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("PKpoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("IncPkPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("ChangeLuck", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Hunger", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "player_state"),
      command("hair", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Training", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("DeleteSkill", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("ChangeJob", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("ChangeGender", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("NameColor", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Mission", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("MobPlace", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "monster_spawn"),
      command("Transparency", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "gm_mode", {"tp"}),
      command("DeleteItem", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "item"),
      command("Level0", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("setflag", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("setopen", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("setunit", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("Reconnection", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "session"),
      command("DisableFilter", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "chat_filter"),
      command("CHGUSERFULL", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "runtime_config"),
      command("CHGZENFASTSTEP", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "runtime_config"),
      command("ContestPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("StartContest", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("EndContest", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("Announcement", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("OXQuizRoom", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "quiz"),

      // === 超级管理员命令 (superadmin) ===
      command("Make", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "item"),
      command("DelGold", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("AddGold", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("Test_GOLD_Change", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("WeaponRefinery", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "weapon"),
      command("ReloadAdmin", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "admin"),
      command("ReloadNpc", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "npc"),
      command("ReloadMonItems", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "monster_drop"),
      command("ReloadDiary", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "quest"),
      command("AdjustLevel", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("AdjustExp", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("AddGuild", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "guild"),
      command("DelGuild", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "guild"),
      command("ChangeSabukLord", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "castle"),
      command("ForcedWallconquestWar", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "castle"),
      command("AddToItemEvent", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("AddToItemEventAsPieces", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("ItemEventList", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("StartingGiftNo", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("DeleteAllItemEven", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("StartItemEvent", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("ItemEventTerm", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("AdjustTestLevel", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("OPTraining", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("OPDeleteSkill", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("ChangeWeaponDura", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "weapon")};
  return definitions;
}

/**
 * @brief 根据命令名称查找命令定义
 * @details 遍历所有命令定义及其别名，检查是否匹配输入的命令名。
 *          ASCII命令名不区分大小写，确保如 "move"、"Move"、"MOVE" 都能匹配。
 *          非ASCII命令名（如中文）精确匹配。
 * @param command_name 要查找的命令名称
 * @return 匹配的命令定义指针，未找到返回 nullptr
 */
const LegacyGmCommandDefinition* find_legacy_gm_command(std::string_view command_name) {
  for (const auto& definition : legacy_gm_command_definitions()) {
    for (const auto& alias : definition.aliases) {
      if (alias_matches(command_name, alias)) {
        return &definition;
      }
    }
  }
  return nullptr;
}

/**
 * @brief 从文件加载管理员名单
 * @details 文件格式（每行）：
 *          - "3管理员名"：超级管理员
 *          - "*管理员名"：管理员
 *          - "1操作员名"：系统操作员
 *          - "2观察者名"：观察者
 *          - ";注释"或空行：跳过
 *
 *          名称中的权限标记字符（3、星号、1、2）被去除后存入映射表。
 *          所有名称均转为小写存储，便于不区分大小写的查询。
 *
 * @param path 管理员列表文件路径
 * @return 名称到权限等级的映射表
 */
std::unordered_map<std::string, LegacyUserDegree> load_legacy_admin_list(
    const std::filesystem::path& path) {
  std::unordered_map<std::string, LegacyUserDegree> result;
  std::ifstream input(path);
  if (!input.is_open()) {
    return result;
  }

  std::string line;
  while (std::getline(input, line)) {
    line = util::trim(line);
    if (line.empty() || line.front() == ';') {
      continue;
    }
    // 根据首字符判断权限等级
    LegacyUserDegree degree = LegacyUserDegree::normal;
    if (line.front() == '3') {
      degree = LegacyUserDegree::superadmin;
    } else if (line.front() == '*') {
      degree = LegacyUserDegree::admin;
    } else if (line.front() == '1') {
      degree = LegacyUserDegree::sysop;
    } else if (line.front() == '2') {
      degree = LegacyUserDegree::observer;
    } else {
      continue; // 未知标记，跳过
    }
    // 去除权限标记字符，提取名称
    auto name = util::trim(line.substr(1));
    if (!name.empty()) {
      result[legacy_command_key(name)] = degree;
    }
  }
  return result;
}

/**
 * @brief 根据账号ID启发式判断用户权限
 * @details 用于在管理员列表中未找到匹配项时的备用判断方法。
 *          规则：
 *          - "guest"、"admin" 账号视为管理员
 *          - "gm" 开头的账号视为管理员
 *          - 其他账号视为普通玩家
 * @param account_id 账号ID
 * @return 推断的用户权限等级
 */
LegacyUserDegree legacy_heuristic_user_degree(std::string_view account_id) {
  const auto lowered = legacy_command_key(account_id);
  if (lowered == "guest" || lowered == "admin" || util::starts_with(lowered, "gm")) {
    return LegacyUserDegree::admin;
  }
  return LegacyUserDegree::normal;
}

/**
 * @brief 判断实际权限是否达到所需权限
 * @details LegacyUserDegree 枚举值按权限升序排列：
 *          normal(0) < observer(1) < sysop(2) < admin(3) < superadmin(4)
 *          因此直接比较枚举整数值即可。
 * @param actual 用户的实际权限
 * @param required 所需的最低权限
 * @return true 权限足够，false 权限不足
 */
bool legacy_user_degree_at_least(LegacyUserDegree actual, LegacyUserDegree required) {
  return static_cast<std::uint8_t>(actual) >= static_cast<std::uint8_t>(required);
}

/**
 * @brief 获取权限等级的字符串名称
 * @param degree 权限等级枚举值
 * @return 对应的权限名称字符串，未知值返回 "normal"
 */
std::string_view legacy_user_degree_name(LegacyUserDegree degree) {
  switch (degree) {
    case LegacyUserDegree::normal:
      return "normal";
    case LegacyUserDegree::observer:
      return "observer";
    case LegacyUserDegree::sysop:
      return "sysop";
    case LegacyUserDegree::admin:
      return "admin";
    case LegacyUserDegree::superadmin:
      return "superadmin";
  }
  return "normal";
}

}  // namespace mir2
