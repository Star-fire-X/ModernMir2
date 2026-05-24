#include "game/game_state.hpp"

#include <cassert>

int main() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  WorldViewState world;
  ActorState self;
  self.actor_id = 1;
  self.x = 12;
  self.y = 10;
  self.dir = 2;

  world.action_locked = true;
  world.action_lock_started_ms = 1000;
  world.pending_action_acks.push_back(
      PendingActionAckState{mir2::legacy::kCmHit, 2, true, 12, 10, 2, 1000});
  assert(!server_accept_next_action(world, 1100));
  assert(!can_next_action(world, self, 1100));
  assert(!server_accept_next_action(world, 11000));
  assert(world.action_locked);
  assert(!server_accept_next_action(world, 11001));
  assert(!world.action_locked);
  assert(world.action_lock_timeout_cleared_ms == 11001);
  assert(world.pending_action_acks.empty());
  assert(can_next_action(world, self, 11001));
  assert(server_accept_next_action(world, 11002));
  self.action_started_ms = 1000;
  self.action_duration_ms = 500;
  assert(!can_next_action(world, self, false, 11002));
  assert(can_next_action(world, self, true, 11002));
  self.dead = true;
  assert(!can_next_action(world, self, true, 11002));
  self.dead = false;
  world.dizzy_delay_start_ms = 11000;
  world.dizzy_delay_time_ms = 100;
  assert(!can_next_action(world, self, true, 11050));
  world.dizzy_delay_time_ms = 0;

  world.last_sent_action_ident = mir2::legacy::kCmHit;
  world.last_sent_action_dir = 2;
  self.legacy_old_x = 11;
  self.legacy_old_y = 10;
  self.legacy_old_dir = 1;
  self.legacy_has_old_position = true;
  self.current_action = ActorActionKind::hit;
  legacy_action_failed(world, self, 12000);
  assert(world.action_fail_lock);
  assert(self.x == 11 && self.y == 10 && self.dir == 1);
  assert(self.current_action == ActorActionKind::turn);
  assert(!is_unlock_action(world, mir2::legacy::kCmHit, 2, 12999));
  assert(is_unlock_action(world, mir2::legacy::kCmHit, 2, 13000));
  assert(!world.action_fail_lock);

  GameStateStore state;
  state.world.action_locked = true;
  state.apply(ActionAck{true, 0});
  assert(!state.world.action_locked);
  assert(!state.world.action_fail_lock);
  state.world.action_locked = true;
  state.world.pending_action_acks.push_back(
      PendingActionAckState{mir2::legacy::kCmHit, 2, true, 12, 10, 2, 1200});
  state.clear_world_ui_state();
  assert(!state.world.action_locked);
  assert(state.world.pending_action_acks.empty());

  WorldSnapshot snapshot;
  snapshot.self_actor_id = 1;
  snapshot.actors.push_back(WorldActor{1, "Hero", 30, 20, 2, 0, 0, ActorType::player});
  state.apply(snapshot);
  auto& actor = state.world.actors[1];
  actor.from_x = 29;
  actor.from_y = 20;
  actor.x = 30;
  actor.y = 20;
  actor.current_action = ActorActionKind::walk;
  state.world.action_locked = true;
  state.world.last_sent_action_ident = static_cast<std::uint16_t>(3000U + mir2::legacy::kSmWalk);
  state.world.last_sent_action_dir = 2;
  state.apply(ActionAck{false, 0});
  assert(!state.world.action_locked);
  assert(actor.x == 29 && actor.y == 20);
  assert(state.world.action_fail_lock);
  state.world.pending_action_acks.push_back(
      PendingActionAckState{mir2::legacy::kCmHit, 2, true, 29, 20, 2, 1300});
  state.apply(MapChange{"1"});
  state.apply(MapEntered{"1", 1, 29, 20, 2});
  assert(state.world.pending_action_acks.empty());
  return 0;
}
