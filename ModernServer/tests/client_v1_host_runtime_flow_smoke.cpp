#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "core/default_modules.hpp"
#include "core/host_runtime.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

mir2::client_v1::AccountProfile complete_profile() {
  mir2::client_v1::AccountProfile profile;
  profile.display_name = "Host Runtime";
  profile.user_name = "Host Runtime User";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "first";
  profile.answer = "answer";
  profile.quiz2 = "second";
  profile.answer2 = "answer2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = "host-runtime@example.test";
  return profile;
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.enable_legacy_gateways = false;
  config.runtime.enable_client_v1_gateways = true;
  config.runtime.castle_context_refresh_ms = 0;
  config.budgets.tick_ms = 5;
  config.maps.push_back(mir2::MapConfig{"0", "HostRuntimeMap", {}, 700, 700, 330, 270});
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5628;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7128;
  return config;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5628);
}

int fail(const char* stage) {
  std::cerr << "client_v1_host_runtime_flow_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_host_runtime_flow_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_host_runtime_flow_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());
  mir2::HostRuntime runtime(source_root, make_config(temp_root), logger);
  mir2::register_default_modules(runtime);
  runtime.start_all();

  const auto stop_runtime = [&] {
    runtime.stop_all();
    runtime.join_all();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto login_socket = connect_login(io_context);
  if (!login_socket.has_value()) {
    stop_runtime();
    return fail("login connect");
  }

  mir2::tests::ClientV1SocketReader login_reader(*login_socket);
  std::uint32_t login_sequence = 1;
  mir2::tests::send_client_v1_message(*login_socket, mir2::client_v1::ClientHello{},
                                      login_sequence);
  mir2::tests::send_client_v1_message(
      *login_socket,
      mir2::client_v1::CreateAccountRequest{"hostrt", "pass", complete_profile()},
      login_sequence);
  const auto create_account =
      login_reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!create_account.has_value() || !create_account->success) {
    stop_runtime();
    return fail("create account");
  }

  mir2::tests::send_client_v1_message(
      *login_socket, mir2::client_v1::LoginRequest{"hostrt", "pass"}, login_sequence);
  const auto login_result = login_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login_result.has_value() || !login_result->success) {
    stop_runtime();
    return fail("login");
  }
  const auto server_list = login_reader.wait_for_message<mir2::client_v1::ServerList>();
  if (!server_list.has_value() || server_list->servers.empty() ||
      server_list->servers.front().name != "ModernServer") {
    stop_runtime();
    return fail("server list");
  }

  mir2::tests::send_client_v1_message(
      *login_socket, mir2::client_v1::SelectServerRequest{"ModernServer"},
      login_sequence);
  const auto select_server =
      login_reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select_server.has_value() || !select_server->success ||
      select_server->lobby_token.empty()) {
    stop_runtime();
    return fail("select server");
  }

  auto lobby_socket = mir2::tests::connect_socket(io_context, select_server->address,
                                                  select_server->port);
  if (!lobby_socket.has_value()) {
    stop_runtime();
    return fail("lobby connect");
  }
  mir2::tests::ClientV1SocketReader lobby_reader(*lobby_socket);
  std::uint32_t lobby_sequence = 1;
  mir2::tests::send_client_v1_message(*lobby_socket, mir2::client_v1::ClientHello{},
                                      lobby_sequence);
  mir2::tests::send_client_v1_message(
      *lobby_socket,
      mir2::client_v1::CharacterListRequest{select_server->lobby_token},
      lobby_sequence);
  const auto empty_characters =
      lobby_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!empty_characters.has_value() || !empty_characters->characters.empty()) {
    stop_runtime();
    return fail("empty character list");
  }

  mir2::tests::send_client_v1_message(
      *lobby_socket, mir2::client_v1::CreateCharacterRequest{"RuntimeHero", 1, 0, 2},
      lobby_sequence);
  const auto create_character =
      lobby_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || !create_character->success) {
    stop_runtime();
    return fail("create character");
  }

  mir2::tests::send_client_v1_message(
      *lobby_socket, mir2::client_v1::SelectCharacterRequest{"RuntimeHero"},
      lobby_sequence);
  const auto select_character =
      lobby_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_runtime();
    return fail("select character");
  }

  auto game_socket = mir2::tests::connect_socket(io_context, select_character->address,
                                                 select_character->port);
  if (!game_socket.has_value()) {
    stop_runtime();
    return fail("game connect");
  }
  mir2::tests::ClientV1SocketReader game_reader(*game_socket);
  std::uint32_t game_sequence = 1;
  mir2::tests::send_client_v1_message(*game_socket, mir2::client_v1::ClientHello{},
                                      game_sequence);
  mir2::tests::send_client_v1_message(
      *game_socket,
      mir2::client_v1::EnterWorldRequest{select_character->enter_world_token, 1, 1},
      game_sequence);
  const auto enter_world = game_reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_world.has_value() || !enter_world->success ||
      enter_world->character_name != "RuntimeHero" || enter_world->map_id != "0") {
    stop_runtime();
    return fail("enter world");
  }
  const auto snapshot = game_reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter_world->self_actor_id ||
      snapshot->actors.empty() || snapshot->actors.front().name != "RuntimeHero") {
    stop_runtime();
    return fail("world snapshot");
  }

  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::MoveIntent{331, 271, mir2::client_v1::MoveMode::walk},
      game_sequence);
  const auto ack = game_reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& action_ack) { return action_ack.ok; });
  if (!ack.has_value()) {
    stop_runtime();
    return fail("move ack");
  }

  stop_runtime();
  return 0;
}
