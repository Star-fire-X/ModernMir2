#include "app/client_app.hpp"
#include "scene/scenes.hpp"
#include "shared/legacy/map_render_math.hpp"
#include "shared/legacy/movement_rules.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

using namespace mir2::client;
using namespace mir2::client_v1;
namespace legacy = mir2::legacy;

namespace {

std::pair<int, int> mouse_for_tile(const int self_x, const int self_y, const int x, const int y) {
  return mir2::legacy::legacy_screen_from_map(
      mir2::legacy::make_legacy_map_viewport(self_x, self_y), x, y);
}

LegacyInputEvent left_down_event(const int x, const int y) {
  LegacyInputEvent event;
  event.kind = LegacyInputEventKind::left_down;
  event.mouse_x = x;
  event.mouse_y = y;
  event.left_down = true;
  return event;
}

LegacyInputEvent key_down_event(const std::uint16_t key) {
  LegacyInputEvent event;
  event.kind = LegacyInputEventKind::key_down;
  event.key = key;
  return event;
}

void connect_for_test(ClientApp& app) {
  ClientConfig config;
  config.login_host = "127.0.0.1";
  config.login_port = 7000;
  config.auto_play.enabled = false;
  app.set_config_for_test(config);
  app.enable_protocol_test_mode_for_test();
  app.request_login("user", "password");
  app.complete_connect_for_test();
  (void)app.drain_sent_frames_for_test();
}

void reset_world(GameStateStore& state) {
  state.world = WorldViewState{};
  auto& world = state.world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;
  world.map_id = "0";
  ActorState self;
  self.actor_id = 1;
  self.x = 50;
  self.y = 50;
  self.dir = 2;
  self.mp = 30;
  world.actors.emplace(1, self);
}

void test_magic_key_does_not_stick_across_blocked_frame() {
  GameStateStore state;
  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{7, 1, 0, 0, 900, "Fire", 0, 0, 1, 0, 0, 0});

  world.latest_spell_ms = detail::monotonic_ms();
  world.magic_delay_time_ms = 100000;
  input.key_pressed[VK_F1] = true;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = InputState{};
  world.latest_spell_ms = 0;
  input.key_pressed[VK_F1] = true;
  scenes.process_key_messages(context);
  assert(world.action_key == 0);

  input = InputState{};
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();
  scenes.process_action_messages(context, 0.016F);
  assert(world.action_key == -1);
  assert(world.action_locked);
}

void test_source_only_magic_bypasses_action_gate_without_local_spell() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.actors[1].mp = 0;
  world.actors[1].current_action = ActorActionKind::walk;
  world.actors[1].action_started_ms = detail::monotonic_ms();
  world.actors[1].action_duration_ms = 10000;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->magic_id == 26);
  assert(spell->x == 2);
  assert(spell->y == 0);
  assert(world.action_key == -1);
  assert(world.action_locked);
  assert(world.magic_delay_time_ms == 0);
  assert(world.actors[1].current_action == ActorActionKind::walk);
  state.apply(ActionAck{false, 0});
  assert(world.actors[1].x == 50);
  assert(world.actors[1].y == 50);
}

void test_source_only_magic_bypasses_existing_action_lock() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.actors[1].x = 51;
  world.actors[1].legacy_old_x = 50;
  world.actors[1].legacy_old_y = 50;
  world.actors[1].legacy_has_old_position = true;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
  assert(world.action_key == -1);
  assert(world.action_locked);
  assert(world.actors[1].current_action != ActorActionKind::spell);
  state.apply(ActionAck{false, 0});
  assert(world.actors[1].x == 51);
  assert(world.actors[1].y == 50);
}

void test_source_only_magic_refreshes_lock_time_and_preserves_prior_action_rollback() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;

  ActionIntent walk;
  walk.kind = WorldActionKind::walk;
  walk.x = 51;
  walk.y = 50;
  walk.dir = 2;
  walk.legacy_ident = legacy::kSmWalk;
  app.request_action(walk);
  const auto walk_frames = app.drain_sent_frames_for_test();
  assert(walk_frames.size() == 1);
  assert(world.actors[1].x == 51);
  assert(world.pending_action_acks.size() == 1);
  const auto original_lock_started_ms = detail::monotonic_ms() - 8000U;
  world.action_lock_started_ms = original_lock_started_ms;

  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
  assert(world.pending_action_acks.size() == 2);
  const auto refreshed_lock_started_ms = world.action_lock_started_ms;
  assert(refreshed_lock_started_ms > original_lock_started_ms);

  state.apply(ActionAck{false, 1234});
  const auto expected_walk_fail_ident = static_cast<std::uint16_t>(3000U + legacy::kSmWalk);
  assert(world.actors[1].x == 50);
  assert(world.actors[1].y == 50);
  assert(!world.action_locked);
  assert(world.action_fail_lock);
  assert(world.fail_action_ident == expected_walk_fail_ident);
  assert(world.fail_dir == walk.dir);
  state.apply(ActionAck{false, 0});
  assert(world.pending_action_acks.size() == 1);
  assert(!world.action_locked);
  assert(world.action_fail_lock);
  assert(world.fail_action_ident == expected_walk_fail_ident);
  assert(world.fail_dir == walk.dir);
  state.apply(ActionAck{false, 0});
  assert(world.actors[1].x == 50);
  assert(world.actors[1].y == 50);
  assert(!world.action_locked);
  assert(world.action_fail_lock);
  assert(world.fail_action_ident == expected_walk_fail_ident);
  assert(world.fail_dir == walk.dir);
  assert(!is_unlock_action(world, expected_walk_fail_ident, walk.dir, detail::monotonic_ms()));
}

void test_source_only_magic_after_sm_movefail_ack_consumes_real_spell_failure() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;

  ActionIntent walk;
  walk.kind = WorldActionKind::walk;
  walk.x = 51;
  walk.y = 50;
  walk.dir = 2;
  walk.legacy_ident = legacy::kSmWalk;
  app.request_action(walk);
  const auto walk_frames = app.drain_sent_frames_for_test();
  assert(walk_frames.size() == 1);

  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
  assert(world.pending_action_acks.size() == 2);

  state.apply(ActionAck{false, 0});
  assert(world.pending_action_acks.size() == 1);
  assert(!world.action_locked);
  assert(!world.skip_next_move_fail_ack);

  state.apply(ActionAck{false, 0});
  assert(world.pending_action_acks.empty());
  assert(!world.action_locked);
  assert(world.actors[1].x == 50);
  assert(world.actors[1].y == 50);
}

void test_source_only_magic_expires_stale_lock_before_send() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.actors[1].x = 51;
  world.actors[1].legacy_old_x = 50;
  world.actors[1].legacy_old_y = 50;
  world.actors[1].legacy_has_old_position = true;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;
  world.action_locked = true;
  world.action_lock_started_ms = 1;
  world.pending_action_acks.push_back(
      PendingActionAckState{legacy::kCmHit, 2, true, 50, 50, 2, 1});

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
  assert(world.pending_action_acks.size() == 1);
  state.apply(ActionAck{false, 0});
  assert(world.actors[1].x == 51);
  assert(world.actors[1].y == 50);
  assert(!world.action_locked);
}

void test_attack_attempt_updates_move_suppression() {
  GameStateStore state;
  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  reset_world(state);
  auto& world = state.world;
  world.actors.emplace(2, ActorState{2, "Target", 51, 50, 51, 50, 6, 0, 0, 0,
                                     ActorType::monster});
  world.target_actor_id = 2;
  world.latest_hit_ms = detail::monotonic_ms();
  world.last_attack_ms = 0;

  scenes.process_action_messages(context, 0.016F);
  assert(world.last_attack_ms != 0);
  assert(!world.action_locked);
}

void test_locked_two_cell_target_drops_stale_chase_move() {
  GameStateStore state;
  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  reset_world(state);
  auto& world = state.world;
  world.can_long_hit = true;
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();
  world.actors.emplace(2, ActorState{2, "Target", 52, 50, 52, 50, 6, 0, 0, 0,
                                     ActorType::monster});
  world.target_actor_id = 2;

  scenes.process_action_messages(context, 0.016F);
  assert(world.target_actor_id == 2);
  assert(world.legacy_chr_action == LegacyChrAction::none);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
}

void test_pickup_has_no_extra_client_throttle() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.ground_items.emplace(900, GroundItemState{900, 50, 50, 1, "Gold"});

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  world.pending_pickup_item_id = 900;
  scenes.process_action_messages(context, 0.016F);
  world.pending_pickup_item_id = 900;
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 2);
  assert(decode_message<PickupIntent>(frames[0]).has_value());
  assert(decode_message<PickupIntent>(frames[1]).has_value());
}

void test_ground_pickup_clears_stale_target_before_attack_phase() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  ActorState target;
  target.actor_id = 2;
  target.x = 55;
  target.y = 50;
  world.actors.emplace(2, target);
  world.target_actor_id = 2;
  world.ground_items.emplace(900, GroundItemState{900, 50, 50, 1, "Gold"});

  ClientConfig config;
  InputState input;
  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 50);
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  input.left_pressed = true;
  input.left_down = true;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.process_action_messages(context, 0.016F);
  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<PickupIntent>(frames.front()).has_value());
  assert(world.target_actor_id == 0);
  assert(world.legacy_chr_action == LegacyChrAction::none);
}

void test_locked_pending_move_candidate_is_dropped() {
  GameStateStore state;
  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  reset_world(state);
  auto& world = state.world;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  scenes.process_action_messages(context, 0.016F);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == LegacyChrAction::none);
}

void test_locked_same_tile_pickup_candidate_is_dropped() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.ground_items.emplace(900, GroundItemState{900, 50, 50, 1, "Gold"});
  world.pending_pickup_item_id = 900;
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.pending_pickup_item_id == 0);
}

void test_locked_far_pickup_does_not_auto_move_after_unlock() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.ground_items.emplace(900, GroundItemState{900, 51, 50, 1, "Gold"});
  world.pending_pickup_item_id = 900;
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.pending_pickup_item_id == 0);
  assert(world.legacy_chr_action == LegacyChrAction::none);

  world.action_locked = false;
  world.action_lock_started_ms = 0;
  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
}

void test_far_ground_item_click_only_queues_move() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.ground_items.emplace(900, GroundItemState{900, 51, 50, 1, "Gold"});

  ClientConfig config;
  InputState input;
  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 51, 50);
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  input.left_pressed = true;
  input.left_down = true;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.process_action_messages(context, 0.016F);
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto move = decode_message<ActionIntent>(frames.front());
  assert(move.has_value());
  assert(move->kind == WorldActionKind::walk);
  assert(world.pending_pickup_item_id == 0);
  input = InputState{};

  state.apply(ActionAck{true, 1234});
  assert(!world.action_locked);
  for (int tick = 0; tick < 3; ++tick) {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    scenes.update(context, 0.110F);
    assert(app.drain_sent_frames_for_test().empty());
  }
  assert(world.pending_pickup_item_id == 0);
}

void test_pickup_turn_candidate_is_one_shot() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  ActorState blocker;
  blocker.actor_id = 2;
  blocker.x = 49;
  blocker.y = 50;
  world.actors.emplace(2, blocker);
  world.ground_items.emplace(900, GroundItemState{900, 49, 50, 1, "Gold"});
  world.pending_pickup_item_id = 900;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.process_action_messages(context, 0.016F);
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto turn = decode_message<ActionIntent>(frames.front());
  assert(turn.has_value());
  assert(turn->kind == WorldActionKind::turn);
  assert(world.pending_pickup_item_id == 0);

  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.pending_pickup_item_id == 0);
}

void test_move_candidate_drops_when_animation_busy_after_ack() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  world.legacy_target_x = 51;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<ActionIntent>(frames.front()).has_value());

  state.apply(ActionAck{true, 1234});
  scenes.update(context, 0.016F);
  assert(!world.action_locked);

  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == LegacyChrAction::none);

  for (int tick = 0; tick < 3; ++tick) {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    scenes.update(context, 0.110F);
    assert(app.drain_sent_frames_for_test().empty());
  }
  assert(world.legacy_chr_action == LegacyChrAction::none);
}

void test_far_pickup_candidate_drops_when_animation_busy_after_ack() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  world.legacy_target_x = 51;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<ActionIntent>(frames.front()).has_value());

  state.apply(ActionAck{true, 1234});
  scenes.update(context, 0.016F);
  world.ground_items.emplace(901, GroundItemState{901, 52, 50, 1, "Gold"});
  world.pending_pickup_item_id = 901;

  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.pending_pickup_item_id == 0);
  assert(world.legacy_chr_action == LegacyChrAction::none);

  for (int tick = 0; tick < 3; ++tick) {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    scenes.update(context, 0.110F);
    assert(app.drain_sent_frames_for_test().empty());
  }
}

void test_same_tile_pickup_candidate_drops_when_animation_busy_after_ack() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;

  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  world.legacy_target_x = 51;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);
  auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<ActionIntent>(frames.front()).has_value());

  state.apply(ActionAck{true, 1234});
  scenes.update(context, 0.016F);
  world.ground_items.emplace(902, GroundItemState{902, 51, 50, 1, "Gold"});
  world.pending_pickup_item_id = 902;

  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
  assert(world.pending_pickup_item_id == 0);

  for (int tick = 0; tick < 3; ++tick) {
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    scenes.update(context, 0.110F);
    assert(app.drain_sent_frames_for_test().empty());
  }
  assert(world.pending_pickup_item_id == 0);
}

void test_mouse_down_cancels_earlier_same_frame_magic_key() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 52, 50);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  input.events.push_back(key_down_event(VK_F1));
  input.events.push_back(left_down_event(mouse_x, mouse_y));
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.dispatch_legacy_input_events(context);
  assert(world.action_key == -1);
  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());
}

void test_later_same_frame_magic_key_survives_mouse_down() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 52, 50);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  input.events.push_back(left_down_event(mouse_x, mouse_y));
  input.events.push_back(key_down_event(VK_F1));
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.dispatch_legacy_input_events(context);
  assert(world.action_key == 0);
  scenes.process_action_messages(context, 0.016F);
  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
}

void test_magic_key_survives_held_mouse_repeat() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0, 0, 0, 0});
  world.action_locked = true;
  world.action_lock_started_ms = detail::monotonic_ms();

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 52, 50);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  input.left_down = true;
  input.events.push_back(left_down_event(mouse_x, mouse_y));
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);

  scenes.dispatch_legacy_input_events(context);
  scenes.process_action_messages(context, 0.016F);
  assert(app.drain_sent_frames_for_test().empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(320));
  input.events.clear();
  auto key_event = key_down_event(VK_F1);
  key_event.mouse_x = mouse_x;
  key_event.mouse_y = mouse_y;
  key_event.left_down = true;
  input.events.push_back(key_event);
  context.legacy_input_dispatched = false;
  scenes.dispatch_legacy_input_events(context);
  assert(world.action_key == 0);
  scenes.process_action_messages(context, 0.016F);
  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  assert(decode_message<SpellIntent>(frames.front()).has_value());
}

void test_rush_confirmation_records_rush_cooldown() {
  GameStateStore state;
  reset_world(state);
  auto& world = state.world;
  auto& self = world.actors[1];
  const auto now_ms = detail::monotonic_ms();

  LegacyActorMessage message;
  message.kind = LegacyActorMessage::Kind::action;
  message.actor_id = 1;
  message.action_kind = ActorActionKind::rush;
  message.legacy_ident = legacy::kSmRush;
  message.x = 52;
  message.y = 50;
  message.dir = 2;

  state.start_legacy_actor_action(self, message, now_ms);
  assert(world.latest_rush_rush_ms == now_ms);
}

void test_rush_kung_confirmation_does_not_record_rush_cooldown() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  auto& self = world.actors[1];
  const auto now_ms = detail::monotonic_ms();

  LegacyActorMessage message;
  message.kind = LegacyActorMessage::Kind::action;
  message.actor_id = 1;
  message.action_kind = ActorActionKind::rush_kung;
  message.legacy_ident = legacy::kSmRushKung;
  message.x = 52;
  message.y = 50;
  message.dir = 2;

  state.start_legacy_actor_action(self, message, now_ms);
  assert(world.latest_rush_rush_ms == 0);

  world.magics.push_back(MagicShortcutState{27, 1, 0, 0, 1200, "RushKung", 0, 0, 0, 0, 0, 0});
  world.action_key = 0;
  ClientConfig config;
  InputState input;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->magic_id == 27);
}

void test_target_magic_uses_target_position_and_mouse_direction() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{7, 1, 0, 0, 900, "Fire", 0, 0, 1, 0, 0, 0});
  ActorState target;
  target.actor_id = 2;
  target.x = 52;
  target.y = 50;
  target.actor_type = ActorType::player;
  world.actors.emplace(2, target);
  world.focus_actor_id = 2;
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->x == 52);
  assert(spell->y == 50);
  assert(spell->target_actor_id == 2);
  assert(spell->dir == legacy::next_direction(50, 50, 50, 52));
  assert(spell->dir != legacy::next_direction(50, 50, 52, 50));
  assert(world.magic_pk_delay_ms >= 300);
  assert(world.magic_pk_delay_ms <= 1399);
}

void test_target_magic_does_not_fallback_to_selected_target() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{7, 1, 0, 0, 900, "Fire", 0, 0, 1, 0, 0, 0});
  ActorState target;
  target.actor_id = 2;
  target.x = 52;
  target.y = 50;
  target.actor_type = ActorType::player;
  world.actors.emplace(2, target);
  world.target_actor_id = 2;
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->x == 50);
  assert(spell->y == 52);
  assert(spell->target_actor_id == 0);
  assert(spell->dir == legacy::next_direction(50, 50, 50, 52));
  assert(world.magic_pk_delay_ms == 0);
}

void test_normal_magic_requires_delphi_mp_cost_before_lock() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.actors[1].mp = 99;
  world.self_ability_detail.mp = 22;
  world.magics.push_back(MagicShortcutState{7, 1, 1, 0, 900, "Fire", 0, 0, 1, 20, 3, 3});
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.empty());
  assert(!world.action_locked);
  assert(world.actors[1].current_action != ActorActionKind::spell);

  world.self_ability_detail.mp = 23;
  world.action_key = 0;
  scenes.process_action_messages(context, 0.016F);
  const auto allowed_frames = app.drain_sent_frames_for_test();
  assert(allowed_frames.size() == 1);
  assert(decode_message<SpellIntent>(allowed_frames.front()).has_value());
  assert(world.action_locked);
}

void test_normal_magic_uses_client_raw_spell_cost_before_lock() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.self_ability_detail.mp = 2;
  world.magics.push_back(MagicShortcutState{7, 1, 1, 0, 900, "Fire", 0, 0, 1, 5, 0, 3});
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.empty());
  assert(!world.action_locked);

  world.self_ability_detail.mp = 5;
  world.action_key = 0;
  scenes.process_action_messages(context, 0.016F);
  const auto allowed_frames = app.drain_sent_frames_for_test();
  assert(allowed_frames.size() == 1);
  assert(decode_message<SpellIntent>(allowed_frames.front()).has_value());
  assert(world.action_locked);
}

void test_self_buff_magic_uses_focus_target_like_delphi() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{31, 1, 0, 0, 900, "MagicShield", 0, 0, 31, 0, 0, 0});
  ActorState target;
  target.actor_id = 2;
  target.x = 52;
  target.y = 50;
  target.actor_type = ActorType::player;
  world.actors.emplace(2, target);
  world.focus_actor_id = 2;
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->x == 52);
  assert(spell->y == 50);
  assert(spell->target_actor_id == 2);
  assert(spell->dir == legacy::next_direction(50, 50, 50, 52));
  assert(world.magic_pk_delay_ms >= 300);
  assert(world.magic_pk_delay_ms <= 1399);
}

void test_ground_magic_uses_focus_target_actor() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{22, 1, 0, 0, 1200, "FireWall", 0, 0, 4, 0, 0, 0});
  ActorState target;
  target.actor_id = 2;
  target.x = 52;
  target.y = 50;
  target.actor_type = ActorType::player;
  world.actors.emplace(2, target);
  world.focus_actor_id = 2;
  world.target_actor_id = 2;
  world.action_key = 0;

  auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
  ClientConfig config;
  InputState input;
  input.mouse_x = mouse_x;
  input.mouse_y = mouse_y;
  SceneManager scenes;
  ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
  context.legacy_input_dispatched = true;
  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  scenes.process_action_messages(context, 0.016F);

  const auto frames = app.drain_sent_frames_for_test();
  assert(frames.size() == 1);
  const auto spell = decode_message<SpellIntent>(frames.front());
  assert(spell.has_value());
  assert(spell->x == 52);
  assert(spell->y == 50);
  assert(spell->target_actor_id == 2);
  assert(spell->dir == legacy::next_direction(50, 50, 50, 52));
  assert(world.magic_pk_delay_ms >= 300);
  assert(world.magic_pk_delay_ms <= 1399);
}

void test_area_magic_uses_focus_target_actor() {
  const auto run_case = [](const MagicShortcutState& magic) {
    ClientApp app;
    connect_for_test(app);
    auto& state = app.state_for_test();
    reset_world(state);
    auto& world = state.world;
    world.magics.push_back(magic);
    ActorState target;
    target.actor_id = 2;
    target.x = 52;
    target.y = 50;
    target.actor_type = ActorType::monster;
    world.actors.emplace(2, target);
    world.focus_actor_id = 2;
    world.target_actor_id = 2;
    world.action_key = 0;

    auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
    ClientConfig config;
    InputState input;
    input.mouse_x = mouse_x;
    input.mouse_y = mouse_y;
    SceneManager scenes;
    ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
    context.legacy_input_dispatched = true;
    scenes.initialize(context);
    scenes.change_scene(SceneId::world, context);
    scenes.process_action_messages(context, 0.016F);

    const auto frames = app.drain_sent_frames_for_test();
    assert(frames.size() == 1);
    const auto spell = decode_message<SpellIntent>(frames.front());
    assert(spell.has_value());
    assert(spell->x == 52);
    assert(spell->y == 50);
    assert(spell->target_actor_id == 2);
    assert(spell->dir == legacy::next_direction(50, 50, 50, 52));
  };

  run_case(MagicShortcutState{19, 1, 0, 0, 500, "GroupHide", 0, 0, 8, 0, 0, 0});
  run_case(MagicShortcutState{23, 1, 0, 0, 600, "Explosion", 0, 0, 2, 0, 0, 0});
  run_case(MagicShortcutState{29, 1, 0, 0, 400, "GroupHeal", 0, 0, 2, 0, 0, 0});
}

void test_area_magic_uses_ground_without_focus() {
  const auto run_case = [](const MagicShortcutState& magic) {
    ClientApp app;
    connect_for_test(app);
    auto& state = app.state_for_test();
    reset_world(state);
    auto& world = state.world;
    world.magics.push_back(magic);
    ActorState target;
    target.actor_id = 2;
    target.x = 52;
    target.y = 50;
    target.actor_type = ActorType::monster;
    world.actors.emplace(2, target);
    world.target_actor_id = 2;
    world.action_key = 0;

    auto [mouse_x, mouse_y] = mouse_for_tile(world.actors[1].x, world.actors[1].y, 50, 52);
    ClientConfig config;
    InputState input;
    input.mouse_x = mouse_x;
    input.mouse_y = mouse_y;
    SceneManager scenes;
    ClientContext context{&app, &config, &state, nullptr, nullptr, nullptr, &input};
    context.legacy_input_dispatched = true;
    scenes.initialize(context);
    scenes.change_scene(SceneId::world, context);
    scenes.process_action_messages(context, 0.016F);

    const auto frames = app.drain_sent_frames_for_test();
    assert(frames.size() == 1);
    const auto spell = decode_message<SpellIntent>(frames.front());
    assert(spell.has_value());
    assert(spell->x == 50);
    assert(spell->y == 52);
    assert(spell->target_actor_id == 0);
  };

  run_case(MagicShortcutState{19, 1, 0, 0, 500, "GroupHide", 0, 0, 8, 0, 0, 0});
  run_case(MagicShortcutState{23, 1, 0, 0, 600, "Explosion", 0, 0, 2, 0, 0, 0});
  run_case(MagicShortcutState{29, 1, 0, 0, 400, "GroupHeal", 0, 0, 2, 0, 0, 0});
}

}  // namespace

int main() {
  test_magic_key_does_not_stick_across_blocked_frame();
  test_source_only_magic_bypasses_action_gate_without_local_spell();
  test_source_only_magic_bypasses_existing_action_lock();
  test_source_only_magic_refreshes_lock_time_and_preserves_prior_action_rollback();
  test_source_only_magic_after_sm_movefail_ack_consumes_real_spell_failure();
  test_source_only_magic_expires_stale_lock_before_send();
  test_attack_attempt_updates_move_suppression();
  test_locked_two_cell_target_drops_stale_chase_move();
  test_pickup_has_no_extra_client_throttle();
  test_ground_pickup_clears_stale_target_before_attack_phase();
  test_locked_pending_move_candidate_is_dropped();
  test_locked_same_tile_pickup_candidate_is_dropped();
  test_locked_far_pickup_does_not_auto_move_after_unlock();
  test_far_ground_item_click_only_queues_move();
  test_pickup_turn_candidate_is_one_shot();
  test_move_candidate_drops_when_animation_busy_after_ack();
  test_far_pickup_candidate_drops_when_animation_busy_after_ack();
  test_same_tile_pickup_candidate_drops_when_animation_busy_after_ack();
  test_mouse_down_cancels_earlier_same_frame_magic_key();
  test_later_same_frame_magic_key_survives_mouse_down();
  test_magic_key_survives_held_mouse_repeat();
  test_rush_confirmation_records_rush_cooldown();
  test_rush_kung_confirmation_does_not_record_rush_cooldown();
  test_target_magic_uses_target_position_and_mouse_direction();
  test_target_magic_does_not_fallback_to_selected_target();
  test_normal_magic_requires_delphi_mp_cost_before_lock();
  test_normal_magic_uses_client_raw_spell_cost_before_lock();
  test_self_buff_magic_uses_focus_target_like_delphi();
  test_ground_magic_uses_focus_target_actor();
  test_area_magic_uses_focus_target_actor();
  test_area_magic_uses_ground_without_focus();
  return 0;
}
