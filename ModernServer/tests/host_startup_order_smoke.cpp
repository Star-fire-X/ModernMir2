#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/default_modules.hpp"
#include "core/host_runtime.hpp"
#include "core/module.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

class RecordingModule final : public mir2::Module {
 public:
  RecordingModule(std::string module_name, std::vector<std::string>& events)
      : module_name_(std::move(module_name)), events_(events) {}

  [[nodiscard]] std::string name() const override { return module_name_; }

  void start(mir2::HostContext&) override {
    running_ = true;
    events_.push_back("start:" + module_name_);
  }

  void stop() override {
    if (!running_) {
      return;
    }
    running_ = false;
    events_.push_back("stop:" + module_name_);
  }

  void join() override {
    if (joined_) {
      return;
    }
    joined_ = true;
    events_.push_back("join:" + module_name_);
  }

  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override {
    return {{"running", running_ ? "true" : "false"}};
  }

 private:
  std::string module_name_{};
  std::vector<std::string>& events_;
  bool running_{false};
  bool joined_{false};
};

mir2::HostConfig make_config(const std::filesystem::path& temp_root) {
  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.enable_legacy_gateways = false;
  config.runtime.enable_client_v1_gateways = false;
  config.ports.login_gateway.address = "127.0.0.1";
  config.ports.login_gateway.port = 5527;
  config.ports.game_gateway.address = "127.0.0.1";
  config.ports.game_gateway.port = 7027;
  config.ports.client_v1_login_gateway.address = "127.0.0.1";
  config.ports.client_v1_login_gateway.port = 5627;
  config.ports.client_v1_game_gateway.address = "127.0.0.1";
  config.ports.client_v1_game_gateway.port = 7127;
  return config;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

bool appears_in_order(const std::string& text, const std::vector<std::string>& needles) {
  std::size_t cursor = 0;
  for (const auto& needle : needles) {
    const auto next = text.find(needle, cursor);
    if (next == std::string::npos) {
      return false;
    }
    cursor = next + needle.size();
  }
  return true;
}

void check_runtime_sequence(const std::filesystem::path& temp_root,
                            const std::shared_ptr<spdlog::logger>& logger) {
  std::vector<std::string> events;
  auto config = make_config(temp_root / "sequence");
  mir2::HostRuntime runtime(temp_root, std::move(config), logger);
  runtime.register_module(std::make_unique<RecordingModule>("first", events));
  runtime.register_module(std::make_unique<RecordingModule>("second", events));
  runtime.register_module(std::make_unique<RecordingModule>("third", events));

  runtime.start_all();
  runtime.stop_all();
  runtime.join_all();
  runtime.stop_all();
  runtime.join_all();

  const std::vector<std::string> expected_prefix{
      "start:first", "start:second", "start:third",
      "stop:third", "join:third", "stop:second", "join:second", "stop:first",
      "join:first"};
  assert(events.size() >= expected_prefix.size());
  for (std::size_t index = 0; index < expected_prefix.size(); ++index) {
    assert(events[index] == expected_prefix[index]);
  }
}

void check_default_module_registration(const std::filesystem::path& temp_root,
                                       const std::shared_ptr<spdlog::logger>& logger) {
  {
    auto config = make_config(temp_root / "all_gateways");
    config.runtime.enable_legacy_gateways = true;
    config.runtime.enable_client_v1_gateways = true;
    config.runtime.status_file = temp_root / "all_gateways" / "status.json";
    mir2::HostRuntime runtime(temp_root, std::move(config), logger);
    mir2::register_default_modules(runtime);
    runtime.write_status_snapshot();

    const auto status = read_text(temp_root / "all_gateways" / "status.json");
    assert(appears_in_order(status,
                            {"\"log_service\"", "\"persistence_service\"",
                             "\"auth_service\"", "\"world_service\"",
                             "\"login_gateway\"", "\"game_gateway\"",
                             "\"client_v1_login_gateway\"",
                             "\"client_v1_game_gateway\""}));
  }

  {
    auto config = make_config(temp_root / "client_only");
    config.runtime.enable_legacy_gateways = false;
    config.runtime.enable_client_v1_gateways = true;
    config.runtime.status_file = temp_root / "client_only" / "status.json";
    mir2::HostRuntime runtime(temp_root, std::move(config), logger);
    mir2::register_default_modules(runtime);
    runtime.write_status_snapshot();

    const auto status = read_text(temp_root / "client_only" / "status.json");
    assert(status.find("\"login_gateway\":") == std::string::npos);
    assert(status.find("\"game_gateway\":") == std::string::npos);
    assert(status.find("\"client_v1_login_gateway\":") != std::string::npos);
    assert(status.find("\"client_v1_game_gateway\":") != std::string::npos);
  }
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_host_startup_order_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  auto logger = std::make_shared<spdlog::logger>(
      "host_startup_order_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());
  check_runtime_sequence(temp_root, logger);
  check_default_module_registration(temp_root, logger);

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
