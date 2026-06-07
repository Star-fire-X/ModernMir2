#include "services/interserver_broadcast_service.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

namespace {

constexpr std::uint32_t kMaxBroadcastPayloadBytes = 64 * 1024;

void append_u32(std::string& buffer, const std::uint32_t value) {
  buffer.push_back(static_cast<char>(value & 0xFFU));
  buffer.push_back(static_cast<char>((value >> 8U) & 0xFFU));
  buffer.push_back(static_cast<char>((value >> 16U) & 0xFFU));
  buffer.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

std::uint32_t read_u32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

void append_string(std::string& buffer, std::string_view value) {
  append_u32(buffer, static_cast<std::uint32_t>(value.size()));
  buffer.append(value.data(), value.size());
}

bool consume_string(std::string_view payload, std::size_t& offset, std::string& out) {
  if (offset + sizeof(std::uint32_t) > payload.size()) {
    return false;
  }
  const auto size = read_u32(reinterpret_cast<const std::uint8_t*>(payload.data() + offset));
  offset += sizeof(std::uint32_t);
  if (offset + size > payload.size()) {
    return false;
  }
  out.assign(payload.substr(offset, size));
  offset += size;
  return true;
}

std::string encode_broadcast_payload(const InterserverBroadcast& broadcast) {
  std::string payload;
  payload.reserve(broadcast.message_id.size() + broadcast.source_server_tag.size() +
                  broadcast.text.size() + 32);
  append_string(payload, broadcast.message_id);
  payload.push_back(static_cast<char>(broadcast.scope));
  append_string(payload, broadcast.source_server_tag);
  append_string(payload, broadcast.text);

  std::string framed;
  framed.reserve(payload.size() + sizeof(std::uint32_t));
  append_u32(framed, static_cast<std::uint32_t>(payload.size()));
  framed += payload;
  return framed;
}

std::optional<InterserverBroadcast> decode_broadcast_payload(std::string_view payload) {
  std::size_t offset = 0;
  InterserverBroadcast broadcast;
  if (!consume_string(payload, offset, broadcast.message_id)) {
    return std::nullopt;
  }
  if (offset >= payload.size()) {
    return std::nullopt;
  }
  broadcast.scope = static_cast<InterserverBroadcastScope>(
      static_cast<std::uint8_t>(payload[offset]));
  ++offset;
  if (!consume_string(payload, offset, broadcast.source_server_tag) ||
      !consume_string(payload, offset, broadcast.text)) {
    return std::nullopt;
  }
  if (offset != payload.size()) {
    return std::nullopt;
  }
  return broadcast;
}

std::optional<InterserverBroadcast> read_broadcast_from_socket(asio::ip::tcp::socket& socket) {
  std::array<std::uint8_t, sizeof(std::uint32_t)> size_bytes{};
  std::error_code error;
  asio::read(socket, asio::buffer(size_bytes), error);
  if (error) {
    return std::nullopt;
  }
  const auto payload_size = read_u32(size_bytes.data());
  if (payload_size == 0 || payload_size > kMaxBroadcastPayloadBytes) {
    return std::nullopt;
  }
  std::string payload(payload_size, '\0');
  asio::read(socket, asio::buffer(payload.data(), payload.size()), error);
  if (error) {
    return std::nullopt;
  }
  return decode_broadcast_payload(payload);
}

}  // namespace

void InterserverBroadcastService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  running_.store(true, std::memory_order_relaxed);

  if (context.config.interserver.enabled && context.config.interserver.listen.port != 0) {
    std::error_code error;
    const auto address =
        asio::ip::make_address(context.config.interserver.listen.address, error);
    if (!error) {
      asio::ip::tcp::endpoint endpoint(address, context.config.interserver.listen.port);
      acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_);
      acceptor_->open(endpoint.protocol(), error);
      if (!error) {
        acceptor_->set_option(asio::socket_base::reuse_address(true), error);
      }
      if (!error) {
        acceptor_->bind(endpoint, error);
      }
      if (!error) {
        acceptor_->listen(asio::socket_base::max_listen_connections, error);
      }
      if (error) {
        acceptor_.reset();
      }
    }
  }

  bus_thread_ = std::thread([this] { bus_loop(); });
  if (acceptor_ != nullptr) {
    listener_thread_ = std::thread([this] { listener_loop(); });
  }
}

void InterserverBroadcastService::stop() {
  running_.store(false, std::memory_order_relaxed);
  if (acceptor_ != nullptr) {
    std::error_code ignored;
    acceptor_->cancel(ignored);
    acceptor_->close(ignored);
  }
}

void InterserverBroadcastService::join() {
  if (bus_thread_.joinable()) {
    bus_thread_.join();
  }
  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }
}

std::unordered_map<std::string, std::string> InterserverBroadcastService::snapshot() const {
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"enabled", context_ != nullptr && context_->config.interserver.enabled ? "true"
                                                                                    : "false"},
          {"forwarded", std::to_string(forwarded_count_.load(std::memory_order_relaxed))},
          {"received", std::to_string(received_count_.load(std::memory_order_relaxed))}};
}

void InterserverBroadcastService::bus_loop() {
  while (running_.load(std::memory_order_relaxed)) {
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }
    auto* broadcast = std::get_if<InterserverBroadcast>(&*message);
    if (broadcast == nullptr || broadcast->local_only ||
        context_ == nullptr || !context_->config.interserver.enabled) {
      continue;
    }
    forward_to_peers(*broadcast);
  }
}

void InterserverBroadcastService::listener_loop() {
  while (running_.load(std::memory_order_relaxed) && acceptor_ != nullptr) {
    std::error_code error;
    asio::ip::tcp::socket socket(io_context_);
    acceptor_->accept(socket, error);
    if (error) {
      if (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      continue;
    }

    auto inbound = read_broadcast_from_socket(socket);
    if (!inbound.has_value() || context_ == nullptr || context_->bus == nullptr) {
      continue;
    }
    inbound->local_only = true;
    if (context_->bus->post("world_service", std::move(*inbound))) {
      ++received_count_;
    }
  }
}

void InterserverBroadcastService::forward_to_peers(const InterserverBroadcast& broadcast) {
  if (context_ == nullptr) {
    return;
  }

  const auto framed = encode_broadcast_payload(broadcast);
  for (const auto& peer : context_->config.interserver.peers) {
    if (peer.port == 0) {
      continue;
    }
    std::error_code error;
    asio::ip::tcp::socket socket(io_context_);
    const auto address = asio::ip::make_address(peer.address, error);
    if (error) {
      continue;
    }
    socket.connect(asio::ip::tcp::endpoint(address, peer.port), error);
    if (error) {
      continue;
    }
    asio::write(socket, asio::buffer(framed.data(), framed.size()), error);
    std::error_code ignored;
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
    if (!error) {
      ++forwarded_count_;
    }
  }
}

}  // namespace mir2
