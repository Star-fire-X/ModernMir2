/**
 * @file world_service.hpp
 * @brief 世界服务头文件
 *
 * @details 定义 WorldService 类，作为游戏世界逻辑的核心协调模块。
 *          负责驱动游戏主循环(tick)、管理玩家会话、处理进出游戏流程、
 *          协调持久化操作、管理公会/城堡状态、以及通过 LegacyFrameDriver
 *          兼容原版 Delphi 的帧同步机制。
 *
 * @note 这是系统中最为核心和复杂的模块，集成了 LogicRuntime 游戏逻辑引擎、
 *       LegacyFrameDriver 旧版帧驱动、消息总线通信和状态机管理等功能。
 */

#pragma once

#include <chrono>
#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/module.hpp"
#include "protocol/canonical_login_state.hpp"
#include "world/legacy_frame_driver.hpp"
#include "world/logic_runtime.hpp"

namespace mir2 {

/**
 * @class WorldService
 * @brief 世界服务模块
 *
 * @details 负责游戏世界的整体运行，主要职责包括：
 *
 *          1. 游戏主循环(tick)：固定间隔驱动游戏逻辑更新
 *          2. 会话生命周期：管理从"进入游戏"到"离开游戏"的全流程
 *          3. 消息路由：将网关数据包路由到 LogicRuntime 处理
 *          4. 帧同步：通过 LegacyFrameDriver 实现与旧版兼容的帧同步
 *          5. 持久化协调：管理角色加载/保存、公会/城堡数据刷新
 *          6. 安全防护：消息序列检查(防重放)、登录状态门控
 *
 *          游戏主循环包含以下阶段：
 *          1. RunSocketRun — 刷新网关输出事件
 *          2. DecodeIdSocket — 处理消息总线输入
 *          3. UserEngineExecuteRun — 执行游戏逻辑 tick
 *          4. EventManagerRun — 执行事件管理器
 *          5. ServerMessageRun — 处理服务器消息
 */
class WorldService : public Module {
 public:
  WorldService() = default;

  /**
   * @brief 析构函数，自动停止服务并等待线程结束
   */
  ~WorldService() override {
    stop();
    join();
  }

  /**
   * @brief 获取模块名称
   * @return "world_service"
   */
  [[nodiscard]] std::string name() const override { return "world_service"; }

  /**
   * @brief 启动世界服务
   * @param context 宿主上下文
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止世界服务
   */
  void stop() override;

  /**
   * @brief 等待工作线程结束
   */
  void join() override;

  /**
   * @brief 获取服务快照
   * @return 包含运行状态、地图数、会话数、帧信息、公会/城堡状态等详细信息的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

#ifdef MIR2_ENABLE_TEST_HOOKS
  /// @name 测试钩子(仅在 MIR2_ENABLE_TEST_HOOKS 编译时启用)
  /// @{
  void attach_context_for_test(HostContext& context);
  void initialize_runtime_for_test(const HostConfig& config);
  void enqueue_gate_event_for_test(SessionEvent event);
  void seed_session_sequence_for_test(std::uint64_t session_id, std::uint64_t session_seq);
  [[nodiscard]] std::size_t legacy_session_inbox_size_for_test(std::uint64_t session_id) const;
  [[nodiscard]] std::vector<std::uint64_t> legacy_session_inbox_sequences_for_test(
      std::uint64_t session_id) const;
  [[nodiscard]] RuntimeDispatch tick_runtime_for_test(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch run_legacy_socket_stage_for_test(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch process_ingress_batch_for_test(WorldIngressBatch& batch);
  /// @}
#endif

 private:
  /**
   * @struct PendingLoad
   * @brief 待加载的角色信息
   *
   * @details 当玩家发送进入游戏请求后，服务器开始加载角色数据，
   *          在此过程中暂存会话和角色信息。
   */
  struct PendingLoad {
    std::uint64_t session_id{0};            ///< 会话ID
    std::string gateway{"game_gateway"};    ///< 网关名称
    std::string account_id{};               ///< 账号ID
    std::string character_name{};           ///< 角色名
    std::int32_t certification{0};          ///< 认证凭据
    CanonicalLoginStage stage{CanonicalLoginStage::entering_game}; ///< 当前阶段
  };

  /**
   * @struct Admission
   * @brief 游戏世界准入记录
   */
  struct Admission {
    std::string account_id{};           ///< 账号ID
    std::string character_name{};       ///< 角色名
    std::int32_t certification{0};      ///< 认证凭据
    CanonicalLoginStage stage{CanonicalLoginStage::character_selected}; ///< 当前阶段
  };

  /**
   * @brief 工作线程主循环
   */
  void run();

  /**
   * @brief 请求刷新城堡对话框上下文
   */
  void request_castle_dialog_context_refresh();

  /**
   * @brief 处理消息总线的输入批次
   * @param batch 输入批次
   * @return 处理结果，包括待发送事件和持久化请求
   */
  [[nodiscard]] RuntimeDispatch process_ingress_batch(WorldIngressBatch& batch);

  /**
   * @brief 检查输入消息的序列号是否可以接受
   * @param ingress 输入消息
   * @param dispatch 输出分发(用于记录审计)
   * @return true 如果序列号有效且可接受
   */
  [[nodiscard]] bool accept_ingress_sequence(const WorldIngressMessage& ingress,
                                             RuntimeDispatch& dispatch);

  /// @name 事件处理函数
  /// @{
  [[nodiscard]] RuntimeDispatch handle_session_event(const SessionEvent& event);
  [[nodiscard]] RuntimeDispatch handle_logic_command(const LogicCommand& command);
  [[nodiscard]] RuntimeDispatch handle_persist_result(const PersistResult& result);
  /// @}

  /**
   * @brief 将网关事件放入待发送队列
   * @param dispatch 运行时分发数据
   */
  void queue_gate_events(RuntimeDispatch& dispatch);

  /**
   * @brief 运行遗留协议的 Socket 阶段(发送网关事件)
   * @param now_ms 当前时间戳(毫秒)
   * @return 处理结果
   */
  [[nodiscard]] RuntimeDispatch run_legacy_socket_stage(std::uint64_t now_ms);

  /**
   * @brief 运行服务器消息阶段
   * @param now_ms 当前时间戳(毫秒)
   * @return 处理结果
   */
  [[nodiscard]] RuntimeDispatch run_server_message_stage(std::uint64_t now_ms);

  /**
   * @brief 向网关发送事件
   * @param event 会话事件
   * @return true 发送成功
   */
  bool post_gate_event(SessionEvent& event);

  /**
   * @brief 分配角色保存版本号
   * @param dispatch 运行时分发数据
   */
  void assign_character_save_versions(RuntimeDispatch& dispatch);

  /**
   * @brief 刷新分发数据(发送事件、审计、持久化请求等)
   * @param dispatch 运行时分发数据
   */
  void flush_dispatch(RuntimeDispatch dispatch);

  HostContext* context_{nullptr};                              ///< 宿主上下文指针
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};             ///< 消息总线端点
  std::thread worker_{};                                       ///< 工作线程
  std::atomic_bool running_{false};                            ///< 运行状态标志
  std::unique_ptr<LogicRuntime> runtime_{};                    ///< 游戏逻辑运行时
  LegacyFrameDriver legacy_frame_driver_{};                    ///< 遗留帧驱动
  mutable std::mutex legacy_frame_mutex_{};                    ///< 帧跟踪数据的互斥锁
  LegacyFrameTrace legacy_frame_trace_{};                      ///< 帧跟踪数据
  bool legacy_frame_seen_{false};                              ///< 是否已记录帧跟踪数据
  std::unordered_map<std::string, PendingLoad> pending_loads_{}; ///< 待加载的角色表
  std::unordered_map<std::int32_t, Admission> admissions_{};   ///< 准入表，键为认证凭据
  std::unordered_map<std::uint64_t, Admission> active_sessions_{}; ///< 活跃会话表
  std::unordered_map<std::string, std::uint64_t> active_accounts_{}; ///< 活跃账号表
  std::unordered_map<std::string, std::uint64_t> character_save_versions_{}; ///< 角色保存版本号表
  std::unordered_map<std::uint64_t, std::string> session_gateways_{}; ///< 会话对应的网关名
  std::unordered_map<std::uint64_t, std::uint64_t> session_sequence_watermarks_{}; ///< 会话序列号水位线
  std::uint64_t next_ingress_seq_{0};                          ///< 下一个输入序列号
  std::uint64_t current_frame_now_ms_{0};                      ///< 当前帧的时间戳(毫秒)
  mutable std::mutex gate_events_mutex_{};                     ///< 网关事件队列互斥锁
  std::deque<SessionEvent> pending_gate_events_{};             ///< 待发送的网关事件队列
  std::uint64_t run_socket_last_flushed_{0};                   ///< 上次刷新的数量
  std::uint64_t run_socket_last_remaining_{0};                 ///< 上次剩余的待发送事件数
  std::uint64_t run_socket_last_ms_{0};                        ///< 上次刷新耗时(毫秒)
  CastleDialogContext castle_dialog_context_{};                 ///< 城堡对话框上下文
  GuildCastleSnapshot guild_castle_snapshot_{};                 ///< 公会城堡快照
  std::chrono::steady_clock::time_point next_castle_context_refresh_{}; ///< 下次城堡上下文刷新时间点
  std::uint64_t castle_context_refresh_count_{0};              ///< 城堡上下文刷新次数
  std::uint64_t offline_guild_result_count_{0};                ///< 离线公会操作结果计数
  std::uint64_t offline_guild_route_count_{0};                 ///< 离线公会路由计数
  std::uint64_t offline_guild_error_count_{0};                 ///< 离线公会错误计数
  bool castle_context_refresh_in_flight_{false};               ///< 城堡上下文刷新是否进行中
};

}  // namespace mir2
