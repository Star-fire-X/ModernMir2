#include <cassert>
#include <filesystem>
#include <fstream>

#include "config/config_loader.hpp"

namespace {

void write_text(const std::filesystem::path& path, const char* text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

}  // namespace

int main() {
  assert(mir2::LogicBudgetConfig{}.tick_ms == 10);

  const auto root = std::filesystem::temp_directory_path() / "mir2_config_gateway_flags_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  write_text(root / "server.toml",
             "log_dir = \"logs\"\n"
             "data_dir = \"data\"\n"
             "status_file = \"runtime/status.json\"\n"
             "io_threads = 2\n"
             "enable_legacy_gateways = false\n"
             "enable_client_v1_gateways = true\n");
  write_text(root / "ports.toml",
             "[login_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 5500\n"
             "[game_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 7000\n"
             "[client_v1_login_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 5600\n"
             "[client_v1_game_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 7100\n");
  write_text(root / "runtime" / "logic.toml",
             "tick_ms = 20\n"
             "player_budget_ms = 30\n"
             "player_input_budget_per_tick = 2\n"
             "monster_budget_ms = 30\n"
             "spawn_budget_ms = 30\n"
             "npc_budget_ms = 5\n"
             "net_flush_budget_ms = 30\n");
  write_text(root / "maps" / "0.toml",
             "id = \"0\"\n"
             "title = \"Test\"\n"
             "width = 100\n"
             "height = 100\n"
             "home_x = 10\n"
             "home_y = 10\n");
  std::filesystem::create_directories(root / "spawns", ignored);
  std::filesystem::create_directories(root / "items", ignored);
  std::filesystem::create_directories(root / "magic", ignored);
  std::filesystem::create_directories(root / "npcs", ignored);

  mir2::ConfigLoader loader;
  const auto config = loader.load(root);
  assert(!config.runtime.enable_legacy_gateways);
  assert(config.runtime.enable_client_v1_gateways);
  assert(!config.runtime.legacy_approval_mode);
  assert(config.runtime.io_threads == 2);
  assert(config.budgets.tick_ms == 20);
  assert(config.budgets.player_input_budget_per_tick == 2);

  write_text(root / "server.toml",
             "log_dir = \"logs\"\n"
             "data_dir = \"data\"\n"
             "status_file = \"runtime/status.json\"\n"
             "io_threads = 2\n"
             "enable_legacy_gateways = false\n"
             "enable_client_v1_gateways = true\n"
             "legacy_approval_mode = true\n");
  const auto approval_config = loader.load(root);
  assert(approval_config.runtime.legacy_approval_mode);
  assert(approval_config.budgets.tick_ms == 20);

  write_text(root / "runtime" / "logic.toml",
             "player_budget_ms = 30\n"
             "player_input_budget_per_tick = 2\n"
             "monster_budget_ms = 30\n"
             "spawn_budget_ms = 30\n"
             "npc_budget_ms = 5\n"
             "net_flush_budget_ms = 30\n");
  const auto default_tick_config = loader.load(root);
  assert(default_tick_config.budgets.tick_ms == 10);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
