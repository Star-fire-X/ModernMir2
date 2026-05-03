#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/module.hpp"
#include "core/shutdown_token.hpp"
#include "protocol/legacy_game_codec.hpp"
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

bool packet_ident_is(const mir2::SessionEvent& event, std::uint16_t ident) {
  if (event.kind != mir2::SessionEventKind::send_packet &&
      event.kind != mir2::SessionEventKind::send_packet_and_close) {
    return false;
  }
  const auto decoded = mir2::decode_legacy_game_packet(event.packet);
  return decoded.has_value() && decoded->message.ident == ident;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();

  mir2::HostConfig config;
  config.runtime.default_queue_capacity = 256;
  config.runtime.castle_context_refresh_ms = 0;
  config.maps.push_back(mir2::MapConfig{"0", "TestMap", {}, 0, 0, 10, 10});

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto logger = std::make_shared<spdlog::logger>(
      "world_invalid_command_smoke", std::make_shared<spdlog::sinks::null_sink_mt>());

  mir2::HostContext context;
  context.root_dir = source_root;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;
  context.logger = logger;

  auto game_gateway = bus.register_endpoint("game_gateway", 256);

  mir2::WorldService world;
  world.start(context);

  const auto stop_service = [&] {
    world.stop();
    world.join();
  };

  mir2::SessionEvent invalid_move;
  invalid_move.kind = mir2::SessionEventKind::packet_received;
  invalid_move.gateway = "game_gateway";
  invalid_move.session_id = 7;
  invalid_move.packet = mir2::make_legacy_raw_packet(0, "MOVE nope 2");
  if (!bus.post("world_service", std::move(invalid_move))) {
    stop_service();
    return 1;
  }

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.ability.level = 1;
  hero.ability.hp = 15;
  hero.ability.mp = 15;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 15;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.gateway = "game_gateway";
  enter.session_id = 7;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  if (!bus.post("world_service", std::move(enter))) {
    stop_service();
    return 1;
  }

  const auto logon = wait_for_session_event(game_gateway, [](const mir2::SessionEvent& event) {
    return event.session_id == 7 && packet_ident_is(event, mir2::kSmLogon);
  });
  if (!logon.has_value()) {
    stop_service();
    return 1;
  }

  stop_service();
  return 0;
}
