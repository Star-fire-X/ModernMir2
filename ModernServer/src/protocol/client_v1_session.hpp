/**
 * @file client_v1_session.hpp
 * @brief Client v1 网络会话管理
 *
 * @details 本文件定义了 Client v1 协议的网络会话类。
 * Client v1 使用基于帧（Frame）的二进制协议，与 Legacy 协议的 #...#! 文本帧格式不同。
 *
 * 主要组件：
 * - ClientV1SessionOwner：会话拥有者接口，定义连接/断开/消息到达的回调
 * - ClientV1Session：单个 TCP 连接的会话管理，负责读写帧、序列号管理、
 *   延迟发送、断线通知等
 *
 * @note 该类使用 asio::strand 确保所有操作在同一个执行线程上下文中进行，
 *       避免并发问题。读取缓冲区和发送队列都在 strand 内部管理。
 * @warning send_disconnect_and_close() 发送断线原因帧后标记关闭，
 *          等待待发送数据排空后再真正关闭连接。
 */

#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "asio.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

/**
 * @class ClientV1SessionOwner
 * @brief Client v1 会话拥有者的抽象接口
 *
 * @details 任何需要管理 ClientV1Session 的类都应继承此接口。
 * 它定义了三个核心事件回调：连接建立、连接断开和消息到达。
 * 这种设计将网络层与业务逻辑层解耦，会话本身只负责网络传输，
 * 而消息处理由拥有者实现。
 */
class ClientV1SessionOwner {
 public:
  virtual ~ClientV1SessionOwner() = default;

  /**
   * @brief 当客户端建立连接时调用
   * @param session_id 分配的会话标识符
   * @param peer_address 客户端地址字符串
   */
  virtual void on_client_v1_connected(std::uint64_t session_id,
                                      const std::string& peer_address) = 0;

  /**
   * @brief 当客户端断开连接时调用
   * @param session_id 会话标识符
   * @param peer_address 客户端地址字符串
   * @param reason 断开原因描述
   */
  virtual void on_client_v1_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                         const std::string& reason) = 0;

  /**
   * @brief 当收到客户端消息时调用
   * @param session_id 会话标识符
   * @param peer_address 客户端地址字符串
   * @param sequence 消息序列号
   * @param message 解码后的 Client v1 协议消息
   */
  virtual void on_client_v1_message(std::uint64_t session_id, const std::string& peer_address,
                                    std::uint32_t sequence,
                                    const client_v1::Message& message) = 0;
};

/**
 * @class ClientV1Session
 * @brief Client v1 协议的 TCP 会话管理
 *
 * @details 管理单个客户端 TCP 连接的全生命周期，包括：
 * 1. 异步读取帧数据并解码为 client_v1::Message
 * 2. 异步发送编码后的帧给客户端
 * 3. 序列号管理（递增分配）
 * 4. 支持延迟发送
 * 5. 优雅断线（发送 DisconnectReason 帧后再关闭）
 *
 * @note 使用 std::enable_shared_from_this 确保异步操作期间会话对象不会提前析构。
 */
class ClientV1Session : public std::enable_shared_from_this<ClientV1Session> {
 public:
  /**
   * @brief 构造函数
   * @param socket 已接受的 TCP 套接字
   * @param owner 会话拥有者引用
   */
  ClientV1Session(asio::ip::tcp::socket socket, ClientV1SessionOwner& owner);

  /**
   * @brief 启动会话，开始读取客户端帧数据
   * @param session_id 为此会话分配的标识符
   */
  void start(std::uint64_t session_id);

  /**
   * @brief 立即发送一条消息
   * @param message 要发送的消息
   */
  void send(const client_v1::Message& message);

  /**
   * @brief 延迟发送一条消息
   * @param message 要发送的消息
   * @param delay 发送前的延迟时间
   */
  void send(const client_v1::Message& message, std::chrono::milliseconds delay);

  /**
   * @brief 发送单帧数据
   * @param frame 要发送的帧
   */
  void send_frame(const client_v1::Frame& frame);

  /**
   * @brief 批量发送帧数据
   * @param frames 要发送的帧列表
   */
  void send_frames(const std::vector<client_v1::Frame>& frames);

  /**
   * @brief 延迟批量发送帧数据
   * @param frames 要发送的帧列表
   * @param delay 发送前的延迟时间
   */
  void send_frames(const std::vector<client_v1::Frame>& frames,
                   std::chrono::milliseconds delay);

  /**
   * @brief 发送断线原因帧并关闭连接
   * @details 发送 DisconnectReason 帧给客户端后，等待待发送数据排空再关闭。
   * @param code 断线原因码
   * @param reason 断线原因描述
   */
  void send_disconnect_and_close(std::uint16_t code, std::string reason);

  /**
   * @brief 立即关闭连接（不发送断线帧）
   * @param reason 关闭原因描述
   */
  void close(std::string reason);

 private:
  /** @brief 启动异步读取操作 */
  void do_read();

  /** @brief 执行异步写入操作，发送队列中的下一帧 */
  void do_write();

  /**
   * @brief 将断线帧加入发送队列，并标记待发送数据排空后关闭连接
   * @param code 断线原因码
   * @param reason 断线原因描述
   */
  void queue_disconnect_and_close(std::uint16_t code, std::string reason);

  /**
   * @brief 通知拥有者连接已关闭，并清理套接字资源
   * @param reason 关闭原因描述
   */
  void notify_closed(const std::string& reason);

  asio::ip::tcp::socket socket_;                          ///< TCP 套接字
  asio::strand<asio::any_io_executor> strand_;            ///< 执行串行化保护
  ClientV1SessionOwner& owner_;                           ///< 会话拥有者
  asio::steady_timer close_timer_;                        ///< 关闭定时器
  std::uint64_t session_id_{0};                           ///< 会话标识符
  std::string peer_address_{"unknown"};                   ///< 客户端地址
  std::uint32_t next_sequence_{1};                        ///< 下一个帧序列号
  std::array<std::uint8_t, 8192> read_buffer_{};          ///< 套接字读取缓冲区
  std::vector<std::uint8_t> inbound_buffer_{};            ///< 入站数据累积缓冲区
  std::deque<std::vector<std::uint8_t>> outbound_frames_{}; ///< 出站帧队列
  bool writing_{false};           ///< 是否正在执行写入操作
  bool closed_{false};            ///< 连接是否已关闭
  bool close_after_write_{false}; ///< 是否在写入排空后关闭
  std::string close_reason_{};    ///< 关闭原因
};

}  // namespace mir2
