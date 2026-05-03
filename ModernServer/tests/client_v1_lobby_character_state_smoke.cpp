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
#include "services/client_v1_game_gateway_service.hpp"
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
  std::cerr << "client_v1_lobby_character_state_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5631);
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 7131);
}

void send_hello(asio::ip::tcp::socket& socket, std::uint32_t& sequence) {
  mir2::tests::send_client_v1_message(socket, mir2::client_v1::ClientHello{}, sequence);
}

bool create_account(asio::io_context& io_context, const std::string& account_id,
                    const std::string& password) {
  auto socket = connect_login(io_context);
  if (!socket.has_value()) {
    return false;
  }
  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_hello(*socket, sequence);
  mir2::tests::send_client_v1_message(
      *socket,
      mir2::client_v1::CreateAccountRequest{account_id, password,
                                            complete_profile(account_id)},
      sequence);
  const auto result = reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  return result.has_value() && result->success;
}

std::optional<std::string> login_and_select_server(asio::io_context& io_context,
                                                   const std::string& account_id,
                                                   const std::string& password) {
  auto socket = connect_login(io_context);
  if (!socket.has_value()) {
    return std::nullopt;
  }
  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_hello(*socket, sequence);
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::LoginRequest{account_id, password}, sequence);
  const auto login = reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || !login->success) {
    return std::nullopt;
  }
  if (!reader.wait_for_message<mir2::client_v1::ServerList>().has_value()) {
    return std::nullopt;
  }
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::SelectServerRequest{"ModernServer"}, sequence);
  const auto select = reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select.has_value() || !select->success || select->lobby_token.empty()) {
    return std::nullopt;
  }
  return select->lobby_token;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_lobby_character_state_smoke";
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
  config.ports.client_v1_login_gateway.port = 5631;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7131;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_lobby_character_state_smoke",
      std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::ClientV1LoginGatewayService login_gateway(admissions);
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  login_gateway.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    game_gateway.stop();
    login_gateway.stop();
    game_gateway.join();
    login_gateway.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  if (!create_account(io_context, "alpha", "pw") ||
      !create_account(io_context, "beta", "pw")) {
    stop_services();
    return fail("create accounts");
  }

  {
    auto socket = connect_login(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect unknown server");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::LoginRequest{"alpha", "pw"}, sequence);
    if (!reader.wait_for_message<mir2::client_v1::LoginResult>().has_value() ||
        !reader.wait_for_message<mir2::client_v1::ServerList>().has_value()) {
      stop_services();
      return fail("unknown server login");
    }
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::SelectServerRequest{"OtherServer"}, sequence);
    const auto select = reader.wait_for_message<mir2::client_v1::SelectServerResult>();
    if (!select.has_value() || select->success || select->error_message != "server_not_found" ||
        !select->lobby_token.empty()) {
      stop_services();
      return fail("unknown server no token");
    }
  }

  const auto alpha_token = login_and_select_server(io_context, "alpha", "pw");
  if (!alpha_token.has_value()) {
    stop_services();
    return fail("alpha token");
  }

  {
    auto no_hello_socket = connect_login(io_context);
    if (!no_hello_socket.has_value()) {
      stop_services();
      return fail("connect no hello lobby token");
    }
    mir2::tests::ClientV1SocketReader no_hello_reader(*no_hello_socket);
    std::uint32_t no_hello_sequence = 1;
    mir2::tests::send_client_v1_message(
        *no_hello_socket, mir2::client_v1::CharacterListRequest{*alpha_token},
        no_hello_sequence);
    const auto disconnect =
        no_hello_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("lobby token requires hello");
    }
  }

  {
    auto bad_token_socket = connect_login(io_context);
    if (!bad_token_socket.has_value()) {
      stop_services();
      return fail("connect bad lobby token");
    }
    mir2::tests::ClientV1SocketReader bad_token_reader(*bad_token_socket);
    std::uint32_t bad_token_sequence = 1;
    send_hello(*bad_token_socket, bad_token_sequence);
    mir2::tests::send_client_v1_message(
        *bad_token_socket, mir2::client_v1::CharacterListRequest{"bad-token"},
        bad_token_sequence);
    const auto disconnect =
        bad_token_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("bad lobby token rejected");
    }
  }

  auto alpha_lobby = connect_login(io_context);
  if (!alpha_lobby.has_value()) {
    stop_services();
    return fail("connect alpha lobby");
  }
  mir2::tests::ClientV1SocketReader alpha_reader(*alpha_lobby);
  std::uint32_t alpha_sequence = 1;
  send_hello(*alpha_lobby, alpha_sequence);
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::CharacterListRequest{*alpha_token}, alpha_sequence);
  auto characters = alpha_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || !characters->characters.empty()) {
    stop_services();
    return fail("alpha empty character list");
  }

  {
    auto reuse_socket = connect_login(io_context);
    if (!reuse_socket.has_value()) {
      stop_services();
      return fail("connect reused lobby token");
    }
    mir2::tests::ClientV1SocketReader reuse_reader(*reuse_socket);
    std::uint32_t reuse_sequence = 1;
    send_hello(*reuse_socket, reuse_sequence);
    mir2::tests::send_client_v1_message(
        *reuse_socket, mir2::client_v1::CharacterListRequest{*alpha_token},
        reuse_sequence);
    const auto disconnect =
        reuse_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "not_authenticated") {
      stop_services();
      return fail("lobby token one shot");
    }
  }

  for (const auto& invalid_name : {"Ab", "Name-With-Dash", "abcdefghijklmnop"}) {
    mir2::tests::send_client_v1_message(
        *alpha_lobby, mir2::client_v1::CreateCharacterRequest{invalid_name, 0, 0, 0},
        alpha_sequence);
    const auto create =
        alpha_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
    if (!create.has_value() || create->success ||
        create->error_message != "invalid_character_name") {
      stop_services();
      return fail("invalid character name");
    }
  }

  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::CreateCharacterRequest{"Abc", 1, 0, 2},
      alpha_sequence);
  auto create = alpha_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create.has_value() || !create->success || create->character.name != "Abc" ||
      create->character.level != 1 || create->character.job != 1 ||
      create->character.sex != 0 || create->character.hair != 2 ||
      create->character.map_id != "0") {
    stop_services();
    return fail("create three char name");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::CreateCharacterRequest{"Abc", 1, 0, 2},
      alpha_sequence);
  create = alpha_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create.has_value() || create->success ||
      create->error_message != "create_character_failed") {
    stop_services();
    return fail("duplicate alpha character");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby,
      mir2::client_v1::CreateCharacterRequest{"abcdefghijklmn", 2, 1, 3},
      alpha_sequence);
  create = alpha_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create.has_value() || !create->success ||
      create->character.name != "abcdefghijklmn") {
    stop_services();
    return fail("create fourteen char name");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::CreateCharacterRequest{"Third", 0, 0, 0},
      alpha_sequence);
  create = alpha_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create.has_value() || create->success ||
      create->error_message != "character_slots_full") {
    stop_services();
    return fail("alpha slot full");
  }

  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::SelectCharacterRequest{"Missing"}, alpha_sequence);
  auto select_character =
      alpha_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || select_character->success ||
      select_character->error_message != "character_not_found") {
    stop_services();
    return fail("select missing alpha character");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::SelectCharacterRequest{"Abc"}, alpha_sequence);
  select_character =
      alpha_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_services();
    return fail("select Abc");
  }
  const auto stale_enter_world_token = select_character->enter_world_token;

  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::DeleteCharacterRequest{"Abc"}, alpha_sequence);
  auto delete_character =
      alpha_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_character.has_value() || !delete_character->success ||
      delete_character->deleted_name != "Abc") {
    stop_services();
    return fail("delete Abc");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::SelectCharacterRequest{"Abc"}, alpha_sequence);
  select_character =
      alpha_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || select_character->success ||
      select_character->error_message != "character_not_found") {
    stop_services();
    return fail("select deleted Abc");
  }

  {
    auto game_socket = connect_game(io_context);
    if (!game_socket.has_value()) {
      stop_services();
      return fail("connect stale token game");
    }
    mir2::tests::ClientV1SocketReader game_reader(*game_socket);
    std::uint32_t game_sequence = 1;
    send_hello(*game_socket, game_sequence);
    mir2::tests::send_client_v1_message(
        *game_socket,
        mir2::client_v1::EnterWorldRequest{stale_enter_world_token, 1, 1},
        game_sequence);
    const auto disconnect =
        game_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 404 ||
        disconnect->text != "character_not_found") {
      stop_services();
      return fail("deleted character invalidates enter token");
    }
  }

  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::DeleteCharacterRequest{"Missing"}, alpha_sequence);
  delete_character =
      alpha_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_character.has_value() || delete_character->success ||
      delete_character->error_message != "delete_character_failed") {
    stop_services();
    return fail("delete missing alpha character");
  }

  const auto beta_token = login_and_select_server(io_context, "beta", "pw");
  if (!beta_token.has_value()) {
    stop_services();
    return fail("beta token");
  }
  auto beta_lobby = connect_login(io_context);
  if (!beta_lobby.has_value()) {
    stop_services();
    return fail("connect beta lobby");
  }
  mir2::tests::ClientV1SocketReader beta_reader(*beta_lobby);
  std::uint32_t beta_sequence = 1;
  send_hello(*beta_lobby, beta_sequence);
  mir2::tests::send_client_v1_message(
      *beta_lobby, mir2::client_v1::CharacterListRequest{*beta_token}, beta_sequence);
  characters = beta_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || !characters->characters.empty()) {
    stop_services();
    return fail("beta empty character list");
  }
  mir2::tests::send_client_v1_message(
      *beta_lobby,
      mir2::client_v1::CreateCharacterRequest{"abcdefghijklmn", 0, 1, 4},
      beta_sequence);
  create = beta_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create.has_value() || !create->success ||
      create->character.name != "abcdefghijklmn") {
    stop_services();
    return fail("same character name allowed per account");
  }

  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::DeleteCharacterRequest{"abcdefghijklmn"},
      alpha_sequence);
  delete_character =
      alpha_reader.wait_for_message<mir2::client_v1::DeleteCharacterResult>();
  if (!delete_character.has_value() || !delete_character->success) {
    stop_services();
    return fail("alpha delete own same-name character");
  }
  mir2::tests::send_client_v1_message(
      *beta_lobby, mir2::client_v1::CharacterListRequest{}, beta_sequence);
  characters = beta_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || characters->characters.size() != 1 ||
      characters->characters.front().name != "abcdefghijklmn") {
    stop_services();
    return fail("beta same-name character isolated from alpha delete");
  }
  mir2::tests::send_client_v1_message(
      *alpha_lobby, mir2::client_v1::CharacterListRequest{}, alpha_sequence);
  characters = alpha_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || !characters->characters.empty()) {
    stop_services();
    return fail("alpha list empty after deleting own characters");
  }

  stop_services();
  return 0;
}
