// ============================================================
// Mir2 现代客户端 — 协议客户端（非阻塞 TCP 网络层）
// 职责：管理 TCP Socket 连接，非阻塞 Select 轮询，
//       发送 client_v1::Message 帧，接收并解码协议事件
//
// 传奇客户端网络架构说明：
// 经典传奇客户端的网络层基于 Windows Socket（阻塞模式），
// 使用 TClientSocket 组件与网关服务器通信。客户端与三种
// 网关服务器交互：
//   1. LoginGate（登录网关，端口 5600）
//   2. SelGate（角色网关，端口 5800）
//   3. RunGate（游戏网关，端口 5690）
// 响应超过 100 字节的消息使用分段接收，前 2 字节指示长度。
//
// 本实现使用非阻塞模式 + select() 零超时轮询替代原版的
// 阻塞模式，避免阻塞主循环。消息格式改为长度前缀帧
// （length-prefixed frame），保持与新版协议兼容。
// ============================================================
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <winsock2.h>

#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2::client {

/// TCP 连接成功建立事件（非阻塞 connect 完成后触发）
struct ConnectedEvent {};

/// TCP 连接断开事件，附带断开原因字符串
/// 原因包括：remote_closed（对端关闭）、connect_failed（连接失败）、
/// socket_select_failed（select 错误）、socket_read/write_failed（读写错误）、
/// protocol_decode_error（协议解码错误）
struct DisconnectedEvent {
  std::string reason{};
};

/// 协议帧事件：网络层只负责拆帧，业务层按 MessageId 做 typed decode
struct ProtocolFrameEvent {
  client_v1::Frame frame{};
};

/// 协议事件联合体：可以是连接事件、断开事件或业务消息
/// 主循环通过 drain_events() 统一获取所有待处理事件
using ProtocolEvent = std::variant<ConnectedEvent, DisconnectedEvent, ProtocolFrameEvent>;

/// 非阻塞 TCP 协议客户端
/// 使用 select() 实现零超时轮询，通过事件队列与主循环通信
///
/// 工作流程：
///   1. connect() 发起非阻塞连接
///   2. 每帧调用 poll() 检测 socket 状态
///   3. poll() 内部检测可读/可写/连接完成事件
///   4. 主循环调用 drain_events() 取出所有事件
///   5. 事件中有 client_v1::Message 时由 handle_protocol_events() 分发
class ProtocolClient {
 public:
#ifdef MIR2_CLIENT_TESTING
  struct ConnectAttempt {
    std::string host{};
    std::uint16_t port{0};
  };
#endif

  ProtocolClient();
  ~ProtocolClient();

  /// 发起非阻塞 TCP 连接
  /// @param host 主机地址（IPv4）
  /// @param port 端口号
  /// @return false 表示立即失败（无效地址/socket 创建失败）
  bool connect(const std::string& host, std::uint16_t port);
  /// 断开连接（可附带原因），清空收发缓冲区
  void disconnect(const std::string& reason = "client_disconnect");
  /// 非阻塞轮询：使用 select(0 超时) 检测 socket 可读/可写/连接完成状态
  /// 应在主循环中每帧调用一次
  void poll();
  /// 模板方法：直接发送指定类型的消息，自动编码为帧
  template <typename T>
  void send(const T& message) {
    if (state_ == State::disconnected) {
      return;
    }
#ifdef MIR2_CLIENT_TESTING
    if (test_mode_) {
      sent_frames_.push_back(client_v1::make_frame(message, next_sequence_++));
      return;
    }
#endif
    // make_frame 将消息包装成帧结构，encode_frame 添加长度前缀
    outbound_frames_.push_back(
        client_v1::encode_frame(client_v1::make_frame(message, next_sequence_++)));
  }
  /// 取出并清空所有待处理的事件（主循环每帧调用一次）
  [[nodiscard]] std::vector<ProtocolEvent> drain_events();
  /// 是否已完全建立连接（异步 connect 已完成）
  [[nodiscard]] bool connected() const { return state_ == State::connected; }
  /// 是否正在连接中（非阻塞 connect 尚未完成）
  [[nodiscard]] bool connecting() const { return state_ == State::connecting; }

#ifdef MIR2_CLIENT_TESTING
  void enable_test_mode_for_test();
  void complete_connect_for_test();
  template <typename T>
  void push_message_for_test(const T& message) {
    events_.push_back(ProtocolFrameEvent{client_v1::make_frame(message, 0)});
  }
  void push_disconnected_for_test(std::string reason);
  [[nodiscard]] std::vector<client_v1::Frame> drain_sent_frames_for_test();
  [[nodiscard]] const std::vector<ConnectAttempt>& connect_attempts_for_test() const {
    return connect_attempts_;
  }
#endif

 private:
  /// 连接状态枚举
  enum class State { disconnected, connecting, connected };

  /// 完成异步连接：通过 getsockopt SO_ERROR 检查连接结果
  bool finish_connect();
  /// 处理 socket 可读事件：循环 recv -> 累积缓冲区 -> 解码帧 -> 压入事件队列
  void handle_readable();
  /// 处理 socket 可写事件：从发送队列取帧并循环 send
  void handle_writable();
  /// 压入断开事件（供 disconnect 和内部错误使用）
  void push_disconnect(std::string reason);

  SOCKET socket_{INVALID_SOCKET};              ///< TCP socket 句柄
  State state_{State::disconnected};           ///< 当前连接状态
  std::uint32_t next_sequence_{1};             ///< 消息序列号（用于请求-应答匹配）
  std::vector<std::uint8_t> inbound_buffer_{}; ///< 接收缓冲区（累积未完成帧的数据）
  std::deque<std::vector<std::uint8_t>> outbound_frames_{}; ///< 发送队列（已编码的帧，FIFO）
  std::size_t current_write_offset_{0};        ///< 当前发送帧的已发送字节偏移
  std::vector<ProtocolEvent> events_{};        ///< 待主循环处理的事件队列
  bool winsock_ready_{false};                  ///< WSAStartup 是否成功初始化
#ifdef MIR2_CLIENT_TESTING
  bool test_mode_{false};
  std::vector<ConnectAttempt> connect_attempts_{};
  std::vector<client_v1::Frame> sent_frames_{};
#endif
};

}  // namespace mir2::client
