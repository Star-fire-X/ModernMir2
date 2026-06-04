/**
 * @file gateway_service_base.hpp
 * @brief 遗留网关服务基类头文件
 *
 * @details 定义 GatewayServiceBase 类，作为传统(遗留协议)网关服务的抽象基类。
 *          提供基于 ASIO 的 TCP 服务器框架，包括连接接受、会话管理和
 *          消息路由功能。登录网关和游戏网关均继承自此基类。
 *
 * @note 该基类实现了消息总线的事件循环，将网关收到的数据包转发到
 *       后端服务(如 AuthService 或 WorldService)，同时将后端响应
 *       转发回对应的 TCP 客户端。
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "asio.hpp"
#include "config/models.hpp"
#include "core/module.hpp"
#include "protocol/session_router.hpp"

namespace mir2 {

class GameSession;

/**
 * @class GatewayServiceBase
 * @brief 遗留网关服务基类
 *
 * @details 提供通用的 TCP 网关服务框架，主要功能包括：
 *          - 基于 ASIO 的异步 TCP 服务器(监听、接受连接)
 *          - 会话生命周期管理(创建、销毁)
 *          - 消息总线事件循环(接收后端应答并转发给客户端)
 *          - 背压控制(当后端队列深度超过阈值时暂停或断开连接)
 *
 *          子类需要实现 binding() 和 ingress_target() 两个纯虚函数
 *          来指定监听端口和消息路由目标。
 */
class GatewayServiceBase : public Module {
 public:
  /**
   * @brief 构造函数
   * @param module_name 模块名称，用于消息总线注册和日志标识
   */
  explicit GatewayServiceBase(std::string module_name);

  /// @brief 析构函数，自动停止服务并等待所有线程结束
  ~GatewayServiceBase() override;

  /**
   * @brief 获取模块名称
   * @return 模块名称字符串
   */
  [[nodiscard]] std::string name() const override { return module_name_; }

  /**
   * @brief 启动网关服务
   * @param context 宿主上下文，包含配置和系统资源引用
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
   * @return 包含运行状态和会话数量等信息的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

  /**
   * @brief 通知有新客户端连接
   * @param session_id 分配的会话ID
   * @param peer_address 客户端地址
   */
  void notify_connected(std::uint64_t session_id, const std::string& peer_address);

  /**
   * @brief 通知有客户端断开连接
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param reason 断开原因
   */
  void notify_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason);

  /**
   * @brief 转发接收到的数据包到后端服务
   * @param session_id 会话ID
   * @param peer_address 客户端地址
   * @param packet 遗留协议数据包
   * @param session 对应的 GameSession 智能指针
   * @return true 表示数据包被成功投递到消息总线
   * @note 如果后端队列过深，会触发背压控制
   */
  bool forward_packet(std::uint64_t session_id, const std::string& peer_address,
                      const LegacyPacket& packet, const std::shared_ptr<GameSession>& session);

  /**
   * @brief 移除会话记录
   * @param session_id 要移除的会话ID
   */
  void remove_session(std::uint64_t session_id);

 protected:
  /**
   * @brief 获取端口绑定配置
   * @param context 宿主上下文
   * @return 端口绑定配置(地址和端口号)
   */
  virtual PortBinding binding(const HostContext& context) const = 0;

  /**
   * @brief 获取消息路由目标服务名称
   * @return 目标服务名称字符串(如 "auth_service" 或 "world_service")
   */
  virtual std::string ingress_target() const = 0;

  /**
   * @brief 分配新的会话ID
   * @return 新的唯一会话ID
   */
  [[nodiscard]] std::uint64_t allocate_session_id();

 private:
  /**
   * @brief 启动异步连接接受循环
   */
  void do_accept();

  /**
   * @brief 消息总线事件循环
   * @details 从消息队列接收后端事件(SessionEvent)，根据事件类型转发给客户端
   */
  void bus_loop();

  std::string module_name_{};                                                       ///< 模块名称
  HostContext* context_{nullptr};                                                   ///< 宿主上下文指针
  SessionRouter router_{};                                                          ///< 会话路由器
  asio::io_context io_context_{};                                                   ///< ASIO IO 上下文
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_{}; ///< 防止 io_context 在没有工作时退出
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_{};                            ///< TCP 连接接收器
  std::vector<std::thread> io_threads_{};                                          ///< IO 工作线程池
  std::thread bus_thread_{};                                                        ///< 消息总线线程
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};                                 ///< 消息总线端点
  mutable std::mutex mutex_{};                                                     ///< 保护会话表的互斥锁
  std::unordered_map<std::uint64_t, std::shared_ptr<GameSession>> sessions_{};     ///< 会话表，键为会话ID
  std::unordered_map<std::uint64_t, std::uint64_t> session_sequences_{};           ///< 会话序列号，用于消息排序
  std::atomic_bool running_{false};                                                ///< 运行状态标志
  std::atomic_uint64_t next_session_id_{1};                                        ///< 下一个会话ID
};

}  // namespace mir2
