#include <cassert>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
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

std::vector<std::string> timer_and_mail_actions(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::string> actions;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "ProcessNpcs" || trace.stage == "MapMailbox" ||
        trace.stage == "LegacyMission" || trace.stage == "LegacyTimer") {
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

mir2::LegacyReadyUser make_ready(std::uint64_t session_id) {
  mir2::CharacterRecord character;
  character.account_id = "acct_mailbox";
  character.character_name = "MailboxHero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;

  mir2::LegacyReadyUser ready;
  ready.session_id = session_id;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  return ready;
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

void check_map_mailbox_runs_before_timers() {
  auto runtime = make_runtime();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(51)));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));
  assert(runtime.legacy_session_state(51) == mir2::LegacyPlayerState::running);

  mir2::ActorMail notice;
  notice.kind = mir2::ActorMailKind::system_notice;
  notice.map_id = "0";
  notice.actor_id = 1;
  notice.session_id = 51;
  notice.payload = "mailbox before timers";
  static_cast<void>(runtime.route_actor_mail(notice));

  const auto dispatch = runtime.tick(2001);
  const std::vector<std::string> expected{
      "ProcessNpcs:begin",
      "MapMailbox:drain",
      "LegacyMission:ProcessMissions",
      "LegacyMission:CheckServerWaitTimeOut",
      "LegacyMission:CheckHolySeizeValid",
      "LegacyTimer:DoorTimer"};
  assert(timer_and_mail_actions(dispatch) == expected);
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
  check_map_mailbox_runs_before_timers();
  check_wraparound_delta_timers();
  return 0;
}
