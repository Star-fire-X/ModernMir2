#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "core/host_runtime.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "world_shutdown_persistence_smoke failed at " << stage << '\n';
  return 1;
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.runtime.enable_legacy_gateways = false;
  config.runtime.enable_client_v1_gateways = false;
  config.runtime.castle_context_refresh_ms = 0;
  config.budgets.tick_ms = 10;
  config.maps.push_back(mir2::MapConfig{"0", "ShutdownMap", {}, 700, 700, 330, 270});
  return config;
}

mir2::CharacterRecord make_online_character() {
  mir2::CharacterRecord character;
  character.account_id = "guest";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 345;
  character.y = 278;
  character.gold = 7654;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.mp = 15;
  character.ability.max_hp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.bag_items[0].make_index = 8801;
  character.bag_items[0].index = 2;
  character.bag_items[0].dura = 1000;
  character.bag_items[0].dura_max = 1000;
  return character;
}

bool wait_snapshot_value(mir2::Module& module, const std::string& key,
                         const std::string& expected) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = module.snapshot();
    const auto it = snapshot.find(key);
    if (it != snapshot.end() && it->second == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_world_shutdown_persistence_smoke";
  const auto database_path = temp_root / "data" / "mir2.sqlite";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  auto logger = std::make_shared<spdlog::logger>(
      "world_shutdown_persistence_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());
  mir2::HostRuntime runtime(source_root, make_config(temp_root), logger);

  auto persistence = std::make_unique<mir2::PersistenceService>();
  auto world = std::make_unique<mir2::WorldService>();
  auto* world_ptr = world.get();
  runtime.register_module(std::move(persistence));
  runtime.register_module(std::move(world));
  runtime.start_all();

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.gateway = "game_gateway";
  enter.session_id = 77;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 345;
  enter.y = 278;
  enter.character = make_online_character();
  if (!runtime.context().bus->post("world_service", enter)) {
    runtime.stop_all();
    runtime.join_all();
    return fail("post enter world");
  }

  if (!wait_snapshot_value(*world_ptr, "sessions", "1")) {
    runtime.stop_all();
    runtime.join_all();
    return fail("online session");
  }

  runtime.stop_all();
  runtime.join_all();

  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  const auto saved = repository.load_character("guest", "Hero");
  if (!saved.has_value() || saved->x != 345 || saved->y != 278 || saved->gold != 7654 ||
      saved->bag_items[0].make_index != 8801 || saved->save_version == 0) {
    std::filesystem::remove_all(temp_root, ignored);
    return fail("shutdown save");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
