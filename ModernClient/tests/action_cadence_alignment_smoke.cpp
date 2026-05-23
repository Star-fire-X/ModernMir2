#include "app/client_app.hpp"
#include "scene/scenes.hpp"

#include <cassert>

using namespace mir2::client;
using namespace mir2::client_v1;
namespace legacy = mir2::legacy;

namespace {

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
  world.magics.push_back(MagicShortcutState{7, 1, 0, 0, 900, "Fire", 0, 0, 1});

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
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0});
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

void test_source_only_magic_respects_existing_action_lock() {
  ClientApp app;
  connect_for_test(app);
  auto& state = app.state_for_test();
  reset_world(state);
  auto& world = state.world;
  world.magics.push_back(MagicShortcutState{26, 1, 0, 0, 1200, "FireSword", 0, 0, 0});
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
  assert(frames.empty());
  assert(world.action_key == -1);
  assert(world.action_locked);
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
  world.actors.emplace(2, ActorState{2, "Target", 51, 50, 51, 50, 6, 0, 0,
                                     ActorType::monster});
  world.target_actor_id = 2;
  world.latest_hit_ms = detail::monotonic_ms();
  world.last_attack_ms = 0;

  scenes.process_action_messages(context, 0.016F);
  assert(world.last_attack_ms != 0);
  assert(!world.action_locked);
}

void test_two_cell_target_chases_instead_of_longhit() {
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
  world.actors.emplace(2, ActorState{2, "Target", 52, 50, 52, 50, 6, 0, 0,
                                     ActorType::monster});
  world.target_actor_id = 2;

  scenes.process_action_messages(context, 0.016F);
  assert(world.legacy_chr_action == LegacyChrAction::walk);
  assert(world.legacy_target_x == 51);
  assert(world.legacy_target_y == 50);
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

void test_rush_kung_confirmation_records_rush_cooldown() {
  GameStateStore state;
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
  assert(world.latest_rush_rush_ms == now_ms);
}

}  // namespace

int main() {
  test_magic_key_does_not_stick_across_blocked_frame();
  test_source_only_magic_bypasses_action_gate_without_local_spell();
  test_source_only_magic_respects_existing_action_lock();
  test_attack_attempt_updates_move_suppression();
  test_two_cell_target_chases_instead_of_longhit();
  test_pickup_has_no_extra_client_throttle();
  test_rush_kung_confirmation_records_rush_cooldown();
  return 0;
}
