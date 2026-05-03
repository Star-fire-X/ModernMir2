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

void GatewayServiceBase::remove_session(std::uint64_t session_id) {
  std::scoped_lock lock(mutex_);
  sessions_.erase(session_id);
  if (context_ != nullptr) {
    context_->metrics->set_gauge(name() + ".sessions", static_cast<std::int64_t>(sessions_.size()));
  }
}

std::uint64_t GatewayServiceBase::allocate_session_id() {
  return next_session_id_.fetch_add(1, std::memory_order_relaxed);
}

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
      }
      session->start(session_id);
    }

    if (running_.load(std::memory_order_relaxed)) {
      do_accept();
    }
  });
}

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
          if (session_event->kind == SessionEventKind::send_packet_and_close) {
            const auto delay_ms = session_event->delay_ms >= 0 ? session_event->delay_ms : 0;
            session->deliver_and_close(session_event->packet, std::chrono::milliseconds(delay_ms),
                                       session_event->reason);
          } else {
            session->deliver(session_event->packet);
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
