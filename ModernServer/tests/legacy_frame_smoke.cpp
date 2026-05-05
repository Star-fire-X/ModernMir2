#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "core/local_bus.hpp"
#include "core/metrics_registry.hpp"
#include "core/shutdown_token.hpp"
#include "services/world_service.hpp"
#include "world/legacy_frame_driver.hpp"
#include "world/logic_runtime.hpp"

namespace {

using namespace std::chrono_literals;

std::size_t stage_index(const mir2::LegacyFrameTrace& trace, mir2::LegacyFrameStage stage) {
  for (std::size_t i = 0; i < trace.stages.size(); ++i) {
    if (trace.stages[i].stage == stage) {
      return i;
    }
  }
  return trace.stages.size();
}

bool check_frame_driver_order() {
  mir2::LegacyFrameDriver driver;
  mir2::WorldIngressBatch batch;
  batch.messages.push_back(mir2::SessionEvent{});
  mir2::PersistResult persist;
  persist.kind = mir2::PersistResultKind::schema_ready;
  batch.messages.push_back(persist);
  mir2::ActorMail mail;
  mail.kind = mir2::ActorMailKind::system_notice;
  batch.messages.push_back(mail);

  std::vector<std::string> observed;
  bool ingress_fifo = true;
  mir2::LegacyFrameCallbacks callbacks;
  callbacks.run_socket_run = [&]() -> mir2::RuntimeDispatch {
    observed.emplace_back("RunSocketRun");
    return {};
  };
  callbacks.decode_id_socket = [&](mir2::WorldIngressBatch& ingress) -> mir2::RuntimeDispatch {
    observed.emplace_back("DecodeIdSocket");
    ingress_fifo = ingress.size() == 3 && std::get_if<mir2::SessionEvent>(&ingress.messages[0]) != nullptr &&
                   std::get_if<mir2::PersistResult>(&ingress.messages[1]) != nullptr &&
                   std::get_if<mir2::ActorMail>(&ingress.messages[2]) != nullptr;
    mir2::RuntimeDispatch dispatch;
    dispatch.session_events.push_back(
        mir2::SessionEvent{mir2::SessionEventKind::send_packet, "game_gateway", 1});
    dispatch.audit_events.push_back({"legacy_frame.decode", "ok", "smoke"});
    return dispatch;
  };
  callbacks.user_engine_execute_run = [&]() -> mir2::RuntimeDispatch {
    observed.emplace_back("UserEngineExecuteRun");
    mir2::RuntimeDispatch dispatch;
    dispatch.session_events.push_back(
        mir2::SessionEvent{mir2::SessionEventKind::send_packet, "game_gateway", 2});
    return dispatch;
  };
  callbacks.event_manager_run = [&]() -> mir2::RuntimeDispatch {
    observed.emplace_back("EventManagerRun");
    mir2::RuntimeDispatch dispatch;
    dispatch.session_events.push_back(
        mir2::SessionEvent{mir2::SessionEventKind::force_disconnect, "game_gateway", 3});
    return dispatch;
  };
  callbacks.server_message_run = [&]() -> mir2::RuntimeDispatch {
    observed.emplace_back("ServerMessageRun");
    return {};
  };

  const auto dispatch = driver.run_frame(1000, std::move(batch), callbacks);
  const std::vector<std::string> expected{"RunSocketRun", "DecodeIdSocket",
                                          "UserEngineExecuteRun", "EventManagerRun",
                                          "ServerMessageRun"};
  if (observed != expected || !ingress_fifo) {
    std::cerr << "legacy_frame_order\n";
    return false;
  }
  if (dispatch.audit_events.size() != 1 || dispatch.session_events.size() != 3) {
    std::cerr << "legacy_frame_dispatch\n";
    return false;
  }
  if (dispatch.session_events[0].session_id != 1 ||
      dispatch.session_events[1].session_id != 2 ||
      dispatch.session_events[2].session_id != 3) {
    std::cerr << "legacy_frame_session_fifo\n";
    return false;
  }

  const auto& first_trace = driver.last_trace();
  if (first_trace.frame_index != 1 || first_trace.now_ms != 1000 ||
      first_trace.last_stage != "ServerMessageRun" || first_trace.stages.size() != 5 ||
      first_trace.stages[stage_index(first_trace, mir2::LegacyFrameStage::decode_id_socket)]
              .input_count != 3) {
    std::cerr << "legacy_frame_trace\n";
    return false;
  }
  if (first_trace.stages[stage_index(first_trace, mir2::LegacyFrameStage::run_socket_run)]
          .output_count != 0 ||
      first_trace.stages[stage_index(first_trace, mir2::LegacyFrameStage::decode_id_socket)]
          .output_count != 2 ||
      first_trace.stages[stage_index(first_trace,
                                     mir2::LegacyFrameStage::user_engine_execute_run)]
          .output_count != 1 ||
      stage_index(first_trace, mir2::LegacyFrameStage::user_engine_execute_run) >=
          stage_index(first_trace, mir2::LegacyFrameStage::event_manager_run)) {
    std::cerr << "legacy_frame_stage_counts\n";
    return false;
  }
  const auto first_now_ms = first_trace.now_ms;

  const auto second_dispatch = driver.run_frame(1010, {}, callbacks);
  static_cast<void>(second_dispatch);
  if (driver.last_trace().frame_index != 2 || driver.last_trace().now_ms < first_now_ms) {
    std::cerr << "legacy_frame_monotonic\n";
    return false;
  }
  return true;
}

bool check_map_order() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "Map0", {}, 0, 0, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "Map1", {}, 1, 1, 10, 10});
  config.maps.push_back(mir2::MapConfig{"2", "Map2", {}, 2, 2, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  const std::vector<std::string> expected{"0", "1", "2"};
  if (runtime.map_order() != expected) {
    std::cerr << "map_order\n";
    return false;
  }
  return true;
}

bool wait_for_legacy_frame(mir2::WorldService& world) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = world.snapshot();
    const auto frame = snapshot.find("legacy_frame_index");
    const auto stage = snapshot.find("legacy_last_stage");
    if (frame != snapshot.end() && stage != snapshot.end() && stage->second == "ServerMessageRun") {
      try {
        if (std::stoull(frame->second) >= 2) {
          return true;
        }
      } catch (...) {
        return false;
      }
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

bool check_world_service_snapshot() {
  mir2::HostConfig config;
  config.runtime.default_queue_capacity = 128;
  config.budgets.tick_ms = 5;
  config.maps.push_back(mir2::MapConfig{"0", "Map0", {}, 0, 0, 10, 10});

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  static_cast<void>(bus.register_endpoint("log_service", 128));
  static_cast<void>(bus.register_endpoint("game_gateway", 128));

  mir2::HostContext context;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;

  mir2::WorldService world;
  world.start(context);
  const bool ready = wait_for_legacy_frame(world);
  world.stop();
  bus.close_all();
  world.join();
  if (!ready) {
    std::cerr << "world_snapshot_legacy_frame\n";
  }
  return ready;
}

bool check_gate_fifo_zero_budget() {
  mir2::HostConfig config;
  config.budgets.net_flush_budget_ms = 0;

  mir2::LocalBus bus;
  mir2::MetricsRegistry metrics;
  mir2::ShutdownToken shutdown;
  auto gateway = bus.register_endpoint("game_gateway", 128);

  mir2::HostContext context;
  context.config = config;
  context.bus = &bus;
  context.metrics = &metrics;
  context.shutdown = &shutdown;

  mir2::WorldService world;
  world.attach_context_for_test(context);
  world.enqueue_gate_event_for_test(
      mir2::SessionEvent{mir2::SessionEventKind::send_packet, "game_gateway", 11});
  world.enqueue_gate_event_for_test(
      mir2::SessionEvent{mir2::SessionEventKind::send_packet_and_close, "game_gateway", 12});
  world.enqueue_gate_event_for_test(
      mir2::SessionEvent{mir2::SessionEventKind::force_disconnect, "game_gateway", 13});

  const auto dispatch = world.run_legacy_socket_stage_for_test(5000);
  if (dispatch.legacy_traces.size() != 3 || bus.queue_depth("game_gateway") != 3) {
    std::cerr << "gate_fifo_zero_budget_count\n";
    return false;
  }

  for (const auto expected_session : {11ULL, 12ULL, 13ULL}) {
    auto message = gateway->queue->try_pop();
    if (!message.has_value()) {
      std::cerr << "gate_fifo_missing_message\n";
      return false;
    }
    const auto* event = std::get_if<mir2::SessionEvent>(&*message);
    if (event == nullptr || event->session_id != expected_session) {
      std::cerr << "gate_fifo_order\n";
      return false;
    }
  }

  const auto empty = world.run_legacy_socket_stage_for_test(5001);
  if (!empty.legacy_traces.empty()) {
    std::cerr << "gate_fifo_not_drained\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!check_frame_driver_order()) {
    return 1;
  }
  if (!check_map_order()) {
    return 1;
  }
  if (!check_world_service_snapshot()) {
    return 1;
  }
  if (!check_gate_fifo_zero_budget()) {
    return 1;
  }
  return 0;
}
