#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

#include "core/default_modules.hpp"
#include "core/host_runtime.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

bool contains_all(const std::string& text, std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (text.find(needle) == std::string::npos) {
      return false;
    }
  }
  return true;
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 128;
  config.runtime.io_threads = 2;
  config.runtime.enable_legacy_gateways = false;
  config.runtime.enable_client_v1_gateways = false;
  config.runtime.castle_context_refresh_ms = 0;
  config.budgets.tick_ms = 5;
  config.maps.push_back(mir2::MapConfig{"0", "StatusMap", {}, 64, 64, 10, 10});
  return config;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_host_status_snapshot_smoke";
  const auto status_path = temp_root / "runtime" / "status.json";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  auto logger = std::make_shared<spdlog::logger>(
      "host_status_snapshot_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());
  mir2::HostRuntime runtime(source_root, make_config(temp_root), logger);
  mir2::register_default_modules(runtime);
  runtime.start_all();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  runtime.write_status_snapshot();
  const auto running_status = read_text(status_path);
  assert(contains_all(running_status,
                      {"\"queues\"", "\"metrics\"", "\"modules\"",
                       "\"log_service\"", "\"persistence_service\"",
                       "\"auth_service\"", "\"world_service\"",
                       "\"running\": \"true\"", "\"maps\": \"1\"",
                       "\"sessions\": \"0\"", "\"tick\":",
                       "\"legacy_frame_index\":", "\"legacy_last_stage\":",
                       "ServerMessageRun"}));

  runtime.stop_all();
  runtime.join_all();
  runtime.stop_all();
  runtime.join_all();
  runtime.write_status_snapshot();
  const auto stopped_status = read_text(status_path);
  assert(contains_all(stopped_status,
                      {"\"world_service\"", "\"running\": \"false\"",
                       "\"legacy_frame\": \"enabled\""}));

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
