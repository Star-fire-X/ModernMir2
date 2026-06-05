/**
 * @file legacy_gm_commands.hpp
 * @brief GM（游戏管理员）命令系统头文件
 * @details 定义了GM命令的权限等级、命令定义结构和相关查询函数。
 *          支持多级权限体系：普通玩家、观察者、系统操作员、管理员、超级管理员。
 *          命令定义包含标准名称、别名、最低权限要求、实现状态和依赖模块。
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mir2 {

/**
 * @enum LegacyUserDegree
 * @brief 用户权限等级枚举
 * @details 定义GM命令系统的访问权限等级，等级值越大权限越高。
 *          superadmin 拥有最高权限，normal 为普通玩家。
 *          权限比较使用枚举值大小判断：数值越大权限越高。
 */
enum class LegacyUserDegree : std::uint8_t {
  normal,     ///< 普通玩家（最低权限）
  observer,   ///< 观察者（可查看但有限操作）
  sysop,      ///< 系统操作员（可执行多数管理命令）
  admin,      ///< 管理员（可执行高级管理命令）
  superadmin  ///< 超级管理员（最高权限，可执行所有命令）
};

/**
 * @enum LegacyGmCommandImplementation
 * @brief GM命令实现状态枚举
 * @details 标记命令是否已在ModernServer中实现。
 *          用于区分已移植的命令和尚未实现的遗留命令。
 */
enum class LegacyGmCommandImplementation : std::uint8_t {
  pending,     ///< 待实现（尚未移植到ModernServer）
  implemented  ///< 已实现（在ModernServer中可用）
};

/**
 * @struct LegacyGmCommandDefinition
 * @brief GM命令定义结构
 * @details 描述一个GM命令的完整定义，包括命令名称、别名、
 *          所需权限等级、实现状态和依赖的功能模块。
 */
struct LegacyGmCommandDefinition {
  std::string canonical_name{};                                 ///< 标准命令名称
  std::vector<std::string> aliases{};                           ///< 命令别名列表（包含标准名称）
  LegacyUserDegree minimum_degree{LegacyUserDegree::normal};     ///< 执行所需的最低权限
  LegacyGmCommandImplementation implementation{LegacyGmCommandImplementation::pending}; ///< 实现状态
  std::string dependency{};                                     ///< 依赖的功能模块名称
};

/**
 * @brief 获取所有GM命令定义
 * @details 返回一个包含所有支持的命令定义的静态常量引用。
 *          命令分为以下几类：
 *          - 普通命令（normal）：聊天设置、求助等
 *          - 观察者命令（observer）：广播相关
 *          - 系统操作员命令（sysop）：地图传送、玩家查询、会话管理等
 *          - 管理员命令（admin）：战斗调试、怪物生成、玩家状态修改等
 *          - 超级管理员命令（superadmin）：物品生成、金币操作、公会管理等
 * @return 包含所有GM命令定义的常量向量引用
 */
[[nodiscard]] const std::vector<LegacyGmCommandDefinition>&
legacy_gm_command_definitions();

/**
 * @brief 根据命令名称查找对应的命令定义
 * @details 遍历所有命令定义，检查名称和别名是否匹配。
 *          匹配时对ASCII字符不区分大小写，非ASCII字符精确匹配。
 * @param command_name 要查找的命令名称
 * @return 命令定义指针，未找到返回 nullptr
 */
[[nodiscard]] const LegacyGmCommandDefinition* find_legacy_gm_command(
    std::string_view command_name);

/**
 * @brief 从文件加载管理员名单
 * @details 读取管理员列表文件，每行格式为"权限标记+名称"：
 *          - 3: 超级管理员（superadmin）
 *          - *: 管理员（admin）
 *          - 1: 系统操作员（sysop）
 *          - 2: 观察者（observer）
 *          以;开头的行为注释，空行跳过。
 * @param path 管理员列表文件路径
 * @return 名称到权限等级的映射表
 */
[[nodiscard]] std::unordered_map<std::string, LegacyUserDegree> load_legacy_admin_list(
    const std::filesystem::path& path);

/**
 * @brief 根据账号ID启发式判断用户权限
 * @details 当管理员列表中没有匹配项时，使用启发式规则判断：
 *          - 账号为 "guest"、"admin" 或 "gm" 开头的视为管理员
 *          - 其他账号视为普通玩家
 * @param account_id 账号ID
 * @return 推断的用户权限等级
 */
[[nodiscard]] LegacyUserDegree legacy_heuristic_user_degree(std::string_view account_id);

/**
 * @brief 判断实际权限是否达到所需权限
 * @details 比较两个权限等级的枚举值大小。
 *          由于枚举值按权限升序排列（normal < observer < sysop < admin < superadmin），
 *          直接比较枚举值即可。
 * @param actual 用户的实际权限等级
 * @param required 命令要求的最低权限等级
 * @return true 用户有足够权限，false 权限不足
 */
[[nodiscard]] bool legacy_user_degree_at_least(LegacyUserDegree actual,
                                               LegacyUserDegree required);

/**
 * @brief 获取权限等级的字符串名称
 * @param degree 权限等级枚举值
 * @return 权限等级的名称字符串
 */
[[nodiscard]] std::string_view legacy_user_degree_name(LegacyUserDegree degree);

}  // namespace mir2
