#include "app/client_app.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
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

template <typename T>
mir2::client_v1::Frame make_legacy_bundle_frame(
    const T& message, const std::uint64_t bundle_id, const std::uint16_t index,
    const std::uint16_t count, const std::uint16_t legacy_ident,
    const mir2::client_v1::LegacyBundleMode mode) {
  return mir2::client_v1::make_frame(
      message, 0, 0,
      mir2::client_v1::LegacyBundleMeta{bundle_id, index, count, legacy_ident, mode});
}

void bootstrap_world(mir2::client::ClientApp& app) {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  EnterWorldResult enter;
  enter.success = true;
  enter.self_actor_id = 1000;
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 330;
  enter.y = 270;

  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors.push_back(
      WorldActor{1000, "Hero", 330, 270, 0, 0, 0, ActorType::player});

  app.push_protocol_message_for_test(enter);
  app.push_protocol_message_for_test(snapshot);
  app.pump_protocol_for_test();
  assert(app.state_for_test().auth_phase == AuthFlowPhase::InGame);
  assert(app.current_scene_for_test() == SceneId::world);
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
  state.apply(MapEntered{"1", 1000, 5, 5, 2});
  assert(state.world.map_entered_waiting);
  trace.emplace_back("recv_new_map");

  const auto stale_actor =
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}};
  app.push_protocol_message_for_test(stale_actor);
  app.pump_protocol_for_test();
  assert(!has_actor(state, 2000));
  trace.emplace_back("defer_runtime_actor_upsert_during_transition");

  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  assert(state.world.map_id == "1");
  assert(has_actor(state, 1000));
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
  assert(!has_actor(state, 2000));
  assert(!has_actor(state, 2500));
  trace.emplace_back("apply_deferred_runtime_before_snapshot");
  trace.emplace_back("recv_world_snapshot");
  trace.emplace_back("snapshot_clears_deferred_runtime");

  state.apply(ActorUpsert{WorldActor{3000, "Hen", 6, 5, 4, 0, 0, ActorType::monster}});
  assert(has_actor(state, 3000));
  assert(std::find(state.world.actor_draw_order.begin(), state.world.actor_draw_order.end(),
                   3000) != state.world.actor_draw_order.end());
  trace.emplace_back("apply_runtime_actor_upsert_after_snapshot");

  return trace;
}

void test_map_transition_defers_runtime_fifo_until_self_ready() {
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
  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;

  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_message_for_test(MapEntered{"1", 1000, 5, 5, 2});
  app.push_protocol_message_for_test(ChatLine{"arrived", 0xFFFFFFFFU, 0});
  app.push_protocol_message_for_test(
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}});
  app.push_protocol_message_for_test(GroundItemAdd{GroundItemState{3000, 6, 5, 1, "Gold"}});
  app.pump_protocol_for_test();

  assert(state.world.map_transition_pending);
  assert(state.world.map_id == "0");
  assert(state.world.chat_lines.empty());
  assert(!has_actor(state, 2000));
  assert(state.world.ground_items.find(3000) == state.world.ground_items.end());

  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  app.pump_protocol_for_test();

  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(state.world.chat_lines.size() == 1);
  assert(state.world.chat_lines.front().text == "arrived");
  assert(has_actor(state, 2000));
  assert(state.world.ground_items.find(3000) != state.world.ground_items.end());
}

void test_runtime_before_snapshot_keeps_fifo_in_same_drain() {
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
  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;

  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_message_for_test(MapEntered{"1", 1000, 5, 5, 2});
  app.push_protocol_message_for_test(
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}});
  app.push_protocol_message_for_test(
      WorldSnapshot{"1", 500, 400, 1000,
                    {WorldActor{1000, "Hero", 5, 5, 2, 0, 0, ActorType::player}}});
  app.pump_protocol_for_test();

  assert(state.world.map_transition_pending);
  assert(!has_actor(state, 2000));

  state.world.actors[1000].action_started_ms = 0;
  assert(state.finish_pending_map_transition_if_ready(mir2::client::detail::monotonic_ms()));
  app.pump_protocol_for_test();

  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(has_actor(state, 1000));
  assert(!has_actor(state, 2000));
}

void test_runtime_fifo_barrier_survives_map_entered_same_drain() {
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
  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_message_for_test(ChatLine{"first", 0xFFFFFFFFU, 0});
  app.push_protocol_message_for_test(MapEntered{"1", 1000, 5, 5, 2});
  app.push_protocol_message_for_test(ChatLine{"second", 0xFFFFFFFFU, 0});
  app.pump_protocol_for_test();

  assert(!state.world.map_transition_pending);
  assert(state.world.map_id == "1");
  assert(state.world.chat_lines.empty());

  app.pump_protocol_for_test();
  assert(state.world.chat_lines.size() == 2);
  assert(state.world.chat_lines[0].text == "first");
  assert(state.world.chat_lines[1].text == "second");
}

void test_runtime_state_apply_keeps_drain_order() {
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

  ItemState potion;
  potion.name = "Potion";
  potion.make_index = 7001;
  potion.looks = 3;
  potion.std_mode = 0;
  potion.dura = 1;
  potion.dura_max = 1;

  auto updated_potion = potion;
  updated_potion.dura = 0;

  app.push_protocol_message_for_test(ActionAck{true, 456});
  app.push_protocol_message_for_test(GroundItemAdd{GroundItemState{9001, 331, 270, 7, "Gold"}});
  app.push_protocol_message_for_test(InventoryAdd{ItemSlotState{6, potion}});
  app.push_protocol_message_for_test(InventoryUpdate{ItemSlotState{6, updated_potion}});
  app.push_protocol_message_for_test(SysMessage{"server notice", 1});
  app.push_protocol_message_for_test(ChatLine{"player line", 0xFFFFFFFFU, 0});
  app.pump_protocol_for_test();

  auto& state = app.state_for_test();
  assert(state.world.last_action_ack_ok);
  const auto ground_item = state.world.ground_items.find(9001);
  assert(ground_item != state.world.ground_items.end());
  assert(ground_item->second.name == "Gold");
  assert(!state.world.bag_items[6].empty());
  assert(state.world.bag_items[6].name == "Potion");
  assert(state.world.bag_items[6].dura == 0);
  assert(state.world.sys_messages.size() == 1);
  assert(state.world.sys_messages.front().text == "server notice");
  assert(state.world.chat_lines.size() == 2);
  assert(state.world.chat_lines[0].text == "server notice");
  assert(state.world.chat_lines[1].text == "player line");

  app.push_protocol_message_for_test(GroundItemRemove{9001, 331, 270});
  app.push_protocol_message_for_test(InventoryRemove{6});
  app.pump_protocol_for_test();

  assert(state.world.ground_items.find(9001) == state.world.ground_items.end());
  assert(state.world.bag_items[6].empty());
}

void test_bad_runtime_frame_during_map_transition_disconnects_immediately() {
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

  auto bad_chat = Frame{};
  bad_chat.message_id = MessageId::chat_line;
  bad_chat.sequence = 77;
  bad_chat.payload = {0};

  auto& state = app.state_for_test();
  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_frame_for_test(std::move(bad_chat));
  app.push_protocol_message_for_test(ChatLine{"after_bad_frame", 0xFFFFFFFFU, 0});
  app.pump_protocol_for_test();

  assert(app.last_disconnect_reason_for_test() == "protocol_decode_error");
  assert(state.world.chat_lines.empty());

  app.pump_protocol_for_test();
  assert(state.world.chat_lines.empty());
}

void test_disconnect_during_map_transition_is_not_deferred() {
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
  state.world.actors[1000].action_started_ms = mir2::client::detail::monotonic_ms();
  state.world.actors[1000].action_duration_ms = 1000;

  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_message_for_test(MapEntered{"1", 1000, 5, 5, 2});
  app.push_protocol_message_for_test(
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}});
  app.push_protocol_disconnect_for_test("remote_closed");
  app.pump_protocol_for_test();

  assert(state.auth_phase == AuthFlowPhase::Disconnected);
  assert(!state.world.map_transition_pending);
  assert(!has_actor(state, 2000));

  app.pump_protocol_for_test();
  assert(state.auth_phase == AuthFlowPhase::Disconnected);
  assert(!has_actor(state, 2000));
}

void test_typed_disconnect_reason_clears_deferred_map_runtime() {
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
  app.push_protocol_message_for_test(WorldClearObjects{});
  app.push_protocol_message_for_test(MapChange{"1"});
  app.push_protocol_message_for_test(
      ActorUpsert{WorldActor{2000, "Guard", 332, 270, 0, 0, 0, ActorType::npc}});
  app.push_protocol_message_for_test(DisconnectReason{409, "server_closed"});
  app.push_protocol_message_for_test(ChatLine{"after_disconnect", 0xFFFFFFFFU, 0});
  app.pump_protocol_for_test();

  assert(state.auth_phase == AuthFlowPhase::EditingLogin);
  assert(app.current_scene_for_test() == SceneId::login);
  assert(!state.world.map_transition_pending);
  assert(!has_actor(state, 2000));
  assert(state.world.chat_lines.empty());

  app.pump_protocol_for_test();
  assert(!has_actor(state, 2000));
  assert(state.world.chat_lines.empty());
}

void test_immediate_legacy_bundle_waits_for_all_frames() {
  using namespace mir2::client_v1;

  mir2::client::ClientApp app;
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();
  bootstrap_world(app);

  auto& state = app.state_for_test();
  state.world.last_action_ack_ms = 0;
  state.world.actors[1000].x = 330;
  state.world.actors[1000].y = 270;

  constexpr std::uint16_t kSmMoveFail = 28;
  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      ActionAck{false, 0}, 9001, 0, 2, kSmMoveFail, LegacyBundleMode::immediate));
  app.pump_protocol_for_test();
  assert(state.world.last_action_ack_ms == 0);
  assert(state.world.actors[1000].x == 330);

  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      ActorStateDelta{1000, 329, 270, 2}, 9001, 1, 2, kSmMoveFail,
      LegacyBundleMode::immediate));
  app.pump_protocol_for_test();
  assert(state.world.last_action_ack_ms != 0);
  assert(state.world.actors[1000].x == 329);
  assert(state.world.actors[1000].dir == 2);
}

void test_actor_queue_legacy_bundle_stages_state_until_queue_consume() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  ClientApp app;
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();
  bootstrap_world(app);

  auto& state = app.state_for_test();
  state.apply(ActorUpsert{WorldActor{2000, "Guard", 331, 270, 0, 0, 0,
                                     ActorType::monster}});
  state.world.actors[2000].hp = 20;
  state.world.actors[2000].current_action = ActorActionKind::turn;

  constexpr std::uint16_t kSmStruck = 31;
  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      ActorVitals{2000, 10, 20, -1, -1, 7, 1000, false, kSmStruck}, 9002, 0, 2,
      kSmStruck, LegacyBundleMode::actor_queue));
  app.pump_protocol_for_test();
  assert(state.world.actors[2000].hp == 20);
  assert(state.world.actors[2000].current_action == ActorActionKind::turn);
  assert(state.world.actors[2000].legacy_actor_bundle_queue.empty());

  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      ActorAction{2000, ActorActionKind::struck, 0, 0, 0, 1000, 7, kSmStruck, 0,
                  false, 0},
      9002, 1, 2, kSmStruck, LegacyBundleMode::actor_queue));
  app.pump_protocol_for_test();
  assert(state.world.actors[2000].hp == 20);
  assert(state.world.actors[2000].current_action == ActorActionKind::turn);
  assert(state.world.actors[2000].legacy_actor_bundle_queue.size() == 1);

  state.process_legacy_actor_queues(12345);
  assert(state.world.actors[2000].hp == 10);
  assert(state.world.actors[2000].last_damage == 7);
  assert(state.world.actors[2000].current_action == ActorActionKind::struck);
}

void test_map_transition_defers_whole_legacy_bundle() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  ClientApp app;
  app.set_config_for_test(test_config());
  app.enable_protocol_test_mode_for_test();
  bootstrap_world(app);

  auto& state = app.state_for_test();
  state.apply(WorldClearObjects{});
  assert(state.world.map_transition_pending);

  constexpr std::uint16_t kSmItemShow = 610;
  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      GroundItemAdd{GroundItemState{3000, 331, 270, 1, "Gold"}}, 9003, 0, 2,
      kSmItemShow, LegacyBundleMode::immediate));
  app.push_protocol_frame_for_test(make_legacy_bundle_frame(
      ChatLine{"bundle chat", 0xFFFFFFFFU, 0}, 9003, 1, 2, kSmItemShow,
      LegacyBundleMode::immediate));
  app.pump_protocol_for_test();

  assert(state.world.ground_items.empty());
  assert(state.world.chat_lines.empty());
}

}  // namespace

int main() {
  const auto sections = read_trace_sections();
  assert(run_protocol_trace() == sections.at("protocol.map_transfer"));
  test_map_transition_defers_runtime_fifo_until_self_ready();
  test_runtime_before_snapshot_keeps_fifo_in_same_drain();
  test_runtime_fifo_barrier_survives_map_entered_same_drain();
  test_runtime_state_apply_keeps_drain_order();
  test_bad_runtime_frame_during_map_transition_disconnects_immediately();
  test_disconnect_during_map_transition_is_not_deferred();
  test_typed_disconnect_reason_clears_deferred_map_runtime();
  test_immediate_legacy_bundle_waits_for_all_frames();
  test_actor_queue_legacy_bundle_stages_state_until_queue_consume();
  test_map_transition_defers_whole_legacy_bundle();
  return 0;
}
