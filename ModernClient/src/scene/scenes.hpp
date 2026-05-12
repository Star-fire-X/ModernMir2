// ============================================================
// Mir2 现代客户端 — 场景系统声明
// 职责：定义场景接口、场景管理器和场景上下文
//
// 架构说明：
//   经典传奇（Mir2）的客户端采用场景（Scene）状态机模式来管理
//   从启动到进入游戏世界的完整流程。每个场景是一个独立的 UI 状态节点：
//
//   启动(boot) → 登录(login) → 选服(server_select) → 选角(character_select)
//   → 公告(login_notice) → 加载(loading) → 游戏世界(world)
//
//   场景管理器（SceneManager）负责场景的创建、切换和生命周期管理。
//   切换场景时，先 exit() 旧场景，再 enter() 新场景。
//   每帧依次调用 update() → render()。
//
//   与 Delphi 客户端的对应关系：
//   - boot               → 启动画面（对应 Delphi 的启动 Splash 窗口）
//   - login              → frmLogin（登录/注册表单）
//   - server_select      → frmServerSelect（服务器列表选择）
//   - character_select   → frmCharacterSelect（角色列表和创建）
//   - world              → frmMain（游戏主界面）
// ============================================================
#pragma once

#include <memory>

#include "assets/asset_manager.hpp"
#include "audio/audio_service.hpp"
#include "game/game_state.hpp"
#include "platform/win32_window.hpp"
#include "render/software_renderer.hpp"
#include "ui/ui.hpp"

namespace mir2::client {

class ClientApp;

/// 场景上下文：将 ClientApp 的子系统指针打包传递给场景
///
/// 设计目的：避免场景直接依赖 ClientApp，通过单一的 Context 指针传递
/// 所有子系统（配置/状态/资源/渲染器/输入），使得场景可以独立测试。
///
/// ui_input 在每帧 UiTree::capture_input() 后被填充，场景根据它决定是否
/// 继续处理输入事件（如果 UI 已消费，场景不应再响应）。
struct ClientContext {
  ClientApp* app{nullptr};
  ClientConfig* config{nullptr};
  GameStateStore* state{nullptr};
  AssetManager* assets{nullptr};
  AudioService* audio{nullptr};
  SoftwareRenderer* renderer{nullptr};
  const InputState* input{nullptr};
  ui::UiInputResult ui_input{};  ///< 当前帧的 UI 输入查询结果
};

/// 场景枚举：客户端的状态机节点
enum class SceneId {
  boot,              ///< 启动画面
  login,             ///< 登录/注册界面
  server_select,     ///< 选服界面
  character_select,  ///< 选角界面
  login_notice,      ///< 登录公告
  loading,           ///< 加载等待画面
  world              ///< 游戏世界主场景
};

/// 场景接口：所有场景必须实现的四个生命周期方法
///
/// 执行顺序：
///   1. enter()  — 场景创建后立即调用，初始化 UI 树和注册回调
///   2. capture_ui_input() → process_key_messages() → process_action_messages()
///      → dwin_process() → scene_run()
///   3. render_scene() → paint_ui()
///   4. exit()   — 切换场景前调用，释放场景持有的资源
///
/// 场景切换是同步的：exit(old) → enter(new) 在同一帧内完成。
class Scene {
 public:
  virtual ~Scene() = default;
  virtual void enter(ClientContext& context) = 0;   ///< 进入场景时的初始化（创建 UI 树、请求数据）
  virtual void exit(ClientContext& context) = 0;    ///< 离开场景时的清理（释放精灵缓存、断开网络）
  virtual void update(ClientContext& context, float delta_seconds) = 0;  ///< 每帧逻辑更新（网络轮询、动画、状态同步）
  virtual void render(ClientContext& context) = 0;  ///< 每帧渲染（清屏、绘制背景、精灵、UI）
  virtual void capture_ui_input(ClientContext& context);
  virtual void process_key_messages(ClientContext& context);
  virtual void process_action_messages(ClientContext& context, float delta_seconds);
  virtual void dwin_process(ClientContext& context);
  virtual void scene_run(ClientContext& context, float delta_seconds);
  virtual void render_scene(ClientContext& context);
  virtual void paint_ui(ClientContext& context);
  [[nodiscard]] virtual ui::UiTree& ui_tree() = 0; ///< 获取场景的 UI 树（用于输入分发和精灵缓存）
};

/// 场景管理器：负责创建、切换和驱动场景
///
/// 切换是延迟的：change_scene() 设置 pending_scene_id_，
/// 在下一帧 update() 时执行实际切换。这避免了在事件回调中
/// 销毁当前场景导致的内存不安全问题。
class SceneManager {
 public:
  /// 初始化：创建启动场景（boot）
  void initialize(ClientContext& context);
  /// 切换到指定场景（延迟到下一帧 update 时执行）
  void change_scene(SceneId id, ClientContext& context);
  /// 兼容入口：按 legacy 阶段顺序更新当前场景
  void update(ClientContext& context, float delta_seconds);
  void capture_ui_input(ClientContext& context);
  void process_key_messages(ClientContext& context);
  void process_action_messages(ClientContext& context, float delta_seconds);
  void dwin_process(ClientContext& context);
  void scene_run(ClientContext& context, float delta_seconds);
  void render_scene(ClientContext& context);
  void paint_ui(ClientContext& context);
  /// 兼容入口：渲染当前场景和 UI
  void render(ClientContext& context);
  /// 获取当前场景 ID
  [[nodiscard]] SceneId current_id() const { return current_id_; }
  /// 获取当前场景 UI 树，用于 smoke 测试和调试输入分发
  [[nodiscard]] ui::UiTree* current_ui_tree();

 private:
  /// 根据 SceneId 创建对应的场景实例
  std::unique_ptr<Scene> make_scene(SceneId id);

  SceneId current_id_{SceneId::boot};
  std::unique_ptr<Scene> current_scene_{};
};

}  // namespace mir2::client
