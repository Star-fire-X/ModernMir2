/**
 * @file legacy_importer.hpp
 * @brief 遗留游戏资源导入器声明
 * @details 定义从旧版传奇游戏服务端资源文件（地图、刷怪、NPC、物品、魔法等）
 *          导入并转换为新系统 TOML 配置格式的接口。
 * @author mir2 Team
 * @date 2026-06-04
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mir2 {

/**
 * @struct LegacyImportReport
 * @brief 资源导入结果报告结构体
 * @details 汇总导入过程中的各项资源计数器以及警告信息。
 *          涵盖地图、刷怪、怪物、怪物掉落、NPC、地图任务、物品、魔法等类别。
 *          用于在导入完成后向管理员展示全面的导入统计。
 */
struct LegacyImportReport {
  std::size_t map_count{0};                   ///< 导入的地图数量
  std::size_t spawn_count{0};                 ///< 导入的刷怪配置数量
  std::size_t monster_count{0};               ///< 导入的怪物定义数量
  std::size_t monster_drop_count{0};          ///< 导入的怪物掉落配置数量
  std::size_t npc_count{0};                   ///< 导入的NPC数量
  std::size_t map_quest_count{0};             ///< 导入的地图任务数量
  std::size_t item_count{0};                  ///< 导入的物品数量
  std::size_t magic_count{0};                 ///< 导入的魔法数量
  std::vector<std::string> warnings{};        ///< 导入过程中的警告信息列表
};

/**
 * @class LegacyImporter
 * @brief 遗留游戏资源导入器
 * @details 负责从旧版传奇服务端目录结构中解析各类资源文件（TXT 格式的 INI
 *          配置文件、MapInfo.txt、MonGen.txt、Merchant.txt、Npcs.txt、MapQuest.txt、
 *          以及从 Access 数据库通过 ODBC 导入的物品和怪物数据），
 *          转换为新系统标准化的 TOML 配置文件格式。
 *
 *          支持的导入类型：
 *          - 实例化地图信息（MapInfo.txt -> maps/*.toml）
 *          - 刷怪点（MonGen.txt -> spawns/*.toml）
 *          - 怪物定义（Data.mdb / MonItems 目录 -> monsters/*.toml）
 *          - 怪物掉落（MonItems/*.txt -> monsters/*.toml）
 *          - NPC 定义（Merchant.txt / Npcs.txt -> npcs/*.toml）
 *          - NPC 脚本（market_def / Npc_def -> npc_scripts/*）
 *          - 地图任务（MapQuest.txt -> map_quests/*.toml）
 *          - 物品定义（Data.mdb / MakeItem.txt -> items/*.toml）
 *          - 魔法定义（Data.mdb -> magic/*.toml）
 *          - 服务器配置（!SetUp.txt -> server.toml, ports.toml, runtime/logic.toml）
 *
 * @note 本导入器支持两种数据源：
 *       1. ODBC 驱动从 Access 数据库（.mdb）读取（需编译时启用 MIR2_ENABLE_ODBC）
 *       2. 纯文本文件解析作为后备方案
 */
class LegacyImporter {
 public:
  /**
   * @brief 执行完整的遗留资源导入
   * @param legacy_root 旧版服务端根目录路径
   * @param output_root 新系统输出根目录路径
   * @return LegacyImportReport 导入结果报告（含各资源计数和警告）
   * @details 导入流程：
   *          1. 创建输出目录结构
   *          2. 解析 !SetUp.txt 获取服务器配置
   *          3. 写入 server.toml、ports.toml、runtime/logic.toml 配置
   *          4. 复制 NPC 脚本目录（Defines、QuestDiary）
   *          5. 导入 NPC、地图任务、物品
   *          6. 如果启用 ODBC，从 Data.mdb 导入物品、魔法、怪物数据
   *          7. 如果未启用 ODBC 或导入失败，使用后备数据
   *          8. 导入地图和刷怪配置
   *          9. 导入怪物掉落配置
   *          10. 生成导入报告文档
   * @note 所有输出文件均为 TOML 格式，存放在 output_root 的子目录中
   */
  LegacyImportReport import_tree(const std::filesystem::path& legacy_root,
                                 const std::filesystem::path& output_root) const;
};

}  // namespace mir2
