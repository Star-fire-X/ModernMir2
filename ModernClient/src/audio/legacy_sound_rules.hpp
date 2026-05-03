#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "assets/asset_manager.hpp"
#include "audio/sound_constants.hpp"
#include "game/game_state.hpp"

namespace mir2::client {

enum class LegacyClickSound {
  none,
  stone,
  glass,
  normal,
};

enum class LegacyMagicSoundPhase {
  start,
  fire,
  explosion,
};

[[nodiscard]] std::optional<int> legacy_click_sound_id(LegacyClickSound sound);
[[nodiscard]] int item_click_sound_id(std::uint8_t std_mode, std::string_view name);
[[nodiscard]] std::optional<int> item_use_sound_id(std::uint8_t std_mode);
[[nodiscard]] int footstep_sound_id(const MapCell* cell, bool running, bool right_foot);
[[nodiscard]] int human_weapon_sound_id(int weapon_feature);
[[nodiscard]] std::vector<int> legacy_human_attack_extra_sound_ids(std::uint16_t legacy_ident,
                                                                   int sex);
[[nodiscard]] std::optional<int> human_struck_weapon_sound_id(int attacker_weapon_feature);
[[nodiscard]] int human_struck_body_sound_id(int defender_dress_feature,
                                             int attacker_weapon_feature);
[[nodiscard]] int human_struck_vocal_sound_id(int sex);
[[nodiscard]] int human_die_sound_id(int sex);
[[nodiscard]] int monster_sound_id(int appearance, MonsterSoundOffset offset);
[[nodiscard]] int magic_sound_id(int magic_serial, LegacyMagicSoundPhase phase);
[[nodiscard]] int actor_local_frame_for_sound(const ActorState& actor,
                                              const ActorRenderPose& pose);

}  // namespace mir2::client
