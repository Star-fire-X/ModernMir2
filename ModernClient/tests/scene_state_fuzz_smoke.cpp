#include "game/game_state.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <unordered_set>

namespace {

mir2::client_v1::WorldActor actor(const std::uint64_t id, const std::int32_t x,
                                  const std::int32_t y) {
  return mir2::client_v1::WorldActor{id, "Actor", x, y, 0, 0, 0,
                                     mir2::client_v1::ActorType::monster};
}

mir2::client_v1::GroundItemState item(const std::uint64_t id, const std::int32_t x,
                                      const std::int32_t y) {
  return mir2::client_v1::GroundItemState{id, x, y, 1, "Gold"};
}

void assert_draw_orders_valid(const mir2::client::GameStateStore& state) {
  std::unordered_set<std::uint64_t> seen_actors;
  for (const auto id : state.world.actor_draw_order) {
    assert(state.world.actors.find(id) != state.world.actors.end());
    assert(seen_actors.insert(id).second);
  }

  std::unordered_set<std::uint64_t> seen_items;
  for (const auto id : state.world.ground_item_draw_order) {
    assert(state.world.ground_items.find(id) != state.world.ground_items.end());
    assert(seen_items.insert(id).second);
  }
}

}  // namespace

int main() {
  mir2::client::GameStateStore state;
  std::mt19937 rng{0x4D495232U};

  for (int step = 0; step < 1000; ++step) {
    switch (rng() % 9U) {
      case 0: {
        mir2::client_v1::WorldSnapshot snapshot;
        snapshot.map_id = "0";
        snapshot.width = 700;
        snapshot.height = 700;
        snapshot.self_actor_id = 1;
        snapshot.actors.push_back(actor(1, 330, 270));
        state.apply(snapshot);
        break;
      }
      case 1: {
        const auto id = 2U + (rng() % 32U);
        state.apply(mir2::client_v1::ActorUpsert{actor(id, static_cast<std::int32_t>(rng() % 700U),
                                                       static_cast<std::int32_t>(rng() % 700U))});
        break;
      }
      case 2: {
        const auto id = 2U + (rng() % 32U);
        state.apply(mir2::client_v1::ActorRemove{id});
        break;
      }
      case 3: {
        const auto id = 100U + (rng() % 64U);
        state.apply(mir2::client_v1::GroundItemAdd{
            item(id, static_cast<std::int32_t>(rng() % 700U),
                 static_cast<std::int32_t>(rng() % 700U))});
        break;
      }
      case 4: {
        const auto id = 100U + (rng() % 64U);
        state.apply(mir2::client_v1::GroundItemRemove{id, 0, 0});
        break;
      }
      case 5:
        state.world.action_locked = true;
        state.world.legacy_target_x = static_cast<int>(rng() % 700U);
        state.world.legacy_target_y = static_cast<int>(rng() % 700U);
        state.world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
        state.world.pending_pickup_item_id = 100U + (rng() % 64U);
        state.clear_world_ui_state();
        assert(!state.world.action_locked);
        assert(state.world.legacy_target_x == -1);
        assert(state.world.pending_pickup_item_id == 0);
        break;
      case 6:
        state.clear_play_scene_state();
        assert(state.world.actors.empty());
        assert(state.world.ground_items.empty());
        break;
      case 7:
        state.apply(mir2::client_v1::ActionAck{true, static_cast<std::uint32_t>(step)});
        break;
      default:
        state.apply(mir2::client_v1::SysMessage{"notice", 0});
        break;
    }
    assert_draw_orders_valid(state);
  }

  return 0;
}
