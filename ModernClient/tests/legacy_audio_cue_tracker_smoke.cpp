#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "animation/legacy_animation.hpp"
#include "audio/audio_service.hpp"
#include "audio/legacy_audio_cue_tracker.hpp"
#include "audio/legacy_sound_rules.hpp"
#include "shared/legacy/action_ids.hpp"

namespace {

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

bool has_sound_id(const mir2::client::AudioService& audio, const int sound_id) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [sound_id](const auto& event) {
    return event.sound_id.has_value() && *event.sound_id == sound_id;
  });
}

bool has_reason(const mir2::client::AudioService& audio, const std::string& reason) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [&reason](const auto& event) {
    return event.reason == reason;
  });
}

int count_sound_id(const mir2::client::AudioService& audio, const int sound_id) {
  const auto& events = audio.trace_events();
  return static_cast<int>(
      std::count_if(events.begin(), events.end(), [sound_id](const auto& event) {
        return event.sound_id.has_value() && *event.sound_id == sound_id;
      }));
}

mir2::client::ActorState make_human(std::uint64_t id, int x, int y, int weapon = 4,
                                    int dress = 0) {
  mir2::client::ActorState actor;
  actor.actor_id = id;
  actor.name = "actor";
  actor.x = x;
  actor.y = y;
  actor.from_x = x;
  actor.from_y = y;
  actor.dir = 0;
  actor.actor_type = mir2::client_v1::ActorType::player;
  actor.feature = mir2::client::make_legacy_feature(
      0, static_cast<std::uint8_t>(dress), static_cast<std::uint8_t>(weapon), 2);
  actor.current_action = mir2::client_v1::ActorActionKind::turn;
  return actor;
}

mir2::client::ActorState make_monster(std::uint64_t id, int x, int y, int appearance = 11,
                                      int race = 10) {
  mir2::client::ActorState actor;
  actor.actor_id = id;
  actor.name = "monster";
  actor.x = x;
  actor.y = y;
  actor.from_x = x;
  actor.from_y = y;
  actor.dir = 0;
  actor.actor_type = mir2::client_v1::ActorType::monster;
  actor.feature = static_cast<std::int32_t>((appearance << 16) | race);
  actor.current_action = mir2::client_v1::ActorActionKind::turn;
  return actor;
}

void update_all(mir2::client::WorldViewState& world, mir2::client::AnimationManager& animation,
                mir2::client::LegacyAudioCueTracker& tracker, mir2::client::AudioService& audio,
                std::uint64_t now_ms) {
  animation.update(world, now_ms);
  tracker.update(world, animation, nullptr, audio, now_ms);
  audio.flush_queued_sounds(now_ms);
}

}  // namespace

int main() {
  using namespace mir2::client;

  AudioService audio(std::make_unique<NullAudioBackend>());
  assert(audio.initialize(asset_root()));

  WorldViewState world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;
  world.actors[1] = make_human(1, 10, 10, 4);

  AnimationManager animation;
  LegacyAudioCueTracker tracker;
  animation.reset(1000);
  update_all(world, animation, tracker, audio, 1000);

  auto& walk_actor = world.actors[1];
  walk_actor.current_action = mir2::client_v1::ActorActionKind::walk;
  walk_actor.running = false;
  walk_actor.from_x = 10;
  walk_actor.from_y = 10;
  walk_actor.x = 11;
  walk_actor.y = 10;
  walk_actor.move_started_ms = 1100;
  walk_actor.move_duration_ms = 500;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 1100);
  update_all(world, animation, tracker, audio, 1201);
  update_all(world, animation, tracker, audio, 1302);
  update_all(world, animation, tracker, audio, 1403);
  update_all(world, animation, tracker, audio, 1504);
  assert(has_sound_id(audio, s_walk_ground_l));
  assert(has_sound_id(audio, s_walk_ground_r));

  world.actors[2] = make_human(2, 12, 10, 4);
  update_all(world, animation, tracker, audio, 1600);
  auto& other_walk_actor = world.actors[2];
  other_walk_actor.current_action = mir2::client_v1::ActorActionKind::walk;
  other_walk_actor.running = false;
  other_walk_actor.from_x = 12;
  other_walk_actor.from_y = 10;
  other_walk_actor.x = 13;
  other_walk_actor.y = 10;
  other_walk_actor.move_started_ms = 1700;
  other_walk_actor.move_duration_ms = 500;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 1700);
  update_all(world, animation, tracker, audio, 1801);
  update_all(world, animation, tracker, audio, 1902);
  update_all(world, animation, tracker, audio, 2003);
  update_all(world, animation, tracker, audio, 2104);
  assert(count_sound_id(audio, s_walk_ground_l) == 0);
  assert(count_sound_id(audio, s_walk_ground_r) == 0);

  tracker.reset();
  animation.reset(2200);
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::turn;
  world.actors[1].move_started_ms = 0;
  world.actors[2].current_action = mir2::client_v1::ActorActionKind::turn;
  world.actors[2].move_started_ms = 0;
  update_all(world, animation, tracker, audio, 2200);
  auto& hit_actor = world.actors[1];
  hit_actor.current_action = mir2::client_v1::ActorActionKind::hit;
  hit_actor.action_started_ms = 2300;
  hit_actor.action_duration_ms = 700;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 2300);
  update_all(world, animation, tracker, audio, 2390);
  update_all(world, animation, tracker, audio, 2480);
  assert(has_sound_id(audio, s_hit_sword));

  hit_actor.legacy_action_ident = mir2::legacy::kSmPowerHit;
  hit_actor.action_started_ms = 2600;
  audio.clear_trace_events();
  for (std::uint64_t now = 2600; now <= 3300; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(has_sound_id(audio, s_yedo_man));

  hit_actor.legacy_action_ident = mir2::legacy::kSmLongHit;
  hit_actor.action_started_ms = 3500;
  audio.clear_trace_events();
  for (std::uint64_t now = 3500; now <= 4200; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(has_sound_id(audio, s_longhit));

  hit_actor.legacy_action_ident = mir2::legacy::kSmWideHit;
  hit_actor.action_started_ms = 4400;
  audio.clear_trace_events();
  for (std::uint64_t now = 4400; now <= 5100; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(has_sound_id(audio, s_widehit));

  hit_actor.legacy_action_ident = mir2::legacy::kSmFireHit;
  hit_actor.action_started_ms = 5300;
  audio.clear_trace_events();
  for (std::uint64_t now = 5300; now <= 6000; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(has_sound_id(audio, s_firehit));

  hit_actor.legacy_action_ident = mir2::legacy::kSmCrossHit;
  hit_actor.action_started_ms = 6200;
  audio.clear_trace_events();
  for (std::uint64_t now = 6200; now <= 6900; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(!has_sound_id(audio, s_yedo_man));
  assert(!has_sound_id(audio, s_longhit));
  assert(!has_sound_id(audio, s_widehit));
  assert(!has_sound_id(audio, s_firehit));

  hit_actor.legacy_action_ident = mir2::legacy::kSmHeavyHit;
  hit_actor.action_started_ms = 7100;
  audio.clear_trace_events();
  for (std::uint64_t now = 7100; now <= 7800; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(!has_sound_id(audio, s_yedo_man));
  assert(!has_sound_id(audio, s_widehit));

  hit_actor.legacy_action_ident = mir2::legacy::kSmBigHit;
  hit_actor.action_started_ms = 8000;
  audio.clear_trace_events();
  for (std::uint64_t now = 8000; now <= 8700; now += 90) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, s_hit_sword));
  assert(!has_sound_id(audio, s_yedo_man));
  assert(!has_sound_id(audio, s_widehit));
  const auto big_hit_sword_count = count_sound_id(audio, s_hit_sword);
  update_all(world, animation, tracker, audio, 8790);
  assert(count_sound_id(audio, s_hit_sword) == big_hit_sword_count);

  world.actors[2] = make_human(2, 10, 9, 6);
  auto& struck_actor = world.actors[1];
  struck_actor.current_action = mir2::client_v1::ActorActionKind::struck;
  struck_actor.action_started_ms = 8900;
  struck_actor.last_hitter_id = 2;
  struck_actor.last_damage_magic = false;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 8900);
  assert(has_sound_id(audio, s_struck_axe));
  assert(has_sound_id(audio, s_struck_body_axe));
  assert(has_sound_id(audio, s_man_struck));

  auto& dead_actor = world.actors[1];
  dead_actor.dead = true;
  dead_actor.action_started_ms = 9900;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 9900);
  assert(has_sound_id(audio, s_man_die));

  tracker.reset();
  animation.reset(5000);
  world.actors.clear();
  world.actors[1] = make_human(1, 10, 10, 4);
  world.self_actor_id = 1;
  update_all(world, animation, tracker, audio, 5000);
  auto& caster = world.actors[1];
  caster.current_action = mir2::client_v1::ActorActionKind::spell;
  caster.magic_id = 1;
  caster.action_started_ms = 5100;
  caster.action_duration_ms = 500;
  caster.action_target_x = 12;
  caster.action_target_y = 10;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 5100);
  assert(has_sound_id(audio, magic_sound_id(1, LegacyMagicSoundPhase::start)));
  assert(!has_sound_id(audio, magic_sound_id(1, LegacyMagicSoundPhase::fire)));

  audio.clear_trace_events();
  for (std::uint64_t now = 5200; now <= 6200; now += 100) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, magic_sound_id(1, LegacyMagicSoundPhase::fire)));
  assert(has_sound_id(audio, magic_sound_id(1, LegacyMagicSoundPhase::explosion)));

  audio.set_sound_enabled(false);
  caster.current_action = mir2::client_v1::ActorActionKind::hit;
  caster.action_started_ms = 7000;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 7000);
  update_all(world, animation, tracker, audio, 7090);
  update_all(world, animation, tracker, audio, 7180);
  assert(has_reason(audio, "sound_disabled"));
  audio.set_sound_enabled(true);

  tracker.reset();
  animation.reset(8000);
  world.actors.clear();
  world.self_actor_id = 1;
  world.actors[1] = make_human(1, 10, 10, 4);
  world.actors[3] = make_monster(3, 12, 12, 11);
  update_all(world, animation, tracker, audio, 8000);
  assert(!tracker.next_monster_normal_sound_hit());
  auto& walking_monster = world.actors[3];
  walking_monster.current_action = mir2::client_v1::ActorActionKind::walk;
  walking_monster.from_x = 12;
  walking_monster.from_y = 12;
  walking_monster.x = 13;
  walking_monster.y = 12;
  walking_monster.move_started_ms = 8100;
  walking_monster.move_duration_ms = 500;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 8100);
  update_all(world, animation, tracker, audio, 8201);
  update_all(world, animation, tracker, audio, 8302);
  assert(has_sound_id(audio, monster_sound_id(11, monster_offset_normal)));

  tracker.reset();
  animation.reset(9000);
  world.actors.clear();
  world.actors[4] = make_monster(4, 10, 10, 80);
  update_all(world, animation, tracker, audio, 9000);
  auto& dying_monster = world.actors[4];
  dying_monster.dead = true;
  dying_monster.action_started_ms = 9100;
  audio.clear_trace_events();
  update_all(world, animation, tracker, audio, 9100);
  for (std::uint64_t now = 9200; now <= 10000; now += 100) {
    update_all(world, animation, tracker, audio, now);
  }
  assert(has_sound_id(audio, monster_sound_id(80, monster_offset_die)));
  assert(has_sound_id(audio, monster_sound_id(80, monster_offset_die2)));
  const auto die2_count = count_sound_id(audio, monster_sound_id(80, monster_offset_die2));
  update_all(world, animation, tracker, audio, 10100);
  assert(count_sound_id(audio, monster_sound_id(80, monster_offset_die2)) == die2_count);

  std::cout << "legacy_audio_cue_tracker_smoke ok\n";
  return 0;
}
