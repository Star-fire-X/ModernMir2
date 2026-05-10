// ============================================================
// Mir2 现代客户端 — ClientApp 主循环与协议处理实现
// 职责：实现客户端程序的完整生命周期管理
//
// 主循环（run()）每帧执行顺序：
//   1. 窗口消息处理（WM_PUMP → WM_QUIT 检测 → 关闭请求）
//   2. 窗口大小自适应（resize 时重建 D3D11 后台缓冲区）
//   3. 网络 I/O 轮询（非阻塞 select → 收包 → 帧解码）
//   4. 协议事件分发（handle_protocol_events，std::visit 分发所有消息）
//   5. 定时器更新（心跳、外挂检测、鼠标轮询、动作锁定超时）
//   6. 场景更新（输入处理 → 动画推进 → UI 交互）
//   7. 场景渲染（清屏 → 场景绘制 → 模态对话框覆盖 → D3D11 present）
//   8. sleep(1ms) 让渡 CPU
//
// 三网关架构（对应 Delphi 客户端的三级认证流程）：
//   LoginGate → 登录认证、获取服务器列表、选服
//   SelGate   → 角色列表、创建/删除/选择角色
//   RunGate   → 进入游戏世界、完整游戏交互
//   每次网关切换都重新建立 TCP 连接（非长连接模型）
//
// 自动播放模式（AutoPlay）：
//   通过 client.ini 中的 [autoplay] 配置段，在无用户交互的情况下
//   自动完成登录→选服→选角→进入游戏 的完整流程，用于自动化测试。
//   可通过 MIR2_LEGACY_TRACE 环境变量开启调试日志。
// ============================================================

#include "app/client_app.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "shared/legacy/action_ids.hpp"
#include "text/encoding.hpp"

namespace mir2::client {

namespace {

// 经典客户端原生分辨率（800x600），所有坐标和精灵以此为准
constexpr int kNativeClientWidth = 800;
constexpr int kNativeClientHeight = 600;

// Prguse.wil 中模态对话框精灵的索引常量
constexpr int kMessageDialogIndex = 360;
constexpr int kMessageOkButtonIndex = 361;      ///< 模态对话框"确定"按钮
constexpr int kMessageYesButtonIndex = 363;     ///< 确认对话框"是"按钮
constexpr int kMessageCancelButtonIndex = 365;  ///< 确认对话框"取消"按钮
constexpr int kMessageNoButtonIndex = 367;      ///< 确认对话框"否"按钮

/// 窄字符串转宽字符串
std::wstring widen(const std::string& text) { return text::utf8_to_wide(text); }
/// 宽字符串转窄字符串
std::string narrow(const std::wstring& text) { return text::wide_to_utf8(text); }

std::vector<std::wstring> split_modal_lines(const std::wstring& text) {
  std::vector<std::wstring> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find(L'\\', start);
    lines.push_back(end == std::wstring::npos ? text.substr(start) : text.substr(start, end - start));
    if (end == std::wstring::npos) {
      break;
    }
    start = end + 1;
  }
  return lines;
}

void draw_legacy_bold_text(SoftwareRenderer& renderer, const int x, const int y,
                           const std::wstring& text, const std::uint32_t color) {
  renderer.draw_text(x + 1, y, text, color);
  renderer.draw_text(x, y, text, color);
}

/// 计算精灵在画面中居中显示的矩形区域
/// @param frame 精灵帧（可为空，为空时使用 fallback 尺寸）
/// @param fallback_width/falback_height 精灵加载失败时的兜底尺寸
RectI centered_rect(const std::shared_ptr<const SpriteFrame>& frame, const int width,
                    const int height, const int fallback_width, const int fallback_height) {
  const auto sprite_width = frame != nullptr ? frame->width : fallback_width;
  const auto sprite_height = frame != nullptr ? frame->height : fallback_height;
  return RectI{(width - sprite_width) / 2, (height - sprite_height) / 2, sprite_width,
               sprite_height};
}

/// 计算精灵在指定坐标的矩形区域
RectI sprite_rect(const std::shared_ptr<const SpriteFrame>& frame, const int x, const int y,
                  const int fallback_width, const int fallback_height) {
  return RectI{x, y, frame != nullptr ? frame->width : fallback_width,
               frame != nullptr ? frame->height : fallback_height};
}

/// 检查是否启用了旧版客户端跟踪日志（通过环境变量 MIR2_LEGACY_TRACE 控制）
bool legacy_trace_enabled() {
  static const bool enabled = [] {
    char buffer[8]{};
    return GetEnvironmentVariableA("MIR2_LEGACY_TRACE", buffer, sizeof(buffer)) > 0 &&
           buffer[0] != '0';
  }();
  return enabled;
}

/// 输出旧版兼容性跟踪日志到调试输出（仅在启用时）
void legacy_trace(const std::string_view text) {
  if (!legacy_trace_enabled()) {
    return;
  }
  std::string line{"[mir2-legacy] "};
  line.append(text);
  line.push_back('\n');
  OutputDebugStringA(line.c_str());
  char path[1024]{};
  const auto length = GetEnvironmentVariableA("MIR2_LEGACY_TRACE_FILE", path, sizeof(path));
  if (length > 0 && length < sizeof(path)) {
    std::ofstream file(path, std::ios::app | std::ios::binary);
    file << line;
  }
}

/// 在软件渲染器上绘制精灵帧
void draw_sprite(SoftwareRenderer& renderer, const std::shared_ptr<const SpriteFrame>& frame,
                 const int x, const int y) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  renderer.surface().blit_rgba(x, y, frame->width, frame->height, frame->pixels.data());
}

/// 模态对话框专用的精灵按钮（继承自 SpriteButton）
class ModalSpriteButton final : public ui::SpriteButton {
 public:
  ModalSpriteButton(RectI bounds, std::shared_ptr<const SpriteFrame> frame)
      : ui::SpriteButton(bounds, std::move(frame)) {}
};

/// 在模态对话框中添加一个按钮
/// @param parent 父 UI 节点
/// @param sprite_index Prguse.wil 中的精灵索引
/// @param fallback_width/height 精灵加载失败时的兜底尺寸
ModalSpriteButton* add_modal_button(ui::UiNode* parent, AssetManager& assets, const int sprite_index,
                                    const int x, const int y, const int fallback_width = 88,
                                    const int fallback_height = 28) {
  const auto frame = assets.get_frame(ArchiveId::prguse, sprite_index);
  auto* button = parent->emplace_child<ModalSpriteButton>(
      sprite_rect(frame, x, y, fallback_width, fallback_height), frame);
  button->face = ui::LegacySpriteRef{ArchiveId::prguse, sprite_index};
  button->real_hit_test_enabled = true;  // 启用像素级点击检测
  return button;
}

/// 从 .ini 文件中读取字符串配置
std::wstring read_ini_string(const std::filesystem::path& path, LPCWSTR section, LPCWSTR key,
                             const std::wstring& fallback) {
  wchar_t buffer[256]{};
  GetPrivateProfileStringW(section, key, fallback.c_str(), buffer, static_cast<DWORD>(std::size(buffer)),
                           path.c_str());
  return buffer;
}

/// 从 .ini 文件中读取整数配置
int read_ini_int(const std::filesystem::path& path, LPCWSTR section, LPCWSTR key, int fallback) {
  return GetPrivateProfileIntW(section, key, fallback, path.c_str());
}

/// 从 .ini 文件中读取布尔配置
bool read_ini_bool(const std::filesystem::path& path, LPCWSTR section, LPCWSTR key, bool fallback) {
  return read_ini_int(path, section, key, fallback ? 1 : 0) != 0;
}

/// 规范化账号资料：用账号 ID 填充必填字段的空缺，SSN 使用默认测试值
/// 注意：默认 SSN "650101-1455111" 是为了兼容旧版韩服客户端的字段校验
client_v1::AccountProfile normalize_account_profile(const std::string& account_id,
                                                     client_v1::AccountProfile profile) {
  if (profile.display_name.empty()) {
    profile.display_name = account_id;
  }
  if (profile.user_name.empty()) {
    profile.user_name = profile.display_name;
  }
  if (profile.ss_no.empty()) {
    profile.ss_no = "650101-1455111";
  }
  return profile;
}

/// 为自动播放模式创建账户资料
client_v1::AccountProfile make_autoplay_account_profile(const std::string& account_id,
                                                        const std::string& display_name) {
  auto profile = normalize_account_profile(
      account_id, client_v1::AccountProfile{display_name.empty() ? account_id : display_name});
  profile.birthday = "1975/08/21";
  profile.quiz = "autoplay";
  profile.answer = "autoplay";
  profile.quiz2 = "autoplay2";
  profile.answer2 = "autoplay2";
  return profile;
}

/// 解析默认的资源根目录（包含 Data/ 和 Map/ 目录）
/// 从当前目录和上级目录依次查找 "Legend of Mir" 目录
std::wstring resolve_default_asset_root() {
  const std::vector<std::filesystem::path> candidates{
      std::filesystem::absolute(L"Legend of Mir"),
      std::filesystem::absolute(L"..\\Legend of Mir"),
      std::filesystem::absolute(L"..\\..\\Legend of Mir"),
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate / "Data") && std::filesystem::exists(candidate / "Map")) {
      return candidate.wstring();
    }
  }
  return {};
}

std::vector<int> missing_required_prguse_frames(AssetManager& assets) {
  constexpr std::array kRequiredFrames{
      1,   3,   4,   5,   6,   7,   8,   9,   10,  11,  15,  16,
      17,  18,  19,  26,  29,  128, 130, 132, 134, 136, 138, 140,
      229, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 249,
      250, 251, 252, 253, 254, 255, 360, 361, 363, 365, 367, 370,
      371, 372, 373, 376, 377, 382, 383, 385, 386, 387, 388, 392,
      393, 396, 398};
  std::vector<int> missing;
  for (const auto index : kRequiredFrames) {
    const auto frame = assets.get_frame(ArchiveId::prguse, index);
    if (frame == nullptr || frame->empty()) {
      missing.push_back(index);
    }
  }
  return missing;
}

}  // namespace

// 默认构造，所有子系统在其各自的默认构造函数中初始化为空状态
ClientApp::ClientApp() = default;

bool ClientApp::initialize() {
  // 第一步：从 client.ini 加载配置
  if (!load_config()) {
    return false;
  }

  // 如果启用了自动播放模式，预填账号密码
  if (config_.auto_play.enabled) {
    state_.login.account_id = config_.auto_play.account_id;
    state_.login.password = config_.auto_play.password;
    state_.login.status = L"Auto play ready";
  }

  // 创建 Win32 窗口（800x600，与经典客户端一致）
  if (!window_.create(config_.window_title, kNativeClientWidth, kNativeClientHeight)) {
    return false;
  }
  // 初始化 D3D11 软件渲染器
  if (!renderer_.initialize(window_.handle(), kNativeClientWidth, kNativeClientHeight)) {
    return false;
  }
  // 加载资源管理器（WIL/WIX/地图文件索引）
  if (!assets_.initialize(config_.asset_root)) {
    return false;
  }
  const auto missing_frames = missing_required_prguse_frames(assets_);
  // 初始化音频服务并加载 Delphi sound.lst；音频失败不阻塞客户端启动。
  audio_.initialize(config_.asset_root, window_.handle());
  audio_.apply_settings(config_.audio);

  // 初始化输入坐标映射并构建场景上下文
  refresh_mapped_input();
  ClientContext context{this, &config_, &state_, &assets_, &audio_, &renderer_, &mapped_input_};
  scenes_.initialize(context);
  if (!missing_frames.empty()) {
    std::wstringstream message;
    message << L"Missing Prguse frames:";
    for (const auto index : missing_frames) {
      message << L" " << index;
    }
    OutputDebugStringW((message.str() + L"\n").c_str());
    show_modal(L"Resource Check", message.str());
  }
  return true;
}

int ClientApp::run() {
  // 使用单调时钟计算帧间隔时间
  using clock = std::chrono::steady_clock;
  auto last_tick = clock::now();

  while (true) {
    // ---- 窗口消息处理阶段 ----
    window_.begin_frame();
    if (!window_.pump_messages()) {  // 收到 WM_QUIT 时返回 false
      break;
    }
    if (window_.consume_close_request()) {  // 用户点击关闭按钮
      handle_close_request();
    }

    // 窗口大小变化时重置渲染器后台缓冲区尺寸
    if (window_.was_resized()) {
      renderer_.resize(window_.client_width(), window_.client_height());
      window_.clear_resize_flag();
    }
    // 重新映射鼠标坐标（考虑缩放）
    refresh_mapped_input();

    // ---- 网络事件处理阶段 ----
    protocol_.poll();           // 非阻塞轮询 socket，收取数据
    handle_protocol_events();   // 分发并处理所有已到达的协议消息

    // ---- 定时器更新阶段 ----
    const auto now = clock::now();
    const auto delta =
        std::chrono::duration_cast<std::chrono::duration<float>>(now - last_tick).count();
    last_tick = now;
    run_timers(delta);  // 驱动鼠标轮询、心跳、外挂检测等定时器

    // ---- 场景更新阶段 ----
    ClientContext context{this, &config_, &state_, &assets_, &audio_, &renderer_, &mapped_input_};
    if (!state_.modal.visible) {
      // 无模态对话框：正常更新场景
      scenes_.update(context, delta);
    } else {
      // 模态对话框弹出时：屏蔽所有输入，防止对话框下层界面被操作
      auto blocked_input = mapped_input_;
      blocked_input.left_down = false;
      blocked_input.left_pressed = false;
      blocked_input.left_released = false;
      blocked_input.right_down = false;
      blocked_input.right_pressed = false;
      blocked_input.right_released = false;
      blocked_input.key_down.fill(false);
      blocked_input.key_pressed.fill(false);
      blocked_input.text_input.clear();
      blocked_input.backspace_pressed = false;
      blocked_input.enter_pressed = false;
      ClientContext blocked_context{this, &config_, &state_, &assets_, &audio_, &renderer_,
                                    &blocked_input};
      scenes_.update(blocked_context, delta);
    }
    // 执行待处理的场景切换
    if (scene_change_pending_) {
      scenes_.change_scene(requested_scene_, context);
      scene_change_pending_ = false;
    }
    // 自动播放模式：到达登录场景时自动触发登录流程
    maybe_start_auto_play();

    // ---- 渲染阶段 ----
    renderer_.begin_frame(0xFF0B1016U);  // 清屏为深蓝灰色背景
    scenes_.render(context);              // 绘制当前场景
    render_modal();                       // 在场景之上绘制模态对话框
    renderer_.present();                  // 将后台缓冲区提交到屏幕
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 让渡 CPU，降低功耗
  }

  audio_.shutdown();  // 退出后关闭音频
  return 0;
}

// 将窗口像素坐标映射到逻辑坐标（考虑 DPI 缩放和拉伸填充）
void ClientApp::refresh_mapped_input() {
  mapped_input_ = window_.input();
  const auto logical_point =
      renderer_.window_to_logical(window_.input().mouse_x, window_.input().mouse_y);
  mapped_input_.mouse_x = logical_point.x;
  mapped_input_.mouse_y = logical_point.y;
}

// 请求切换到指定场景（延迟执行，在当前帧更新完毕后生效）
void ClientApp::request_scene_change(SceneId id) {
  requested_scene_ = id;
  scene_change_pending_ = true;
}

// 请求登录网关：清理旧状态 -> 重置角色/选服/世界数据 -> 发起 TCP 连接
void ClientApp::request_login(const std::string& account_id, const std::string& password) {
  state_.login.status = L"Connecting login gateway...";
  state_.login.account_id = account_id;
  state_.login.password = password;
  state_.login.login_state = LoginState::lsLogin;
  state_.login_notice = LoginNoticeViewState{};
  // 清除可能残留的上次登录记录
  state_.enter_world_token.clear();
  state_.selected_character.clear();
  state_.pending_lobby_token.clear();
  state_.pending_character_host.clear();
  state_.pending_character_port = 0;
  state_.pending_game_host.clear();
  state_.pending_game_port = 0;
  state_.pending_self_actor_id = 0;
  state_.pending_spawn_x = 0;
  state_.pending_spawn_y = 0;
  state_.lobby.characters.clear();
  state_.connection_phase = GameStateStore::ConnectionPhase::login;
  pending_connect_ = PendingConnect::login;
  // 设置 5 秒超时提示
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for login gateway..."); });
  if (!protocol_.connect(config_.login_host, config_.login_port)) {
    pending_connect_ = PendingConnect::none;
    cancel_one_shot_timer(wait_msg_timer_);
    state_.login.status = L"Login gateway connection failed.";
    show_modal(L"Connection Failed", L"Unable to connect to login gateway.");
  }
}

// 请求注册账号：保存注册信息并连接登录网关
void ClientApp::request_create_account(const std::string& account_id, const std::string& password,
                                       const client_v1::AccountProfile& profile) {
  state_.login.status = L"Connecting login gateway for registration...";
  state_.login.login_state = LoginState::lsNewid;
  state_.login.account_id = account_id;
  state_.login.password = password;
  state_.login.account_profile = normalize_account_profile(account_id, profile);
  pending_create_account_ =
      client_v1::CreateAccountRequest{account_id, password, state_.login.account_profile};
  pending_connect_ = PendingConnect::create_account;
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for registration..."); });
  if (!protocol_.connect(config_.login_host, config_.login_port)) {
    pending_connect_ = PendingConnect::none;
    cancel_one_shot_timer(wait_msg_timer_);
    state_.login.status = L"Login gateway connection failed.";
    show_modal(L"Connection Failed", L"Unable to connect to login gateway.");
  }
}

// 请求更新账号资料（需先登录，在已有连接上直接发送 UpdateAccountRequest）
void ClientApp::request_update_account(const std::string& account_id, const std::string& password,
                                       const client_v1::AccountProfile& profile) {
  if (!protocol_.connected()) {
    state_.login.status = L"Login gateway is not connected.";
    show_modal(L"Connection Required", L"Log in again before updating account details.");
    return;
  }
  state_.login.status = L"Updating account details...";
  state_.login.login_state = LoginState::lsNewid;
  state_.login.account_id = account_id;
  state_.login.password = password;
  state_.login.account_profile = normalize_account_profile(account_id, profile);
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for account update..."); });
  protocol_.send(client_v1::UpdateAccountRequest{account_id, password, state_.login.account_profile});
}

// 请求修改密码：发起独立的修改密码连接
void ClientApp::request_change_password(const std::string& account_id, const std::string& password,
                                        const std::string& new_password) {
  state_.login.status = L"Connecting login gateway for password change...";
  state_.login.login_state = LoginState::lsChgpw;
  state_.login.account_id = account_id;
  state_.login.password = password;
  pending_change_password_ =
      client_v1::ChangePasswordRequest{account_id, password, new_password};
  pending_connect_ = PendingConnect::change_password;
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for password change..."); });
  if (!protocol_.connect(config_.login_host, config_.login_port)) {
    pending_connect_ = PendingConnect::none;
    cancel_one_shot_timer(wait_msg_timer_);
    state_.login.status = L"Login gateway connection failed.";
    show_modal(L"Connection Failed", L"Unable to connect to login gateway.");
  }
}

// 请求选择游戏服务器：在已登录的连接上发送选服请求
void ClientApp::request_select_server(const std::string& server_name) {
  if (server_name.empty()) {
    show_modal(L"Select Server", L"No server is selected.");
    return;
  }
  if (!protocol_.connected()) {
    state_.login.status = L"Login gateway is not connected.";
    show_modal(L"Connection Required", L"Log in again before selecting a server.");
    return;
  }
  // 清理旧的角色网关状态
  state_.pending_lobby_token.clear();
  state_.pending_character_host.clear();
  state_.pending_character_port = 0;
  state_.lobby.characters.clear();
  state_.connection_phase = GameStateStore::ConnectionPhase::login;
  state_.login.status = L"Selecting server...";
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for server selection..."); });
  protocol_.send(client_v1::SelectServerRequest{server_name});
}

// 请求角色列表：向角色网关发送列表请求（需先通过 SelectServerResult 获取 lobby_token）
void ClientApp::request_character_list() {
  state_.connection_phase = GameStateStore::ConnectionPhase::select_character;
  state_.login.status = L"Requesting character list...";
  schedule_one_shot_timer(sel_chr_wait_timer_, 5.0f,
                          [this] { sel_chr_wait_timer_tick(L"Still waiting for character list..."); });
  protocol_.send(client_v1::CharacterListRequest{state_.pending_lobby_token});
}

// 请求创建角色：发送名字/职业/性别/发型到角色网关
void ClientApp::request_create_character(const std::string& name, const std::uint8_t job,
                                         const std::uint8_t sex, const std::uint8_t hair) {
  if (name.empty()) {
    return;
  }
  schedule_one_shot_timer(cmd_timer_, 3.0f,
                          [this] { cmd_timer_tick(L"Still waiting for character creation..."); });
  protocol_.send(client_v1::CreateCharacterRequest{name, job, sex, hair});
}

// 请求删除当前选中的角色：弹出确认对话框，确认后发送删除请求
void ClientApp::request_delete_selected_character() {
  if (state_.lobby.selected_index < 0 ||
      state_.lobby.selected_index >= static_cast<int>(state_.lobby.characters.size())) {
    return;
  }
  const auto name =
      state_.lobby.characters[static_cast<std::size_t>(state_.lobby.selected_index)].name;
  const auto character_name = widen(name);
  show_destructive_confirm_modal(
      L"Delete Character",
      L"Delete \"" + character_name +
          L"\"?\\Deleted characters cannot be restored.\\You may be unable to reuse this name for a while.",
      [this, name] {
        schedule_one_shot_timer(
            cmd_timer_, 3.0f,
            [this] { cmd_timer_tick(L"Still waiting for character deletion..."); });
        protocol_.send(client_v1::DeleteCharacterRequest{name});
      });
}

// 请求使用选中角色进入游戏：发送 SelectCharacterRequest，服务端返回 enter_world_token
void ClientApp::request_selected_character_enter() {
  if (state_.lobby.selected_index < 0 ||
      state_.lobby.selected_index >= static_cast<int>(state_.lobby.characters.size())) {
    return;
  }
  state_.selected_character = state_.lobby.characters[static_cast<std::size_t>(state_.lobby.selected_index)].name;
  schedule_one_shot_timer(sel_chr_wait_timer_, 5.0f,
                          [this] { sel_chr_wait_timer_tick(L"Still waiting for character entry..."); });
  protocol_.send(client_v1::SelectCharacterRequest{state_.selected_character});
}

// 确认登录公告：向服务端发送 LoginNoticeOk，然后切换到加载场景等待世界快照
void ClientApp::acknowledge_login_notice() {
  state_.login.status = L"Login notice accepted. Waiting for world...";
  state_.login_notice = LoginNoticeViewState{};
  if (protocol_.connected()) {
    protocol_.send(client_v1::LoginNoticeOk{});
  }
  schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                          [this] { wait_msg_timer_tick(L"Still waiting for world snapshot..."); });
  request_scene_change(SceneId::loading);
}

// 返回角色选择界面：通过重放登录流程回到选角页面
void ClientApp::request_reselect_character() {
  if (state_.login.account_id.empty() || state_.login.password.empty()) {
    show_modal(L"Reselect Character", L"Login credentials are not available.");
    return;
  }
  begin_login_replay(false);
}

// 快捷移动请求：将 MoveMode（walk/run）包装为 ActionIntent 发送
void ClientApp::request_move(int x, int y, client_v1::MoveMode mode) {
  client_v1::ActionIntent intent;
  intent.kind = mode == client_v1::MoveMode::run ? client_v1::WorldActionKind::run
                                                  : client_v1::WorldActionKind::walk;
  intent.x = x;
  intent.y = y;
  request_action(intent);
}

// 通用动作请求：锁定当前角色动作，设置本地预测的状态后发送到服务器
// 使用动作锁定（action_locked）防止在服务器确认前重复发送动作
void ClientApp::request_action(const client_v1::ActionIntent& intent) {
  const auto now_ms = detail::monotonic_ms();
  state_.world.action_locked = true;
  state_.world.action_lock_started_ms = now_ms;
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_action now=" << now_ms << " kind=" << static_cast<int>(intent.kind)
        << " x=" << intent.x << " y=" << intent.y << " dir=" << static_cast<int>(intent.dir)
        << " target=" << intent.target_actor_id << " legacy=" << intent.legacy_ident;
    legacy_trace(out.str());
  }
  // 本地预测：立即更新玩家角色的方向/动作/坐标，不等服务器回包
  if (auto it = state_.world.actors.find(state_.world.self_actor_id);
      it != state_.world.actors.end()) {
    auto& actor = it->second;
    const auto legacy_ident =
        intent.kind == client_v1::WorldActionKind::attack
            ? legacy::normalize_attack_ident_to_sm(intent.legacy_ident)
            : intent.legacy_ident;
    actor.dir = intent.dir;
    // 将 WorldActionKind 映射为 ActorActionKind（动画层使用）
    actor.current_action = intent.kind == client_v1::WorldActionKind::attack
                               ? client_v1::ActorActionKind::hit
                               : (intent.kind == client_v1::WorldActionKind::run
                                      ? client_v1::ActorActionKind::run
                                      : (intent.kind == client_v1::WorldActionKind::walk
                                             ? client_v1::ActorActionKind::walk
                                             : client_v1::ActorActionKind::turn));
    actor.legacy_action_ident = legacy_ident;
    actor.action_target_actor_id = intent.target_actor_id;
    actor.action_target_x = intent.x;
    actor.action_target_y = intent.y;
    actor.action_magic = false;
    actor.action_started_ms = now_ms;
    actor.action_duration_ms =
        GameStateStore::action_duration_ms(actor.current_action, actor.legacy_action_ident);
    // 移动类动作：立即更新坐标以实现无延迟行走
    if (intent.kind == client_v1::WorldActionKind::walk ||
        intent.kind == client_v1::WorldActionKind::run) {
      actor.from_x = actor.x;
      actor.from_y = actor.y;
      actor.x = intent.x;
      actor.y = intent.y;
      actor.running = intent.kind == client_v1::WorldActionKind::run;
      actor.move_started_ms = actor.action_started_ms;
      actor.move_duration_ms = actor.running ? 140U : 180U;  // 跑步 140ms/格，走路 180ms/格
    }
  }
  protocol_.send(intent);
}

// 法术/技能请求：锁定动作并设置魔法释放状态后发送到服务器
void ClientApp::request_spell(const client_v1::SpellIntent& intent) {
  const auto now_ms = detail::monotonic_ms();
  state_.world.action_locked = true;
  state_.world.action_lock_started_ms = now_ms;
  state_.world.latest_spell_ms = state_.world.action_lock_started_ms;
  state_.world.magic_pk_delay_ms = 300;  // 魔法攻击最小间隔 300ms
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_spell now=" << now_ms << " magic=" << intent.magic_id << " x=" << intent.x
        << " y=" << intent.y << " dir=" << static_cast<int>(intent.dir)
        << " target=" << intent.target_actor_id;
    legacy_trace(out.str());
  }
  // 本地预测：立即更新角色动作为施法状态
  if (auto it = state_.world.actors.find(state_.world.self_actor_id);
      it != state_.world.actors.end()) {
    auto& actor = it->second;
    actor.dir = intent.dir;
    actor.current_action = client_v1::ActorActionKind::spell;
    actor.magic_id = intent.magic_id;
    actor.action_target_actor_id = intent.target_actor_id;
    actor.action_target_x = intent.x;
    actor.action_target_y = intent.y;
    actor.action_magic = true;
    actor.action_magic_effect = 0;
    actor.action_magic_effect_type = -1;
    for (const auto& magic : state_.world.magics) {
      if (magic.magic_id == intent.magic_id) {
        actor.action_magic_effect = magic.effect;
        actor.action_magic_effect_type = magic.effect_type;
        break;
      }
    }
    actor.action_started_ms = now_ms;
    actor.action_duration_ms = GameStateStore::action_duration_ms(actor.current_action, 0);
  }
  protocol_.send(intent);
}

// 拾取地面物品请求
void ClientApp::request_pickup(const client_v1::PickupIntent& intent) {
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_pickup now=" << detail::monotonic_ms() << " x=" << intent.x
        << " y=" << intent.y;
    legacy_trace(out.str());
  }
  protocol_.send(intent);
}

// 使用物品请求（药物/卷轴等）：记录使用时间用于动画同步
void ClientApp::request_use_item(const client_v1::UseItemIntent& intent) {
  const auto now_ms = detail::monotonic_ms();
  state_.world.eating_item_make_index = intent.item_make_index;
  state_.world.eating_item_slot = intent.item_slot;
  state_.world.eat_time_ms = now_ms;
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_use_item now=" << now_ms << " make_index=" << intent.item_make_index
        << " slot=" << intent.item_slot << " name=" << intent.name;
    legacy_trace(out.str());
  }
  protocol_.send(intent);
}

// 装备物品请求：步骤 3 只发送最小 Delphi 兼容字段，合法性由服务端最终裁决
void ClientApp::request_equip_item(const client_v1::EquipItemRequest& request) {
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_equip_item now=" << detail::monotonic_ms()
        << " slot=" << request.equipment_slot
        << " make_index=" << request.item_make_index << " name=" << request.name;
    legacy_trace(out.str());
  }
  protocol_.send(request);
}

// 卸下装备请求
void ClientApp::request_unequip_item(const client_v1::UnequipItemRequest& request) {
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_unequip_item now=" << detail::monotonic_ms()
        << " slot=" << request.equipment_slot
        << " make_index=" << request.item_make_index << " name=" << request.name;
    legacy_trace(out.str());
  }
  protocol_.send(request);
}

// 丢弃物品请求（步骤 3 仅保留协议入口，具体地面投放后续接入）
void ClientApp::request_drop_item(const client_v1::DropItemRequest& request) {
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_drop_item now=" << detail::monotonic_ms()
        << " make_index=" << request.item_make_index << " name=" << request.name;
    legacy_trace(out.str());
  }
  protocol_.send(request);
}

// 魔法快捷键变更：服务端返回权威 MagicList 后更新客户端
void ClientApp::request_magic_key_change(const client_v1::MagicKeyChangeRequest& request) {
  if (request.magic_id == 0) {
    return;
  }
  protocol_.send(request);
}

// 聊天发送：对应 Delphi SendSay
void ClientApp::request_chat_send(std::string text) {
  if (text.empty()) {
    return;
  }
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_chat_send now=" << detail::monotonic_ms() << " text=" << text;
    legacy_trace(out.str());
  }
  protocol_.send(client_v1::ChatSend{std::move(text)});
}

// 点击 NPC：对应 Delphi CM_CLICKNPC
void ClientApp::request_npc_click(const std::uint64_t actor_id) {
  if (actor_id == 0) {
    return;
  }
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_npc_click now=" << detail::monotonic_ms() << " actor_id=" << actor_id;
    legacy_trace(out.str());
  }
  protocol_.send(client_v1::NpcClickRequest{actor_id});
}

// NPC 对话链接选择：对应 Delphi CM_MERCHANTDLGSELECT
void ClientApp::request_npc_dialog_select(
    const client_v1::NpcDialogSelectRequest& request) {
  if (request.merchant_id == 0 || request.selection.empty()) {
    return;
  }
  if (legacy_trace_enabled()) {
    std::ostringstream out;
    out << "request_npc_dialog_select now=" << detail::monotonic_ms()
        << " merchant_id=" << request.merchant_id << " selection=" << request.selection;
    legacy_trace(out.str());
  }
  protocol_.send(request);
}

// 商店购买：对应 Delphi CM_USERBUYITEM
void ClientApp::request_merchant_buy(const client_v1::MerchantBuyRequest& request) {
  if (request.merchant_id == 0 || request.item_server_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

// 商店出售：对应 Delphi CM_USERSELLITEM
void ClientApp::request_merchant_sell(const client_v1::MerchantSellRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

// 商店出售询价：对应 Delphi CM_MERCHANTQUERYSELLPRICE
void ClientApp::request_merchant_sell_price(
    const client_v1::MerchantSellPriceRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_repair_price(const client_v1::MerchantRepairPriceRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_repair_item(const client_v1::MerchantRepairRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_storage_deposit(const client_v1::StorageDepositRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_storage_withdraw(const client_v1::StorageWithdrawRequest& request) {
  if (request.merchant_id == 0 || request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_group_mode(const client_v1::GroupModeRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_group_create(const client_v1::GroupCreateRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_group_add(const client_v1::GroupAddMemberRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_group_remove(const client_v1::GroupRemoveMemberRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_trade_try(const client_v1::TradeTryRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_trade_cancel(const client_v1::TradeCancelRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_trade_add_item(const client_v1::TradeAddItemRequest& request) {
  if (request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_trade_remove_item(const client_v1::TradeRemoveItemRequest& request) {
  if (request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_trade_gold(const client_v1::TradeSetGoldRequest& request) {
  if (request.gold < 0) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_trade_accept(const client_v1::TradeAcceptRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_guild_open(const client_v1::GuildOpenRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_guild_home(const client_v1::GuildHomeRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_guild_members(const client_v1::GuildMemberListRequest& request) {
  protocol_.send(request);
}

void ClientApp::request_guild_add(const client_v1::GuildAddMemberRequest& request) {
  if (request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_guild_remove(const client_v1::GuildRemoveMemberRequest& request) {
  if (request.name.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_guild_update_notice(const client_v1::GuildUpdateNoticeRequest& request) {
  if (request.text.empty()) {
    return;
  }
  protocol_.send(request);
}

void ClientApp::request_guild_update_grade(const client_v1::GuildUpdateGradeRequest& request) {
  if (request.text.empty()) {
    return;
  }
  protocol_.send(request);
}

// 请求当前地图的小地图缩略图
void ClientApp::request_minimap(const client_v1::MiniMapRequest& request) {
  protocol_.send(request);
}

// 请求关闭：委托给 handle_close_request 弹出退出确认
void ClientApp::request_close() {
  handle_close_request();
}

// 从 ModernClient/config/client.ini 加载所有配置项
// 包括：网络连接参数、自动播放设置、资源路径、窗口标题
// 如果资源根目录未配置（为空），返回 false 表示初始化失败
bool ClientApp::load_config() {
  const auto path = std::filesystem::absolute(L"ModernClient\\config\\client.ini");
  const auto login_host = read_ini_string(path, L"network", L"login_host", L"127.0.0.1");
  config_.login_host = narrow(login_host);
  config_.login_port = static_cast<std::uint16_t>(read_ini_int(path, L"network", L"login_port", 5600));
  config_.client_build = static_cast<std::uint32_t>(read_ini_int(path, L"network", L"client_build", 1));
  config_.resource_revision =
      static_cast<std::uint32_t>(read_ini_int(path, L"network", L"resource_revision", 1));
  config_.auto_play.enabled = read_ini_bool(path, L"autoplay", L"enabled", false);
  config_.auto_play.create_account =
      read_ini_bool(path, L"autoplay", L"create_account", true);
  config_.auto_play.create_character =
      read_ini_bool(path, L"autoplay", L"create_character", true);
  config_.auto_play.account_id =
      narrow(read_ini_string(path, L"autoplay", L"account_id", L""));
  config_.auto_play.password =
      narrow(read_ini_string(path, L"autoplay", L"password", L""));
  config_.auto_play.display_name =
      narrow(read_ini_string(path, L"autoplay", L"display_name", L""));
  config_.auto_play.character_name =
      narrow(read_ini_string(path, L"autoplay", L"character_name", L""));
  config_.auto_play.job =
      static_cast<std::uint8_t>(read_ini_int(path, L"autoplay", L"job", 0));
  config_.auto_play.sex =
      static_cast<std::uint8_t>(read_ini_int(path, L"autoplay", L"sex", 0));
  config_.auto_play.hair =
      static_cast<std::uint8_t>(read_ini_int(path, L"autoplay", L"hair", 0));
  config_.audio.sound_enabled = read_ini_bool(path, L"audio", L"sound_enabled", true);
  config_.audio.bgm_enabled = read_ini_bool(path, L"audio", L"bgm_enabled", true);
  config_.audio.sound_volume =
      std::clamp(read_ini_int(path, L"audio", L"sound_volume", 100), 0, 100);
  config_.audio.bgm_volume =
      std::clamp(read_ini_int(path, L"audio", L"bgm_volume", 100), 0, 100);
  config_.asset_root = read_ini_string(path, L"assets", L"root", resolve_default_asset_root());
  config_.window_title = read_ini_string(path, L"window", L"title", L"Modern Mir2 Client");
  config_.window_width = kNativeClientWidth;
  config_.window_height = kNativeClientHeight;
  return !config_.asset_root.empty();
}

void ClientApp::maybe_start_auto_play() {
  if (!config_.auto_play.enabled || auto_login_started_ || scenes_.current_id() != SceneId::login) {
    return;
  }
  if (config_.auto_play.account_id.empty() || config_.auto_play.password.empty()) {
    auto_login_started_ = true;
    show_modal(L"Auto Play", L"Missing autoplay account or password in client.ini.");
    return;
  }

  auto_login_started_ = true;
  state_.login.status = L"Auto login requested...";
  request_login(config_.auto_play.account_id, config_.auto_play.password);
}

// 自动播放模式下的角色列表处理逻辑：
//   1. 角色列表为空时，自动创建预设角色
//   2. 有角色时，自动选中匹配的角色名（或第一个）并进入游戏
void ClientApp::handle_auto_character_list() {
  if (!config_.auto_play.enabled) {
    return;
  }

  // 角色列表为空：自动创建角色
  if (state_.lobby.characters.empty()) {
    if (!config_.auto_play.create_character || auto_character_create_requested_) {
      return;
    }
    if (config_.auto_play.character_name.empty()) {
      auto_character_create_requested_ = true;
      show_modal(L"Auto Play", L"Missing autoplay character_name in client.ini.");
      return;
    }

    auto_character_create_requested_ = true;
    state_.login.status = L"Creating autoplay character...";
    protocol_.send(client_v1::CreateCharacterRequest{
        config_.auto_play.character_name, config_.auto_play.job,
        config_.auto_play.sex, config_.auto_play.hair});
    return;
  }

  // 已有角色：如果已经发起过进入请求则跳过
  if (auto_enter_requested_) {
    return;
  }

  // 按名字匹配或默认选第一个
  auto selection = 0;
  for (std::size_t index = 0; index < state_.lobby.characters.size(); ++index) {
    if (!config_.auto_play.character_name.empty() &&
        state_.lobby.characters[index].name == config_.auto_play.character_name) {
      selection = static_cast<int>(index);
      break;
    }
  }

  state_.lobby.selected_index = selection;
  state_.login.status = L"Character ready. Entering world...";
  auto_enter_requested_ = true;
  request_selected_character_enter();
}

// 核心协议事件分发器：在每一帧的 network poll 之后调用
// 处理三种类型的事件：
//   1. ConnectedEvent —— 连接建立后根据 pending_connect_ 发送对应的首个请求
//   2. DisconnectedEvent —— 连接断开处理（游戏中断线弹出重连确认）
//   3. ProtocolFrameEvent —— 协议帧，按 MessageId 解码后分发
void ClientApp::handle_protocol_events() {
  for (auto& event : protocol_.drain_events()) {
    // ---- 连接建立事件 ----
    if (std::holds_alternative<ConnectedEvent>(event)) {
      // 发送客户端握手包（协议版本、构建号、资源修订号）
      protocol_.send(client_v1::ClientHello{client_v1::kProtocolVersion, config_.client_build,
                                            config_.resource_revision, 0});
      // 根据连接类型发送对应的首个业务请求
      if (pending_connect_ == PendingConnect::login) {
        protocol_.send(client_v1::LoginRequest{state_.login.account_id, state_.login.password});
      } else if (pending_connect_ == PendingConnect::create_account) {
        protocol_.send(pending_create_account_);
      } else if (pending_connect_ == PendingConnect::change_password) {
        protocol_.send(pending_change_password_);
      } else if (pending_connect_ == PendingConnect::select_character) {
        request_character_list();
      } else if (pending_connect_ == PendingConnect::game) {
        protocol_.send(
            client_v1::EnterWorldRequest{state_.enter_world_token, config_.client_build,
                                         config_.resource_revision});
        schedule_one_shot_timer(wait_msg_timer_, 5.0f,
                                [this] { wait_msg_timer_tick(L"Still waiting for world entry..."); });
      }
      continue;
    }

    // ---- 连接断开事件 ----
    if (auto* disconnected = std::get_if<DisconnectedEvent>(&event)) {
      cancel_network_wait_timers();
      state_.clear_world_ui_state();
      // 客户端主动发起的断开（reason="client_disconnect"）不弹提示
      if (!disconnected->reason.empty() && disconnected->reason != "client_disconnect") {
        // 游戏中意外掉线：弹出重连确认框
        if (state_.connection_phase == GameStateStore::ConnectionPhase::play &&
            !state_.login.account_id.empty() && !state_.login.password.empty()) {
          show_confirm_modal(L"Disconnected", widen(disconnected->reason) + L". Reconnect?",
                             [this] { request_reconnect(); });
        } else {
          show_modal(L"Disconnected", widen(disconnected->reason));
        }
      }
      continue;
    }

    // ---- 协议消息分发 ----
    auto* frame_event = std::get_if<ProtocolFrameEvent>(&event);
    if (frame_event == nullptr) {
      continue;
    }
    // 收到有效消息就取消网络等待定时器
    cancel_network_wait_timers();

    auto dispatch = [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;

          // 登录结果：成功则等待服务器列表，失败时自动模式尝试自动注册
          if constexpr (std::is_same_v<T, client_v1::LoginResult>) {
            if (value.success) {
              state_.display_name = value.display_name;
              state_.login.login_state = LoginState::lsLogin;
              state_.login.needs_account_update = false;
              state_.login.status = L"Authenticated. Waiting for server list...";
            } else {
              state_.login.needs_account_update = false;
              // 自动播放模式：账号不存在（code=-4）时自动创建
              if (config_.auto_play.enabled && config_.auto_play.create_account &&
                  !auto_account_create_requested_ && value.code == -4) {
                auto_account_create_requested_ = true;
                state_.login.status = L"Account missing. Creating autoplay account...";
                protocol_.send(client_v1::CreateAccountRequest{
                    config_.auto_play.account_id, config_.auto_play.password,
                    make_autoplay_account_profile(config_.auto_play.account_id,
                                                  config_.auto_play.display_name)});
                return;
              }
              state_.login.status = L"Login failed";
              show_modal(L"Login Failed", widen(value.error_message));
            }

          // 服务端要求补充账号资料（如 SSN、生日等）
          } else if constexpr (std::is_same_v<T, client_v1::NeedUpdateAccount>) {
            state_.login.account_id = value.account_id;
            state_.login.account_profile = normalize_account_profile(value.account_id, value.profile);
            state_.login.needs_account_update = true;
            state_.login.login_state = LoginState::lsNewid;
            state_.login.status = L"Account details required.";
            if (config_.auto_play.enabled) {
              protocol_.send(client_v1::UpdateAccountRequest{
                  value.account_id, state_.login.password,
                  make_autoplay_account_profile(value.account_id, config_.auto_play.display_name)});
              state_.login.status = L"Completing autoplay account details...";
              return;
            }
            request_scene_change(SceneId::login);

          // 服务器列表：自动模式或重连时自动选服，否则切换到选服界面
          } else if constexpr (std::is_same_v<T, client_v1::ServerList>) {
            state_.login.needs_account_update = false;
            state_.apply(value);
            if (state_.lobby.servers.empty()) {
              show_modal(L"Server List", L"No game servers are available.");
              return;
            }
            if (login_replay_active_) {
              auto server_name = login_replay_server_name_;
              if (server_name.empty()) {
                server_name = state_.lobby.servers.front().name;
              } else {
                const auto found = std::any_of(
                    state_.lobby.servers.begin(), state_.lobby.servers.end(),
                    [&](const client_v1::ServerEntry& entry) { return entry.name == server_name; });
                if (!found) {
                  server_name = state_.lobby.servers.front().name;
                }
              }
              request_select_server(server_name);
              return;
            }
            if (config_.auto_play.enabled) {
              request_select_server(state_.lobby.servers.front().name);
              return;
            }
            state_.login.status = L"Select a server.";
            request_scene_change(SceneId::server_select);

          // 选服结果：保存角色网关地址并发起连接
          } else if constexpr (std::is_same_v<T, client_v1::SelectServerResult>) {
            if (!value.success) {
              show_modal(L"Select Server Failed", widen(value.error_message));
              return;
            }
            state_.apply(value);
            if (state_.pending_lobby_token.empty() || state_.pending_character_host.empty() ||
                state_.pending_character_port == 0) {
              pending_connect_ = PendingConnect::none;
              state_.login.status = L"Server selection returned an invalid character gateway.";
              show_modal(L"Select Server Failed", L"Character gateway details were missing.");
              return;
            }
            state_.login.status = L"Connecting character gateway...";
            pending_connect_ = PendingConnect::select_character;
            if (!protocol_.connect(value.address, value.port)) {
              pending_connect_ = PendingConnect::none;
              cancel_one_shot_timer(sel_chr_wait_timer_);
              state_.login.status = L"Character gateway connection failed.";
              show_modal(L"Connection Failed", L"Unable to connect to character gateway.");
            }

          // 角色列表：切换到选角场景，自动模式/重连时自动处理
          } else if constexpr (std::is_same_v<T, client_v1::CharacterList>) {
            pending_connect_ = PendingConnect::none;
            state_.apply(value);
            state_.connection_phase = GameStateStore::ConnectionPhase::select_character;
            state_.login.status = L"Character list received.";
            request_scene_change(SceneId::character_select);
            if (login_replay_active_) {
              for (std::size_t index = 0; index < state_.lobby.characters.size(); ++index) {
                if (state_.lobby.characters[index].name == login_replay_character_name_) {
                  state_.lobby.selected_index = static_cast<int>(index);
                  break;
                }
              }
              if (login_replay_enter_selected_ && !state_.lobby.characters.empty()) {
                state_.login.status = L"Reconnected. Entering selected character...";
                request_selected_character_enter();
                return;
              }
              state_.connection_phase = GameStateStore::ConnectionPhase::reselect_character;
              state_.login.status = L"Select a character.";
              login_replay_active_ = false;
              return;
            }
            handle_auto_character_list();

          // 选中角色结果：获取 enter_world_token 和游戏网关地址，连接世界服务器
          } else if constexpr (std::is_same_v<T, client_v1::SelectCharacterResult>) {
            if (!value.success) {
              show_modal(L"Character Select Failed", widen(value.error_message));
              return;
            }
            state_.enter_world_token = value.enter_world_token;
            state_.pending_game_host = value.address;
            state_.pending_game_port = value.port;
            request_scene_change(SceneId::loading);
            state_.connection_phase = GameStateStore::ConnectionPhase::play;
            state_.login.status = L"Connecting game gateway...";
            pending_connect_ = PendingConnect::game;
            if (!protocol_.connect(value.address, value.port)) {
              pending_connect_ = PendingConnect::none;
              cancel_one_shot_timer(wait_msg_timer_);
              state_.login.status = L"Game gateway connection failed.";
              show_modal(L"Connection Failed", L"Unable to connect to world gateway.");
            }

          // 进入世界结果：保存角色出生坐标和 actor_id
          } else if constexpr (std::is_same_v<T, client_v1::EnterWorldResult>) {
            if (!value.success) {
              show_modal(L"Enter World Failed", widen(value.error_message));
              return;
            }
            state_.pending_self_actor_id = value.self_actor_id;
            state_.selected_character = value.character_name;
            state_.pending_spawn_x = value.x;
            state_.pending_spawn_y = value.y;
            state_.connection_phase = GameStateStore::ConnectionPhase::play;
            state_.login.status = L"World admission accepted.";

          // 世界快照：客户端收到完整的初始世界状态，切换到世界场景
          } else if constexpr (std::is_same_v<T, client_v1::WorldSnapshot>) {
            state_.apply(value);
            login_replay_active_ = false;
            login_replay_enter_selected_ = false;
            state_.login_notice = LoginNoticeViewState{};
            request_scene_change(SceneId::world);

          // ---- 以下为世界运行时的增量更新消息 ----
          } else if constexpr (std::is_same_v<T, client_v1::ActorStateDelta>) {
            state_.apply(value);                    // 角色属性增量更新
          } else if constexpr (std::is_same_v<T, client_v1::ActorUpsert>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_actor_upsert now=" << detail::monotonic_ms()
                  << " actor=" << value.actor.actor_id << " name=" << value.actor.name
                  << " type=" << static_cast<int>(value.actor.actor_type)
                  << " x=" << value.actor.x << " y=" << value.actor.y;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 新增或更新角色
          } else if constexpr (std::is_same_v<T, client_v1::ActorAction>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_actor_action now=" << detail::monotonic_ms()
                  << " actor=" << value.actor_id << " kind=" << static_cast<int>(value.kind)
                  << " x=" << value.x << " y=" << value.y
                  << " dir=" << static_cast<int>(value.dir)
                  << " target=" << value.target_actor_id << " legacy=" << value.legacy_ident;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 其他角色动作同步
          } else if constexpr (std::is_same_v<T, client_v1::ActorMagicFire>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_actor_magic_fire now=" << detail::monotonic_ms()
                  << " actor=" << value.actor_id << " target=" << value.target_actor_id
                  << " x=" << value.x << " y=" << value.y
                  << " effect_type=" << static_cast<int>(value.effect_type)
                  << " effect=" << static_cast<int>(value.effect);
              legacy_trace(out.str());
            }
            state_.apply(value);
          } else if constexpr (std::is_same_v<T, client_v1::ActorVitals>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_actor_vitals now=" << detail::monotonic_ms()
                  << " actor=" << value.actor_id << " hp=" << value.hp
                  << " mp=" << value.mp << " damage=" << value.damage
                  << " source=" << value.source_actor_id << " magic=" << value.magic;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 角色血量/蓝量更新
          } else if constexpr (std::is_same_v<T, client_v1::ActorDeath>) {
            state_.apply(value);                    // 角色死亡状态
          } else if constexpr (std::is_same_v<T, client_v1::MagicList>) {
            state_.apply(value);                    // 已习得魔法列表
          } else if constexpr (std::is_same_v<T, client_v1::SelfAbility>) {
            state_.apply(value);                    // 主角 HUD 能力摘要
          } else if constexpr (std::is_same_v<T, client_v1::SelfAbilityDetail>) {
            state_.apply(value);                    // 主角完整能力摘要
          } else if constexpr (std::is_same_v<T, client_v1::MiniMapData>) {
            state_.apply(value);                    // 小地图数据
          } else if constexpr (std::is_same_v<T, client_v1::BagSnapshot>) {
            state_.apply(value);                    // 完整背包镜像
          } else if constexpr (std::is_same_v<T, client_v1::InventoryAdd>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_inventory_add now=" << detail::monotonic_ms()
                  << " slot=" << value.entry.slot << " make=" << value.entry.item.make_index
                  << " name=" << value.entry.item.name;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 背包新增
          } else if constexpr (std::is_same_v<T, client_v1::InventoryUpdate>) {
            state_.apply(value);                    // 背包更新
          } else if constexpr (std::is_same_v<T, client_v1::InventoryRemove>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_inventory_remove now=" << detail::monotonic_ms()
                  << " slot=" << value.slot;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 背包移除
          } else if constexpr (std::is_same_v<T, client_v1::InventoryClearRange>) {
            state_.apply(value);                    // 背包范围清理
          } else if constexpr (std::is_same_v<T, client_v1::EquipmentSnapshot>) {
            state_.apply(value);                    // 装备栏镜像
          } else if constexpr (std::is_same_v<T, client_v1::ActionAck>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_action_ack now=" << detail::monotonic_ms() << " ok=" << value.ok
                  << " server_time=" << value.server_time_ms;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 服务端动作确认（解锁 action_locked）
          } else if constexpr (std::is_same_v<T, client_v1::GroundItemAdd>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_ground_item_add now=" << detail::monotonic_ms()
                  << " id=" << value.item.object_id << " x=" << value.item.x
                  << " y=" << value.item.y << " name=" << value.item.name;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 地面新增物品
          } else if constexpr (std::is_same_v<T, client_v1::GroundItemRemove>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_ground_item_remove now=" << detail::monotonic_ms()
                  << " id=" << value.object_id << " x=" << value.x << " y=" << value.y;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 地面物品消失
          } else if constexpr (std::is_same_v<T, client_v1::UseItemResult>) {
            if (legacy_trace_enabled()) {
              std::ostringstream out;
              out << "recv_use_item_result now=" << detail::monotonic_ms()
                  << " ok=" << value.ok;
              legacy_trace(out.str());
            }
            state_.apply(value);                    // 使用物品结果
          } else if constexpr (std::is_same_v<T, client_v1::ChatLine>) {
            state_.apply(value);                    // 底部聊天板行
          } else if constexpr (std::is_same_v<T, client_v1::ActorSay>) {
            state_.apply(value);                    // 角色头顶说话
          } else if constexpr (std::is_same_v<T, client_v1::NpcDialog>) {
            state_.apply(value);                    // NPC/商人对话
          } else if constexpr (std::is_same_v<T, client_v1::NpcDialogClose>) {
            state_.apply(value);                    // NPC/商人对话关闭
          } else if constexpr (std::is_same_v<T, client_v1::MerchantGoodsList>) {
            state_.apply(value);                    // 商店商品列表
          } else if constexpr (std::is_same_v<T, client_v1::MerchantPriceResult>) {
            state_.apply(value);                    // 商人询价结果
          } else if constexpr (std::is_same_v<T, client_v1::MerchantRepairPriceResult>) {
            state_.apply(value);                    // 修理询价结果
          } else if constexpr (std::is_same_v<T, client_v1::StorageList>) {
            state_.apply(value);                    // 仓库列表
          } else if constexpr (std::is_same_v<T, client_v1::GroupState>) {
            state_.apply(value);                    // 组队窗口状态
          } else if constexpr (std::is_same_v<T, client_v1::TradeState>) {
            state_.apply(value);                    // 交易窗口状态
          } else if constexpr (std::is_same_v<T, client_v1::GuildState>) {
            state_.apply(value);                    // 行会窗口状态

          // ---- 通知/公告类消息 ----
          } else if constexpr (std::is_same_v<T, client_v1::LoginNotice>) {
            state_.login_notice.title = value.title;
            state_.login_notice.text = value.text;
            state_.login.status = L"Login notice received.";
            request_scene_change(SceneId::login_notice);
          } else if constexpr (std::is_same_v<T, client_v1::SysMessage>) {
            state_.apply(value);                    // 系统信息（顶部短提示 + 聊天板）
          } else if constexpr (std::is_same_v<T, client_v1::Notice>) {
            show_modal(widen(value.title), widen(value.text));  // 弹出公告对话框
          } else if constexpr (std::is_same_v<T, client_v1::DisconnectReason>) {
            show_modal(L"Disconnected", widen(value.text));     // 服务端发起的断开原因

          // ---- 心跳应答 ----
          } else if constexpr (std::is_same_v<T, client_v1::Pong>) {
            (void)value;  // 不需要特殊处理

          // ---- 账号操作结果 ----
          } else if constexpr (std::is_same_v<T, client_v1::CreateAccountResult>) {
            if (!value.success) {
              state_.login.login_state = LoginState::lsNewidRetry;
              state_.login.status = L"Account creation failed. Please retry.";
              request_scene_change(SceneId::login);
              show_modal(L"Create Account Failed", widen(value.error_message));
              return;
            }
            state_.login.login_state = LoginState::lsLogin;
            // 非自动模式：通知用户注册成功
            if (!config_.auto_play.enabled || !auto_account_create_requested_) {
              state_.login.status = L"Account created. Login is ready.";
              show_modal(L"Account Created", L"Registration succeeded. Use Login to continue.");
              return;
            }
            // 自动模式：创建成功后立即用刚注册的账号登录
            state_.login.status = L"Autoplay account ready. Logging in...";
            protocol_.send(client_v1::LoginRequest{config_.auto_play.account_id,
                                                   config_.auto_play.password});

          } else if constexpr (std::is_same_v<T, client_v1::UpdateAccountResult>) {
            if (!value.success) {
              state_.login.needs_account_update = true;
              state_.login.login_state = LoginState::lsNewid;
              state_.login.status = L"Account update failed.";
              show_modal(L"Update Account Failed", widen(value.error_message));
              return;
            }
            state_.login.needs_account_update = false;
            state_.login.login_state = LoginState::lsLogin;
            state_.login.status = L"Account details updated. Waiting for server list...";

          } else if constexpr (std::is_same_v<T, client_v1::ChangePasswordResult>) {
            if (!value.success) {
              show_modal(L"Operation Failed", widen(value.error_message));
              state_.login.status = L"Password change failed.";
              state_.login.login_state = LoginState::lsChgpw;
              return;
            }
            state_.login.status = L"Password changed.";
            state_.login.login_state = LoginState::lsLogin;
            show_modal(L"Password Changed", L"Password updated successfully.");

          } else if constexpr (std::is_same_v<T, client_v1::CreateCharacterResult>) {
            if (!value.success) {
              state_.login.status = L"Character creation failed. Please retry.";
              show_modal(L"Create Character Failed", widen(value.error_message));
              return;
            }
            state_.login.status = L"Character created. Refreshing lobby...";
            request_character_list();

          } else if constexpr (std::is_same_v<T, client_v1::DeleteCharacterResult>) {
            if (!value.success) {
              show_modal(L"Delete Character Failed", widen(value.error_message));
              return;
            }
            state_.login.status = L"Character deleted. Refreshing lobby...";
            request_character_list();
          }
        };

    auto decode_and_dispatch = [&]<typename T>() -> bool {
      auto value = client_v1::decode_message<T>(frame_event->frame);
      if (!value.has_value()) {
        return false;
      }
      dispatch(*value);
      return true;
    };

    auto decoded = false;
    switch (frame_event->frame.message_id) {
      case client_v1::MessageId::login_result:
        decoded = decode_and_dispatch.operator()<client_v1::LoginResult>();
        break;
      case client_v1::MessageId::need_update_account:
        decoded = decode_and_dispatch.operator()<client_v1::NeedUpdateAccount>();
        break;
      case client_v1::MessageId::server_list:
        decoded = decode_and_dispatch.operator()<client_v1::ServerList>();
        break;
      case client_v1::MessageId::select_server_result:
        decoded = decode_and_dispatch.operator()<client_v1::SelectServerResult>();
        break;
      case client_v1::MessageId::character_list:
        decoded = decode_and_dispatch.operator()<client_v1::CharacterList>();
        break;
      case client_v1::MessageId::select_character_result:
        decoded = decode_and_dispatch.operator()<client_v1::SelectCharacterResult>();
        break;
      case client_v1::MessageId::enter_world_result:
        decoded = decode_and_dispatch.operator()<client_v1::EnterWorldResult>();
        break;
      case client_v1::MessageId::world_snapshot:
        decoded = decode_and_dispatch.operator()<client_v1::WorldSnapshot>();
        break;
      case client_v1::MessageId::actor_state_delta:
        decoded = decode_and_dispatch.operator()<client_v1::ActorStateDelta>();
        break;
      case client_v1::MessageId::actor_upsert:
        decoded = decode_and_dispatch.operator()<client_v1::ActorUpsert>();
        break;
      case client_v1::MessageId::actor_action:
        decoded = decode_and_dispatch.operator()<client_v1::ActorAction>();
        break;
      case client_v1::MessageId::actor_magic_fire:
        decoded = decode_and_dispatch.operator()<client_v1::ActorMagicFire>();
        break;
      case client_v1::MessageId::actor_vitals:
        decoded = decode_and_dispatch.operator()<client_v1::ActorVitals>();
        break;
      case client_v1::MessageId::actor_death:
        decoded = decode_and_dispatch.operator()<client_v1::ActorDeath>();
        break;
      case client_v1::MessageId::magic_list:
        decoded = decode_and_dispatch.operator()<client_v1::MagicList>();
        break;
      case client_v1::MessageId::self_ability:
        decoded = decode_and_dispatch.operator()<client_v1::SelfAbility>();
        break;
      case client_v1::MessageId::self_ability_detail:
        decoded = decode_and_dispatch.operator()<client_v1::SelfAbilityDetail>();
        break;
      case client_v1::MessageId::mini_map_data:
        decoded = decode_and_dispatch.operator()<client_v1::MiniMapData>();
        break;
      case client_v1::MessageId::bag_snapshot:
        decoded = decode_and_dispatch.operator()<client_v1::BagSnapshot>();
        break;
      case client_v1::MessageId::inventory_add:
        decoded = decode_and_dispatch.operator()<client_v1::InventoryAdd>();
        break;
      case client_v1::MessageId::inventory_update:
        decoded = decode_and_dispatch.operator()<client_v1::InventoryUpdate>();
        break;
      case client_v1::MessageId::inventory_remove:
        decoded = decode_and_dispatch.operator()<client_v1::InventoryRemove>();
        break;
      case client_v1::MessageId::inventory_clear_range:
        decoded = decode_and_dispatch.operator()<client_v1::InventoryClearRange>();
        break;
      case client_v1::MessageId::equipment_snapshot:
        decoded = decode_and_dispatch.operator()<client_v1::EquipmentSnapshot>();
        break;
      case client_v1::MessageId::action_ack:
        decoded = decode_and_dispatch.operator()<client_v1::ActionAck>();
        break;
      case client_v1::MessageId::ground_item_add:
        decoded = decode_and_dispatch.operator()<client_v1::GroundItemAdd>();
        break;
      case client_v1::MessageId::ground_item_remove:
        decoded = decode_and_dispatch.operator()<client_v1::GroundItemRemove>();
        break;
      case client_v1::MessageId::use_item_result:
        decoded = decode_and_dispatch.operator()<client_v1::UseItemResult>();
        break;
      case client_v1::MessageId::chat_line:
        decoded = decode_and_dispatch.operator()<client_v1::ChatLine>();
        break;
      case client_v1::MessageId::actor_say:
        decoded = decode_and_dispatch.operator()<client_v1::ActorSay>();
        break;
      case client_v1::MessageId::npc_dialog:
        decoded = decode_and_dispatch.operator()<client_v1::NpcDialog>();
        break;
      case client_v1::MessageId::npc_dialog_close:
        decoded = decode_and_dispatch.operator()<client_v1::NpcDialogClose>();
        break;
      case client_v1::MessageId::merchant_goods_list:
        decoded = decode_and_dispatch.operator()<client_v1::MerchantGoodsList>();
        break;
      case client_v1::MessageId::merchant_price_result:
        decoded = decode_and_dispatch.operator()<client_v1::MerchantPriceResult>();
        break;
      case client_v1::MessageId::merchant_repair_price_result:
        decoded = decode_and_dispatch.operator()<client_v1::MerchantRepairPriceResult>();
        break;
      case client_v1::MessageId::storage_list:
        decoded = decode_and_dispatch.operator()<client_v1::StorageList>();
        break;
      case client_v1::MessageId::group_state:
        decoded = decode_and_dispatch.operator()<client_v1::GroupState>();
        break;
      case client_v1::MessageId::trade_state:
        decoded = decode_and_dispatch.operator()<client_v1::TradeState>();
        break;
      case client_v1::MessageId::guild_state:
        decoded = decode_and_dispatch.operator()<client_v1::GuildState>();
        break;
      case client_v1::MessageId::login_notice:
        decoded = decode_and_dispatch.operator()<client_v1::LoginNotice>();
        break;
      case client_v1::MessageId::sys_message:
        decoded = decode_and_dispatch.operator()<client_v1::SysMessage>();
        break;
      case client_v1::MessageId::notice:
        decoded = decode_and_dispatch.operator()<client_v1::Notice>();
        break;
      case client_v1::MessageId::disconnect_reason:
        decoded = decode_and_dispatch.operator()<client_v1::DisconnectReason>();
        break;
      case client_v1::MessageId::pong:
        decoded = decode_and_dispatch.operator()<client_v1::Pong>();
        break;
      case client_v1::MessageId::create_account_result:
        decoded = decode_and_dispatch.operator()<client_v1::CreateAccountResult>();
        break;
      case client_v1::MessageId::update_account_result:
        decoded = decode_and_dispatch.operator()<client_v1::UpdateAccountResult>();
        break;
      case client_v1::MessageId::change_password_result:
        decoded = decode_and_dispatch.operator()<client_v1::ChangePasswordResult>();
        break;
      case client_v1::MessageId::create_character_result:
        decoded = decode_and_dispatch.operator()<client_v1::CreateCharacterResult>();
        break;
      case client_v1::MessageId::delete_character_result:
        decoded = decode_and_dispatch.operator()<client_v1::DeleteCharacterResult>();
        break;
      default:
        break;
    }
    if (!decoded) {
      protocol_.disconnect("protocol_decode_error");
    }
  }
}

// 驱动所有系统中的定时器（每帧在主循环中调用）
// 执行顺序：timer1 -> 鼠标轮询 -> 网络等待超时 -> 选角等待 -> 操作等待
//          -> 每分钟任务 -> 外挂检测 -> 心跳发送
void ClientApp::run_timers(const float delta_seconds) {
  timer1_tick();  // 兼容 Delph Timer1，目前为空
  run_repeating_timer(mouse_timer_, delta_seconds, [this] { mouse_timer_tick(); });
  run_one_shot_timer(wait_msg_timer_, delta_seconds);    // 网络等待超时提示
  run_one_shot_timer(sel_chr_wait_timer_, delta_seconds); // 选角等待超时提示
  run_one_shot_timer(cmd_timer_, delta_seconds);          // 建角/删角操作超时
  run_repeating_timer(min_timer_, delta_seconds, [this] { min_timer_tick(); });
  run_repeating_timer(check_hack_timer_, delta_seconds, [this] { check_hack_timer_tick(); });
  run_repeating_timer(send_time_timer_, delta_seconds, [this] { send_time_timer_tick(); });
}

// 驱动循环定时器：累计 delta 时间，达到间隔时执行回调
// 使用减法方式（而非重置）确保即使帧间隔大于定时周期也不会丢失 tick
void ClientApp::run_repeating_timer(RepeatingTimer& timer, const float delta_seconds,
                                    const std::function<void()>& callback) {
  if (!timer.enabled || timer.interval_seconds <= 0.0f) {
    return;
  }
  timer.elapsed_seconds += delta_seconds;
  while (timer.elapsed_seconds >= timer.interval_seconds) {
    timer.elapsed_seconds -= timer.interval_seconds;
    callback();
  }
}

// 驱动单次定时器：倒计时归零时触发一次回调并自动清除
void ClientApp::run_one_shot_timer(OneShotTimer& timer, const float delta_seconds) {
  if (!timer.enabled) {
    return;
  }
  timer.remaining_seconds -= delta_seconds;
  if (timer.remaining_seconds > 0.0f) {
    return;
  }
  auto callback = std::move(timer.callback);
  timer = OneShotTimer{};  // 清除定时器状态
  if (callback) {
    callback();
  }
}

// 安排一个单次定时器：delay_seconds 后执行回调
void ClientApp::schedule_one_shot_timer(OneShotTimer& timer, const float delay_seconds,
                                        std::function<void()> callback) {
  timer.remaining_seconds = std::max(0.0f, delay_seconds);
  timer.enabled = true;
  timer.callback = std::move(callback);
}

// 取消单次定时器（直接重置为空状态）
void ClientApp::cancel_one_shot_timer(OneShotTimer& timer) {
  timer = OneShotTimer{};
}

// 取消所有网络相关的等待定时器（收到网络事件时调用）
void ClientApp::cancel_network_wait_timers() {
  cancel_one_shot_timer(wait_msg_timer_);
  cancel_one_shot_timer(sel_chr_wait_timer_);
  cancel_one_shot_timer(cmd_timer_);
}

// Timer1 兼容钩子：原 Delphi 客户端在每个空闲帧 DrainSocket 的定时器
// 现代客户端在 protocol_.poll() 中完成了相同的工作，此处保留为空函数
void ClientApp::timer1_tick() {
}

// 鼠标状态轮询钩子：鼠标事件已由 Win32 消息驱动采集
// 保留此函数保持与 Delphi 定时器边界的对应关系
void ClientApp::mouse_timer_tick() {
}

// 网络等待超时提示：在状态栏显示等待消息
void ClientApp::wait_msg_timer_tick(const std::wstring& message) {
  state_.login.status = message;
}

// 角色选择等待超时提示
void ClientApp::sel_chr_wait_timer_tick(const std::wstring& message) {
  state_.login.status = message;
}

// 操作命令（建角/删角）等待超时提示
void ClientApp::cmd_timer_tick(const std::wstring& message) {
  state_.login.status = message;
}

// 每分钟定时器：预留的延时清理入口（如地面物品过期、角色动画 GC）
void ClientApp::min_timer_tick() {
}

// 外挂检测定时器（1 秒间隔）：速度外挂检测功能预留
// 当前协议映射尚未包含速度检测上报，待后续协议层扩展后启用
void ClientApp::check_hack_timer_tick() {
}

// 发送心跳 Ping（30 秒间隔）：仅在游戏连接阶段（ConnectionPhase::play）有效
void ClientApp::send_time_timer_tick() {
  if (state_.connection_phase != GameStateStore::ConnectionPhase::play || !protocol_.connected()) {
    return;
  }
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  protocol_.send(client_v1::Ping{static_cast<std::uint64_t>(now)});
}

// 处理窗口关闭请求：如果已有模态对话框则忽略，否则弹出退出确认
void ClientApp::handle_close_request() {
  if (state_.modal.visible) {
    return;
  }
  state_.login.login_state = LoginState::lsCloseAll;
  show_confirm_modal(L"Exit", L"Close the client?", [this] { confirm_exit(); });
}

// 确认退出：发送客户端主动断开信号，立即关闭窗口
void ClientApp::confirm_exit() {
  protocol_.disconnect("client_disconnect");
  window_.close_now();
}

// 断线重连：检查凭据有效性后通过登录重放流程重新进入游戏
void ClientApp::request_reconnect() {
  if (state_.login.account_id.empty() || state_.login.password.empty()) {
    show_modal(L"Reconnect", L"Login credentials are not available.");
    return;
  }
  begin_login_replay(true);
}

// 开始登录重放流程：保存当前凭据 -> 清空世界状态 -> 断开当前连接 -> 重新走登录
// enter_selected_character 为 true 时在收到角色列表后自动选角进入，
// 为 false 时停留在角色选择界面让用户重新选择
void ClientApp::begin_login_replay(const bool enter_selected_character) {
  const auto account_id = state_.login.account_id;
  const auto password = state_.login.password;
  login_replay_active_ = true;
  login_replay_enter_selected_ = enter_selected_character;
  login_replay_server_name_ = state_.lobby.selected_server_name;
  login_replay_character_name_ = state_.selected_character;
  // 如果 selected_character 为空但 lobby 中有选中索引，从角色列表取名字
  if (login_replay_character_name_.empty() && state_.lobby.selected_index >= 0 &&
      state_.lobby.selected_index < static_cast<int>(state_.lobby.characters.size())) {
    login_replay_character_name_ =
        state_.lobby.characters[static_cast<std::size_t>(state_.lobby.selected_index)].name;
  }
  state_.world = WorldViewState{};  // 清空世界状态
  state_.connection_phase = enter_selected_character
                                ? GameStateStore::ConnectionPhase::login
                                : GameStateStore::ConnectionPhase::reselect_character;
  protocol_.disconnect("client_disconnect");
  request_login(account_id, password);
}

// 弹出信息模态对话框：使用 Prguse.wil 中的对话框精灵背景 + "确定"按钮
// @param title 对话框标题（由 GameStateStore 的 modal 状态管理）
// @param message 对话框文本内容
void ClientApp::show_modal(const std::wstring& title, const std::wstring& message) {
  state_.show_modal(title, message);
  modal_ui_.clear();
  modal_confirm_action_ = {};
  modal_has_cancel_ = false;
  modal_enter_confirms_ = true;
  // 计算对话框居中位置
  const auto dialog_rect =
      centered_rect(assets_.get_frame(ArchiveId::prguse, kMessageDialogIndex),
                    kNativeClientWidth, kNativeClientHeight, 360, 180);
  // 创建全屏根节点（用于屏蔽点击穿透）
  auto* root = modal_ui_.set_root<ui::UiNode>(RectI{0, 0, kNativeClientWidth, kNativeClientHeight});
  // 添加"确定"按钮
  auto* button = add_modal_button(root, assets_, kMessageOkButtonIndex,
                                  dialog_rect.x + (dialog_rect.w - 88) / 2,
                                  dialog_rect.y + 126);
  button->on_click = [this] {
    state_.hide_modal();
    modal_ui_.clear();
    modal_confirm_action_ = {};
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
  };
}

void ClientApp::show_info_modal(const std::wstring& title, const std::wstring& message) {
  show_modal(title, message);
}

// 弹出确认模态对话框：包含"是"和"取消"两个按钮
// @param on_confirm 用户点击"是"时的回调
void ClientApp::show_confirm_modal(const std::wstring& title, const std::wstring& message,
                                   std::function<void()> on_confirm) {
  state_.show_modal(title, message);
  modal_ui_.clear();
  modal_confirm_action_ = std::move(on_confirm);
  modal_has_cancel_ = true;
  modal_enter_confirms_ = true;
  const auto dialog_rect =
      centered_rect(assets_.get_frame(ArchiveId::prguse, kMessageDialogIndex),
                    kNativeClientWidth, kNativeClientHeight, 360, 180);
  auto* root = modal_ui_.set_root<ui::UiNode>(RectI{0, 0, kNativeClientWidth, kNativeClientHeight});
  // "是"按钮
  auto* ok_button =
      add_modal_button(root, assets_, kMessageYesButtonIndex, dialog_rect.x + 104,
                       dialog_rect.y + 126);
  ok_button->on_click = [this] {
    auto on_confirm = std::move(modal_confirm_action_);
    state_.hide_modal();
    modal_ui_.clear();
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
    if (on_confirm) {
      on_confirm();
    }
  };
  // "取消"按钮：如果是在 lsCloseAll（退出确认）状态下取消，恢复登录状态
  auto* cancel_button =
      add_modal_button(root, assets_, kMessageCancelButtonIndex, dialog_rect.x + 210,
                       dialog_rect.y + 126);
  cancel_button->on_click = [this] {
    state_.hide_modal();
    modal_ui_.clear();
    modal_confirm_action_ = {};
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
    if (state_.login.login_state == LoginState::lsCloseAll) {
      state_.login.login_state = LoginState::lsLogin;
    }
  };
}

// 弹出高风险确认对话框：必须点击 Yes，Enter 不触发确认
void ClientApp::show_destructive_confirm_modal(const std::wstring& title,
                                               const std::wstring& message,
                                               std::function<void()> on_confirm) {
  state_.show_modal(title, message);
  modal_ui_.clear();
  modal_confirm_action_ = std::move(on_confirm);
  modal_has_cancel_ = true;
  modal_enter_confirms_ = false;
  const auto dialog_rect =
      centered_rect(assets_.get_frame(ArchiveId::prguse, kMessageDialogIndex),
                    kNativeClientWidth, kNativeClientHeight, 360, 180);
  auto* root = modal_ui_.set_root<ui::UiNode>(RectI{0, 0, kNativeClientWidth, kNativeClientHeight});

  auto close_without_confirm = [this] {
    state_.hide_modal();
    modal_ui_.clear();
    modal_confirm_action_ = {};
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
  };

  auto* yes_button =
      add_modal_button(root, assets_, kMessageYesButtonIndex, dialog_rect.x + 34,
                       dialog_rect.y + 126);
  yes_button->on_click = [this] {
    auto on_confirm = std::move(modal_confirm_action_);
    state_.hide_modal();
    modal_ui_.clear();
    modal_confirm_action_ = {};
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
    if (on_confirm) {
      on_confirm();
    }
  };

  auto* no_button =
      add_modal_button(root, assets_, kMessageNoButtonIndex, dialog_rect.x + 136,
                       dialog_rect.y + 126);
  no_button->on_click = close_without_confirm;

  auto* cancel_button =
      add_modal_button(root, assets_, kMessageCancelButtonIndex, dialog_rect.x + 238,
                       dialog_rect.y + 126);
  cancel_button->on_click = close_without_confirm;
}

// 渲染模态对话框：在场景之上绘制对话框背景精灵和 UI 按钮
void ClientApp::render_modal() {
  if (!state_.modal.visible) {
    return;
  }
  if (modal_enter_confirms_ &&
      (mapped_input_.key_pressed[VK_RETURN] || mapped_input_.enter_pressed)) {
    auto on_confirm = std::move(modal_confirm_action_);
    state_.hide_modal();
    modal_ui_.clear();
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
    if (on_confirm) {
      on_confirm();
    }
    return;
  }
  if (mapped_input_.key_pressed[VK_ESCAPE] && modal_has_cancel_) {
    state_.hide_modal();
    modal_ui_.clear();
    modal_confirm_action_ = {};
    modal_has_cancel_ = false;
    modal_enter_confirms_ = true;
    if (state_.login.login_state == LoginState::lsCloseAll) {
      state_.login.login_state = LoginState::lsLogin;
    }
    return;
  }
  const auto dialog_frame = assets_.get_frame(ArchiveId::prguse, kMessageDialogIndex);
  const auto dialog_rect =
      centered_rect(dialog_frame, renderer_.logical_width(), renderer_.logical_height(), 360, 180);
  draw_sprite(renderer_, dialog_frame, dialog_rect.x, dialog_rect.y);
  if (!state_.modal.title.empty()) {
    const auto title_x =
        dialog_rect.x + std::max(39, (dialog_rect.w - renderer_.measure_text_width(state_.modal.title)) / 2);
    draw_legacy_bold_text(renderer_, title_x, dialog_rect.y + 20, state_.modal.title, 0xFFFFFFFFU);
  }
  auto y = dialog_rect.y + 38;
  for (const auto& line : split_modal_lines(state_.modal.message)) {
    if (!line.empty()) {
      draw_legacy_bold_text(renderer_, dialog_rect.x + 39, y, line, 0xFFFFFFFFU);
    }
    y += 14;
  }
  modal_ui_.set_asset_manager(&assets_);
  modal_ui_.update(mapped_input_);
  modal_ui_.paint(renderer_);
}

}  // namespace mir2::client
