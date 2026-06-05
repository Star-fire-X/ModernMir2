/**
 * @file startup_resource_check.hpp
 * @brief 启动资源完整性检查模块 —— 验证客户端启动所需的 WIL 精灵帧资源
 *
 * @details 在客户端启动阶段，验证所有必需的 UI 精灵帧（登录界面、选角界面、
 *          服务器列表、提示对话框等）是否存在且格式正确。如果关键资源缺失，
 *          客户端将以错误提示中止启动，避免运行时崩溃。
 *
 * 主要功能：
 * - 验证资源根目录结构（Data/ 和 Map/ 子目录）
 * - 检查登录界面所需的 Prguse.wil 帧
 * - 检查选角界面所需的 ChrSel.wil 帧（含各职业/性别的空闲、冻结动画）
 * - 生成可读的检查报告（格式化输出所有问题）
 *
 * 检查策略：
 * - fatal：缺失会导致客户端无法正常运行的资源（启动中止）
 * - warning：格式异常但不会阻塞启动的资源（仅记录警告）
 *
 * @note 此检查仅在启动时执行一次，不影响运行时性能
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "assets/asset_manager.hpp"

namespace mir2::client {

/**
 * @enum StartupResourceSeverity
 * @brief 启动资源问题的严重程度
 */
enum class StartupResourceSeverity {
  fatal,   ///< 致命错误 —— 资源缺失导致客户端无法正常运行
  warning, ///< 警告 —— 资源格式异常但不阻塞启动
};

/**
 * @struct StartupResourceIssue
 * @brief 单个启动资源问题的描述
 */
struct StartupResourceIssue {
  StartupResourceSeverity severity{StartupResourceSeverity::fatal};  ///< 问题严重程度
  std::wstring archive{};       ///< 所属 WIL 归档文件名（如 L"Prguse.wil"）
  int frame_index{-1};          ///< 帧索引（-1 表示非特定帧的问题，如目录缺失）
  std::wstring message{};       ///< 问题的详细描述
};

/**
 * @struct StartupResourceCheckResult
 * @brief 启动资源检查的完整结果
 */
struct StartupResourceCheckResult {
  std::vector<StartupResourceIssue> fatal_issues{};    ///< 致命问题列表
  std::vector<StartupResourceIssue> warning_issues{};  ///< 警告问题列表

  /// 检查是否通过（无致命问题）
  [[nodiscard]] bool ok() const { return fatal_issues.empty(); }
};

/**
 * @brief 验证资源根目录结构
 *
 * @details 检查以下内容：
 * - root_path 不为空
 * - root_path 目录存在
 * - Data/ 子目录存在（存放 WIL/WIX 文件）
 * - Map/ 子目录存在（存放 .map 地图文件）
 *
 * @param root_path 资源根目录路径
 * @return 检查结果（包含所有发现的问题）
 */
[[nodiscard]] StartupResourceCheckResult check_startup_asset_root(
    const std::filesystem::path& root_path);

/**
 * @brief 检查登录/选角界面的关键精灵帧
 *
 * @details 验证以下资源的可用性：
 * - Prguse.wil：登录对话框、提交/关闭按钮、服务器选择界面、消息对话框等
 * - Prguse2.wil：服务器选择按钮、多列对话框等
 * - ChrSel.wil：选角背景、各职业/性别角色的空闲动画帧和冻结动画帧
 *
 * 对于每个帧，检查：
 * 1. 帧是否存在（可加载）
 * 2. 帧是否非空（有像素数据）
 * 3. 像素数据大小是否与尺寸匹配
 * 4. 帧是否包含可见像素（非完全透明/黑色）
 *
 * @param assets 已初始化的资源管理器（需先设置好资源根目录）
 * @return 检查结果
 */
[[nodiscard]] StartupResourceCheckResult check_startup_auth_resources(AssetManager& assets);

/**
 * @brief 格式化资源检查结果为可读的文本报告
 *
 * @details 将检查结果格式化为包含资源根目录路径、致命问题列表和警告列表的文本。
 *          适用于在启动日志中输出或显示在错误对话框中。
 *
 * @param result 检查结果
 * @param root_path 资源根目录路径（用于报告中显示）
 * @return 格式化后的文本报告
 */
[[nodiscard]] std::wstring format_startup_resource_report(
    const StartupResourceCheckResult& result, const std::filesystem::path& root_path);

}  // namespace mir2::client
