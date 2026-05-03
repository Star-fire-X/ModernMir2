#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "sqlite3.h"

namespace {

using namespace std::chrono_literals;

bool exec_sql(const std::filesystem::path& database_path, std::string_view sql) {
  sqlite3* database = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &database) != SQLITE_OK) {
    if (database != nullptr) {
      sqlite3_close(database);
    }
    return false;
  }
  char* error = nullptr;
  const auto rc = sqlite3_exec(database, std::string(sql).c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  sqlite3_close(database);
  return rc == SQLITE_OK;
}

bool wait_until(std::function<bool()> predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(20ms);
  }
  return predicate();
}

std::string query_single_text(const std::filesystem::path& database_path, std::string_view sql) {
  sqlite3* database = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &database) != SQLITE_OK) {
    if (database != nullptr) {
      sqlite3_close(database);
    }
    return {};
  }

  sqlite3_stmt* statement = nullptr;
  std::string result;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    if (text != nullptr) {
      result = text;
    }
  }

  if (statement != nullptr) {
    sqlite3_finalize(statement);
  }
  sqlite3_close(database);
  return result;
}

mir2::SessionEvent make_text_event(std::uint64_t session_id, std::string text) {
  mir2::LegacyPacket packet;
  packet.body.assign(text.begin(), text.end());
  return mir2::SessionEvent{mir2::SessionEventKind::packet_received, "game_gateway", session_id,
                            "127.0.0.1", std::move(packet), {}};
}

mir2::SessionEvent make_say_event(std::uint64_t session_id, std::string text) {
  return mir2::SessionEvent{
      mir2::SessionEventKind::packet_received,
      "game_gateway",
      session_id,
      "127.0.0.1",
      mir2::make_legacy_game_packet(
          session_id, 0, 0, mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0),
          mir2::legacy_encode_string(text)),
      {}};
}

void copy_schema_tree(const std::filesystem::path& source_root, const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "schema");
  std::filesystem::copy_file(source_root / "schema" / "mir2.sql", root / "schema" / "mir2.sql",
                             std::filesystem::copy_options::overwrite_existing);
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto root = std::filesystem::temp_directory_path() / "mir2_world_castle_push_refresh_smoke";
  const auto database_path = root / "data" / "mir2.sqlite";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(database_path.parent_path());
  copy_schema_tree(source_root, root);

  if (!exec_sql(database_path,
                "PRAGMA journal_mode=WAL;"
                "CREATE TABLE IF NOT EXISTS guilds (guild_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "guild_name TEXT NOT NULL UNIQUE, payload_json TEXT NOT NULL DEFAULT '{}');"
                "CREATE TABLE IF NOT EXISTS castle_state (castle_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "castle_name TEXT NOT NULL UNIQUE, payload_json TEXT NOT NULL DEFAULT '{}');")) {
    std::cerr << "init_schema_sql\n";
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  if (!exec_sql(database_path,
                "INSERT INTO guilds(guild_name, payload_json) VALUES("
                "'DragonSlayers', '{\"lord\":\"Arthur\"}');"
                "INSERT INTO guilds(guild_name, payload_json) VALUES("
                "'PhoenixHall', '{\"lord\":\"Percival\"}');"
                "INSERT INTO castle_state(castle_name, payload_json) VALUES("
                "'Sabuk', "
                "'{\"owner_guild\":\"DragonSlayers\",\"castle_war_date\":\"2026-05-01 20:00\","
                "\"list_of_war\":[\"WolfPack\"],\"guild_war_fee\":45000,"
                "\"upgrade_weapon_fee\":15000}');")) {
    std::cerr << "seed_castle_sql\n";
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  mir2::HostConfig config;
  config.runtime.data_dir = "data";
  config.runtime.castle_context_refresh_ms = 60000;
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));
  static_cast<void>(bus.register_endpoint("game_gateway", 512));

  mir2::HostContext context;
  context.root_dir = root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;

  mir2::PersistenceService persistence;
  mir2::WorldService world;
  persistence.start(context);
  world.start(context);

  const auto cleanup = [&]() {
    world.stop();
    persistence.stop();
    bus.close_all();
    world.join();
    persistence.join();
    std::filesystem::remove_all(root, ec);
  };

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto owner = snapshot.find("castle_owner_guild");
            const auto lord = snapshot.find("castle_lord");
            const auto refreshes = snapshot.find("castle_refreshes");
            return owner != snapshot.end() && lord != snapshot.end() &&
                   refreshes != snapshot.end() && owner->second == "DragonSlayers" &&
                   lord->second == "Arthur" && std::stoull(refreshes->second) >= 1;
          },
          2s)) {
    std::cerr << "initial_snapshot_refresh\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_text_event(77, "ENTER guest Hero"))) {
    std::cerr << "post_enter\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto sessions = snapshot.find("sessions");
            return sessions != snapshot.end() && sessions->second == "1";
          },
          2s)) {
    std::cerr << "enter_world\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@guild lord DragonSlayers Gawain"))) {
    std::cerr << "post_guild_command\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto owner = snapshot.find("castle_owner_guild");
            const auto lord = snapshot.find("castle_lord");
            const auto refreshes = snapshot.find("castle_refreshes");
            return owner != snapshot.end() && lord != snapshot.end() &&
                   refreshes != snapshot.end() && owner->second == "DragonSlayers" &&
                   lord->second == "Gawain" && std::stoull(refreshes->second) >= 2;
          },
          1s)) {
    std::cerr << "guild_push_refresh\n";
    cleanup();
    return 1;
  }

  if (query_single_text(database_path,
                        "SELECT payload_json FROM guilds WHERE guild_name='DragonSlayers' LIMIT 1;")
          .find("\"lord\":\"Gawain\"") == std::string::npos) {
    std::cerr << "guild_payload_not_saved\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@castle owner PhoenixHall"))) {
    std::cerr << "post_castle_owner\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto owner = snapshot.find("castle_owner_guild");
            const auto lord = snapshot.find("castle_lord");
            const auto refreshes = snapshot.find("castle_refreshes");
            return owner != snapshot.end() && lord != snapshot.end() &&
                   refreshes != snapshot.end() && owner->second == "PhoenixHall" &&
                   lord->second == "Percival" && std::stoull(refreshes->second) >= 3;
          },
          1s)) {
    std::cerr << "castle_owner_push_refresh\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@castle wardate 2026-05-03 19:45"))) {
    std::cerr << "post_castle_wardate\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@castle wars AzureSky, WhiteTiger"))) {
    std::cerr << "post_castle_wars\n";
    cleanup();
    return 1;
  }

  if (!bus.post("world_service", make_say_event(77, "@castle fees 53000 17500"))) {
    std::cerr << "post_castle_fees\n";
    cleanup();
    return 1;
  }

  if (!wait_until(
          [&]() {
            const auto snapshot = world.snapshot();
            const auto owner = snapshot.find("castle_owner_guild");
            const auto lord = snapshot.find("castle_lord");
            const auto war_date = snapshot.find("castle_war_date");
            const auto refreshes = snapshot.find("castle_refreshes");
            return owner != snapshot.end() && lord != snapshot.end() &&
                   war_date != snapshot.end() && refreshes != snapshot.end() &&
                   owner->second == "PhoenixHall" && lord->second == "Percival" &&
                   war_date->second == "2026-05-03 19:45" &&
                   std::stoull(refreshes->second) >= 6;
          },
          1s)) {
    std::cerr << "castle_push_refresh\n";
    cleanup();
    return 1;
  }

  const auto castle_payload =
      query_single_text(database_path,
                        "SELECT payload_json FROM castle_state WHERE castle_name='Sabuk' LIMIT 1;");
  if (castle_payload.find("\"owner_guild\":\"PhoenixHall\"") == std::string::npos ||
      castle_payload.find("\"castle_war_date\":\"2026-05-03 19:45\"") == std::string::npos ||
      castle_payload.find("\"list_of_war\":\"AzureSky, WhiteTiger\"") == std::string::npos ||
      castle_payload.find("\"guild_war_fee\":53000") == std::string::npos ||
      castle_payload.find("\"upgrade_weapon_fee\":17500") == std::string::npos) {
    std::cerr << "castle_payload_not_saved\n";
    cleanup();
    return 1;
  }

  cleanup();
  return 0;
}
