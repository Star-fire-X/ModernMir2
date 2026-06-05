/**
 * @file startup_resource_check.cpp
 * @brief 启动资源完整性检查实现
 *
 * @details 实现启动阶段对关键 UI 精灵帧的验证逻辑。
 *          检查涵盖三个 WIL 归档：Prguse.wil（UI 组件）、Prguse2.wil（UI 扩展）、
 *          ChrSel.wil（选角界面角色动画）。
 *
 * 验证流程：
 * 1. 检查资源根目录结构（Data/ 和 Map/ 子目录是否存在）
 * 2. 加载并验证每个关键精灵帧：
 *    - 帧是否可解码（非 nullptr）
 *    - 帧是否为空（width/height > 0 且 pixels 非空）
 *    - 像素数据大小是否匹配（pixels.size() == width * height）
 *    - 帧是否包含可见像素（至少有一个 Alpha > 0 的像素）
 * 3. 收集所有问题并生成格式化报告
 */

#include "app/startup_resource_check.hpp"

#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <utility>

#include "scene/character_select_state.hpp"

namespace mir2::client {

namespace {

// ============================================================================
// 精灵帧索引常量（对应 WIL 归档中的特定帧位置）
// 这些索引基于对经典传奇客户端 Prguse.wil 和 ChrSel.wil 的分析
// ============================================================================

constexpr int kLoginBackgroundIndex = 22;          ///< 登录界面背景图帧索引
constexpr int kLoginDialogIndex = 60;              ///< 登录对话框帧索引
constexpr int kLoginSubmitButtonIndex = 62;        ///< 登录提交按钮帧索引
constexpr int kLoginCloseButtonIndex = 64;         ///< 登录关闭按钮帧索引
constexpr int kSelectBackgroundIndex = 65;         ///< 选角界面背景图帧索引
constexpr int kServerSelectDialogIndex = 256;      ///< 服务器选择对话框帧索引
constexpr int kServerSelectDialogTwoColumnIndex = 4;   ///< 两列服务器选择对话框帧索引
constexpr int kServerSelectDialogThreeColumnIndex = 5; ///< 三列服务器选择对话框帧索引
constexpr int kServerSelectButtonIndex = 2;        ///< 服务器选择按钮帧索引
constexpr int kSelectLeftButtonIndex = 66;         ///< 选角界面左箭头按钮帧索引
constexpr int kSelectRightButtonIndex = 67;        ///< 选角界面右箭头按钮帧索引
constexpr int kSelectStartButtonIndex = 68;        ///< 开始游戏按钮帧索引
constexpr int kSelectNewButtonIndex = 69;          ///< 新建角色按钮帧索引
constexpr int kSelectEraseButtonIndex = 70;        ///< 删除角色按钮帧索引
constexpr int kMessageDialogIndex = 360;           ///< 消息对话框背景帧索引
constexpr int kMessageOkButtonIndex = 361;         ///< 消息对话框"确定"按钮帧索引
constexpr int kMessageYesButtonIndex = 363;        ///< 消息对话框"是"按钮帧索引
constexpr int kMessageCancelButtonIndex = 365;     ///< 消息对话框"取消"按钮帧索引
constexpr int kMessageNoButtonIndex = 367;         ///< 消息对话框"否"按钮帧索引
constexpr int kSelectEffectFirstIndex = 4;         ///< 选角特效动画起始帧索引
constexpr int kSelectIdleFirstIndex = 40;          ///< 选角空闲动画起始帧索引
constexpr int kSelectFreezeFirstIndex = 60;        ///< 选角冻结动画起始帧索引

/**
 * @brief 将 ArchiveId 枚举转换为对应的 WIL 文件名
 *
 * @param archive 归档类型枚举
 * @return WIL 文件名（如 L"Prguse.wil"），未知类型返回 L"unknown.wil"
 */
std::wstring archive_name(const ArchiveId archive) {
  switch (archive) {
    case ArchiveId::prguse:
      return L"Prguse.wil";
    case ArchiveId::prguse2:
      return L"Prguse2.wil";
    case ArchiveId::chr_sel:
      return L"ChrSel.wil";
    default:
      return L"unknown.wil";
  }
}

/**
 * @brief 向检查结果中添加一个问题条目
 *
 * @param result 检查结果（会被修改）
 * @param severity 问题严重程度（fatal 或 warning）
 * @param archive 所属 WIL 文件名
 * @param frame_index 帧索引
 * @param message 问题描述
 */
void add_issue(StartupResourceCheckResult& result, const StartupResourceSeverity severity,
               std::wstring archive, const int frame_index, std::wstring message) {
  StartupResourceIssue issue{severity, std::move(archive), frame_index, std::move(message)};
  if (severity == StartupResourceSeverity::fatal) {
    result.fatal_issues.push_back(std::move(issue));
    return;
  }
  result.warning_issues.push_back(std::move(issue));
}

/**
 * @brief 检测精灵帧是否包含至少一个可见像素
 *
 * @details 遍历帧的所有像素，检查是否有 Alpha 通道 > 0 的像素。
 *          完全透明的帧对于 UI 元素来说是不可用的（用户看不到按钮等控件）。
 *
 * @param frame 精灵帧
 * @return 如果至少有一个不透明像素返回 true，否则返回 false
 */
bool has_visible_pixel(const SpriteFrame& frame) {
  return std::any_of(frame.pixels.begin(), frame.pixels.end(), [](const auto pixel) {
    return ((pixel >> 24U) & 0xFFU) != 0U;
  });
}

/**
 * @brief 验证单个精灵帧的完整性
 *
 * @details 对指定归档中的指定索引帧执行多项检查：
 * 1. 帧可加载（非 nullptr）
 * 2. 帧非空（有有效的像素数据）
 * 3. 像素数组大小与宽高乘积一致（防止数据损坏）
 * 4. 帧包含可见像素（非完全空白）
 *
 * 任何一项检查失败都会记录为致命问题。
 *
 * @param result 检查结果（会被修改）
 * @param assets 资源管理器
 * @param archive 归档类型
 * @param frame_index 帧索引
 */
void require_frame(StartupResourceCheckResult& result, AssetManager& assets,
                   const ArchiveId archive, const int frame_index) {
  const auto frame = assets.get_frame(archive, frame_index);
  if (frame == nullptr) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"missing or unreadable frame");
    return;
  }
  if (frame->empty()) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"empty frame");
    return;
  }
  if (frame->pixels.size() !=
      static_cast<std::size_t>(frame->width) * static_cast<std::size_t>(frame->height)) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"bad pixel count");
    return;
  }
  if (!has_visible_pixel(*frame)) {
    add_issue(result, StartupResourceSeverity::fatal, archive_name(archive), frame_index,
              L"blank frame");
  }
}

/**
 * @brief 批量验证多个精灵帧
 *
 * @param result 检查结果（会被修改）
 * @param assets 资源管理器
 * @param archive 归档类型
 * @param frames 需要验证的帧索引列表
 */
void require_frames(StartupResourceCheckResult& result, AssetManager& assets,
                    const ArchiveId archive, const std::initializer_list<int> frames) {
  for (const auto frame : frames) {
    require_frame(result, assets, archive, frame);
  }
}

/**
 * @brief 验证选角界面所有角色动画帧
 *
 * @details 选角界面需要为每个职业（战士/法师/道士）和性别（男/女）
 *          加载以下动画帧：
 *          - 特效动画帧（effect）：14 帧，用于角色选中时的光效
 *          - 空闲动画帧（idle）：16 帧，角色被选中时的站立动画
 *          - 冻结动画帧（freeze）：13 帧，角色未被选中时的冻结/解冻过渡动画
 *
 *          帧索引计算公式（与 Delphi 客户端一致）：
 *          - idle:   kSelectIdleFirstIndex   + job * 40 + frame + sex * 120
 *          - freeze: kSelectFreezeFirstIndex + job * 40 + frame + sex * 120
 *
 * @param result 检查结果（会被修改）
 * @param assets 资源管理器
 */
void require_character_select_frames(StartupResourceCheckResult& result, AssetManager& assets) {
  require_frame(result, assets, ArchiveId::chr_sel, kLoginBackgroundIndex);
  for (int frame = 0; frame < kCharacterSelectEffectFrameCount; ++frame) {
    require_frame(result, assets, ArchiveId::chr_sel, kSelectEffectFirstIndex + frame);
  }
  for (int sex = 0; sex < 2; ++sex) {
    for (int job = 0; job < 3; ++job) {
      for (int frame = 0; frame < kCharacterSelectSelectedFrameCount; ++frame) {
        require_frame(result, assets, ArchiveId::chr_sel,
                      kSelectIdleFirstIndex + job * 40 + frame + sex * 120);
      }
      for (int frame = 0; frame < kCharacterSelectFreezeFrameCount; ++frame) {
        require_frame(result, assets, ArchiveId::chr_sel,
                      kSelectFreezeFirstIndex + job * 40 + frame + sex * 120);
      }
    }
  }
}

/**
 * @brief 将问题列表追加到输出流中
 *
 * @param out 输出字符串流
 * @param title 问题分类标题（如 L"Fatal issues"）
 * @param issues 问题列表
 */
void append_issues(std::wstringstream& out, const wchar_t* title,
                   const std::vector<StartupResourceIssue>& issues) {
  if (issues.empty()) {
    return;
  }
  out << title << L":\n";
  for (const auto& issue : issues) {
    out << L"- ";
    if (!issue.archive.empty()) {
      out << issue.archive;
      if (issue.frame_index >= 0) {
        out << L"[" << issue.frame_index << L"]";
      }
      out << L": ";
    }
    out << issue.message << L"\n";
  }
}

}  // namespace

StartupResourceCheckResult check_startup_asset_root(const std::filesystem::path& root_path) {
  StartupResourceCheckResult result;
  if (root_path.empty()) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"asset_root is empty");
    return result;
  }
  if (!std::filesystem::exists(root_path)) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"asset_root does not exist");
  }
  if (!std::filesystem::exists(root_path / "Data")) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"missing Data directory");
  }
  if (!std::filesystem::exists(root_path / "Map")) {
    add_issue(result, StartupResourceSeverity::fatal, {}, -1, L"missing Map directory");
  }
  return result;
}

StartupResourceCheckResult check_startup_auth_resources(AssetManager& assets) {
  StartupResourceCheckResult result;
  require_frames(result, assets, ArchiveId::prguse,
                 {kLoginDialogIndex, kLoginSubmitButtonIndex, kLoginCloseButtonIndex,
                  kSelectBackgroundIndex, kServerSelectDialogIndex, kSelectLeftButtonIndex,
                  kSelectRightButtonIndex, kSelectStartButtonIndex, kSelectNewButtonIndex,
                  kSelectEraseButtonIndex, kMessageDialogIndex, kMessageOkButtonIndex,
                  kMessageYesButtonIndex, kMessageCancelButtonIndex, kMessageNoButtonIndex});
  require_frames(result, assets, ArchiveId::prguse2,
                 {kServerSelectButtonIndex, kServerSelectDialogTwoColumnIndex,
                  kServerSelectDialogThreeColumnIndex});
  require_character_select_frames(result, assets);
  return result;
}

std::wstring format_startup_resource_report(const StartupResourceCheckResult& result,
                                            const std::filesystem::path& root_path) {
  std::wstringstream out;
  out << (result.ok() ? L"Startup resource check passed." : L"Startup resource check failed.")
      << L"\nResource root: " << root_path.wstring() << L"\n";
  append_issues(out, L"Fatal issues", result.fatal_issues);
  append_issues(out, L"Warnings", result.warning_issues);
  return out.str();
}

}  // namespace mir2::client
