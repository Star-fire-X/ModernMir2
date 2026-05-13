#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "core/default_modules.hpp"
#include "core/host_runtime.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

mir2::client_v1::AccountProfile complete_profile() {
  mir2::client_v1::AccountProfile profile;
  profile.display_name = "Stage Four";
  profile.user_name = "Stage Four User";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "first";
  profile.answer = "answer";
  profile.quiz2 = "second";
  profile.answer2 = "answer2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = "stage4@example.test";
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
  config.maps.push_back(mir2::MapConfig{"0", "PersistenceMap", {}, 700, 700, 330, 270});
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5632;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7132;
  return config;
}

int fail(const char* stage) {
  std::cerr << "client_v1_enter_map_persistence_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<asio::ip::tcp::socket> connect_login(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 5632);
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 7132);
}

std::optional<mir2::CharacterRecord> wait_for_saved_position(
    const std::filesystem::path& source_root, const std::filesystem::path& database_path,
    const std::string& account_id, const std::string& character_name, const int x, const int y) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      mir2::Repository repository(database_path);
      repository.ensure_schema(source_root / "schema" / "mir2.sql");
      const auto character = repository.load_character(account_id, character_name);
      if (character.has_value() && character->x == x && character->y == y) {
        return character;
      }
    } catch (...) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return std::nullopt;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_enter_map_persistence_smoke";
  const auto database_path = temp_root / "data" / "mir2.sqlite";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_enter_map_persistence_smoke",
      std::make_shared<spdlog::sinks::null_sink_mt>());
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
      mir2::client_v1::CreateAccountRequest{"stage4", "pass", complete_profile()},
      login_sequence);
  const auto create_account =
      login_reader.wait_for_message<mir2::client_v1::CreateAccountResult>();
  if (!create_account.has_value() || !create_account->success) {
    stop_runtime();
    return fail("create account");
  }
  mir2::tests::send_client_v1_message(
      *login_socket, mir2::client_v1::LoginRequest{"stage4", "pass"}, login_sequence);
  const auto login = login_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login.has_value() || !login->success || login->account_id != "stage4") {
    stop_runtime();
    return fail("login");
  }
  if (!login_reader.wait_for_message<mir2::client_v1::ServerList>().has_value()) {
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
      *lobby_socket, mir2::client_v1::CreateCharacterRequest{"PersistHero", 2, 1, 3},
      lobby_sequence);
  const auto create_character =
      lobby_reader.wait_for_message<mir2::client_v1::CreateCharacterResult>();
  if (!create_character.has_value() || !create_character->success ||
      create_character->character.name != "PersistHero" ||
      create_character->character.map_id != "0" ||
      create_character->character.level != 1 ||
      create_character->character.job != 2 ||
      create_character->character.sex != 1 ||
      create_character->character.hair != 3) {
    stop_runtime();
    return fail("create character defaults");
  }

  mir2::tests::send_client_v1_message(
      *lobby_socket, mir2::client_v1::SelectCharacterRequest{"PersistHero"},
      lobby_sequence);
  auto select_character =
      lobby_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_runtime();
    return fail("select character");
  }

  auto game_socket = connect_game(io_context);
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
      enter_world->character_name != "PersistHero" || enter_world->map_id != "0" ||
      enter_world->x != 330 || enter_world->y != 270 ||
      enter_world->self_actor_id == 0) {
    stop_runtime();
    return fail("enter world result");
  }
  const auto snapshot = game_reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter_world->self_actor_id ||
      snapshot->map_id != "0" || snapshot->width != 700 || snapshot->height != 700 ||
      snapshot->actors.empty() || snapshot->actors.front().name != "PersistHero" ||
      snapshot->actors.front().x != 330 || snapshot->actors.front().y != 270) {
    stop_runtime();
    return fail("world snapshot");
  }

  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::MoveIntent{331, 271, mir2::client_v1::MoveMode::walk},
      game_sequence);
  const auto move_ack = game_reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& ack) { return ack.ok; });
  const auto move_delta = game_reader.wait_for_matching<mir2::client_v1::ActorStateDelta>(
      [&](const mir2::client_v1::ActorStateDelta& delta) {
        return delta.actor_id == enter_world->self_actor_id && delta.x == 331 && delta.y == 271;
      });
  if (!move_ack.has_value() || !move_delta.has_value()) {
    stop_runtime();
    return fail("move");
  }

  game_socket->close(ignored);
  const auto saved =
      wait_for_saved_position(source_root, database_path, "stage4", "PersistHero", 331, 271);
  if (!saved.has_value() || saved->map_id != "0" ||
      saved->character_name != "PersistHero") {
    stop_runtime();
    return fail("disconnect save");
  }

  lobby_socket->close(ignored);
  login_socket->close(ignored);

  auto relogin_socket = connect_login(io_context);
  if (!relogin_socket.has_value()) {
    stop_runtime();
    return fail("relogin connect");
  }
  mir2::tests::ClientV1SocketReader relogin_reader(*relogin_socket);
  std::uint32_t relogin_sequence = 1;
  mir2::tests::send_client_v1_message(*relogin_socket, mir2::client_v1::ClientHello{},
                                      relogin_sequence);
  mir2::tests::send_client_v1_message(
      *relogin_socket, mir2::client_v1::LoginRequest{"stage4", "pass"}, relogin_sequence);
  const auto relogin = relogin_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!relogin.has_value() || !relogin->success || relogin->account_id != "stage4") {
    stop_runtime();
    return fail("relogin");
  }
  if (!relogin_reader.wait_for_message<mir2::client_v1::ServerList>().has_value()) {
    stop_runtime();
    return fail("relogin server list");
  }
  mir2::tests::send_client_v1_message(
      *relogin_socket, mir2::client_v1::SelectServerRequest{"ModernServer"},
      relogin_sequence);
  const auto reselect_server =
      relogin_reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!reselect_server.has_value() || !reselect_server->success ||
      reselect_server->lobby_token.empty()) {
    stop_runtime();
    return fail("reselect server");
  }

  auto relobby_socket = mir2::tests::connect_socket(io_context, reselect_server->address,
                                                    reselect_server->port);
  if (!relobby_socket.has_value()) {
    stop_runtime();
    return fail("relobby connect");
  }
  mir2::tests::ClientV1SocketReader relobby_reader(*relobby_socket);
  std::uint32_t relobby_sequence = 1;
  mir2::tests::send_client_v1_message(*relobby_socket, mir2::client_v1::ClientHello{},
                                      relobby_sequence);
  mir2::tests::send_client_v1_message(
      *relobby_socket,
      mir2::client_v1::CharacterListRequest{reselect_server->lobby_token},
      relobby_sequence);
  const auto restored_characters =
      relobby_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!restored_characters.has_value() || restored_characters->characters.size() != 1 ||
      restored_characters->characters.front().name != "PersistHero") {
    stop_runtime();
    return fail("restored character list");
  }

  mir2::tests::send_client_v1_message(
      *relobby_socket, mir2::client_v1::SelectCharacterRequest{"PersistHero"},
      relobby_sequence);
  select_character =
      relobby_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_runtime();
    return fail("select character after save");
  }

  auto reconnect_socket = connect_game(io_context);
  if (!reconnect_socket.has_value()) {
    stop_runtime();
    return fail("reconnect game");
  }
  mir2::tests::ClientV1SocketReader reconnect_reader(*reconnect_socket);
  std::uint32_t reconnect_sequence = 1;
  mir2::tests::send_client_v1_message(*reconnect_socket, mir2::client_v1::ClientHello{},
                                      reconnect_sequence);
  mir2::tests::send_client_v1_message(
      *reconnect_socket,
      mir2::client_v1::EnterWorldRequest{select_character->enter_world_token, 1, 1},
      reconnect_sequence);
  const auto restored = reconnect_reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!restored.has_value() || !restored->success ||
      restored->character_name != "PersistHero" || restored->map_id != "0" ||
      restored->x != 331 || restored->y != 271 || restored->self_actor_id == 0) {
    stop_runtime();
    return fail("restored enter world");
  }
  const auto restored_snapshot =
      reconnect_reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!restored_snapshot.has_value() ||
      restored_snapshot->self_actor_id != restored->self_actor_id ||
      restored_snapshot->actors.empty() ||
      restored_snapshot->actors.front().name != "PersistHero" ||
      restored_snapshot->actors.front().x != 331 ||
      restored_snapshot->actors.front().y != 271) {
    stop_runtime();
    return fail("restored snapshot");
  }

  stop_runtime();
  return 0;
}
