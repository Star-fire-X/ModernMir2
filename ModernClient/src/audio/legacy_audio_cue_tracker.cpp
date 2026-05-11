#include "audio/legacy_audio_cue_tracker.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "audio/audio_service.hpp"
#include "audio/legacy_sound_rules.hpp"

namespace mir2::client {

namespace {

bool actor_is_human_audio(const ActorState& actor) {
  return actor.actor_type == client_v1::ActorType::player;
}

bool actor_is_monster_audio(const ActorState& actor) {
  return actor.actor_type == client_v1::ActorType::monster;
}

bool crossed_frame(const int previous, const int current, const int target) {
  return previous < target && current >= target;
}

void play_sound(AudioService& audio, const int sound_id,
                const std::uint64_t now_ms) {
  if (sound_id >= 0) {
    audio.queue_sound(sound_id, now_ms);
  }
}

void play_optional_sound(AudioService& audio, const std::optional<int> sound_id,
                         const std::uint64_t now_ms) {
  if (sound_id.has_value()) {
    play_sound(audio, *sound_id, now_ms);
  }
}

const ActorState* find_actor(const WorldViewState& world, const std::uint64_t actor_id) {
  const auto it = world.actors.find(actor_id);
  return it == world.actors.end() ? nullptr : &it->second;
}

int actor_weapon_feature(const ActorState* actor) {
  if (actor == nullptr || !actor_is_human_audio(*actor)) {
    return 0;
  }
  return decode_legacy_human_feature(actor->feature).weapon;
}

void play_human_struck(const WorldViewState& world, const ActorState& actor,
                       AudioService& audio, const std::uint64_t now_ms) {
  const auto appearance = decode_legacy_human_feature(actor.feature);
  const auto* hitter = find_actor(world, actor.last_hitter_id);
  const auto hitter_weapon = actor_weapon_feature(hitter);
  if (!actor.last_damage_magic && hitter != nullptr && actor_is_human_audio(*hitter)) {
    play_optional_sound(audio, human_struck_weapon_sound_id(hitter_weapon), now_ms);
    play_sound(audio, human_struck_body_sound_id(appearance.dress, hitter_weapon),
               now_ms);
  }
  play_sound(audio, human_struck_vocal_sound_id(appearance.sex), now_ms);
}

void play_monster_struck(const WorldViewState& world, const ActorState& actor,
                         AudioService& audio, const std::uint64_t now_ms) {
  const auto* hitter = find_actor(world, actor.last_hitter_id);
  if (!actor.last_damage_magic && hitter != nullptr && actor_is_human_audio(*hitter)) {
    const auto hitter_weapon = actor_weapon_feature(hitter);
    play_optional_sound(audio, human_struck_weapon_sound_id(hitter_weapon), now_ms);
    play_sound(audio, human_struck_body_sound_id(0, hitter_weapon), now_ms);
  }
  play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                     monster_offset_scream),
             now_ms);
}

void play_death(const ActorState& actor, AudioService& audio,
                const std::uint64_t now_ms) {
  if (actor_is_human_audio(actor)) {
    play_sound(audio,
               human_die_sound_id(decode_legacy_human_feature(actor.feature).sex),
               now_ms);
    return;
  }
  if (actor_is_monster_audio(actor)) {
    play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                       monster_offset_die),
               now_ms);
  }
}

void play_action_start(const WorldViewState& world, const ActorState& actor,
                       AudioService& audio, const std::uint64_t now_ms) {
  switch (actor.current_action) {
    case client_v1::ActorActionKind::struck:
      if (actor_is_human_audio(actor)) {
        play_human_struck(world, actor, audio, now_ms);
      } else if (actor_is_monster_audio(actor)) {
        play_monster_struck(world, actor, audio, now_ms);
      }
      break;
    case client_v1::ActorActionKind::hit:
      if (actor_is_monster_audio(actor)) {
        play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                           monster_offset_attack),
                   now_ms);
      }
      break;
    case client_v1::ActorActionKind::spell:
      if (actor.magic_id != 0) {
        play_sound(audio, magic_sound_id(actor.magic_id,
                                         LegacyMagicSoundPhase::start),
                   now_ms);
      }
      break;
    case client_v1::ActorActionKind::turn:
    case client_v1::ActorActionKind::walk:
    case client_v1::ActorActionKind::run:
    case client_v1::ActorActionKind::rush:
    case client_v1::ActorActionKind::rush_kung:
    case client_v1::ActorActionKind::backstep:
    case client_v1::ActorActionKind::knockback:
    default:
      break;
  }
}

const MapCell* footstep_cell_for_actor(const MapDocument* map, const ActorState& actor) {
  if (map == nullptr) {
    return nullptr;
  }
  const auto x = actor.x < 0 ? actor.x : (actor.x / 2) * 2;
  const auto y = actor.y < 0 ? actor.y : (actor.y / 2) * 2;
  return map->cell(x, y);
}

void play_frame_cues(const ActorState& actor, const ActorRenderPose& pose,
                     const MapDocument* map, AudioService& audio,
                     LegacyAudioCueTracker::ActorCueState& state,
                     const bool is_self,
                     const std::uint64_t now_ms,
                     LegacyAudioCueTracker& tracker) {
  const auto local_frame = actor_local_frame_for_sound(actor, pose);

  if (actor.dead && actor_is_monster_audio(actor) &&
      legacy_appr_feature(actor.feature) == 80 && state.death_action_active &&
      !state.die2_played &&
      crossed_frame(state.last_action_local_frame, local_frame, 2)) {
    play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                       monster_offset_die2),
               now_ms);
    state.die2_played = true;
  }

  if (!actor.dead && (actor.current_action == client_v1::ActorActionKind::walk ||
                      actor.current_action == client_v1::ActorActionKind::run ||
                      actor.current_action == client_v1::ActorActionKind::rush ||
                      actor.current_action == client_v1::ActorActionKind::rush_kung ||
                      actor.current_action == client_v1::ActorActionKind::backstep ||
                      actor.current_action == client_v1::ActorActionKind::knockback)) {
    if (actor_is_monster_audio(actor) &&
        crossed_frame(state.last_move_local_frame, local_frame, 1) &&
        tracker.next_monster_normal_sound_hit()) {
      play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                         monster_offset_normal),
                 now_ms);
    }
    if (crossed_frame(state.last_move_local_frame, local_frame, 1) &&
        !state.left_foot_played && actor_is_human_audio(actor) && is_self) {
      play_sound(audio, footstep_sound_id(footstep_cell_for_actor(map, actor),
                                          actor.running ||
                                             actor.current_action == client_v1::ActorActionKind::run ||
                                                 actor.current_action == client_v1::ActorActionKind::rush ||
                                                 actor.current_action == client_v1::ActorActionKind::rush_kung,
                                          false),
                 now_ms);
      state.left_foot_played = true;
    }
    if (crossed_frame(state.last_move_local_frame, local_frame, 4) &&
        !state.right_foot_played && actor_is_human_audio(actor) && is_self) {
      play_sound(audio, footstep_sound_id(footstep_cell_for_actor(map, actor),
                                          actor.running ||
                                             actor.current_action == client_v1::ActorActionKind::run ||
                                                 actor.current_action == client_v1::ActorActionKind::rush ||
                                                 actor.current_action == client_v1::ActorActionKind::rush_kung,
                                          true),
                 now_ms);
      state.right_foot_played = true;
    }
    state.last_move_local_frame = local_frame;
    return;
  }

  if (!actor.dead && actor_is_monster_audio(actor) &&
      actor.current_action == client_v1::ActorActionKind::turn &&
      crossed_frame(state.last_action_local_frame, local_frame, 1) &&
      tracker.next_monster_normal_sound_hit()) {
    play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                       monster_offset_normal),
               now_ms);
  }

  if (!actor.dead && actor.current_action == client_v1::ActorActionKind::hit &&
      !state.weapon_played) {
    if (actor_is_human_audio(actor) && crossed_frame(state.last_action_local_frame, local_frame, 2)) {
      const auto appearance = decode_legacy_human_feature(actor.feature);
      play_sound(audio, human_weapon_sound_id(appearance.weapon), now_ms);
      for (const auto sound_id :
           legacy_human_attack_extra_sound_ids(actor.legacy_action_ident, appearance.sex)) {
        play_sound(audio, sound_id, now_ms);
      }
      state.weapon_played = true;
    } else if (actor_is_monster_audio(actor) &&
               crossed_frame(state.last_action_local_frame, local_frame, 3)) {
      play_sound(audio, monster_sound_id(legacy_appr_feature(actor.feature),
                                         monster_offset_weapon),
                 now_ms);
      state.weapon_played = true;
    }
  }

  state.last_action_local_frame = local_frame;
}

void play_magic_audio_cues(AnimationManager& animation, AudioService& audio,
                           const std::uint64_t now_ms) {
  for (const auto& cue : animation.drain_magic_audio_cues()) {
    if (cue.magic_id <= 0) {
      continue;
    }
    const auto phase = cue.phase == LegacyMagicAudioCuePhase::explosion
                           ? LegacyMagicSoundPhase::explosion
                           : LegacyMagicSoundPhase::fire;
    play_sound(audio, magic_sound_id(cue.magic_id, phase), now_ms);
  }
}

}  // namespace

void LegacyAudioCueTracker::reset() {
  actors_.clear();
  monster_normal_rng_ = 0x4D495232U;
}

void LegacyAudioCueTracker::update(const WorldViewState& world, AnimationManager& animation,
                                   const MapDocument* map, AudioService& audio,
                                   const std::uint64_t now_ms) {
  play_magic_audio_cues(animation, audio, now_ms);

  for (auto it = actors_.begin(); it != actors_.end();) {
    if (world.actors.find(it->first) == world.actors.end()) {
      it = actors_.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto& [actor_id, actor] : world.actors) {
    const auto pose = animation.pose_for(actor_id);
    if (!pose.has_value()) {
      continue;
    }

    auto& state = actors_[actor_id];
    const auto first_seen = !state.seen;
    if (first_seen) {
      state.seen = true;
      state.last_dead = actor.dead;
      state.last_action_started_ms = actor.action_started_ms;
      state.last_move_started_ms = actor.move_started_ms;
    }

    if (!actor.dead) {
      state.death_action_active = false;
      state.die2_played = false;
    }

    if (actor.dead && !state.last_dead) {
      state.death_action_active = true;
      state.die2_played = false;
      play_death(actor, audio, now_ms);
    }
    state.last_dead = actor.dead;

    if (actor.move_started_ms != state.last_move_started_ms) {
      state.last_move_started_ms = actor.move_started_ms;
      state.last_move_local_frame = -1;
      state.left_foot_played = false;
      state.right_foot_played = false;
    }

    if (actor.action_started_ms != state.last_action_started_ms) {
      state.last_action_started_ms = actor.action_started_ms;
      state.last_action_local_frame = -1;
      state.weapon_played = false;
      if (!first_seen && actor.action_started_ms != 0 && !actor.dead) {
        play_action_start(world, actor, audio, now_ms);
      }
    }

    play_frame_cues(actor, *pose, map, audio, state,
                    actor_id == world.self_actor_id, now_ms, *this);
  }
}

bool LegacyAudioCueTracker::next_monster_normal_sound_hit() {
  monster_normal_rng_ = monster_normal_rng_ * 1664525U + 1013904223U;
  return ((monster_normal_rng_ >> 16U) & 0x7U) == 1U;
}

}  // namespace mir2::client
