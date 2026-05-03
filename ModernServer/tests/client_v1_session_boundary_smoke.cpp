#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_login_gateway_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

mir2::client_v1::AccountProfile complete_profile(const std::string& name) {
  mir2::client_v1::AccountProfile profile;
  profile.display_name = name;
  profile.user_name = name + " User";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "first";
  profile.answer = "answer";
  profile.quiz2 = "second";
  profile.answer2 = "answer2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = name + "@example.test";
  return profile;
}

std::vector<std::uint8_t> frame_bytes(const mir2::client_v1::Frame& frame) {
  return mir2::client_v1::encode_frame(frame);
}

template <typename T>
std::vector<std::uint8_t> message_bytes(const T& message, std::uint32_t sequence) {
  return frame_bytes(mir2::client_v1::make_frame(message, sequence));
}

void write_bytes(asio::ip::tcp::socket& socket, const std::vector<std::uint8_t>& bytes) {
  asio::write(socket, asio::buffer(bytes));
}

void write_split(asio::ip::tcp::socket& socket, const std::vector<std::uint8_t>& bytes,
                 std::size_t split_at) {
  split_at = std::min(split_at, bytes.size());
  asio::write(socket, asio::buffer(bytes.data(), split_at));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  asio::write(socket, asio::buffer(bytes.data() + split_at, bytes.size() - split_at));
}

std::vector<std::uint8_t> incomplete_large_frame() {
  const auto advertised_length = static_cast<std::uint32_t>(2U << 20U);
  std::vector<std::uint8_t> bytes((1U << 20U) + 64U, 0);
  bytes[0] = static_cast<std::uint8_t>(advertised_length & 0xFFU);
  bytes[1] = static_cast<std::uint8_t>((advertised_length >> 8U) & 0xFFU);
  bytes[2] = static_cast<std::uint8_t>((advertised_length >> 16U) & 0xFFU);
  bytes[3] = static_cast<std::uint8_t>((advertised_length >> 24U) & 0xFFU);
  return bytes;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5617);
}

bool wait_for_sessions(mir2::Module& module, const std::string& expected,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = module.snapshot();
    const auto it = snapshot.find("sessions");
    if (it != snapshot.end() && it->second == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

int fail(const char* stage) {
  std::cerr << "client_v1_session_boundary_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_session_boundary_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5617;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7117;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_session_boundary_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1LoginGatewayService login_gateway(admissions);
  login_gateway.start(context);

  const auto stop_services = [&] {
    login_gateway.stop();
    login_gateway.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect fragmented");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    write_split(*socket, message_bytes(mir2::client_v1::ClientHello{}, 1), 3);
    write_split(*socket,
                message_bytes(mir2::client_v1::CreateAccountRequest{
                                  "frag", "pw", complete_profile("Frag")},
                              2),
                11);
    const auto result = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
    if (!result.has_value() || !result->success) {
      stop_services();
      return fail("fragmented create account");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect sticky");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    auto bytes = message_bytes(mir2::client_v1::ClientHello{}, 1);
    auto create = message_bytes(
        mir2::client_v1::CreateAccountRequest{"sticky", "pw", complete_profile("Sticky")}, 2);
    bytes.insert(bytes.end(), create.begin(), create.end());
    write_bytes(*socket, bytes);
    const auto result = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
    if (!result.has_value() || !result->success) {
      stop_services();
      return fail("sticky create account");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect unknown message");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    write_bytes(*socket, frame_bytes(mir2::client_v1::Frame{
                             static_cast<mir2::client_v1::MessageId>(0xFFFFU), 0, 1, {}}));
    const auto disconnect = reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 400 ||
        disconnect->text != "protocol_decode_error") {
      stop_services();
      return fail("unknown message disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect damaged payload");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    write_bytes(*socket, frame_bytes(mir2::client_v1::Frame{
                             mir2::client_v1::MessageId::login_request, 0, 1, {0xFFU}}));
    const auto disconnect = reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 400 ||
        disconnect->text != "protocol_decode_error") {
      stop_services();
      return fail("damaged payload disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect frame too large");
    }
    const auto bytes = incomplete_large_frame();
    std::error_code write_error;
    asio::write(*socket, asio::buffer(bytes), write_error);
    mir2::tests::ClientV1SocketReader reader(*socket);
    const auto disconnect = reader.wait_for_message<mir2::client_v1::DisconnectReason>(5s);
    if (!disconnect.has_value() || disconnect->code != 413 ||
        disconnect->text != "frame_too_large") {
      stop_services();
      return fail("frame too large disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect remote close");
    }
    std::uint32_t sequence = 1;
    mir2::tests::send_client_v1_message(*socket, mir2::client_v1::ClientHello{}, sequence);
    if (!wait_for_sessions(login_gateway, "1")) {
      stop_services();
      return fail("session registered");
    }
    socket->close(ignored);
    if (!wait_for_sessions(login_gateway, "0")) {
      stop_services();
      return fail("remote close cleanup");
    }
  }

  stop_services();
  return 0;
}
