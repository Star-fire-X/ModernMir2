#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "asio.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_protocol.hpp"
#include "services/game_gateway_service.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

std::optional<mir2::SessionEvent> wait_for_session_event(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    const std::function<bool(const mir2::SessionEvent&)>& predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
        message.has_value()) {
      if (auto event = std::get_if<mir2::SessionEvent>(&*message);
          event != nullptr && predicate(*event)) {
        return *event;
      }
    }
  }
  return std::nullopt;
}

std::optional<mir2::DecodedLegacyGamePacket> wait_for_socket_packet(
    asio::ip::tcp::socket& socket, std::uint16_t expected_ident,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  socket.non_blocking(true);
  std::vector<std::uint8_t> buffer;
  std::array<std::uint8_t, 4096> read_buffer{};
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    const auto bytes_read = socket.read_some(asio::buffer(read_buffer), error);
    if (!error) {
      buffer.insert(buffer.end(), read_buffer.begin(), read_buffer.begin() + bytes_read);
      for (auto& packet : mir2::LegacyProtocolCodec::drain_packets(buffer)) {
        const auto decoded = mir2::decode_legacy_game_packet(packet);
        if (decoded.has_value() && decoded->message.ident == expected_ident) {
          return decoded;
        }
      }
    } else if (error != asio::error::would_block && error != asio::error::try_again) {
      return std::nullopt;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return std::nullopt;
}

bool wait_for_socket_close(asio::ip::tcp::socket& socket,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  socket.non_blocking(true);
  std::array<std::uint8_t, 256> read_buffer{};
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    const auto bytes_read = socket.read_some(asio::buffer(read_buffer), error);
    if (error == asio::error::eof) {
      return true;
    }
    if (!error && bytes_read > 0) {
      continue;
    }
    if (error != asio::error::would_block && error != asio::error::try_again) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return false;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_gateway_kick_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 1;
  config.ports.game_gateway.address = "127.0.0.1";
  config.ports.game_gateway.port = 7011;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger =
      std::make_shared<spdlog::logger>("gateway_kick_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto world_endpoint = bus.register_endpoint("world_service", 256);

  mir2::GameGatewayService game_gateway;
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    game_gateway.join();
  };

  asio::io_context io_context;
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), 7011);
  asio::ip::tcp::socket socket(io_context);
  bool connected = false;
  for (int attempt = 0; attempt < 50 && !connected; ++attempt) {
    std::error_code connect_error;
    socket.connect(endpoint, connect_error);
    if (!connect_error) {
      connected = true;
      break;
    }
    socket.close();
    socket = asio::ip::tcp::socket(io_context);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!connected) {
    stop_services();
    return 1;
  }

  const auto connected_event = wait_for_session_event(world_endpoint, [](const mir2::SessionEvent& event) {
    return event.kind == mir2::SessionEventKind::connected;
  });
  if (!connected_event.has_value()) {
    stop_services();
    return 1;
  }

  const auto kick_packet = mir2::make_legacy_game_packet(
      connected_event->session_id, 0, 0,
      mir2::make_default_message(mir2::kSmOutOfConnection, 0, 0, 0, 0));
  if (!bus.post("game_gateway",
                mir2::SessionEvent{mir2::SessionEventKind::send_packet_and_close,
                                   "game_gateway",
                                   connected_event->session_id,
                                   {},
                                   kick_packet,
                                   "duplicate_login",
                                   50})) {
    stop_services();
    return 1;
  }

  const auto out_of_connection =
      wait_for_socket_packet(socket, mir2::kSmOutOfConnection, std::chrono::milliseconds(3000));
  if (!out_of_connection.has_value()) {
    stop_services();
    return 1;
  }

  if (!wait_for_socket_close(socket, std::chrono::milliseconds(3000))) {
    stop_services();
    return 1;
  }

  stop_services();
  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
