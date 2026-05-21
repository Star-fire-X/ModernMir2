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
#include "services/world_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "client_v1_enter_world_boundary_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 7116);
}

void send_hello(asio::ip::tcp::socket& socket, std::uint32_t& sequence) {
  mir2::tests::send_client_v1_message(socket, mir2::client_v1::ClientHello{}, sequence);
}

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "alpha";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 330;
  character.y = 270;
  character.dir = 0;
  character.job = 1;
  character.sex = 0;
  character.hair = 2;
  character.gold = 0;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  return character;
}

void seed_character_database(const std::filesystem::path& source_root,
                             const std::filesystem::path& database_path) {
  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();
  mir2::AccountRecord account;
  account.account_id = "alpha";
  account.password = "pw";
  account.display_name = "Alpha";
  account.user_name = "Alpha User";
  account.birthday = "1975/08/21";
  account.quiz = "first";
  account.answer = "answer";
  account.quiz2 = "second";
  account.answer2 = "answer2";
  const bool account_created = repository.create_account(account);
  const bool character_created = repository.create_character(make_character());
  static_cast<void>(account_created);
  static_cast<void>(character_created);
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_enter_world_boundary_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.login_notice_title = "Boundary Notice";
  config.runtime.login_notice_text = "Read this before entering.";
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 700, 700, 330, 270});
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7116;

  seed_character_database(source_root, temp_root / "data" / "mir2.sqlite");

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_enter_world_boundary_smoke",
      std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
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

  {
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect missing hello");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{"missing-hello", 1, 1},
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
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect invalid token");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{"bad-token", 1, 1}, sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "invalid_enter_world_token") {
      stop_services();
      return fail("invalid token disconnect");
    }
  }

  {
    const auto expired = admissions->issue("alpha", "Hero", 0s);
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect expired token");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{expired, 1, 1}, sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 401 ||
        disconnect->text != "invalid_enter_world_token") {
      stop_services();
      return fail("expired token disconnect");
    }
  }

  {
    const auto missing_character = admissions->issue("alpha", "Ghost");
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect missing character");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{missing_character, 1, 1},
        sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 404 ||
        disconnect->text != "character_not_found") {
      stop_services();
      return fail("missing character disconnect");
    }
  }

  {
    const auto first_token = admissions->issue("alpha", "Hero");
    const auto duplicate_token = admissions->issue("alpha", "Hero");
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect duplicate enter");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    send_hello(*socket, sequence);
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{first_token, 1, 1}, sequence);
    const auto notice = reader.wait_for_message<mir2::client_v1::LoginNotice>();
    if (!notice.has_value()) {
      stop_services();
      return fail("duplicate enter notice");
    }
    mir2::tests::send_client_v1_message(
        *socket, mir2::client_v1::EnterWorldRequest{duplicate_token, 1, 1}, sequence);
    const auto disconnect =
        reader.wait_for_message<mir2::client_v1::DisconnectReason>();
    if (!disconnect.has_value() || disconnect->code != 409 ||
        disconnect->text != "already_entered_world") {
      stop_services();
      return fail("duplicate enter disconnect");
    }
  }

  const auto token = admissions->issue("alpha", "Hero");
  auto socket = connect_game(io_context);
  if (!socket.has_value()) {
    stop_services();
    return fail("connect valid token");
  }
  mir2::tests::ClientV1SocketReader reader(*socket);
  std::uint32_t sequence = 1;
  send_hello(*socket, sequence);
  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);

  const auto notice = reader.wait_for_message<mir2::client_v1::LoginNotice>();
  if (!notice.has_value() || notice->title != "Boundary Notice" ||
      notice->text != "Read this before entering.") {
    stop_services();
    return fail("login notice");
  }

  auto reuse_socket = connect_game(io_context);
  if (!reuse_socket.has_value()) {
    stop_services();
    return fail("connect reused token");
  }
  mir2::tests::ClientV1SocketReader reuse_reader(*reuse_socket);
  std::uint32_t reuse_sequence = 1;
  send_hello(*reuse_socket, reuse_sequence);
  mir2::tests::send_client_v1_message(
      *reuse_socket, mir2::client_v1::EnterWorldRequest{token, 1, 1},
      reuse_sequence);
  auto disconnect = reuse_reader.wait_for_message<mir2::client_v1::DisconnectReason>();
  if (!disconnect.has_value() || disconnect->code != 401 ||
      disconnect->text != "invalid_enter_world_token") {
    stop_services();
    return fail("reused token disconnect");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MoveIntent{331, 271, mir2::client_v1::MoveMode::walk},
      sequence);
  const auto blocked_ack = reader.wait_for_message<mir2::client_v1::ActionAck>(250ms);
  if (blocked_ack.has_value()) {
    stop_services();
    return fail("notice blocks movement");
  }

  mir2::tests::send_client_v1_message(*socket, mir2::client_v1::LoginNoticeOk{},
                                      sequence);
  const auto enter = reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter.has_value() || !enter->success || enter->character_name != "Hero" ||
      enter->map_id != "0" || enter->x != 330 || enter->y != 270) {
    stop_services();
    return fail("enter world result");
  }

  const auto snapshot = reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter->self_actor_id ||
      snapshot->map_id != "0" || snapshot->width != 700 || snapshot->height != 700 ||
      snapshot->actors.empty() || snapshot->actors.front().name != "Hero" ||
      snapshot->actors.front().x != 330 || snapshot->actors.front().y != 270) {
    stop_services();
    return fail("world snapshot");
  }

  const auto ability = reader.wait_for_message<mir2::client_v1::SelfAbility>();
  if (!ability.has_value() || ability->level != 1 || ability->gold != 0) {
    stop_services();
    return fail("self ability");
  }

  const auto ability_detail =
      reader.wait_for_message<mir2::client_v1::SelfAbilityDetail>();
  if (!ability_detail.has_value() || ability_detail->level != 1 ||
      ability_detail->hp != 15 || ability_detail->max_hp != 15 ||
      ability_detail->mp != 15 || ability_detail->max_mp != 15) {
    stop_services();
    return fail("self ability detail");
  }

  mir2::tests::send_client_v1_message(
      *socket, mir2::client_v1::MoveIntent{331, 271, mir2::client_v1::MoveMode::walk},
      sequence);
  const auto ack = reader.wait_for_matching<mir2::client_v1::ActionAck>(
      [](const mir2::client_v1::ActionAck& action_ack) { return action_ack.ok; });
  if (!ack.has_value()) {
    stop_services();
    return fail("movement ack");
  }

  stop_services();
  return 0;
}
