#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include "world/legacy_frame_driver.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::size_t frame_stage_index(const mir2::LegacyFrameTrace& trace, mir2::LegacyFrameStage stage) {
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

bool has_action(const std::vector<std::string>& actions, const std::string& action) {
  return std::find(actions.begin(), actions.end(), action) != actions.end();
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "EventMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::LegacyEventRecord first;
  first.map_id = "0";
  first.x = 5;
  first.y = 5;
  first.type = mir2::LegacyEventType::fire_burn;
  first.run_tick_ms = 100;
  first.continue_ms = 250;
  first.damage = 7;
  first.skip_if_occupied = true;

  mir2::LegacyEventRecord second;
  second.map_id = "0";
  second.x = 6;
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
  assert(runtime.find_legacy_event("0", 5, 5, mir2::LegacyEventType::fire_burn).has_value());

  const auto run_dispatch = runtime.run_legacy_event_manager(101);
  const auto run_actions = event_actions(run_dispatch);
  assert(run_actions.size() == 3);
  assert(run_actions[0] == "run:fire_burn:5,5");
  assert(run_actions[1] == "fire_tick:fire_burn:5,5");
  assert(run_actions[2] == "run:holy_curtain:6,5");
  assert(runtime.legacy_active_event_count() == 2);

  const auto close_dispatch = runtime.run_legacy_event_manager(301);
  const auto close_actions = event_actions(close_dispatch);
  assert(has_action(close_actions, "run:fire_burn:5,5"));
  assert(!has_action(close_actions, "fire_tick:fire_burn:5,5"));
  assert(has_action(close_actions, "close:fire_burn:5,5"));
  assert(runtime.legacy_active_event_count() == 1);
  assert(runtime.legacy_closed_event_count() == 1);
  assert(!runtime.find_legacy_event("0", 5, 5, mir2::LegacyEventType::fire_burn).has_value());

  const auto cleanup_dispatch = runtime.run_legacy_event_manager(301 + 5ULL * 60ULL * 1000ULL + 1);
  const auto cleanup_actions = event_actions(cleanup_dispatch);
  assert(!cleanup_actions.empty());
  assert(has_action(cleanup_actions, "cleanup_closed:fire_burn:5,5"));
  assert(runtime.legacy_active_event_count() == 0);
  static_cast<void>(
      runtime.run_legacy_event_manager(301 + 10ULL * 60ULL * 1000ULL + 2));
  assert(runtime.legacy_closed_event_count() == 0);

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

  return 0;
}
