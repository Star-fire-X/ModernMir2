#include "app/legacy_frame_scheduler.hpp"
#include "game/game_state.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_golden_trace() {
  const auto path = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR} / "tests" / "golden" /
                    "legacy_scene_management_expected_trace.txt";
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      sections[current] = {};
      continue;
    }
    if (!current.empty()) {
      sections[current].push_back(line);
    }
  }
  return sections;
}

void push(std::vector<std::string>& calls, const char* name) { calls.emplace_back(name); }

std::vector<std::string> run_trace(const bool force_render_due, const bool can_draw) {
  mir2::client::LegacyFrameScheduler scheduler;
  if (force_render_due) {
    scheduler.force_render_due_for_test();
  }
  std::vector<std::string> calls;
  scheduler.run_frame(
      0.0F,
      mir2::client::LegacyFrameScheduler::Hooks{
          [&] { push(calls, "legacy_input_dispatch"); },
          [&] {
            push(calls, "timer1_network_drain");
            push(calls, "decode_packets_in_buffer_order");
          },
          [] {},
          [&] { push(calls, "process_key_messages"); },
          [&] { push(calls, "process_action_messages"); },
          [&] { push(calls, "dwin_process"); },
          [&] { push(calls, "play_scene_run"); },
          [&] {
            push(calls, "draw_screen");
            push(calls, "current_scene_play_scene");
            push(calls, "play_overlays");
          },
          [&] { push(calls, "dwin_direct_paint"); },
          [&] { push(calls, "draw_screen_top"); },
          [&] { push(calls, "draw_hint"); },
          [&] { push(calls, "draw_moving_item"); },
          [&] { push(calls, "flip"); },
          [can_draw] { return can_draw; }});
  return calls;
}

std::vector<std::string> run_map_transfer_trace() {
  using namespace mir2::client_v1;

  mir2::client::GameStateStore state;
  state.apply(WorldSnapshot{"0", 700, 700, 1000,
                            {WorldActor{1000, "Hero", 330, 270, 0, 0, 0,
                                        ActorType::player},
                             WorldActor{2000, "Guard", 331, 270, 0, 0, 0,
                                        ActorType::npc}}});
  state.apply(GroundItemAdd{GroundItemState{3000, 330, 271, 1, "Gold"}});
  state.apply(MapDoorState{12, 13, true});

  std::vector<std::string> calls;
  push(calls, "recv_clear_objects");
  state.apply(WorldClearObjects{});
  assert(state.world.map_transition_pending);
  assert(!state.world.actors.empty());
  assert(!state.world.ground_items.empty());
  assert(state.map_door_open(12, 13));
  push(calls, "enter_map_transition_pending");
  push(calls, "preserve_dynamic_map_objects_until_self_ready");

  push(calls, "recv_change_map");
  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;
  state.apply(MapChange{"1"});
  assert(state.world.map_id == "0");
  assert(state.world.map_change_waiting);
  assert(state.world.pending_map_id == "1");
  push(calls, "set_pending_map_id");
  push(calls, "wait_self_action_before_map_apply");

  const auto stale_actor = ActorUpsert{
      WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}};
  if (state.world.map_change_waiting) {
    push(calls, "defer_runtime_deltas_during_map_wait");
  } else {
    state.apply(stale_actor);
  }
  assert(state.world.actors.find(stale_actor.actor.actor_id) != state.world.actors.end());

  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  assert(state.world.map_id == "1");
  assert(state.world.actors.empty());
  assert(state.world.ground_items.empty());
  assert(!state.map_door_open(12, 13));
  push(calls, "apply_map_after_self_action");
  push(calls, "clear_dynamic_map_objects");

  push(calls, "recv_world_snapshot");
  state.apply(WorldSnapshot{"1", 500, 400, 1000,
                            {WorldActor{1000, "Hero", 5, 5, 2, 0, 0,
                                        ActorType::player}}});
  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(state.world.actors.size() == 1U);
  push(calls, "apply_new_map_snapshot");
  push(calls, "resume_runtime_deltas");
  return calls;
}

std::vector<std::string> run_map_door_state_trace() {
  using namespace mir2::client_v1;

  mir2::client::GameStateStore state;
  std::vector<std::string> calls;

  push(calls, "recv_open_door");
  state.apply(MapDoorState{12, 13, true});
  assert(state.map_door_open(12, 13));
  push(calls, "door_open_visible");

  push(calls, "recv_close_door");
  state.apply(MapDoorState{12, 13, false});
  assert(!state.map_door_open(12, 13));
  assert(state.world.map_doors.find(mir2::client::GameStateStore::map_door_key(12, 13)) !=
         state.world.map_doors.end());
  push(calls, "door_close_override_kept");

  push(calls, "open_door_no_client_expiry");
  state.apply(MapDoorState{12, 13, true});
  state.world.map_doors[mir2::client::GameStateStore::map_door_key(12, 13)].updated_ms = 1;
  assert(state.map_door_open(12, 13));
  push(calls, "stale_open_kept_until_close");
  return calls;
}

}  // namespace

int main() {
  const auto golden = read_golden_trace();
  assert(run_trace(true, true) == golden.at("frame.render_due.can_draw"));
  assert(run_trace(false, true) == golden.at("frame.not_render_due"));
  assert(run_trace(true, false) == golden.at("frame.cannot_draw"));
  assert(run_map_transfer_trace() == golden.at("play.map_transfer"));
  assert(run_map_door_state_trace() == golden.at("play.map_door_state"));
  return 0;
}
