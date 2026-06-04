/**
 * @file config_loader.hpp
 * @brief 配置加载器类声明
 * @details 声明 ConfigLoader 类，负责从 TOML 配置文件和遗留脚本中
 *          加载完整的服务端配置，包括服务器参数、端口绑定、地图、
 *          怪物、刷怪、物品、魔法、NPC、地图任务等所有运行时配置。
 *          该类是服务端配置系统的入口点，所有配置的解析和组装均由
 *          ConfigLoader::load() 方法统一调度。
 */

#pragma once

#include <filesystem>

#include "config/models.hpp"

namespace mir2 {

/**
 * @brief 配置加载器
 * @details 核心配置加载类，负责：
 *          - 解析 server.toml、ports.toml、logic.toml 等配置文件
 *          - 加载地图、怪物、刷怪、物品、魔法、NPC 的 TOML 定义
 *          - 解析遗留 NPC 脚本（.txt 格式）并转换为结构化配置
 *          - 为 NPC 推断服务类型
 *          - 解析地图任务配置和启动任务脚本
 *          - 将所有配置组装为完整的 HostConfig 对象
 * @note 所有解析逻辑都在 ConfigLoader::load() 方法中按顺序执行，
 *       解析失败时会抛出 std::runtime_error
 */
class ConfigLoader {
 public:
  /**
   * @brief 从指定根目录加载完整配置
   * @details 此方法是配置加载的总入口。它按固定顺序执行以下步骤：
   *          1. 解析 server.toml（运行时参数）
   *          2. 解析 ports.toml（端口绑定）
   *          3. 解析 runtime/logic.toml（逻辑预算）
   *          4. 加载 maps/ 目录下所有地图配置
   *          5. 加载 monsters/ 目录下所有怪物定义
   *          6. 加载 spawns/ 目录下所有刷怪配置
   *          7. 加载 items/ 目录下所有物品定义
   *          8. 加载 magic/ 目录下所有魔法定义
   *          9. 加载 npcs/ 目录下所有 NPC 配置
   *          10. 加载 map_quests/ 目录下所有地图任务
   *          11. 加载启动任务脚本
   * @param root 配置文件根目录，需包含 server.toml、ports.toml、
   *             maps/、monsters/、spawns/、items/、magic/、npcs/、
   *             map_quests/ 等子目录和文件
   * @return 组装完成的 HostConfig 对象
   * @throws std::runtime_error 如果缺少地图配置文件或解析失败
   */
  HostConfig load(const std::filesystem::path& root) const;
};

}  // namespace mir2
