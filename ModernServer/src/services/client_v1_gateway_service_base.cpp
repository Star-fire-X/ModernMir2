/**
 * @file client_v1_gateway_service_base.cpp
 * @brief Client v1 网关服务基类实现
 *
 * @details 实现 ClientV1GatewayServiceBase 类，包括 TCP 服务器生命周期、
 *          异步连接接受、消息序列检查、会话管理和消息发送等核心功能。
 *
 * @note 消息序列检查机制：
 *       每次收到新消息时，会检查其序列号是否大于上次收到的序列号，
 *       如果不大于则判定为过期消息并丢弃。这防止了重放攻击和乱序消息。
 */

#include "services/client_v1_gateway_service_base.hpp"

#include <algorithm>

namespace mir2 {

ClientV1GatewayServiceBase::ClientV1GatewayServiceBase(std::string module_name)
    : module_name_(std::move(module_name)) {}

ClientV1GatewayServiceBase::~ClientV1GatewayServiceBase() {
  stop();
  join();
}

/**
 * @brief 启动网关服务
 *
 * @details 初始化 ASIO acceptor 并绑定端口，开始异步接受连接，
 *          然后启动 IO 线程池处理网络事件。
 *
 * @param context 宿主上下文
 */
void ClientV1GatewayServiceBase::start(HostContext& context) {
  context_ = &context;
  running_.store(true, std::memory_order_relaxed);
  work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
      io_context_.get_executor());

  const auto bind = binding(context);
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(bind.address), bind.port);
  acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_);
  acceptor_->open(endpoint.protocol());
  acceptor_->set_option(asio::socket_base::reuse_address(true));
  acceptor_->bind(endpoint);
  acceptor_->listen(asio::socket_base::max_listen_connections);

  do_accept();
  const auto io_threads = std::max<std::size_t>(1, context.config.runtime.io_threads);
  for (std::size_t index = 0; index < io_threads; ++index) {
    io_threads_.emplace_back([this] { io_context_.run(); });
  }
}

/**
 * @brief 停止网关服务
 *
 * @details 关闭所有客户端会话，关闭 acceptor，释放 work_guard
 */
void ClientV1GatewayServiceBase::stop() {
  running_.store(false, std::memory_order_relaxed);
  std::vector<std::shared_ptr<ClientV1Session>> sessions;
  {
    std::scoped_lock lock(mutex_);
    sessions.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
      sessions.push_back(session);
    }
  }
  for (const auto& session : sessions) {
    if (session != nullptr) {
      session->close("service_stopped");
    }
  }
  if (acceptor_ != nullptr) {
    asio::post(io_context_, [acceptor = acceptor_.get()] {
      std::error_code ignored;
      acceptor->close(ignored);
    });
  }
  if (work_guard_ != nullptr) {
    work_guard_->reset();
  }
}

void ClientV1GatewayServiceBase::join() {
  for (auto& thread : io_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  io_threads_.clear();
}

std::unordered_map<std::string, std::string> ClientV1GatewayServiceBase::snapshot() const {
  std::scoped_lock lock(mutex_);
  return {
      {"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
      {"sessions", std::to_string(sessions_.size())},
  };
}

/**
 * @brief 客户端连接回调
 *
 * @details 初始化该会话的帧序列号为0，增加连接计数器，然后调用子类的
 *          handle_connected() 处理连接事件。
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 */
void ClientV1GatewayServiceBase::on_client_v1_connected(std::uint64_t session_id,
                                                        const std::string& peer_address) {
  {
    std::scoped_lock lock(mutex_);
    client_frame_sequences_[session_id] = 0;
  }
  if (context_ != nullptr) {
    context_->metrics->increment_counter(name() + ".connected");
  }
  handle_connected(session_id, peer_address);
}

/**
 * @brief 客户端断开连接回调
 *
 * @details 清除会话记录和帧序列号记录，更新断开计数器，
 *          然后调用子类的 handle_disconnected() 处理断开事件。
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 * @param reason 断开原因
 */
void ClientV1GatewayServiceBase::on_client_v1_disconnected(std::uint64_t session_id,
                                                           const std::string& peer_address,
                                                           const std::string& reason) {
  {
    std::scoped_lock lock(mutex_);
    sessions_.erase(session_id);
    client_frame_sequences_.erase(session_id);
  }
  if (context_ != nullptr) {
    context_->metrics->increment_counter(name() + ".disconnected");
  }
  handle_disconnected(session_id, peer_address, reason);
}

/**
 * @brief 客户端消息回调(带序列号检查)
 *
 * @details 检查消息序列号是否单调递增：
 *          - 如果序列号不大于上次收到的序列号，丢弃消息并记录
 *          - 否则更新序列号并调用子类的 handle_message() 处理
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 * @param sequence 消息序列号
 * @param message 消息内容
 */
void ClientV1GatewayServiceBase::on_client_v1_message(std::uint64_t session_id,
                                                      const std::string& peer_address,
                                                      std::uint32_t sequence,
                                                      const client_v1::Message& message) {
  {
    std::scoped_lock lock(mutex_);
    auto& last_sequence = client_frame_sequences_[session_id];
    if (sequence <= last_sequence) {
      if (context_ != nullptr) {
        context_->metrics->increment_counter(name() + ".stale_client_sequence");
      }
      return;
    }
    last_sequence = sequence;
  }
  handle_message(session_id, peer_address, sequence, message);
}

/**
 * @brief 向客户端发送消息
 * @param session_id 目标会话ID
 * @param message 要发送的消息
 */
void ClientV1GatewayServiceBase::send_message(std::uint64_t session_id,
                                              const client_v1::Message& message) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send(message);
  }
}

void ClientV1GatewayServiceBase::send_message(std::uint64_t session_id,
                                              const client_v1::Message& message,
                                              std::chrono::milliseconds delay) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send(message, delay);
  }
}

void ClientV1GatewayServiceBase::send_frame(std::uint64_t session_id,
                                            const client_v1::Frame& frame) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send_frame(frame);
  }
}

void ClientV1GatewayServiceBase::send_frames(
    std::uint64_t session_id, const std::vector<client_v1::Frame>& frames) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send_frames(frames);
  }
}

void ClientV1GatewayServiceBase::send_frames(
    std::uint64_t session_id, const std::vector<client_v1::Frame>& frames,
    std::chrono::milliseconds delay) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send_frames(frames, delay);
  }
}

void ClientV1GatewayServiceBase::disconnect(std::uint64_t session_id, std::uint16_t code,
                                            const std::string& reason) {
  std::shared_ptr<ClientV1Session> session;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    session->send_disconnect_and_close(code, reason);
  }
}

/**
 * @brief 异步连接接受循环
 *
 * @details 使用 ASIO async_accept 非阻塞接受新连接。
 *        每次接受后创建 ClientV1Session 并启动会话，
 *        然后继续等待下一个连接。
 */
void ClientV1GatewayServiceBase::do_accept() {
  if (!running_.load(std::memory_order_relaxed) || acceptor_ == nullptr) {
    return;
  }

  acceptor_->async_accept([this](const std::error_code& error, asio::ip::tcp::socket socket) {
    if (!error) {
      auto session = std::make_shared<ClientV1Session>(std::move(socket), *this);
      const auto session_id = next_session_id_.fetch_add(1, std::memory_order_relaxed);
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id] = session;
      }
      session->start(session_id);
    }

    if (running_.load(std::memory_order_relaxed)) {
      do_accept();
    }
  });
}

}  // namespace mir2
