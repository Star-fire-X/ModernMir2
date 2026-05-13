#include "services/client_v1_gateway_service_base.hpp"

#include <algorithm>

namespace mir2 {

ClientV1GatewayServiceBase::ClientV1GatewayServiceBase(std::string module_name)
    : module_name_(std::move(module_name)) {}

ClientV1GatewayServiceBase::~ClientV1GatewayServiceBase() {
  stop();
  join();
}

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
