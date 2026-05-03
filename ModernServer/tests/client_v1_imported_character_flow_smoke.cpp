#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "asio.hpp"
#include "client_v1_test_utils.hpp"
#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "importer/legacy_character_importer.hpp"
#include "legacy_character_fixture.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/client_v1_login_gateway_service.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "shared/protocol/client_v1/protocol.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "client_v1_imported_character_flow_smoke failed at " << stage << '\n';
  return 1;
}

mir2::MagicConfig make_fireball() {
  mir2::MagicConfig magic;
  magic.id = 1;
  magic.name = "Fireball";
  magic.mp_cost = 4;
  magic.power = 8;
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = 1;
  magic.legacy.spell = 4;
  magic.legacy.min_power = 5;
  magic.legacy.max_power = 10;
  magic.legacy.job = 2;
  magic.legacy.max_train_level = 3;
  magic.legacy.delay_time = 100;
  return magic;
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root, std::uint16_t login_port,
                             std::uint16_t game_port) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.io_threads = 2;
  config.runtime.castle_context_refresh_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "ImportedMap", {}, 700, 700, 330, 270});
  config.items.push_back(mir2::ItemConfig{1, "Sword", 1, 100, 5, 0, 10, 1000, 1, 0, 0});
  config.magics.push_back(make_fireball());
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = login_port;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = game_port;
  return config;
}

bool has_item(const mir2::client_v1::EquipmentSnapshot& snapshot, std::int32_t make_index) {
  return std::any_of(snapshot.items.begin(), snapshot.items.end(),
                     [&](const mir2::client_v1::ItemSlotState& item) {
                       return item.item.make_index == make_index;
                     });
}

bool has_bag_item(const mir2::client_v1::BagSnapshot& snapshot, std::int32_t make_index) {
  return std::any_of(snapshot.items.begin(), snapshot.items.end(),
                     [&](const mir2::client_v1::ItemSlotState& item) {
                       return item.item.make_index == make_index;
                     });
}

bool has_magic(const mir2::client_v1::MagicList& list, std::uint16_t magic_id) {
  return std::any_of(list.magics.begin(), list.magics.end(),
                     [&](const mir2::client_v1::MagicEntry& magic) {
                       return magic.magic_id == magic_id;
                     });
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_client_v1_imported_character_flow_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  const auto config = make_config(temp_root, 5624, 7124);
  const auto fixture = mir2::tests::write_legacy_character_fixture(temp_root / "legacy");
  {
    mir2::Repository repository(temp_root / "data" / "mir2.sqlite");
    repository.ensure_schema(source_root / "schema" / "mir2.sql");

    mir2::LegacyCharacterImporter importer;
    mir2::LegacyCharacterImportOptions options;
    options.hum_db_path = fixture.hum_db;
    options.mir_db_path = fixture.mir_db;
    options.config = &config;
    const auto report = importer.import_characters(options, repository);
    if (report.imported != 1 || report.failed != 0) {
      return fail("import");
    }
  }

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "client_v1_imported_character_flow_smoke",
      std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto admissions = std::make_shared<mir2::ClientV1AdmissionRegistry>();
  mir2::PersistenceService persistence_service;
  mir2::WorldService world_service;
  mir2::ClientV1LoginGatewayService login_gateway(admissions);
  mir2::ClientV1GameGatewayService game_gateway(admissions);
  persistence_service.start(context);
  world_service.start(context);
  login_gateway.start(context);
  game_gateway.start(context);

  const auto stop_services = [&] {
    login_gateway.stop();
    game_gateway.stop();
    world_service.stop();
    persistence_service.stop();
    login_gateway.join();
    game_gateway.join();
    world_service.join();
    persistence_service.join();
    std::filesystem::remove_all(temp_root, ignored);
  };

  asio::io_context io_context;
  auto login_socket = mir2::tests::connect_socket(io_context, "127.0.0.1", 5624);
  if (!login_socket.has_value()) {
    stop_services();
    return fail("login connect");
  }

  mir2::tests::ClientV1SocketReader login_reader(*login_socket);
  std::uint32_t login_sequence = 1;
  mir2::tests::send_client_v1_message(*login_socket, mir2::client_v1::ClientHello{},
                                      login_sequence);
  mir2::tests::send_client_v1_message(*login_socket,
                                      mir2::client_v1::LoginRequest{"legacyacct", "pass"},
                                      login_sequence);
  const auto login_result = login_reader.wait_for_message<mir2::client_v1::LoginResult>();
  if (!login_result.has_value() || !login_result->success) {
    stop_services();
    return fail("login");
  }
  const auto server_list = login_reader.wait_for_message<mir2::client_v1::ServerList>();
  if (!server_list.has_value() || server_list->servers.empty()) {
    stop_services();
    return fail("server list");
  }
  mir2::tests::send_client_v1_message(
      *login_socket, mir2::client_v1::SelectServerRequest{server_list->servers.front().name},
      login_sequence);
  const auto select_server =
      login_reader.wait_for_message<mir2::client_v1::SelectServerResult>();
  if (!select_server.has_value() || !select_server->success ||
      select_server->lobby_token.empty()) {
    stop_services();
    return fail("select server");
  }

  auto char_socket =
      mir2::tests::connect_socket(io_context, select_server->address, select_server->port);
  if (!char_socket.has_value()) {
    stop_services();
    return fail("character connect");
  }
  mir2::tests::ClientV1SocketReader char_reader(*char_socket);
  std::uint32_t char_sequence = 1;
  mir2::tests::send_client_v1_message(*char_socket, mir2::client_v1::ClientHello{},
                                      char_sequence);
  mir2::tests::send_client_v1_message(
      *char_socket, mir2::client_v1::CharacterListRequest{select_server->lobby_token},
      char_sequence);
  const auto characters = char_reader.wait_for_message<mir2::client_v1::CharacterList>();
  if (!characters.has_value() || characters->characters.size() != 1 ||
      characters->characters.front().name != "LegacyHero" ||
      characters->characters.front().level != 7 ||
      characters->characters.front().job != 2 ||
      characters->characters.front().sex != 1 ||
      characters->characters.front().hair != 2 ||
      characters->characters.front().map_id != "0") {
    stop_services();
    return fail("character list");
  }

  mir2::tests::send_client_v1_message(*char_socket,
                                      mir2::client_v1::SelectCharacterRequest{"LegacyHero"},
                                      char_sequence);
  const auto select_character =
      char_reader.wait_for_message<mir2::client_v1::SelectCharacterResult>();
  if (!select_character.has_value() || !select_character->success ||
      select_character->enter_world_token.empty()) {
    stop_services();
    return fail("select character");
  }

  auto game_socket =
      mir2::tests::connect_socket(io_context, select_character->address, select_character->port);
  if (!game_socket.has_value()) {
    stop_services();
    return fail("game connect");
  }
  mir2::tests::ClientV1SocketReader game_reader(*game_socket);
  std::uint32_t game_sequence = 1;
  mir2::tests::send_client_v1_message(*game_socket, mir2::client_v1::ClientHello{},
                                      game_sequence);
  mir2::tests::send_client_v1_message(
      *game_socket, mir2::client_v1::EnterWorldRequest{select_character->enter_world_token, 1, 1},
      game_sequence);

  const auto enter_world = game_reader.wait_for_message<mir2::client_v1::EnterWorldResult>();
  if (!enter_world.has_value() || !enter_world->success ||
      enter_world->character_name != "LegacyHero" || enter_world->map_id != "0" ||
      enter_world->x != 123 || enter_world->y != 234) {
    stop_services();
    return fail("enter world");
  }
  const auto snapshot = game_reader.wait_for_message<mir2::client_v1::WorldSnapshot>();
  if (!snapshot.has_value() || snapshot->self_actor_id != enter_world->self_actor_id ||
      snapshot->map_id != "0" || snapshot->actors.empty()) {
    stop_services();
    return fail("world snapshot");
  }
  const auto equipment = game_reader.wait_for_matching<mir2::client_v1::EquipmentSnapshot>(
      [](const mir2::client_v1::EquipmentSnapshot& snapshot) { return has_item(snapshot, 1001); });
  const auto bag = game_reader.wait_for_matching<mir2::client_v1::BagSnapshot>(
      [](const mir2::client_v1::BagSnapshot& snapshot) { return has_bag_item(snapshot, 1002); });
  const auto magic = game_reader.wait_for_matching<mir2::client_v1::MagicList>(
      [](const mir2::client_v1::MagicList& list) { return has_magic(list, 1); });
  if (!equipment.has_value() || !bag.has_value() || !magic.has_value()) {
    stop_services();
    return fail("initial imported payloads");
  }

  stop_services();
  return 0;
}
