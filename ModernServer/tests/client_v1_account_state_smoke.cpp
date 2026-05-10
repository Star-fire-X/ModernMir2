#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

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

mir2::client_v1::AccountProfile incomplete_profile(const std::string& name) {
  mir2::client_v1::AccountProfile profile;
  profile.display_name = name;
  return profile;
}

int fail(const char* stage) {
  std::cerr << "client_v1_account_state_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5630);
}

void send_hello(asio::ip::tcp::socket& socket, std::uint32_t& sequence) {
  mir2::tests::send_client_v1_message(socket, mir2::client_v1::ClientHello{}, sequence);
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_account_state_smoke";
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
  config.ports.client_v1_login_gateway.port = 5630;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7130;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_account_state_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

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
      return fail("connect unauth update");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket,
        mir2::client_v1::UpdateAccountRequest{"alpha", "", complete_profile("Alpha")},
        sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("unauth update disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect create accounts");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket,
        mir2::client_v1::CreateAccountRequest{"bad/name", "pw",
                                              complete_profile("Invalid")},
        sequence);
    auto create = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
    if (!create.has_value() || create->success ||
        create->error_message != "create_account_failed") {
      stop_services();
      return fail("invalid account id rejected");
    }
    mir2::tests::send_client_v1_message(
        *socket,
        mir2::client_v1::CreateAccountRequest{"alpha", "oldpw",
                                              incomplete_profile("Alpha")},
        sequence);
    create = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
    if (!create.has_value() || !create->success) {
      stop_services();
      return fail("create incomplete alpha");
    }
    mir2::tests::send_client_v1_message(
        *socket,
        mir2::client_v1::CreateAccountRequest{"beta", "betapw", complete_profile("Beta")},
        sequence);
    create = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
    if (!create.has_value() || !create->success) {
      stop_services();
      return fail("create beta");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect failed login state");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::LoginRequest{"alpha", "badpw"}, sequence);
    const auto login = reader.wait_for_message<mir2::client_v1::LoginResult>();
    if (!login.has_value() || login->success || login->error_message != "login_failed") {
      stop_services();
      return fail("bad login result");
    }
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::SelectServerRequest{"ModernServer"}, sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("bad login leaves unauthenticated");
    }
  }

  auto socket = connect_login(io_context);
  if (!socket.has_value()) {
    stop_services();
    return fail("connect alpha login");
  }
  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_hello(*socket, sequence);
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::LoginRequest{"alpha", "oldpw"}, sequence);
  const auto login = reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || !login->success || login->account_id != "alpha") {
    stop_services();
    return fail("alpha login");
  }
  const auto need_update =
      reader.wait_for_message<mir2::client_v1::NeedUpdateAccount>();
  if (!need_update.has_value() || need_update->account_id != "alpha" ||
      need_update->message != "account_profile_required" ||
      need_update->profile.display_name != "Alpha") {
    stop_services();
    return fail("need update account");
  }
  const auto unexpected_server_list =
      reader.wait_for_message<mir2::client_v1::ServerList>(std::chrono::milliseconds(200));
  if (unexpected_server_list.has_value()) {
    stop_services();
    return fail("server list before profile update");
  }

  {
    auto mismatch_socket = connect_login(io_context);
    if (!mismatch_socket.has_value()) {
      stop_services();
      return fail("connect account mismatch");
    }
    mir2::tests::ClientV1SocketReader mismatch_reader(*mismatch_socket);
    std::uint32_t mismatch_sequence = 1;
    send_hello(*mismatch_socket, mismatch_sequence);
    mir2::tests::send_client_v1_message(
        *mismatch_socket, mir2::client_v1::LoginRequest{"alpha", "oldpw"},
        mismatch_sequence);
    const auto mismatch_login =
        mismatch_reader.wait_for_message<mir2::client_v1::LoginResult>();
    if (!mismatch_login.has_value() || !mismatch_login->success) {
      stop_services();
      return fail("mismatch login");
    }
    if (!mismatch_reader.wait_for_message<mir2::client_v1::NeedUpdateAccount>().has_value()) {
      stop_services();
      return fail("mismatch need update");
    }
    mir2::tests::send_client_v1_message(
        *mismatch_socket,
        mir2::client_v1::UpdateAccountRequest{"beta", "", complete_profile("Beta")},
        mismatch_sequence);
    const auto disconnect =
        mismatch_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 403 ||
        disconnect->text != "account_mismatch") {
      stop_services();
      return fail("account mismatch disconnect");
    }
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::UpdateAccountRequest{"alpha", "", complete_profile("Alpha")},
      sequence);
  const auto update = reader.wait_for_message<mir2::client_v1::UpdateAccountResult>();
  if (!update.has_value() || !update->success) {
    stop_services();
    return fail("update account");
  }
  const auto servers = reader.wait_for_message<mir2::client_v1::ServerList>();
  if (!servers.has_value() || servers->servers.empty() ||
      servers->servers.front().name != "ModernServer") {
    stop_services();
    return fail("server list after update");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::ChangePasswordRequest{"alpha", "wrong", "newpw"},
      sequence);
  auto change = reader.wait_for_message<mir2::client_v1::ChangePasswordResult>();
  if (!change.has_value() || change->success) {
    stop_services();
    return fail("change password rejects wrong old password");
  }
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::ChangePasswordRequest{"alpha", "oldpw", "newpw"},
      sequence);
  change = reader.wait_for_message<mir2::client_v1::ChangePasswordResult>();
  if (!change.has_value() || !change->success) {
    stop_services();
    return fail("change password");
  }

  auto relog_socket = connect_login(io_context);
  if (!relog_socket.has_value()) {
    stop_services();
    return fail("connect relog");
  }
  mir2::tests::ClientV1SocketReader relog_reader(*relog_socket);
  std::uint32_t relog_sequence = 1;
  send_hello(*relog_socket, relog_sequence);
  mir2::tests::send_client_v1_message(
      *relog_socket, mir2::client_v1::LoginRequest{"alpha", "oldpw"}, relog_sequence);
  auto relog = relog_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!relog.has_value() || relog->success) {
    stop_services();
    return fail("old password no longer works");
  }
  mir2::tests::send_client_v1_message(
      *relog_socket, mir2::client_v1::LoginRequest{"alpha", "newpw"}, relog_sequence);
  relog = relog_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!relog.has_value() || !relog->success || relog->account_id != "alpha") {
    stop_services();
    return fail("new password works");
  }
  if (!relog_reader.wait_for_message<mir2::client_v1::ServerList>().has_value()) {
    stop_services();
    return fail("relog server list");
  }

  stop_services();
  return 0;
}
