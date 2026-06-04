/**
 * @file game_session.hpp
 * @brief 游戏世界会话抽象
 *
 * @details 本文件定义了 GameSession 类，管理与旧版客户端（Legacy 协议）
 * 之间的网络会话。GameSession 负责：
 * 1. 异步读写 Legacy 协议数据包（使用 LegacyProtocolCodec 编解码）
 * 2. 数据包分发（将接收到的数据包转发给 GatewayServiceBase）
 * 3. 支持延迟发送、排空后关闭、暂停读取等高级功能
 * 4. 过载保护（通过 note_backpressure 追踪积压次数）
 *
 * GameSession 使用 LegacyProtocolCodec 处理 #...#! 帧格式的 Legacy 协议，
 * 与 ClientV1Session 对应，形成双协议支持架构。
 *
 * @note 数据包在读取时通过 LegacyProtocolCodec::drain_packets() 从原始字节流中
 *       提取完整帧，再调用 owner_.forward_packet() 进行业务处理。
 * @see ClientV1Session
 * @see GatewayServiceBase
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "asio.hpp"
#include "core/messages.hpp"

namespace mir2 {

class GatewayServiceBase;

/**
 * @class GameSession
 * @brief 管理单个旧版客户端连接的游戏会话
 *
 * @details 管理旧版 Legacy 协议客户端的 TCP 连接全生命周期。
 * 使用 asio 异步模型，所有操作在套接字的执行器上下文中执行。
 * 支持数据包延迟发送、暂停读取（如等待场景切换完成）、
 * 排空后关闭（发送完积压数据再断线）等功能。
 */
class GameSession : public std::enable_shared_from_this<GameSession> {
 public:
  /**
   * @brief 构造函数
   * @param socket 已接受的 TCP 套接字
   * @param owner 网关服务基类的引用
   */
  GameSession(asio::ip::tcp::socket socket, GatewayServiceBase& owner);

  /**
   * @brief 启动会话，开始接收数据包
   * @param session_id 分配给此会话的标识符
   */
  void start(std::uint64_t session_id);

  /**
   * @brief 立即发送一个 Legacy 数据包
   * @param packet 要发送的 Legacy 协议数据包
   */
  void deliver(const LegacyPacket& packet);

  /**
   * @brief 延迟发送一个 Legacy 数据包
   * @param packet 要发送的数据包
   * @param delay 发送前的延迟时间
   */
  void deliver(const LegacyPacket& packet, std::chrono::milliseconds delay);

  /**
   * @brief 发送数据包并在指定延迟后关闭连接
   * @details 发送完数据包后，等待 delay 毫秒再关闭连接。
   *          适用于需要先向客户端发送最后一条消息再断开连接的场景。
   * @param packet 要发送的最后一条数据包
   * @param delay 关闭前的延迟时间
   * @param reason 关闭原因
   */
  void deliver_and_close(const LegacyPacket& packet, std::chrono::milliseconds delay,
                         std::string reason);

  /**
   * @brief 立即关闭连接（不发送额外数据包）
   * @param reason 关闭原因
   */
  void close(const std::string& reason);

  /**
   * @brief 暂停数据读取指定时间
   * @details 在延迟期间暂停 do_read 循环，适用于场景切换等需要
   *          暂时停止接收客户端指令的场景。延迟结束后自动恢复读取。
   * @param delay 暂停持续时间
   */
  void pause_for(std::chrono::milliseconds delay);

  /**
   * @brief 记录一次过载事件并判断是否需要断开连接
   * @details 每次调用增加过载计数，当计数超过阈值时返回 true，
   *          提示调用者应考虑断开此连接。
   * @param disconnect_threshold 过载断开阈值
   * @return true 过载计数已超过阈值，应断开连接
   */
  [[nodiscard]] bool note_backpressure(std::size_t disconnect_threshold);

 private:
  /** @brief 启动异步读取操作 */
  void do_read();

  /** @brief 执行异步写入操作 */
  void do_write();

  /** @brief 在数据排空后计划关闭连接 */
  void schedule_close();

  asio::ip::tcp::socket socket_;           ///< TCP 套接字
  GatewayServiceBase& owner_;              ///< 网关服务所有者
  asio::steady_timer pause_timer_;         ///< 暂停读取定时器
  asio::steady_timer close_timer_;         ///< 关闭延迟定时器
  std::uint64_t session_id_{0};            ///< 会话标识符
  std::string peer_address_{};             ///< 客户端地址
  std::array<std::uint8_t, 4096> read_buffer_{};           ///< 套接字读取缓冲区
  std::vector<std::uint8_t> inbound_buffer_{};             ///< 入站数据累积缓冲区
  std::deque<std::vector<std::uint8_t>> outbound_frames_{}; ///< 出站帧队列
  bool write_in_progress_{false};  ///< 是否正在写入
  bool paused_{false};             ///< 是否暂停读取
  bool closed_{false};             ///< 是否已关闭
  bool close_after_flush_{false};  ///< 是否在排空后关闭
  std::chrono::milliseconds close_delay_{0};  ///< 关闭延迟时间
  std::string close_reason_{};     ///< 关闭原因
  std::size_t overload_strikes_{0}; ///< 过载计数
};

}  // namespace mir2
