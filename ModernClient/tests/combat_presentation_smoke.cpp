#include <cassert>
#include <cstdint>

#include "animation/legacy_animation.hpp"
#include "game/game_state.hpp"
#include "shared/legacy/action_ids.hpp"

int main() {
  using namespace mir2::client;
  using namespace mir2::client_v1;

  GameStateStore state;
  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors.push_back(WorldActor{
      1000, "Hero", 330, 270, 2, make_legacy_feature(0, 1, 2, 3), 0, ActorType::player});
  snapshot.actors.push_back(WorldActor{2000, "Scarecrow", 332, 271, 4, 10, 0,
                                       ActorType::monster});
  state.apply(snapshot);

  AnimationManager animations;
  animations.reset(1000);
  animations.sync_world(state.world, 1000);
  animations.update(state.world, 1000);
  assert(animations.pose_for(1000).has_value());
  assert(animations.pose_for(2000).has_value());

  state.apply(ActorAction{1000, ActorActionKind::hit, 330, 270, 2, 2000, 0,
                          mir2::legacy::kSmHit, 0, false, 0});
  state.world.actors[1000].action_started_ms = 1100;
  animations.sync_world(state.world, 1100);
  animations.update(state.world, 1100);
  const auto hit_pose = animations.pose_for(1000);
  assert(hit_pose.has_value());
  assert(hit_pose->body_index ==
         600 + legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));

  state.apply(ActorVitals{2000, 7, 12, -1, -1, 5, 1000, false});
  state.world.actors[2000].action_started_ms = 1200;
  animations.sync_world(state.world, 1200);
  animations.update(state.world, 1200);
  assert(state.world.actors[2000].current_action == ActorActionKind::struck);
  assert(state.world.actors[2000].last_damage == 5);
  assert(state.world.actors[2000].last_hitter_id == 1000);
  assert(animations.pose_for(2000).has_value());

  state.apply(MagicList{{MagicEntry{1, 1, 0, 0, 1000, "Fireball", 32, 900, 7}}});
  state.apply(ActorAction{1000, ActorActionKind::spell, 333, 271, 3, 2000, 0, 0, 1,
                          true, 32});
  state.world.actors[1000].action_started_ms = 1300;
  state.apply(ActorMagicFire{1000, 2000, 333, 271, 7, 32});
  animations.sync_world(state.world, 1300);
  animations.update(state.world, 1300);
  assert(state.world.actors[1000].current_action == ActorActionKind::spell);
  assert(state.world.actors[1000].action_magic_effect_type == 7);
  assert(animations.effects().fly_count() + animations.effects().ground_count() +
             animations.effects().overlay_count() >
         0);

  state.apply(ActorDeath{2000, 332, 271, 4});
  animations.sync_world(state.world, 1400);
  animations.update(state.world, 1400);
  assert(state.world.actors[2000].dead);
  assert(state.world.actors[2000].hp == 0);
  const auto death_pose = animations.pose_for(2000);
  assert(death_pose.has_value());
  assert(death_pose->dead);

  state.world.focus_actor_id = 2000;
  state.world.target_actor_id = 2000;
  state.world.actors[1000].action_target_actor_id = 2000;
  state.apply(ActorRemove{2000});
  assert(state.world.actors.find(2000) == state.world.actors.end());
  assert(state.world.focus_actor_id == 0);
  assert(state.world.target_actor_id == 0);
  assert(state.world.actors[1000].action_target_actor_id == 0);

  GroundItemState gold;
  gold.object_id = 3000;
  gold.x = 331;
  gold.y = 270;
  gold.looks = 5;
  gold.name = "Gold";
  state.apply(GroundItemAdd{gold});
  assert(state.world.ground_items.size() == 1);
  state.world.focus_ground_item_id = 3000;
  state.world.pending_pickup_item_id = 3000;
  state.apply(GroundItemRemove{3000, 331, 270});
  assert(state.world.ground_items.empty());
  assert(state.world.focus_ground_item_id == 0);
  assert(state.world.pending_pickup_item_id == 0);

  auto& self = state.world.actors[1000];
  self.current_action = ActorActionKind::hit;
  state.world.action_locked = true;
  state.apply(ActionAck{false, 1500});
  assert(!state.world.action_locked);
  assert(self.current_action == ActorActionKind::turn);

  return 0;
}
