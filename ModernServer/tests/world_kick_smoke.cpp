#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/null_sink.h"

namespace {

std::optional<mir2::SessionEvent> wait_for_session_event(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    const std::function<bool(const mir2::SessionEvent&)>& predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
        message.has_value()) {
      if (auto event = std::get_if<mir2::SessionEvent>(&*message); event != nullptr) {
        if (predicate(*event)) {
          return *event;
        }
      }
    }
  }
  return std::nullopt;
}

std::string make_run_login_body(const std::string& account_id, const std::string& character_name,
                                std::int32_t certification) {
  const auto xor1 = certification ^ static_cast<std::int32_t>(0xF2E44FFFu);
  const auto xor2 = certification ^ static_cast<std::int32_t>(0xA4A5B277u);
  const auto plain = "**" + account_id + "/" + character_name + "/" + std::to_string(certification) +
                     "/2026/" + std::to_string(xor1) + "/0/" + std::to_string(xor2) + "/0";
  return mir2::legacy_encode_string(plain);
}

bool packet_ident_is(const mir2::SessionEvent& event, std::uint16_t ident) {
  if (event.kind != mir2::SessionEventKind::send_packet &&
      event.kind != mir2::SessionEventKind::send_packet_and_close) {
    return false;
  }
  const auto decoded = mir2::decode_legacy_game_packet(event.packet);
  return decoded.has_value() && decoded->message.ident == ident;
}

std::optional<mir2::AuditEvent> wait_for_audit_event(
    const std::shared_ptr<mir2::LocalBus::Endpoint>& endpoint,
    const std::function<bool(const mir2::AuditEvent&)>& predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = endpoint->queue->wait_pop_for(std::chrono::milliseconds(25));
        message.has_value()) {
      if (auto audit = std::get_if<mir2::AuditEvent>(&*message);
          audit != nullptr && predicate(*audit)) {
        return *audit;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_world_kick_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.log_dir = temp_root / "logs";
  config.runtime.status_file = temp_root / "runtime" / "status.json";
  config.runtime.default_queue_capacity = 256;
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 0, 0, 330, 270});

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger =
      std::make_shared<spdlog::logger>("world_kick_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto game_gateway = bus.register_endpoint("game_gateway", 256);
  auto log_service = bus.register_endpoint("log_service", 256);

  mir2::PersistenceService persistence;
  mir2::WorldService world;
  persistence.start(context);
  world.start(context);

  const auto stop_services = [&] {
    world.stop();
    persistence.stop();
    world.join();
    persistence.join();
  };

  mir2::LogicCommand authenticate;
  authenticate.kind = mir2::LogicCommandKind::authenticate;
  authenticate.account_id = "guest";
  authenticate.character_name = "Hero";
  authenticate.certification = 1234;
  if (!bus.post("world_service", authenticate)) {
    stop_services();
    return 1;
  }

  mir2::SessionEvent run_login;
  run_login.kind = mir2::SessionEventKind::packet_received;
  run_login.gateway = "game_gateway";
  run_login.session_id = 77;
  run_login.peer_address = "127.0.0.1:7777";
  run_login.packet = mir2::make_legacy_raw_packet(0, make_run_login_body("guest", "Hero", 1234));
  if (!bus.post("world_service", std::move(run_login))) {
    stop_services();
    return 1;
  }

  const auto entered = wait_for_audit_event(log_service, [](const mir2::AuditEvent& audit) {
    return audit.category == "world.enter" && audit.message == "guest:Hero";
  });
  if (!entered.has_value()) {
    stop_services();
    return 1;
  }

  mir2::LogicCommand revoke;
  revoke.kind = mir2::LogicCommandKind::revoke_authentication;
  revoke.account_id = "guest";
  revoke.character_name = "Hero";
  revoke.certification = 1234;
  if (!bus.post("world_service", std::move(revoke))) {
    stop_services();
    return 1;
  }

  const auto kicked = wait_for_session_event(game_gateway, [](const mir2::SessionEvent& event) {
    return event.session_id == 77 && event.kind == mir2::SessionEventKind::send_packet_and_close &&
           packet_ident_is(event, mir2::kSmOutOfConnection);
  });
  if (!kicked.has_value() || kicked->gateway != "game_gateway" || kicked->reason != "duplicate_login" ||
      kicked->delay_ms != 50) {
    stop_services();
    return 1;
  }

  stop_services();
  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
