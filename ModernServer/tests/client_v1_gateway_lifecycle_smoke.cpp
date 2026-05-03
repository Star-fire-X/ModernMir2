#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

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

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "life";
  character.character_name = "LifeHero";
  character.map_id = "0";
  character.x = 20;
  character.y = 21;
  character.dir = 2;
  character.job = 1;
  character.sex = 0;
  character.hair = 1;
  character.gold = 300;
  character.ability.level = 3;
  character.ability.hp = 25;
  character.ability.max_hp = 30;
  character.ability.mp = 18;
  character.ability.max_mp = 20;
  character.ability.max_exp = 200;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

void seed_database(const std::filesystem::path& source_root,
                   const std::filesystem::path& database_path) {
  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  mir2::AccountRecord account;
  account.account_id = "life";
  account.password = "pw";
  account.display_name = "Life";
  static_cast<void>(repository.create_account(account));
  static_cast<void>(repository.create_character(make_character()));
}

std::optional<asio::ip::tcp::socket> connect_game(asio::io_context& io_context) {
  return mir2::tests::connect_socket(io_context, "127.0.0.1", 7118);
}

bool wait_snapshot_value(mir2::Module& module, const std::string& key,
                         const std::string& expected,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(4000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = module.snapshot();
    const auto it = snapshot.find(key);
    if (it != snapshot.end() && it->second == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

bool enter_world(asio::ip::tcp::socket& socket,
                 mir2::tests::ClientV1SocketReader& reader,
                 mir2::ClientV1AdmissionRegistry& admissions, std::uint32_t& sequence) {
  const auto token = admissions.issue("life", "LifeHero");
  mir2::tests::send_client_v1_message(socket, mir2::client_v1::ClientHello{}, sequence);
  mir2::tests::send_client_v1_message(
      socket, mir2::client_v1::EnterWorldRequest{token, 1, 1}, sequence);

  const auto enter = reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter.has_value() || !enter->success || enter->character_name != "LifeHero" ||
      enter->map_id != "0" || enter->x != 20 || enter->y != 21) {
    return false;
  }
  const auto snapshot = reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter->self_actor_id ||
      snapshot->map_id != "0" || snapshot->actors.empty() ||
      snapshot->actors.front().name != "LifeHero") {
    return false;
  }
  const auto ability = reader.wait_for_message<mir2::client_v1::SelfAbility>();
  if (!ability.has_value() || ability->level != 3 || ability->gold != 300) {
    return false;
  }
  const auto detail = reader.wait_for_message<mir2::client_v1::SelfAbilityDetail>();
  if (!detail.has_value() || detail->level != 3 || detail->hp != 25 ||
      detail->max_hp != 30) {
    return false;
  }
  return true;
}

int fail(const char* stage) {
  std::cerr << "client_v1_gateway_lifecycle_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_gateway_lifecycle_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.maps.push_back(mir2::MapConfig{"0", "LifecycleMap", {}, 700, 700, 20, 21});
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7118;

  seed_database(source_root, temp_root / "data" / "mir2.sqlite");

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_gateway_lifecycle_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

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

  bool game_gateway_stopped = false;
  const auto stop_services = [&] {
    if (!game_gateway_stopped) {
      game_gateway.stop();
      game_gateway.join();
      game_gateway_stopped = true;
    }
    world_service.stop();
    world_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;

  {
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect no hello");
    }
    if (!wait_snapshot_value(game_gateway, "sessions", "1")) {
      stop_services();
      return fail("no hello session registered");
    }
    socket->close(ignored);
    if (!wait_snapshot_value(game_gateway, "sessions", "0")) {
      stop_services();
      return fail("no hello cleanup");
    }
  }

  {
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect hello close");
    }
    std::uint32_t sequence = 1;
    mir2::tests::send_client_v1_message(*socket, mir2::client_v1::ClientHello{}, sequence);
    if (!wait_snapshot_value(game_gateway, "sessions", "1")) {
      stop_services();
      return fail("hello session registered");
    }
    socket->close(ignored);
    if (!wait_snapshot_value(game_gateway, "sessions", "0")) {
      stop_services();
      return fail("hello cleanup");
    }
  }

  {
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect enter close");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    if (!enter_world(*socket, reader, *admissions, sequence)) {
      stop_services();
      return fail("enter world");
    }
    if (!wait_snapshot_value(game_gateway, "sessions", "1") ||
        !wait_snapshot_value(world_service, "sessions", "1")) {
      stop_services();
      return fail("entered session snapshots");
    }
    socket->close(ignored);
    if (!wait_snapshot_value(game_gateway, "sessions", "0") ||
        !wait_snapshot_value(world_service, "sessions", "0")) {
      stop_services();
      return fail("entered disconnect cleanup");
    }
  }

  {
    auto socket = connect_game(io_context);
    if (!socket.has_value()) {
      stop_services();
      return fail("connect active stop");
    }
    mir2::tests::ClientV1SocketReader reader(*socket);
    std::uint32_t sequence = 1;
    if (!enter_world(*socket, reader, *admissions, sequence)) {
      stop_services();
      return fail("active stop enter");
    }
    if (!wait_snapshot_value(game_gateway, "sessions", "1") ||
        !wait_snapshot_value(world_service, "sessions", "1")) {
      stop_services();
      return fail("active stop snapshots");
    }
    game_gateway.stop();
    game_gateway.join();
    game_gateway_stopped = true;
    if (!wait_snapshot_value(game_gateway, "sessions", "0") ||
        !wait_snapshot_value(world_service, "sessions", "0")) {
      stop_services();
      return fail("active stop cleanup");
    }
  }

  stop_services();
  return 0;
}
