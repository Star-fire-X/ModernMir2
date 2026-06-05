/**
 * @file gateway_service_base.cpp
 * @brief 遗留网关服务基类实现
 *
 * @details 实现 GatewayServiceBase 类的方法，包括 TCP 服务器生命周期管理、
 *          异步连接接受、会话管理、消息转发和背压控制等核心功能。
 *
 * @note 背压机制说明：
 *       当后端服务的消息队列深度超过 backpressure_threshold 时，
 *       会暂停客户端读取(25ms)；如果持续超过 disconnect_threshold
 *       则断开客户端连接。这与 Delphi 原版 RUNGate 的 CheckSendLength
 *       机制相对应。
 */

#include "services/gateway_service_base.hpp"

#include <sstream>

#include "protocol/game_session.hpp"

namespace mir2 {

GatewayServiceBase::GatewayServiceBase(std::string module_name)
    : module_name_(std::move(module_name)) {}

GatewayServiceBase::~GatewayServiceBase() {
  stop();
  join();
}

/**
 * @brief 启动网关服务
 *
 * @details 初始化顺序：
 *          1. 注册消息总线端点
 *          2. 创建 ASIO TCP acceptor
 *          3. 开始异步接受连接
 *          4. 启动 IO 线程池和消息总线线程
 *
 * @param context 宿主上下文
 */
void GatewayServiceBase::start(HostContext& context) {
  context_ = &context;
  running_.store(true, std::memory_order_relaxed);
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
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
  for (std::size_t index = 0; index < context.config.runtime.io_threads; ++index) {
    io_threads_.emplace_back([this] { io_context_.run(); });
  }
  bus_thread_ = std::thread([this] { bus_loop(); });
}

/**
 * @brief 停止网关服务
 *
 * @details 关闭 acceptor 和所有会话，释放 work_guard 让 io_context 可以退出
 */
void GatewayServiceBase::stop() {
  running_.store(false, std::memory_order_relaxed);
  if (acceptor_ != nullptr) {
    asio::post(io_context_, [acceptor = acceptor_.get()] {
      std::error_code ignored;
      acceptor->close(ignored);
    });
  }
  if (work_guard_ != nullptr) {
    work_guard_->reset();
  }
  io_context_.stop();
}

void GatewayServiceBase::join() {
  if (bus_thread_.joinable()) {
    bus_thread_.join();
  }
  for (auto& thread : io_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

std::unordered_map<std::string, std::string> GatewayServiceBase::snapshot() const {
  std::size_t session_count = 0;
  {
    std::scoped_lock lock(mutex_);
    session_count = sessions_.size();
  }
  std::unordered_map<std::string, std::string> result;
  result["running"] = running_.load(std::memory_order_relaxed) ? "true" : "false";
  result["sessions"] = std::to_string(session_count);
  if (context_ != nullptr) {
    result["queue_depth"] = std::to_string(context_->bus->queue_depth(name()));
  }
  return result;
}

/**
 * @brief 通知新客户端连接
 *
 * @details 增加连接计数器，更新活跃会话数度量，然后通过消息总线
 *          向后端服务发送连接事件。
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 */
void GatewayServiceBase::notify_connected(std::uint64_t session_id, const std::string& peer_address) {
  if (context_ == nullptr) {
    return;
  }
  std::size_t session_count = 0;
  {
    std::scoped_lock lock(mutex_);
    session_count = sessions_.size();
  }
  context_->metrics->increment_counter(name() + ".connected");
  context_->metrics->set_gauge(name() + ".sessions", static_cast<std::int64_t>(session_count));
  context_->bus->post(ingress_target(),
                      SessionEvent{SessionEventKind::connected, name(), session_id, peer_address, {}, {}});
}

/**
 * @brief 通知客户端断开连接
 *
 * @details 减少连接计数器，更新度量，然后通过消息总线发送断开事件
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 * @param reason 断开原因
 */
void GatewayServiceBase::notify_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                             const std::string& reason) {
  if (context_ == nullptr) {
    return;
  }
  std::size_t session_count = 0;
  {
    std::scoped_lock lock(mutex_);
    session_count = sessions_.size();
  }
  context_->metrics->increment_counter(name() + ".disconnected");
  context_->metrics->set_gauge(name() + ".sessions", static_cast<std::int64_t>(session_count));
  context_->bus->post(
      ingress_target(),
      SessionEvent{SessionEventKind::disconnected, name(), session_id, peer_address, {}, reason});
}

/**
 * @brief 转发数据包到后端服务
 *
 * @details 为数据包分配递增的会话序列号，投递到后端服务的消息队列。
 *          投递后检查后端队列深度，超过背压阈值时暂停或断开连接。
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 * @param packet 遗留协议数据包
 * @param session 对应的 GameSession 智能指针
 * @return true 表示成功投递
 */
bool GatewayServiceBase::forward_packet(std::uint64_t session_id, const std::string& peer_address,
                                        const LegacyPacket& packet,
                                        const std::shared_ptr<GameSession>& session) {
  if (context_ == nullptr) {
    return false;
  }

  SessionEvent event;
  event.kind = SessionEventKind::packet_received;
  event.gateway = name();
  event.session_id = session_id;
  event.peer_address = peer_address;
  event.packet = packet;
  {
    std::scoped_lock lock(mutex_);
    event.session_seq = ++session_sequences_[session_id];
  }

  const auto accepted = context_->bus->post(ingress_target(), event);
  const auto queue_depth = context_->bus->queue_depth(ingress_target());
  if (!accepted || queue_depth >= context_->config.runtime.backpressure_threshold) {
    if (session->note_backpressure(context_->config.runtime.disconnect_threshold)) {
      session->close("bus_backpressure");
    } else {
      session->pause_for(std::chrono::milliseconds(25));
    }
  }
  return accepted;
}

/**
 * @brief 移除会话
 *
 * @details 清除会话表和序列号表中的记录，并更新活跃会话指标
 *
 * @param session_id 要移除的会话ID
 */
void GatewayServiceBase::remove_session(std::uint64_t session_id) {
  std::scoped_lock lock(mutex_);
  sessions_.erase(session_id);
  session_sequences_.erase(session_id);
  if (context_ != nullptr) {
    context_->metrics->set_gauge(name() + ".sessions", static_cast<std::int64_t>(sessions_.size()));
  }
}

std::uint64_t GatewayServiceBase::allocate_session_id() {
  return next_session_id_.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 异步连接接受循环
 *
 * @details 使用 ASIO async_accept 实现非阻塞连接接受。
 *        每次接受一个新连接后，创建 GameSession 对象并启动会话，
 *        然后继续等待下一个连接。
 */
void GatewayServiceBase::do_accept() {
  if (!running_.load(std::memory_order_relaxed) || acceptor_ == nullptr) {
    return;
  }

  acceptor_->async_accept([this](const std::error_code& error, asio::ip::tcp::socket socket) {
    if (!error) {
      auto session = std::make_shared<GameSession>(std::move(socket), *this);
      const auto session_id = allocate_session_id();
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id] = session;
        session_sequences_[session_id] = 0;
      }
      session->start(session_id);
    }

    if (running_.load(std::memory_order_relaxed)) {
      do_accept();
    }
  });
}

/**
 * @brief 消息总线事件循环
 *
 * @details 从消息队列接收 SessionEvent，根据事件类型进行处理：
 *          - send_packet / send_packet_and_close: 将数据包发送给客户端
 *          - force_disconnect: 强制断开客户端连接
 *
 *          send_packet_and_close 会在发送数据包后关闭连接，用于
 *          踢出客户端(如重复登录检测)。
 */
void GatewayServiceBase::bus_loop() {
  while (running_.load(std::memory_order_relaxed)) {
    if (endpoint_ == nullptr) {
      break;
    }
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }

    if (auto session_event = std::get_if<SessionEvent>(&*message)) {
      if ((session_event->kind == SessionEventKind::send_packet ||
           session_event->kind == SessionEventKind::send_packet_and_close) &&
          (session_event->gateway.empty() || session_event->gateway == name())) {
        std::shared_ptr<GameSession> session;
        {
          std::scoped_lock lock(mutex_);
          auto it = sessions_.find(session_event->session_id);
          if (it != sessions_.end()) {
            session = it->second;
          }
        }
        if (session != nullptr) {
          const auto delay_ms = session_event->delay_ms >= 0 ? session_event->delay_ms : 0;
          if (session_event->kind == SessionEventKind::send_packet_and_close) {
            session->deliver_and_close(session_event->packet, std::chrono::milliseconds(delay_ms),
                                       session_event->reason);
          } else {
            session->deliver(session_event->packet, std::chrono::milliseconds(delay_ms));
          }
        }
      } else if (session_event->kind == SessionEventKind::force_disconnect &&
                 (session_event->gateway.empty() || session_event->gateway == name())) {
        std::shared_ptr<GameSession> session;
        {
          std::scoped_lock lock(mutex_);
          auto it = sessions_.find(session_event->session_id);
          if (it != sessions_.end()) {
            session = it->second;
          }
        }
        if (session != nullptr) {
          session->close(session_event->reason.empty() ? "forced_disconnect" : session_event->reason);
        }
      }
    }
  }
}

}  // namespace mir2
