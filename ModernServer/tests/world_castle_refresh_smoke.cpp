#include <chrono>
#include <iostream>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
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

void copy_schema_tree(const std::filesystem::path& source_root, const std::filesystem::path& root) {
  std::filesystem::create_directories(root / "schema");
  std::filesystem::copy_file(source_root / "schema" / "mir2.sql", root / "schema" / "mir2.sql",
                             std::filesystem::copy_options::overwrite_existing);
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto root = std::filesystem::temp_directory_path() / "mir2_world_castle_refresh_smoke";
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
  config.runtime.castle_context_refresh_ms = 100;
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});
  mir2::NpcConfig npc;
  npc.id = "castle_guide";
  npc.map_id = "0";
  npc.name = "Castle Guide";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back(mir2::NpcDialogSectionConfig{
      "@main",
      "Guild: <$OWNERGUILD>\\Lord: <$LORD>\\War Date: <$CASTLEWARDATE>\\Rivals: <$LISTOFWAR>"});
  config.npcs.push_back(std::move(npc));

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));

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
            return owner != snapshot.end() && lord != snapshot.end() &&
                   owner->second == "DragonSlayers" && lord->second == "Arthur";
          },
          2s)) {
    std::cerr << "initial_snapshot_refresh\n";
    cleanup();
    return 1;
  }

  if (!exec_sql(database_path,
                "UPDATE guilds SET payload_json='{\"lord\":\"Lancelot\"}' "
                "WHERE guild_name='DragonSlayers';"
                "UPDATE castle_state SET payload_json="
                "'{\"owner_guild\":\"PhoenixHall\",\"castle_war_date\":\"2026-05-02 21:30\","
                "\"list_of_war\":[\"AzureSky\",\"WhiteTiger\"],\"guild_war_fee\":52000,"
                "\"upgrade_weapon_fee\":17000}' "
                "WHERE castle_name='Sabuk';")) {
    std::cerr << "update_castle_sql\n";
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
                   owner->second == "PhoenixHall" && lord->second == "Unclaimed" &&
                   war_date->second == "2026-05-02 21:30" &&
                   std::stoull(refreshes->second) >= 2;
          },
          3s)) {
    std::cerr << "updated_snapshot_refresh\n";
    cleanup();
    return 1;
  }

  cleanup();
  return 0;
}
