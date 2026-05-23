#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "asio.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/world_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

template <typename T>
void send_message(asio::ip::tcp::socket& socket, const T& message, std::uint32_t& sequence) {
  const auto bytes = mir2::client_v1::encode_frame(mir2::client_v1::make_frame(message, sequence++));
  asio::write(socket, asio::buffer(bytes));
}

class SocketReader {
 public:
  explicit SocketReader(asio::ip::tcp::socket& socket) : socket_(socket) { socket_.non_blocking(true); }

  template <typename T>
  std::optional<T> wait_for_message(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    if (auto pending = take_pending<T>(); pending.has_value()) {
      return pending;
    }

    std::array<std::uint8_t, 4096> read_buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::error_code error;
      const auto bytes_read = socket_.read_some(asio::buffer(read_buffer), error);
      if (!error) {
        buffer_.insert(buffer_.end(), read_buffer.begin(), read_buffer.begin() + bytes_read);
        auto frames = mir2::client_v1::drain_frames(buffer_);
        std::optional<T> matched;
        for (const auto& frame : frames) {
          const auto decoded = mir2::client_v1::decode_any(frame);
          if (!decoded.has_value()) {
            return std::nullopt;
          }
          if (const auto* value = std::get_if<T>(&*decoded); value != nullptr) {
            if (!matched.has_value()) {
              matched = *value;
              continue;
            }
          }
          pending_.push_back(*decoded);
        }
        if (matched.has_value()) {
          return matched;
        }
      } else if (error != asio::error::would_block && error != asio::error::try_again) {
        return std::nullopt;
      }

      if (auto pending = take_pending<T>(); pending.has_value()) {
        return pending;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return std::nullopt;
  }

 private:
  template <typename T>
  std::optional<T> take_pending() {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (const auto* value = std::get_if<T>(&*it); value != nullptr) {
        auto copy = *value;
        pending_.erase(it);
        return copy;
      }
    }
    return std::nullopt;
  }

  asio::ip::tcp::socket& socket_;
  std::vector<std::uint8_t> buffer_{};
  std::vector<mir2::client_v1::Message> pending_{};
};

}  // namespace

int fail(const char* stage) {
  std::cerr << "client_v1_game_gateway_notice_smoke failed at " << stage << '\n';
  return 1;
}

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_client_v1_game_gateway_notice_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.login_notice_title = "Gateway Notice";
  config.runtime.login_notice_text = "Confirm before entering the world.";
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 700, 700, 330, 270});
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7113;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_game_gateway_notice_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  const auto token = admissions->issue("guest", "Hero");
  mir2::WorldService world_service;
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  world_service.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    world_service.stop();
    game_gateway.join();
    world_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), 7113);
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
    return fail("connect");
  }

  std::uint32_t sequence = 1;
  SocketReader reader(socket);
  send_message(socket, mir2::client_v1::ClientHello{}, sequence);
  send_message(socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);

  const auto notice = reader.wait_for_message<mir2::client_v1::LoginNotice>();
  if (!notice.has_value() || notice->title != "Gateway Notice" ||
      notice->text != "Confirm before entering the world.") {
    stop_services();
    return fail("login notice");
  }

  send_message(socket, mir2::client_v1::LoginNoticeOk{}, sequence);
  const auto enter_result = reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_result.has_value() || !enter_result->success ||
      enter_result->character_name != "Hero" || enter_result->map_id != "0") {
    stop_services();
    return fail("enter result");
  }

  const auto snapshot = reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter_result->self_actor_id ||
      snapshot->actors.empty() || snapshot->actors.front().name != "Hero") {
    stop_services();
    return fail("world snapshot");
  }

  const auto move_x = snapshot->actors.front().x + 1;
  const auto move_y = snapshot->actors.front().y + 1;
  send_message(socket, mir2::client_v1::MoveIntent{move_x, move_y, mir2::client_v1::MoveMode::walk},
               sequence);
  const auto ack = reader.wait_for_message<mir2::client_v1::ActionAck>();
  if (!ack.has_value() || !ack->ok) {
    stop_services();
    return fail("action ack");
  }

  stop_services();
  return 0;
}
