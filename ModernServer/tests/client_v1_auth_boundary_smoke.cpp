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

int fail(const char* stage) {
  std::cerr << "client_v1_auth_boundary_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5615);
}

bool send_hello(asio::ip::tcp::socket& socket, std::uint32_t& sequence,
                std::uint32_t protocol_version = mir2::client_v1::kProtocolVersion) {
  mir2::client_v1::ClientHello hello;
  hello.protocol_version = protocol_version;
  mir2::tests::send_client_v1_message(socket, hello, sequence);
  return true;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_auth_boundary_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5615;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7115;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_auth_boundary_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

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
      return fail("connect missing hello");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    mir2::tests::send_client_v1_message(*socket,
                                        mir2::client_v1::LoginRequest{"alpha", "pw"},
                                        sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 400 ||
        disconnect->text != "missing_client_hello") {
      stop_services();
      return fail("missing hello disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect bad version");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence, mir2::client_v1::kProtocolVersion + 1);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 426 ||
        disconnect->text != "protocol_version_mismatch") {
      stop_services();
      return fail("bad version disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect old version");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence, mir2::client_v1::kProtocolVersion - 1);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 426 ||
        disconnect->text != "protocol_version_mismatch") {
      stop_services();
      return fail("old version disconnect");
    }
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect unauth select");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::SelectServerRequest{"ModernServer"}, sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("unauthenticated select disconnect");
    }
  }

  auto socket = connect_login(io_context);
  if (!socket.has_value()) {
    stop_services();
    return fail("connect login");
  }
  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_hello(*socket, sequence);

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::CreateAccountRequest{"alpha", "oldpw", complete_profile("Alpha")},
      sequence);
  auto create = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!create.has_value() || !create->success) {
    stop_services();
    return fail("create account");
  }

  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::CreateAccountRequest{"alpha", "oldpw", complete_profile("Alpha")},
      sequence);
  create = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!create.has_value() || create->success ||
      create->code != 0 ||
      create->error_message != "create_account_failed") {
    stop_services();
    return fail("duplicate account");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::LoginRequest{"alpha", "badpw"}, sequence);
  auto login = reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || login->success || login->code != -1 ||
      login->error_message != "login_failed") {
    stop_services();
    return fail("bad password");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::ChangePasswordRequest{"alpha", "badpw", "newpw"},
      sequence);
  auto change = reader.wait_for_message<mir2::client_v1::ChangePasswordResult>();
  if (!change.has_value() || change->success || change->code != -1 ||
      change->error_message != "change_password_failed") {
    stop_services();
    return fail("bad password change");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::ChangePasswordRequest{"alpha", "oldpw", "newpw"},
      sequence);
  change = reader.wait_for_message<mir2::client_v1::ChangePasswordResult>();
  if (!change.has_value() || !change->success) {
    stop_services();
    return fail("change password");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::LoginRequest{"alpha", "oldpw"}, sequence);
  login = reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || login->success || login->code != -1 ||
      login->error_message != "login_failed") {
    stop_services();
    return fail("old password rejected");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::LoginRequest{"alpha", "newpw"}, sequence);
  login = reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || !login->success || login->account_id != "alpha") {
    stop_services();
    return fail("login");
  }
  const auto servers = reader.wait_for_message<mir2::client_v1::ServerList>();
  if (!servers.has_value() || servers->servers.empty() ||
      servers->servers.front().name != "ModernServer") {
    stop_services();
    return fail("server list");
  }

  {
    auto early_socket = connect_login(io_context);
    if (!early_socket.has_value()) {
      stop_services();
      return fail("connect early character");
    }
    mir2::tests::ClientV1SocketReader early_reader(*early_socket);
    std::uint32_t early_sequence = 1;
    send_hello(*early_socket, early_sequence);
    mir2::tests::send_client_v1_message(
        *early_socket, mir2::client_v1::LoginRequest{"alpha", "newpw"}, early_sequence);
    auto early_login = early_reader.wait_for_message<mir2::client_v1::LoginResult>();
    auto early_servers = early_reader.wait_for_message<mir2::client_v1::ServerList>();
    if (!early_login.has_value() || !early_login->success || !early_servers.has_value()) {
      stop_services();
      return fail("early character login");
    }
    mir2::tests::send_client_v1_message(
        *early_socket, mir2::client_v1::CharacterListRequest{}, early_sequence);
    auto early_disconnect = early_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!early_disconnect.has_value() || early_disconnect->code != 401 ||
        early_disconnect->text != "not_authenticated") {
      stop_services();
      return fail("character list before select server");
    }
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::SelectServerRequest{"OtherServer"}, sequence);
  auto select_server = reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select_server.has_value() || select_server->success ||
      select_server->error_message != "server_not_found") {
    stop_services();
    return fail("unknown server");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::SelectServerRequest{"ModernServer"}, sequence);
  select_server = reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select_server.has_value() || !select_server->success ||
      select_server->lobby_token.empty() || select_server->address != "127.0.0.1" ||
      select_server->port != 5615) {
    stop_services();
    return fail("select server");
  }

  auto char_socket = connect_login(io_context);
  if (!char_socket.has_value()) {
    stop_services();
    return fail("connect character");
  }
  mir2::tests::ClientV1SocketReader char_reader(*char_socket);
  std::uint32_t char_sequence = 1;
  send_hello(*char_socket, char_sequence);
  mir2::tests::send_client_v1_message(
      *char_socket,
      mir2::client_v1::CharacterListRequest{select_server->lobby_token},
      char_sequence);
  auto characters = char_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || !characters->characters.empty()) {
    stop_services();
    return fail("empty character list");
  }

  auto reuse_socket = connect_login(io_context);
  if (!reuse_socket.has_value()) {
    stop_services();
    return fail("connect token reuse");
  }
  mir2::tests::ClientV1SocketReader reuse_reader(*reuse_socket);
  std::uint32_t reuse_sequence = 1;
  send_hello(*reuse_socket, reuse_sequence);
  mir2::tests::send_client_v1_message(
      *reuse_socket,
      mir2::client_v1::CharacterListRequest{select_server->lobby_token},
      reuse_sequence);
  auto disconnect = reuse_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
  if (!disconnect.has_value() || disconnect->code != 401 ||
      disconnect->text != "not_authenticated") {
    stop_services();
    return fail("lobby token one shot");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CreateCharacterRequest{"X", 0, 0, 0},
      char_sequence);
  auto create_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || create_character->success ||
      create_character->code != 0 ||
      create_character->error_message != "invalid_character_name") {
    stop_services();
    return fail("invalid character");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CreateCharacterRequest{"HeroOne", 0, 0, 1},
      char_sequence);
  create_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || !create_character->success ||
      create_character->character.name != "HeroOne") {
    stop_services();
    return fail("create HeroOne");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CreateCharacterRequest{"HeroOne", 0, 0, 1},
      char_sequence);
  create_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || create_character->success ||
      create_character->code != 2 ||
      create_character->error_message != "create_character_failed") {
    stop_services();
    return fail("duplicate character");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CreateCharacterRequest{"HeroTwo", 1, 1, 2},
      char_sequence);
  create_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || !create_character->success) {
    stop_services();
    return fail("create HeroTwo");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CreateCharacterRequest{"HeroThree", 2, 0, 3},
      char_sequence);
  create_character =
      char_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || create_character->success ||
      create_character->code != 3 ||
      create_character->error_message != "character_slots_full") {
    stop_services();
    return fail("character slots full");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::DeleteCharacterRequest{"Missing"},
      char_sequence);
  auto delete_character =
      char_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_character.has_value() || delete_character->success ||
      delete_character->code != 0 ||
      delete_character->error_message != "delete_character_failed") {
    stop_services();
    return fail("delete missing");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::DeleteCharacterRequest{"HeroOne"},
      char_sequence);
  delete_character =
      char_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_character.has_value() || !delete_character->success ||
      delete_character->deleted_name != "HeroOne") {
    stop_services();
    return fail("delete HeroOne");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CharacterListRequest{}, char_sequence);
  characters = char_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || characters->characters.size() != 1 ||
      characters->characters.front().name != "HeroTwo" ||
      characters->selected_name != "HeroTwo") {
    stop_services();
    return fail("final character list");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::SelectCharacterRequest{"Missing"},
      char_sequence);
  auto select_character =
      char_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || select_character->success ||
      select_character->error_message != "character_not_found") {
    stop_services();
    return fail("select missing character");
  }

  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::SelectCharacterRequest{"HeroTwo"},
      char_sequence);
  select_character =
      char_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty() ||
      select_character->address != "127.0.0.1" || select_character->port != 7115) {
    stop_services();
    return fail("select HeroTwo");
  }

  stop_services();
  return 0;
}
