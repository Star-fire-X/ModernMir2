/**
 * @file legacy_character_importer.hpp
 * @brief 遗留角色数据导入器声明
 * @details 定义从旧版游戏数据库格式（Hum.DB / Mir.DB）导入角色数据的接口。
 *          支持从旧版二进制数据库文件中读取角色信息，转换为新系统的 CharacterRecord 格式。
 * @author mir2 Team
 * @date 2026-06-04
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "config/models.hpp"
#include "storage/repository.hpp"

namespace mir2 {

/**
 * @struct LegacyCharacterImportOptions
 * @brief 遗留角色导入选项结构体
 * @details 配置角色导入过程所需的参数，包括旧版数据库文件路径、
 *          默认密码和主机配置。主机配置用于验证导入的角色数据
 *          （如地图、物品、魔法等是否在服务端有效范围内）。
 */
struct LegacyCharacterImportOptions {
  std::filesystem::path hum_db_path{};      ///< Hum.DB 文件路径（包含角色-账户映射关系）
  std::filesystem::path mir_db_path{};      ///< Mir.DB 文件路径（包含完整的角色数据）
  std::string default_password{"pass"};      ///< 为新创建的导入账户设置的默认密码
  const HostConfig* config{nullptr};         ///< 主机配置指针（用于数据验证，可为nullptr）
};

/**
 * @struct LegacyCharacterImportWarning
 * @brief 导入警告结构体
 * @details 记录导入过程中检测到的数据异常，包括角色名、警告类型和具体值。
 *          例如：未知物品ID、未知魔法ID、未知地图等。
 *          这些警告不影响导入本身，但帮助管理员识别遗留数据中的问题。
 */
struct LegacyCharacterImportWarning {
  std::string character_name{};  ///< 触发警告的角色名
  std::string kind{};            ///< 警告类型（如 unknown_item、unknown_magic、unknown_map）
  std::string value{};           ///< 触发警告的具体值
};

/**
 * @struct LegacyCharacterImportReport
 * @brief 导入结果报告结构体
 * @details 汇总导入过程的统计数据，包括扫描数量、成功导入数量、
 *          跳过数量、失败数量以及所有警告信息。
 *          用于在导入完成后向管理员报告执行结果。
 */
struct LegacyCharacterImportReport {
  std::size_t scanned{0};                              ///< 扫描的总记录数
  std::size_t imported{0};                             ///< 成功导入的记录数
  std::size_t skipped{0};                              ///< 跳过（重复）的记录数
  std::size_t failed{0};                               ///< 失败的记录数
  std::vector<LegacyCharacterImportWarning> warnings{}; ///< 导入警告列表
};

/**
 * @class LegacyCharacterImporter
 * @brief 遗留角色数据导入器
 * @details 负责从旧版的 Hum.DB 和 Mir.DB 二进制数据库文件（基于 BDE/Borland Database Engine）
 *          中读取角色数据，解析二进制格式，转换为新的 CharacterRecord 结构，
 *          并通过 Repository 写入新系统的 SQLite 数据库。
 *
 *          主要功能：
 *          - 读取并解析 Hum.DB 中的角色-账户映射
 *          - 读取并解析 Mir.DB 中的完整角色数据
 *          - 数据验证（检查物品、魔法、地图的有效性）
 *          - 自动创建缺失的账户
 *          - 检测并跳过重复角色
 *          - 记录导入审计日志
 *
 * @note 旧版数据库格式为定长记录的二进制文件，每条记录长度固定（MirRecord 4937 字节，
 *        HumRecord 72 字节），文件头 124 字节。
 */
class LegacyCharacterImporter {
 public:
  /**
   * @brief 执行角色导入
   * @param options 导入选项（包含旧版数据库文件路径和配置）
   * @param repository 数据仓库引用（用于写入导入的数据和记录日志）
   * @return LegacyCharacterImportReport 导入结果报告
   * @throws std::runtime_error 如果 Mir.DB 路径为空或文件太小/无法打开
   * @details 导入流程：
   *          1. 读取 Hum.DB 建立角色名到账户ID的映射
   *          2. 读取 Mir.DB 文件头获取记录数
   *          3. 逐条解析遗留记录并转换为 CharacterRecord
   *          4. 验证数据的合法性（地图、物品、魔法）
   *          5. 检查重复角色
   *          6. 自动创建缺失的账户
   *          7. 通过 Repository 写入新系统数据库
   *          8. 记录每条记录的导入状态
   * @note 如果 Hum.DB 不存在或为空，导入器会尝试从 Mir.DB 中的 user_id 字段
   *       提取账户ID，如果仍然为空则生成 "legacy_角色名" 格式的账户ID。
   */
  [[nodiscard]] LegacyCharacterImportReport import_characters(
      const LegacyCharacterImportOptions& options, Repository& repository) const;
};

}  // namespace mir2
