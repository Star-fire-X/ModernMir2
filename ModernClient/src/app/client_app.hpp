// ============================================================
// Mir2 现代客户端 — ClientApp 类声明
// 职责：游戏客户端的主控类，管理窗口、渲染、网络、场景、音频等
//       所有子系统的生命周期，以及客户端主循环
//
// 传奇客户端架构说明：
// 经典传奇（Mir2）的客户端是一个单线程 Win32 应用程序，核心是一个
// while 循环驱动的消息泵 + 帧更新模式。ClientApp 对应原 Delphi
// 客户端中 ClMain.pas / ClFunc.pas 的全局状态和主循环逻辑。
// 原版客户端使用 TServerSocket + TClientSocket（Windows Socket）
// 进行网络通信，使用 DirectDraw 7 进行 2D 渲染。
//
// 本客户端的主循环流程（每帧）：
//   1. pump_messages()       — 处理 Win32 窗口消息
//   2. protocol_.poll()      — 非阻塞网络 I/O 轮询
//   3. handle_protocol_events() — 处理收到的网络事件
//   4. refresh_mapped_input()   — 更新输入状态
//   5. update()              — 驱动场景逻辑更新
//   6. render()              — 渲染场景并呈现到屏幕
// ============================================================
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "app/legacy_frame_scheduler.hpp"
#include "assets/asset_manager.hpp"
#include "audio/audio_service.hpp"
#include "game/game_state.hpp"
#include "platform/win32_window.hpp"
#include "protocol/protocol_client.hpp"
#include "render/software_renderer.hpp"
#include "scene/scenes.hpp"
#include "ui/ui.hpp"

namespace mir2::client {

/// 客户端主应用类，拥有所有子系统，驱动单线程主循环
/// 生命周期：构造(空状态) -> initialize(初始化子系统) -> run(主循环) -> 析构
class ClientApp {
 public:
  ClientApp();

  /// 初始化所有子系统
  /// 顺序：加载配置 -> 创建窗口 -> 初始化渲染器 -> 资源管理器 -> 场景
  /// @return false 表示初始化失败（窗口创建失败/D3D11 初始化失败等）
  bool initialize();
  /// 进入主消息循环，直到窗口关闭返回 0
  int run();

  // ---- 以下为各场景/UI 触发的请求方法 ----
  // 这些方法由场景（Scene）或 UI 控件在响应玩家操作时调用，
  // 将操作意图传递给 ClientApp，ClientApp 再转换为网络消息发送到服务端

  void request_scene_change(SceneId id);
  void request_login(const std::string& account_id, const std::string& password);
  void request_create_account(const std::string& account_id, const std::string& password,
                              const client_v1::AccountProfile& profile);
  void request_change_password(const std::string& account_id, const std::string& password,
                               const std::string& new_password);
  void request_update_account(const std::string& account_id, const std::string& password,
                              const client_v1::AccountProfile& profile);
  void request_select_server(const std::string& server_name);
  void request_character_list();
  void request_create_character(const std::string& name, std::uint8_t job, std::uint8_t sex,
                                std::uint8_t hair);
  void request_delete_selected_character();
  void request_selected_character_enter();
  void acknowledge_login_notice();
  void request_reselect_character();
  void request_move(int x, int y, client_v1::MoveMode mode);
  void request_action(const client_v1::ActionIntent& intent);
  void request_spell(const client_v1::SpellIntent& intent);
  void request_pickup(const client_v1::PickupIntent& intent);
  void request_use_item(const client_v1::UseItemIntent& intent);
  void request_equip_item(const client_v1::EquipItemRequest& request);
  void request_unequip_item(const client_v1::UnequipItemRequest& request);
  void request_drop_item(const client_v1::DropItemRequest& request);
  void request_drop_gold(const client_v1::DropGoldRequest& request);
  void request_magic_key_change(const client_v1::MagicKeyChangeRequest& request);
  void request_chat_send(std::string text);
  void request_npc_click(std::uint64_t actor_id);
  void request_npc_dialog_select(const client_v1::NpcDialogSelectRequest& request);
  void request_merchant_buy(const client_v1::MerchantBuyRequest& request);
  void request_merchant_sell(const client_v1::MerchantSellRequest& request);
  void request_merchant_sell_price(const client_v1::MerchantSellPriceRequest& request);
  void request_repair_price(const client_v1::MerchantRepairPriceRequest& request);
  void request_repair_item(const client_v1::MerchantRepairRequest& request);
  void request_storage_deposit(const client_v1::StorageDepositRequest& request);
  void request_storage_withdraw(const client_v1::StorageWithdrawRequest& request);
  void request_group_mode(const client_v1::GroupModeRequest& request);
  void request_group_create(const client_v1::GroupCreateRequest& request);
  void request_group_add(const client_v1::GroupAddMemberRequest& request);
  void request_group_remove(const client_v1::GroupRemoveMemberRequest& request);
  void request_trade_try(const client_v1::TradeTryRequest& request);
  void request_trade_cancel(const client_v1::TradeCancelRequest& request);
  void request_trade_add_item(const client_v1::TradeAddItemRequest& request);
  void request_trade_remove_item(const client_v1::TradeRemoveItemRequest& request);
  void request_trade_gold(const client_v1::TradeSetGoldRequest& request);
  void request_trade_accept(const client_v1::TradeAcceptRequest& request);
  void request_guild_open(const client_v1::GuildOpenRequest& request);
  void request_guild_home(const client_v1::GuildHomeRequest& request);
  void request_guild_members(const client_v1::GuildMemberListRequest& request);
  void request_guild_add(const client_v1::GuildAddMemberRequest& request);
  void request_guild_remove(const client_v1::GuildRemoveMemberRequest& request);
  void request_guild_update_notice(const client_v1::GuildUpdateNoticeRequest& request);
  void request_guild_update_grade(const client_v1::GuildUpdateGradeRequest& request);
  void request_minimap(const client_v1::MiniMapRequest& request);
  void request_revive();
  void request_close();
  void show_info_modal(const std::wstring& title, const std::wstring& message);
  [[nodiscard]] HWND window_handle() const { return window_.handle(); }

 private:
  /// 当前正在进行的连接类型
  /// 传奇客户端的网络层分为多个独立的网关连接：
  /// 登录网关(LoginGate)、角色网关(SelGate)、游戏网关(RunGate)。
  /// 客户端根据当前所处阶段连接到不同的网关，每个阶段发送
  /// 的第一个消息包类型不同。此枚举跟踪当前正在建立的连接
  /// 属于哪个阶段，以便连接建立后发送正确的初始化包。
  enum class PendingConnect {
    none,            ///< 无待建立的连接
    login,           ///< 正在连接到登录网关（将发送 LoginRequest）
    create_account,  ///< 正在连接到注册网关（将发送 CreateAccountRequest）
    change_password, ///< 正在连接到修改密码网关
    select_character,///< 正在连接到角色网关（将发送 CharacterListRequest）
    game             ///< 正在连接到游戏网关（将发送 EnterWorldRequest）
  };

  /// 循环定时器，每隔 interval_seconds 触发一次回调
  /// 对应原传奇客户端的定时器组件（如 TTimer），用于驱动
  /// 各种周期性检查（鼠标状态、心跳包、外挂检测等）
  struct RepeatingTimer {
    float interval_seconds{0.0f};
    float elapsed_seconds{0.0f};
    bool enabled{false};
  };

  /// 单次定时器，延迟指定秒数后触发一次回调
  /// 用于网络操作超时提示，如登录等待时间过长时显示提示消息
  struct OneShotTimer {
    float remaining_seconds{0.0f};
    bool enabled{false};
    std::function<void()> callback{};
  };

  bool load_config();
  void maybe_start_auto_play();
  void handle_auto_character_list();
  void handle_protocol_events(ClientContext& context);
  void flush_scene_change_if_pending(ClientContext& context);
  void capture_ui_input(ClientContext& context);
  void dwin_process(ClientContext& context);
  void process_modal_input();
  [[nodiscard]] bool can_draw_frame() const;
  /// 将窗口输入的像素坐标转换为逻辑坐标（考虑缩放）
  void refresh_mapped_input();
  /// 驱动所有定时器的 Tick
  void run_timers(float delta_seconds);
  void run_repeating_timer(RepeatingTimer& timer, float delta_seconds,
                           const std::function<void()>& callback);
  void run_one_shot_timer(OneShotTimer& timer, float delta_seconds);
  void schedule_one_shot_timer(OneShotTimer& timer, float delay_seconds,
                               std::function<void()> callback);
  void cancel_one_shot_timer(OneShotTimer& timer);
  void cancel_network_wait_timers();
  // ---- 各定时器的 Tick 回调 ----
  void timer1_tick();
  void mouse_timer_tick();
  void wait_msg_timer_tick(const std::wstring& message);   ///< 网络等待超时提示
  void sel_chr_wait_timer_tick(const std::wstring& message);
  void cmd_timer_tick(const std::wstring& message);
  void min_timer_tick();             ///< 每分钟定时器（用于延时清理/状态更新）
  void check_hack_timer_tick();      ///< 外挂检测定时器（预留，对应原版 SpeedHack 检测）
  void send_time_timer_tick();       ///< 心跳 Ping 定时器（每 30 秒）
  void handle_close_request();
  void confirm_exit();
  void request_reconnect();
  /// 重放登录流程：断线重连时使用已有凭据重新走一遍登录 -> 选服 -> 选角色
  void begin_login_replay(bool enter_selected_character);
  void show_modal(const std::wstring& title, const std::wstring& message);
  void show_confirm_modal(const std::wstring& title, const std::wstring& message,
                          std::function<void()> on_confirm);
  void show_destructive_confirm_modal(const std::wstring& title, const std::wstring& message,
                                      std::function<void()> on_confirm);
  void render_modal();

  // ---- 子系统实例 ----
  ClientConfig config_{};           ///< 从 client.ini 加载的配置
  GameStateStore state_{};          ///< 全局游戏状态（客户端数据层）
  Win32Window window_{};            ///< Win32 窗口（无边框，800x600）
  SoftwareRenderer renderer_{};     ///< 软件渲染器（软件表面 + D3D11 纹理上传）
  ProtocolClient protocol_{};       ///< 网络协议客户端（非阻塞 TCP）
  AssetManager assets_{};           ///< 资源管理器（WIL/WIX 精灵 + 地图文件）
  AudioService audio_{};            ///< 音频服务（目前为空桩）
  SceneManager scenes_{};           ///< 场景管理器（驱动场景切换）
  LegacyFrameScheduler frame_scheduler_{};  ///< Delphi AppOnIdle 帧内阶段调度器
  ui::UiTree modal_ui_{};           ///< 模态对话框 UI 树（叠加在场景之上）
  std::function<void()> modal_confirm_action_{};  ///< 确认弹窗的 Yes/Enter 回调
  bool modal_has_cancel_{false};                  ///< 当前弹窗是否可用 Esc/Cancel 关闭
  bool modal_enter_confirms_{true};                ///< Enter 是否触发当前弹窗确认

  InputState mapped_input_{};       ///< 经过坐标映射后的输入状态
  SceneId requested_scene_{SceneId::boot};  ///< 请求切换的目标场景 ID
  bool scene_change_pending_{false};        ///< 是否有待执行的场景切换

  PendingConnect pending_connect_{PendingConnect::none};
  client_v1::CreateAccountRequest pending_create_account_{};
  client_v1::ChangePasswordRequest pending_change_password_{};
  enum class PendingCharacterRefreshTrace {
    none,
    create,
    delete_character
  };
  PendingCharacterRefreshTrace pending_character_refresh_trace_{PendingCharacterRefreshTrace::none};

  // ---- 自动播放（测试/演示模式）状态 ----
  // 自动播放是专为测试环境设计的功能，当 client.ini 中配置了
  // AutoPlay=true 时，客户端会自动执行登录/建角/进游戏等操作
  bool auto_login_started_{false};
  bool auto_account_create_requested_{false};
  bool auto_character_create_requested_{false};
  bool auto_enter_requested_{false};

  // ---- 登录重连状态 ----
  // 当与服务端的连接意外断开时，客户端会尝试自动重连，
  // 使用之前成功登录的凭据重新走一遍登录流程
  bool login_replay_active_{false};
  bool login_replay_enter_selected_{false};
  std::string login_replay_server_name_{};
  std::string login_replay_character_name_{};

  // ---- 系统定时器 ----
  // 这些定时器仿真原传奇客户端中的 Windows 定时器机制。
  // 原版客户端将很多操作放在定时器中驱动（如鼠标检测、心跳）
  RepeatingTimer mouse_timer_{0.05f, 0.0f, true};         ///< 鼠标状态轮询，50ms 间隔
  RepeatingTimer min_timer_{60.0f, 0.0f, true};           ///< 每分钟执行（清理等）
  RepeatingTimer check_hack_timer_{1.0f, 0.0f, true};     ///< 外挂检测检查，1 秒间隔
  RepeatingTimer send_time_timer_{30.0f, 0.0f, true};     ///< 心跳包发送，30 秒间隔
  OneShotTimer wait_msg_timer_{};     ///< 网络等待提示定时器（登录/选服阶段超时提示）
  OneShotTimer sel_chr_wait_timer_{}; ///< 选角等待提示定时器（列表加载超时提示）
  OneShotTimer cmd_timer_{};          ///< 操作等待提示定时器（建角/删角操作超时提示）
};

}  // namespace mir2::client
