#include "app/client_app.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_trace_sections() {
  const auto path = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR} / "tests" / "golden" /
                    "client_migration_pr1_expected_trace.txt";
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      sections[current] = {};
      continue;
    }
    assert(!current.empty());
    sections[current].push_back(line);
  }
  return sections;
}

mir2::client::ClientConfig test_config() {
  mir2::client::ClientConfig config;
  config.auto_play.enabled = false;
  return config;
}

bool has_actor(const mir2::client::GameStateStore& state, const std::uint64_t actor_id) {
  return state.world.actors.find(actor_id) != state.world.actors.end();
}

std::vector<std::string> run_protocol_trace() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  std::vector<std::string> trace;
  ClientApp app;
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();

  EnterWorldResult enter;
  enter.success = true;
  enter.self_actor_id = 1000;
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 330;
  enter.y = 270;

  WorldSnapshot initial_snapshot;
  initial_snapshot.map_id = "0";
  initial_snapshot.width = 700;
  initial_snapshot.height = 700;
  initial_snapshot.self_actor_id = 1000;
  initial_snapshot.actors.push_back(
      WorldActor{1000, "Hero", 330, 270, 0, 0, 0, ActorType::player});

  app.push_protocol_message_for_test(enter);
  app.push_protocol_message_for_test(initial_snapshot);
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::InGame);
  assert(app.current_scene_for_test() == SceneId::world);
  assert(has_actor(app.state_for_test(), 1000));
  trace.emplace_back("client_app_bootstrap_world");

  auto& state = app.state_for_test();

  state.apply(WorldClearObjects{});
  assert(state.world.map_transition_pending);
  assert(state.world.map_clear_waiting_for_change);
  assert(has_actor(state, 1000));
  trace.emplace_back("recv_world_clear_objects");
  trace.emplace_back("enter_map_transition_pending");
  trace.emplace_back("preserve_world_runtime_until_self_ready");

  const auto pre_change_actor =
      ActorUpsert{WorldActor{2500, "OldMapGuard", 331, 270, 0, 0, 0, ActorType::npc}};
  app.push_protocol_message_for_test(pre_change_actor);
  app.pump_protocol_for_test();
  assert(!has_actor(state, 2500));
  trace.emplace_back("defer_runtime_actor_upsert_until_map_change");

  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;
  state.apply(MapChange{"1"});
  assert(state.world.map_id != "1");
  assert(state.world.map_transition_pending);
  assert(!state.world.map_clear_waiting_for_change);
  assert(state.world.map_change_waiting);
  trace.emplace_back("recv_map_change");
  trace.emplace_back("set_pending_map_id");
  trace.emplace_back("wait_self_action_before_map_apply");

  const auto stale_actor =
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}};
  app.push_protocol_message_for_test(stale_actor);
  app.pump_protocol_for_test();
  assert(!has_actor(state, 2000));
  trace.emplace_back("defer_runtime_actor_upsert_during_transition");

  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  assert(state.world.map_id == "1");
  assert(state.world.actors.empty());
  trace.emplace_back("self_action_done_apply_pending_map");

  WorldSnapshot transfer_snapshot;
  transfer_snapshot.map_id = "1";
  transfer_snapshot.width = 500;
  transfer_snapshot.height = 400;
  transfer_snapshot.self_actor_id = 1000;
  transfer_snapshot.actors.push_back(
      WorldActor{1000, "Hero", 5, 5, 2, 0, 0, ActorType::player});
  app.push_protocol_message_for_test(transfer_snapshot);
  app.pump_protocol_for_test();
  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(has_actor(state, 1000));
  assert(has_actor(state, 2000));
  assert(has_actor(state, 2500));
  trace.emplace_back("recv_world_snapshot");
  trace.emplace_back("snapshot_clears_map_transition");
  trace.emplace_back("apply_deferred_runtime_after_snapshot");

  state.apply(ActorUpsert{WorldActor{3000, "Hen", 6, 5, 4, 0, 0, ActorType::monster}});
  assert(has_actor(state, 3000));
  assert(std::find(state.world.actor_draw_order.begin(), state.world.actor_draw_order.end(),
                   3000) != state.world.actor_draw_order.end());
  trace.emplace_back("apply_runtime_actor_upsert_after_snapshot");

  return trace;
}

void test_snapshot_overtakes_runtime_in_same_drain() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  ClientApp app;
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();

  app.push_protocol_message_for_test(
      EnterWorldResult{true, 1000, "Hero", "0", 330, 270, ""});
  app.push_protocol_message_for_test(
      WorldSnapshot{"0", 700, 700, 1000,
                    {WorldActor{1000, "Hero", 330, 270, 0, 0, 0, ActorType::player}}});
  app.pump_protocol_for_test();

  auto& state = app.state_for_test();
  state.apply(WorldClearObjects{});
  state.apply(MapChange{"1"});
  state.complete_map_transition("1");
  assert(state.world.map_transition_pending);

  app.push_protocol_message_for_test(
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}});
  app.push_protocol_message_for_test(
      WorldSnapshot{"1", 500, 400, 1000,
                    {WorldActor{1000, "Hero", 5, 5, 2, 0, 0, ActorType::player}}});
  app.pump_protocol_for_test();

  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(has_actor(state, 1000));
  assert(has_actor(state, 2000));
}

}  // namespace

int main() {
  const auto sections = read_trace_sections();
  assert(run_protocol_trace() == sections.at("protocol.map_transfer"));
  test_snapshot_overtakes_runtime_in_same_drain();
  return 0;
}
