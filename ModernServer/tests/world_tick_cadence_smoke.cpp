#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "services/world_service.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

std::uint64_t value_u64(const std::unordered_map<std::string, std::string>& snapshot,
                        const std::string& key) {
  const auto it = snapshot.find(key);
  assert(it != snapshot.end());
  return std::stoull(it->second);
}

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 128;
  config.runtime.castle_context_refresh_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "CadenceMap", {}, 64, 64, 10, 10});
  return config;
}

std::unordered_map<std::string, std::string> wait_for_frame(mir2::WorldService& world,
                                                            std::uint64_t min_frame) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = world.snapshot();
    if (snapshot["legacy_last_stage"] == "ServerMessageRun" &&
        value_u64(snapshot, "legacy_frame_index") >= min_frame &&
        value_u64(snapshot, "tick") >= min_frame) {
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(false && "world did not advance to requested frame");
  return {};
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_world_tick_cadence_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root / "data", ignored);

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));
  static_cast<void>(bus.register_endpoint("game_gateway", 128));

  auto logger = std::make_shared<spdlog::logger>(
      "world_tick_cadence_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = temp_root;
  context.config = make_config(temp_root);
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  mir2::WorldService world;
  world.start(context);

  const auto first = wait_for_frame(world, 3);
  const auto second = wait_for_frame(world, value_u64(first, "legacy_frame_index") + 3);

  assert(first.at("running") == "true");
  assert(first.at("maps") == "1");
  assert(first.at("sessions") == "0");
  assert(first.at("legacy_frame") == "enabled");
  assert(first.at("legacy_last_stage") == "ServerMessageRun");
  assert(first.contains("pending_gate_events"));
  assert(first.contains("run_socket_last_flushed"));
  assert(first.contains("run_socket_last_remaining"));
  assert(first.contains("run_socket_last_ms"));
  assert(value_u64(second, "legacy_frame_index") > value_u64(first, "legacy_frame_index"));
  assert(value_u64(second, "tick") > value_u64(first, "tick"));

  world.stop();
  bus.close_all();
  world.join();
  const auto stopped = world.snapshot();
  assert(stopped.at("running") == "false");

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
