#include "world/legacy_frame_driver.hpp"

#include <chrono>
#include <iterator>
#include <type_traits>
#include <utility>

namespace mir2 {

namespace {

[[nodiscard]] std::size_t dispatch_count(const RuntimeDispatch& dispatch) {
  return dispatch.session_events.size() + dispatch.audit_events.size() +
         dispatch.persist_requests.size() + dispatch.cross_map_mails.size() +
         dispatch.legacy_time_recall_requests.size() + dispatch.legacy_traces.size();
}

void append_dispatch(RuntimeDispatch& target, RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_time_recall_requests.insert(
      target.legacy_time_recall_requests.end(),
      std::make_move_iterator(source.legacy_time_recall_requests.begin()),
      std::make_move_iterator(source.legacy_time_recall_requests.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

}  // namespace

std::string_view legacy_frame_stage_name(LegacyFrameStage stage) {
  switch (stage) {
    case LegacyFrameStage::run_socket_run:
      return "RunSocketRun";
    case LegacyFrameStage::decode_id_socket:
      return "DecodeIdSocket";
    case LegacyFrameStage::user_engine_execute_run:
      return "UserEngineExecuteRun";
    case LegacyFrameStage::event_manager_run:
      return "EventManagerRun";
    case LegacyFrameStage::server_message_run:
      return "ServerMessageRun";
  }
  return "Unknown";
}

// ── Delphi RunSocket.Run → C++ frame stage mapping ──────────────────
//
//  Delphi (RunSock.pas)              C++ (LegacyFrameDriver)
//  ─────────────────────────────     ──────────────────────────
//  1. Receive gate buffers          RunSocketRun
//     + flush previous outbound       (run_legacy_socket_stage:
//                                      flush last frame dispatch,
//                                      drain pending gate events)
//  2. DecodeIdSocket                DecodeIdSocket
//     (parse TMsgHeader +             (process_ingress_batch:
//      route to user engine)           accept ingress, dispatch
//                                      to handlers)
//  3. UserEngine.ExecuteRun          UserEngineExecuteRun
//     (ProcessUserHumans →            (runtime_->tick:
//      ProcessMonsters →              process ready users,
//      ProcessMerchants →             monsters, merchants,
//      ProcessNpcs)                    npcs)
//  4. EventMan.Run                   EventManagerRun
//     (timed events)                  (legacy event manager tick)
//  5. ServerMessage.Run              ServerMessageRun
//     (broadcast system msgs)         (reserved, currently empty)
//
//  Stage order is fixed and must not be rearranged — Delphi scripts
//  and legacy client rendering depend on the processing sequence.
// ──────────────────────────────────────────────────────────────────────
RuntimeDispatch LegacyFrameDriver::run_frame(std::uint64_t now_ms, WorldIngressBatch ingress_batch,
                                             const LegacyFrameCallbacks& callbacks) {
  RuntimeDispatch combined;
  LegacyFrameTrace trace;
  trace.frame_index = ++frame_index_;
  trace.now_ms = now_ms;
  trace.stages.reserve(5);
  ingress_batch.mark_frame(trace.frame_index);

  const auto frame_start = std::chrono::steady_clock::now();

  const auto run_stage = [&](LegacyFrameStage stage, std::size_t input_count,
                             auto&& callback) {
    const auto stage_start = std::chrono::steady_clock::now();
    RuntimeDispatch dispatch;
    if constexpr (std::is_invocable_r_v<RuntimeDispatch, decltype(callback), WorldIngressBatch&>) {
      dispatch = callback(ingress_batch);
    } else {
      dispatch = callback();
    }
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - stage_start)
            .count());

    LegacyFrameStageTrace stage_trace;
    stage_trace.stage = stage;
    stage_trace.now_ms = now_ms;
    stage_trace.elapsed_us = elapsed_us;
    stage_trace.input_count = input_count;
    stage_trace.output_count = dispatch_count(dispatch);
    trace.last_stage = std::string(legacy_frame_stage_name(stage));
    trace.stages.push_back(stage_trace);
    append_dispatch(combined, std::move(dispatch));
  };

  run_stage(LegacyFrameStage::run_socket_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.run_socket_run) {
      return callbacks.run_socket_run();
    }
    return {};
  });
  run_stage(LegacyFrameStage::decode_id_socket, ingress_batch.size(),
            [&](WorldIngressBatch& batch) -> RuntimeDispatch {
              if (callbacks.decode_id_socket) {
                return callbacks.decode_id_socket(batch);
              }
              return {};
            });
  if (!ingress_batch.empty()) {
    LegacyRuntimeTrace guard;
    guard.stage = std::string(legacy_frame_stage_name(LegacyFrameStage::decode_id_socket));
    guard.action = "ingress_batch_not_cleared";
    guard.now_ms = now_ms;
    guard.current_tick = trace.frame_index;
    guard.cursor = ingress_batch.size();
    guard.success = false;
    combined.legacy_traces.push_back(std::move(guard));
    ingress_batch.messages.clear();
  }
  run_stage(LegacyFrameStage::user_engine_execute_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.user_engine_execute_run) {
      return callbacks.user_engine_execute_run();
    }
    return {};
  });
  run_stage(LegacyFrameStage::event_manager_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.event_manager_run) {
      return callbacks.event_manager_run();
    }
    return {};
  });
  run_stage(LegacyFrameStage::server_message_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.server_message_run) {
      return callbacks.server_message_run();
    }
    return {};
  });

  trace.elapsed_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                            frame_start)
          .count());
  trace.last_frame_ms = trace.elapsed_us / 1000;
  last_trace_ = std::move(trace);
  return combined;
}

}  // namespace mir2
