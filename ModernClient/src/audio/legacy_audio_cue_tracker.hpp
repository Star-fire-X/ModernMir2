#pragma once

#include <cstdint>
#include <unordered_map>

#include "assets/asset_manager.hpp"
#include "game/game_state.hpp"

namespace mir2::client {

class AnimationManager;
class AudioService;

class LegacyAudioCueTracker {
 public:
  struct ActorCueState {
    bool seen{false};
    bool last_dead{false};
    std::uint64_t last_action_started_ms{0};
    std::uint64_t last_move_started_ms{0};
    int last_action_local_frame{-1};
    int last_move_local_frame{-1};
    bool left_foot_played{false};
    bool right_foot_played{false};
    bool weapon_played{false};
    bool death_action_active{false};
    bool die2_played{false};
  };

  void reset();
  void update(const WorldViewState& world, AnimationManager& animation,
              const MapDocument* map, AudioService& audio,
              std::uint64_t now_ms);
  bool next_monster_normal_sound_hit();

 private:
  std::unordered_map<std::uint64_t, ActorCueState> actors_{};
  std::uint32_t monster_normal_rng_{0x4D495232U};
};

}  // namespace mir2::client
