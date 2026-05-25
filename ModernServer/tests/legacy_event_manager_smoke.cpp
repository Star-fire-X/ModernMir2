#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "world/legacy_frame_driver.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "EventMap", {}, 0, 0, 20, 20});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

std::size_t frame_stage_index(const mir2::LegacyFrameTrace& trace,
                              mir2::LegacyFrameStage stage) {
  for (std::size_t index = 0; index < trace.stages.size(); ++index) {
    if (trace.stages[index].stage == stage) {
      return index;
    }
  }
  return trace.stages.size();
}

std::vector<std::string> event_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyEventManager") {
      actions.push_back(trace.action + ":" + trace.object_name + ":" + trace.label);
    }
  }
  return actions;
}

std::vector<mir2::LegacyRuntimeTrace> event_traces(const mir2::RuntimeDispatch& dispatch) {
  std::vector<mir2::LegacyRuntimeTrace> traces;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyEventManager") {
      traces.push_back(trace);
    }
  }
  return traces;
}

bool has_action(const std::vector<std::string>& actions, const std::string& action) {
  return std::find(actions.begin(), actions.end(), action) != actions.end();
}

bool has_event_trace_value(const mir2::RuntimeDispatch& dispatch, const std::string& action,
                           const std::string& object_name, std::int32_t value) {
  const auto traces = event_traces(dispatch);
  return std::any_of(traces.begin(), traces.end(), [&](const auto& trace) {
    return trace.action == action && trace.object_name == object_name && trace.value == value;
  });
}

std::size_t action_count(const std::vector<std::string>& actions,
                         const std::string& action) {
  return static_cast<std::size_t>(
      std::count(actions.begin(), actions.end(), action));
}

mir2::LegacyEventRecord event_at(std::int32_t x, mir2::LegacyEventType type) {
  mir2::LegacyEventRecord event;
  event.map_id = "0";
  event.x = x;
  event.y = 5;
  event.type = type;
  event.continue_ms = 10ULL * 60ULL * 1000ULL;
  return event;
}

void check_tick_boundary() {
  auto runtime = make_runtime();
  auto event = event_at(1, mir2::LegacyEventType::holy_curtain);
  event.run_tick_ms = 500;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  assert(event_actions(runtime.run_legacy_event_manager(499)).empty());
  assert(event_actions(runtime.run_legacy_event_manager(500)).empty());

  const auto actions = event_actions(runtime.run_legacy_event_manager(501));
  assert(actions.size() == 1);
  assert(actions[0] == "run:holy_curtain:1,5");
  assert(runtime.legacy_active_event_count() == 1);
}

void check_continue_close_boundary_and_map_state() {
  auto runtime = make_runtime();
  auto event = event_at(2, mir2::LegacyEventType::pile_stones);
  event.run_tick_ms = 500;
  event.continue_ms = 500;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  assert(runtime.find_legacy_event("0", 2, 5,
                                   mir2::LegacyEventType::pile_stones)
             .has_value());
  assert(event_actions(runtime.run_legacy_event_manager(500)).empty());

  const auto actions = event_actions(runtime.run_legacy_event_manager(501));
  assert(actions.size() == 2);
  assert(actions[0] == "run:pile_stones:2,5");
  assert(actions[1] == "close:pile_stones:2,5");
  assert(runtime.legacy_active_event_count() == 0);
  assert(runtime.legacy_closed_event_count() == 1);
  assert(!runtime.find_legacy_event("0", 2, 5,
                                    mir2::LegacyEventType::pile_stones)
              .has_value());
}

void check_zero_continue_closes_on_first_eligible_run() {
  auto runtime = make_runtime();
  auto event = event_at(3, mir2::LegacyEventType::pile_stones);
  event.run_tick_ms = 500;
  event.continue_ms = 0;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  assert(event_actions(runtime.run_legacy_event_manager(500)).empty());

  const auto actions = event_actions(runtime.run_legacy_event_manager(501));
  assert(actions.size() == 2);
  assert(actions[0] == "run:pile_stones:3,5");
  assert(actions[1] == "close:pile_stones:3,5");
  assert(runtime.legacy_active_event_count() == 0);
  assert(runtime.legacy_closed_event_count() == 1);
}

void check_fire_burn_first_tick_and_close_order() {
  auto runtime = make_runtime();
  auto event = event_at(4, mir2::LegacyEventType::fire_burn);
  event.run_tick_ms = 500;
  event.continue_ms = 500;
  event.damage = 7;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  const auto actions = event_actions(runtime.run_legacy_event_manager(501));
  assert(actions.size() == 3);
  assert(actions[0] == "run:fire_burn:4,5");
  assert(actions[1] == "fire_tick:fire_burn:4,5");
  assert(actions[2] == "close:fire_burn:4,5");

  const auto after_close = event_actions(runtime.run_legacy_event_manager(4002));
  assert(action_count(after_close, "fire_tick:fire_burn:4,5") == 0);
}

void check_fire_burn_damage_tick_boundary() {
  auto exact = make_runtime();
  auto exact_event = event_at(5, mir2::LegacyEventType::fire_burn);
  exact_event.run_start_ms = 1;
  exact_event.run_tick_ms = 500;
  exact_event.last_damage_ms = 1000;
  exact_event.continue_ms = 10ULL * 60ULL * 1000ULL;
  exact_event.damage = 7;
  static_cast<void>(exact.enqueue_legacy_event(exact_event));

  const auto exact_actions = event_actions(exact.run_legacy_event_manager(4000));
  assert(has_action(exact_actions, "run:fire_burn:5,5"));
  assert(!has_action(exact_actions, "fire_tick:fire_burn:5,5"));

  auto elapsed = make_runtime();
  auto elapsed_event = exact_event;
  elapsed_event.x = 6;
  static_cast<void>(elapsed.enqueue_legacy_event(elapsed_event));

  const auto elapsed_actions = event_actions(elapsed.run_legacy_event_manager(4001));
  assert(has_action(elapsed_actions, "run:fire_burn:6,5"));
  assert(has_action(elapsed_actions, "fire_tick:fire_burn:6,5"));
}

void check_delete_does_not_skip_shifted_event() {
  auto runtime = make_runtime();
  auto first = event_at(7, mir2::LegacyEventType::pile_stones);
  first.run_tick_ms = 500;
  first.continue_ms = 500;
  auto second = event_at(8, mir2::LegacyEventType::holy_curtain);
  second.run_tick_ms = 500;
  second.continue_ms = 500;
  static_cast<void>(runtime.enqueue_legacy_event(first));
  static_cast<void>(runtime.enqueue_legacy_event(second));

  const auto actions = event_actions(runtime.run_legacy_event_manager(501));
  assert(actions.size() == 4);
  assert(actions[0] == "run:pile_stones:7,5");
  assert(actions[1] == "close:pile_stones:7,5");
  assert(actions[2] == "run:holy_curtain:8,5");
  assert(actions[3] == "close:holy_curtain:8,5");
  assert(runtime.legacy_active_event_count() == 0);
  assert(runtime.legacy_closed_event_count() == 2);
}

void check_closed_list_cleans_one_event_per_run() {
  auto runtime = make_runtime();
  for (std::int32_t x = 9; x < 12; ++x) {
    auto event = event_at(x, mir2::LegacyEventType::pile_stones);
    event.run_tick_ms = 500;
    event.continue_ms = 500;
    static_cast<void>(runtime.enqueue_legacy_event(event));
  }

  static_cast<void>(runtime.run_legacy_event_manager(501));
  assert(runtime.legacy_closed_event_count() == 3);

  constexpr auto closed_ttl_ms = 5ULL * 60ULL * 1000ULL;
  const auto first_cleanup =
      event_actions(runtime.run_legacy_event_manager(501 + closed_ttl_ms + 1));
  assert(action_count(first_cleanup, "cleanup_closed:pile_stones:9,5") == 1);
  assert(runtime.legacy_closed_event_count() == 2);

  const auto second_cleanup =
      event_actions(runtime.run_legacy_event_manager(501 + closed_ttl_ms + 2));
  assert(action_count(second_cleanup, "cleanup_closed:pile_stones:10,5") == 1);
  assert(runtime.legacy_closed_event_count() == 1);

  const auto third_cleanup =
      event_actions(runtime.run_legacy_event_manager(501 + closed_ttl_ms + 3));
  assert(action_count(third_cleanup, "cleanup_closed:pile_stones:11,5") == 1);
  assert(runtime.legacy_closed_event_count() == 0);
}

void check_base_event_types_and_event_param_trace() {
  auto runtime = make_runtime();
  auto digout = event_at(12, mir2::LegacyEventType::digout_zombi);
  digout.run_tick_ms = 500;
  digout.continue_ms = 500;
  digout.event_param = 123;
  auto sculp = event_at(13, mir2::LegacyEventType::sculp_piece);
  sculp.run_tick_ms = 500;
  sculp.continue_ms = 500;
  sculp.event_param = 456;
  static_cast<void>(runtime.enqueue_legacy_event(digout));
  static_cast<void>(runtime.enqueue_legacy_event(sculp));

  const auto dispatch = runtime.run_legacy_event_manager(501);
  const auto actions = event_actions(dispatch);
  assert(actions.size() == 4);
  assert(actions[0] == "run:digout_zombi:12,5");
  assert(actions[1] == "close:digout_zombi:12,5");
  assert(actions[2] == "run:sculp_piece:13,5");
  assert(actions[3] == "close:sculp_piece:13,5");
  assert(has_event_trace_value(dispatch, "run", "digout_zombi", 123));
  assert(has_event_trace_value(dispatch, "close", "digout_zombi", 123));
  assert(has_event_trace_value(dispatch, "run", "sculp_piece", 456));
  assert(has_event_trace_value(dispatch, "close", "sculp_piece", 456));
}

void check_delta_run_boundary_wraparound() {
  auto runtime = make_runtime();
  constexpr auto max_tick = std::numeric_limits<std::uint64_t>::max();
  auto event = event_at(14, mir2::LegacyEventType::holy_curtain);
  event.open_start_ms = max_tick - 100;
  event.run_start_ms = max_tick - 100;
  event.run_tick_ms = 500;
  event.continue_ms = 10ULL * 60ULL * 1000ULL;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  assert(event_actions(runtime.run_legacy_event_manager(399)).empty());

  const auto actions = event_actions(runtime.run_legacy_event_manager(400));
  assert(actions.size() == 1);
  assert(actions[0] == "run:holy_curtain:14,5");
  assert(runtime.legacy_active_event_count() == 1);
}

void check_delta_closed_ttl_wraparound() {
  auto runtime = make_runtime();
  constexpr auto max_tick = std::numeric_limits<std::uint64_t>::max();
  auto event = event_at(15, mir2::LegacyEventType::pile_stones);
  event.open_start_ms = max_tick - 1000;
  event.run_start_ms = max_tick - 1000;
  event.run_tick_ms = 500;
  event.continue_ms = 500;
  static_cast<void>(runtime.enqueue_legacy_event(event));

  const auto close_actions = event_actions(runtime.run_legacy_event_manager(max_tick - 499));
  assert(close_actions.size() == 2);
  assert(close_actions[0] == "run:pile_stones:15,5");
  assert(close_actions[1] == "close:pile_stones:15,5");
  assert(runtime.legacy_closed_event_count() == 1);

  constexpr auto closed_ttl_ms = 5ULL * 60ULL * 1000ULL;
  assert(event_actions(runtime.run_legacy_event_manager(closed_ttl_ms - 500)).empty());
  assert(runtime.legacy_closed_event_count() == 1);

  const auto cleanup_actions =
      event_actions(runtime.run_legacy_event_manager(closed_ttl_ms - 499));
  assert(cleanup_actions.size() == 1);
  assert(cleanup_actions[0] == "cleanup_closed:pile_stones:15,5");
  assert(runtime.legacy_closed_event_count() == 0);
}

void check_existing_lifecycle_smoke() {
  auto runtime = make_runtime();

  mir2::LegacyEventRecord first;
  first.map_id = "0";
  first.x = 16;
  first.y = 5;
  first.type = mir2::LegacyEventType::fire_burn;
  first.run_tick_ms = 100;
  first.continue_ms = 250;
  first.damage = 7;
  first.skip_if_occupied = true;

  mir2::LegacyEventRecord second;
  second.map_id = "0";
  second.x = 17;
  second.y = 5;
  second.type = mir2::LegacyEventType::holy_curtain;
  second.run_tick_ms = 100;
  second.continue_ms = 500;

  const auto first_id = runtime.enqueue_legacy_event(first);
  const auto duplicate_id = runtime.enqueue_legacy_event(first);
  assert(duplicate_id == 0);
  const auto second_id = runtime.enqueue_legacy_event(second);
  assert(first_id != second_id);
  assert(runtime.legacy_active_event_count() == 2);
  assert(runtime.find_legacy_event("0", 16, 5,
                                   mir2::LegacyEventType::fire_burn)
             .has_value());

  const auto run_actions = event_actions(runtime.run_legacy_event_manager(101));
  assert(run_actions.size() == 3);
  assert(run_actions[0] == "run:fire_burn:16,5");
  assert(run_actions[1] == "fire_tick:fire_burn:16,5");
  assert(run_actions[2] == "run:holy_curtain:17,5");
  assert(runtime.legacy_active_event_count() == 2);

  const auto close_actions = event_actions(runtime.run_legacy_event_manager(301));
  assert(has_action(close_actions, "run:fire_burn:16,5"));
  assert(!has_action(close_actions, "fire_tick:fire_burn:16,5"));
  assert(has_action(close_actions, "close:fire_burn:16,5"));
  assert(runtime.legacy_active_event_count() == 1);
  assert(runtime.legacy_closed_event_count() == 1);
  assert(!runtime.find_legacy_event("0", 16, 5,
                                    mir2::LegacyEventType::fire_burn)
              .has_value());

  const auto cleanup_actions =
      event_actions(runtime.run_legacy_event_manager(301 + 5ULL * 60ULL * 1000ULL + 1));
  assert(!cleanup_actions.empty());
  assert(has_action(cleanup_actions, "cleanup_closed:fire_burn:16,5"));
  assert(runtime.legacy_active_event_count() == 0);
  static_cast<void>(
      runtime.run_legacy_event_manager(301 + 10ULL * 60ULL * 1000ULL + 2));
  assert(runtime.legacy_closed_event_count() == 0);
}

void check_frame_order_regression() {
  auto runtime = make_runtime();
  mir2::LegacyFrameDriver driver;
  std::vector<std::string> observed;
  mir2::LegacyFrameCallbacks callbacks;
  callbacks.user_engine_execute_run = [&]() -> mir2::RuntimeDispatch {
    observed.push_back("UserEngineExecuteRun");
    return {};
  };
  callbacks.event_manager_run = [&]() -> mir2::RuntimeDispatch {
    observed.push_back("EventManagerRun");
    return runtime.run_legacy_event_manager(1000);
  };
  callbacks.server_message_run = [&]() -> mir2::RuntimeDispatch {
    observed.push_back("ServerMessageRun");
    return {};
  };
  const auto frame_dispatch = driver.run_frame(1000, {}, callbacks);
  static_cast<void>(frame_dispatch);
  assert((observed == std::vector<std::string>{
                          "UserEngineExecuteRun", "EventManagerRun", "ServerMessageRun"}));
  const auto& frame = driver.last_trace();
  assert(frame_stage_index(frame, mir2::LegacyFrameStage::user_engine_execute_run) <
         frame_stage_index(frame, mir2::LegacyFrameStage::event_manager_run));
  assert(frame_stage_index(frame, mir2::LegacyFrameStage::event_manager_run) <
         frame_stage_index(frame, mir2::LegacyFrameStage::server_message_run));
}

}  // namespace

int main() {
  check_tick_boundary();
  check_continue_close_boundary_and_map_state();
  check_zero_continue_closes_on_first_eligible_run();
  check_fire_burn_first_tick_and_close_order();
  check_fire_burn_damage_tick_boundary();
  check_delete_does_not_skip_shifted_event();
  check_closed_list_cleans_one_event_per_run();
  check_base_event_types_and_event_param_trace();
  check_delta_run_boundary_wraparound();
  check_delta_closed_ttl_wraparound();
  check_existing_lifecycle_smoke();
  check_frame_order_regression();
  return 0;
}
