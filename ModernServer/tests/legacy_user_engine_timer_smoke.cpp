#include <cassert>
#include <string>
#include <vector>

#include "world/logic_runtime.hpp"

namespace {

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action == action) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> timer_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessNpcs" || trace.stage == "LegacyMission" ||
        trace.stage == "LegacyTimer") {
      actions.push_back(trace.stage + ":" + trace.action);
    }
  }
  return actions;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "TimerMap", {}, 0, 0, 20, 20});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto first = runtime.tick(1000);
  assert(!has_trace(first, "LegacyMission", "ProcessMissions"));
  assert(!has_trace(first, "LegacyTimer", "DoorTimer"));
  assert(!has_trace(first, "LegacyTimer", "Timer10Sec"));
  assert(!has_trace(first, "LegacyTimer", "Timer10Min"));

  const auto door_boundary = runtime.tick(1500);
  assert(!has_trace(door_boundary, "LegacyTimer", "DoorTimer"));

  const auto mission_boundary = runtime.tick(2000);
  assert(!has_trace(mission_boundary, "LegacyMission", "ProcessMissions"));
  assert(has_trace(mission_boundary, "LegacyTimer", "DoorTimer"));

  const auto mission = runtime.tick(2001);
  assert(has_trace(mission, "LegacyMission", "ProcessMissions"));
  assert(has_trace(mission, "LegacyMission", "CheckServerWaitTimeOut"));
  assert(has_trace(mission, "LegacyMission", "CheckHolySeizeValid"));
  assert(!has_trace(mission, "LegacyTimer", "DoorTimer"));
  const auto actions = timer_actions(mission);
  const std::vector<std::string> expected{
      "ProcessNpcs:begin",
      "LegacyMission:ProcessMissions",
      "LegacyMission:CheckServerWaitTimeOut",
      "LegacyMission:CheckHolySeizeValid"};
  assert(actions == expected);

  const auto before_door = runtime.tick(2501);
  assert(!has_trace(before_door, "LegacyTimer", "DoorTimer"));

  const auto door = runtime.tick(2502);
  assert(has_trace(door, "LegacyTimer", "DoorTimer"));
  assert(!has_trace(door, "LegacyMission", "ProcessMissions"));

  const auto ten_sec_boundary = runtime.tick(11000);
  assert(!has_trace(ten_sec_boundary, "LegacyTimer", "Timer10Sec"));

  const auto ten_sec = runtime.tick(11001);
  assert(has_trace(ten_sec, "LegacyTimer", "Timer10Sec"));
  assert(has_trace(ten_sec, "LegacyTimer", "FrmIDSoc.SendUserCount"));
  const auto no_repeat = runtime.tick(11002);
  assert(!has_trace(no_repeat, "LegacyTimer", "Timer10Sec"));

  const auto ten_min_boundary = runtime.tick(601000);
  assert(!has_trace(ten_min_boundary, "LegacyTimer", "Timer10Min"));

  const auto ten_min = runtime.tick(601001);
  assert(has_trace(ten_min, "LegacyTimer", "Timer10Min"));
  assert(has_trace(ten_min, "LegacyTimer", "NoticeMan.RefreshNoticeList"));
  assert(has_trace(ten_min, "LegacyTimer", "UserCastle.SaveAll"));

  return 0;
}
