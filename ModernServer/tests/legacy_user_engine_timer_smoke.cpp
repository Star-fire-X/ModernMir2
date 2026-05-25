#include <cassert>
#include <limits>
#include <string>
#include <string_view>
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

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "TimerMap", {}, 0, 0, 20, 20});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

void check_timer_boundaries() {
  auto runtime = make_runtime();

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

  const auto before_door = runtime.tick(2500);
  assert(!has_trace(before_door, "LegacyTimer", "DoorTimer"));

  const auto door = runtime.tick(2501);
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
}

void check_same_frame_timer_order() {
  auto runtime = make_runtime();
  static_cast<void>(runtime.tick(1000));

  const auto dispatch = runtime.tick(601001);
  const std::vector<std::string> expected{
      "ProcessNpcs:begin",
      "LegacyMission:ProcessMissions",
      "LegacyMission:CheckServerWaitTimeOut",
      "LegacyMission:CheckHolySeizeValid",
      "LegacyTimer:DoorTimer",
      "LegacyTimer:Timer10Min",
      "LegacyTimer:NoticeMan.RefreshNoticeList",
      "LegacyTimer:UserCastle.SaveAll",
      "LegacyTimer:Timer10Sec",
      "LegacyTimer:FrmIDSoc.SendUserCount",
      "LegacyTimer:GuildMan.CheckGuildWarTimeOut",
      "LegacyTimer:UserCastle.Run",
      "LegacyTimer:ShutUpList.Cleanup"};
  assert(timer_actions(dispatch) == expected);
}

void check_wraparound_delta_timers() {
  auto runtime = make_runtime();
  constexpr auto max_tick = std::numeric_limits<std::uint64_t>::max();
  static_cast<void>(runtime.tick(max_tick - 100));

  const auto door_boundary = runtime.tick(399);
  assert(!has_trace(door_boundary, "LegacyTimer", "DoorTimer"));

  const auto door = runtime.tick(400);
  assert(has_trace(door, "LegacyTimer", "DoorTimer"));

  const auto mission_boundary = runtime.tick(899);
  assert(!has_trace(mission_boundary, "LegacyMission", "ProcessMissions"));

  const auto mission = runtime.tick(900);
  assert(has_trace(mission, "LegacyMission", "ProcessMissions"));

  const auto ten_sec_boundary = runtime.tick(9899);
  assert(!has_trace(ten_sec_boundary, "LegacyTimer", "Timer10Sec"));

  const auto ten_sec = runtime.tick(9900);
  assert(has_trace(ten_sec, "LegacyTimer", "Timer10Sec"));

  const auto ten_min_boundary = runtime.tick(599899);
  assert(!has_trace(ten_min_boundary, "LegacyTimer", "Timer10Min"));

  const auto ten_min = runtime.tick(599900);
  assert(has_trace(ten_min, "LegacyTimer", "Timer10Min"));
}

}  // namespace

int main() {
  check_timer_boundaries();
  check_same_frame_timer_order();
  check_wraparound_delta_timers();
  return 0;
}
