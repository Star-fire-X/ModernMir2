/**
 * @file client_v1_gateway_service_base.hpp
 * @brief Client v1 网关服务基类头文件
 *
 * @details 定义 ClientV1GatewayServiceBase 类，作为新协议(Client v1)
 *          网关服务的抽象基类。提供基于 ASIO 的 TCP 服务器框架、会话管理、
 *          消息序列检查和消息发送功能。与旧版 GatewayServiceBase 类似，
 *          但使用 ClientV1Session 和 client_v1::Message 协议。
 *
 * @note 该基类继承了 ClientV1SessionOwner 接口，作为所有 ClientV1Session
 *       的所有者，处理连接/断开/消息事件的回调。
 */

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "asio.hpp"
#include "config/models.hpp"
#include "core/module.hpp"
#include "protocol/client_v1_session.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

/**
 * @class ClientV1GatewayServiceBase
 * @brief Client v1 网关服务基类
 *
 * @details 提供新协议网关的公共功能：
 *          - 基于 ASIO 的异步 TCP 服务器
 *          - 客户端会话管理(创建、销毁、查找)
 *          - 消息序列号检查(防止重放攻击)
 *          - 提供了 send_message/send_frame/disconnect 等发送接口
 *
 *          子类需要实现 binding() 获取端口配置，以及 handle_message()、
 *          handle_connected()、handle_disconnected() 三个消息处理函数。
 *
 * @see ClientV1LoginGatewayService
 * @see ClientV1GameGatewayService
 */
class ClientV1GatewayServiceBase : public Module, public ClientV1SessionOwner {
 public:
  /**
   * @brief 构造函数
   * @param module_name 模块名称
   */
  explicit ClientV1GatewayServiceBase(std::string module_name);

  /// @brief 析构函数，自动停止服务并等待线程结束
  ~ClientV1GatewayServiceBase() override;

  /**
   * @brief 获取模块名称
   * @return 模块名称字符串
   */
  [[nodiscard]] std::string name() const override { return module_name_; }

  /**
   * @brief 启动网关服务
   * @param context 宿主上下文
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止网关服务
   */
  void stop() override;

  /**
   * @brief 等待所有工作线程结束
   */
  void join() override;

  /**
   * @brief 获取服务快照信息
   * @return 包含运行状态和会话数量的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

  // --- ClientV1SessionOwner 接口实现 ---

  /**
   * @brief 客户端连接回调
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   */
  void on_client_v1_connected(std::uint64_t session_id, const std::string& peer_address) override;

  /**
   * @brief 客户端断开连接回调
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param reason 断开原因
   */
  void on_client_v1_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                 const std::string& reason) override;

  /**
   * @brief 收到客户端消息回调
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param sequence 消息序列号
   * @param message 反序列化后的消息
   */
  void on_client_v1_message(std::uint64_t session_id, const std::string& peer_address,
                            std::uint32_t sequence,
                            const client_v1::Message& message) override;

 protected:
  /**
   * @brief 获取端口绑定配置(纯虚函数)
   * @param context 宿主上下文
   * @return 端口绑定配置
   */
  virtual PortBinding binding(const HostContext& context) const = 0;

  /**
   * @brief 处理客户端消息(纯虚函数)
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param sequence 消息序列号
   * @param message 消息内容
   */
  virtual void handle_message(std::uint64_t session_id, const std::string& peer_address,
                              std::uint32_t sequence,
                              const client_v1::Message& message) = 0;

  /**
   * @brief 处理客户端连接事件(纯虚函数)
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   */
  virtual void handle_connected(std::uint64_t session_id, const std::string& peer_address) = 0;

  /**
   * @brief 处理客户端断开事件(纯虚函数)
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param reason 断开原因
   */
  virtual void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                   const std::string& reason) = 0;

  /**
   * @brief 向客户端发送消息
   * @param session_id 目标会话ID
   * @param message 要发送的消息
   */
  void send_message(std::uint64_t session_id, const client_v1::Message& message);

  /**
   * @brief 延迟向客户端发送消息
   * @param session_id 目标会话ID
   * @param message 要发送的消息
   * @param delay 延迟时间
   */
  void send_message(std::uint64_t session_id, const client_v1::Message& message,
                    std::chrono::milliseconds delay);

  /**
   * @brief 向客户端发送帧数据
   * @param session_id 目标会话ID
   * @param frame 要发送的帧
   */
  void send_frame(std::uint64_t session_id, const client_v1::Frame& frame);

  /**
   * @brief 向客户端发送多帧数据
   * @param session_id 目标会话ID
   * @param frames 帧列表
   */
  void send_frames(std::uint64_t session_id, const std::vector<client_v1::Frame>& frames);

  /**
   * @brief 延迟向客户端发送多帧数据
   * @param session_id 目标会话ID
   * @param frames 帧列表
   * @param delay 延迟时间
   */
  void send_frames(std::uint64_t session_id, const std::vector<client_v1::Frame>& frames,
                   std::chrono::milliseconds delay);

  /**
   * @brief 断开客户端连接
   * @param session_id 目标会话ID
   * @param code 断开码
   * @param reason 断开原因
   */
  void disconnect(std::uint64_t session_id, std::uint16_t code, const std::string& reason);

  /**
   * @brief 获取宿主上下文引用
   * @return 宿主上下文引用
   */
  [[nodiscard]] HostContext& context() const { return *context_; }

 private:
  /**
   * @brief 启动异步连接接受
   */
  void do_accept();

  std::string module_name_{};
  HostContext* context_{nullptr};
  asio::io_context io_context_{};
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_{};
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_{};
  std::vector<std::thread> io_threads_{};
  mutable std::mutex mutex_{};
  std::unordered_map<std::uint64_t, std::shared_ptr<ClientV1Session>> sessions_{};
  std::unordered_map<std::uint64_t, std::uint32_t> client_frame_sequences_{};
  std::atomic_bool running_{false};
  std::atomic_uint64_t next_session_id_{1};
};

}  // namespace mir2
