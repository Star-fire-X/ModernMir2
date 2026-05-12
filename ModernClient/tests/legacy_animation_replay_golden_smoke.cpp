#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "animation/legacy_animation.hpp"
#include "shared/legacy/action_ids.hpp"

namespace {

using mir2::client::ActorRenderPose;
using mir2::client::ActorState;
using mir2::client::AnimationManager;
using mir2::client::ArchiveId;
using mir2::client::LegacyEffectManager;
using mir2::client::LegacyMagicType;
using mir2::client::WorldViewState;
using mir2::client_v1::ActorActionKind;
using mir2::client_v1::ActorType;

constexpr ArchiveId archive(const int value) {
  return static_cast<ArchiveId>(value);
}

constexpr LegacyMagicType magic_type(const int value) {
  return static_cast<LegacyMagicType>(value);
}

struct PoseGolden {
  std::uint64_t tick;
  ArchiveId archive;
  int body_index;
  int current_frame;
  int rx;
  int ry;
  int shift_x;
  int shift_y;
  int dir;
  bool visible;
  bool dead;
  int overlay_count;
  ArchiveId overlay0_archive;
  int overlay0_frame;
};

struct EffectGolden {
  std::uint64_t tick;
  int ground_count;
  int fly_count;
  ArchiveId archive;
  int effect_base;
  int explosion_base;
  int current_frame;
  int draw_frame;
  LegacyMagicType magic_type;
  int dir16;
  std::uint64_t owner_actor_id;
  int target_x;
  int target_y;
  bool fixed_effect;
};

std::int32_t monster_feature(const int race, const int appearance = 0) {
  return static_cast<std::int32_t>(race | (appearance << 16));
}

WorldViewState monster_action_world(const int race, const std::uint16_t ident,
                                    const ActorActionKind kind,
                                    const std::uint64_t start_ms,
                                    const int appearance = 0,
                                    const std::uint8_t dir = 2) {
  WorldViewState world;
  world.width = 80;
  world.height = 80;
  ActorState actor;
  actor.actor_id = 400 + race + static_cast<std::uint64_t>(appearance);
  actor.actor_type = ActorType::monster;
  actor.x = 30;
  actor.y = 30;
  actor.from_x = 30;
  actor.from_y = 30;
  actor.dir = dir;
  actor.feature = monster_feature(race, appearance);
  actor.current_action = kind;
  actor.legacy_action_ident = ident;
  actor.action_started_ms = start_ms;
  actor.action_target_x = 34;
  actor.action_target_y = 30;
  world.actors[actor.actor_id] = actor;
  return world;
}

WorldViewState human_spell_world(const std::uint64_t start_ms, const std::uint16_t magic_id,
                                 const int effect, const int effect_type,
                                 const int target_x = 14, const int target_y = 10) {
  WorldViewState world;
  world.width = 80;
  world.height = 80;
  world.self_actor_id = 1;
  ActorState actor;
  actor.actor_id = 1;
  actor.actor_type = ActorType::player;
  actor.x = 10;
  actor.y = 10;
  actor.from_x = 10;
  actor.from_y = 10;
  actor.dir = 2;
  actor.feature = mir2::client::make_legacy_feature(0, 1, 2, 3);
  actor.current_action = ActorActionKind::spell;
  actor.magic_id = magic_id;
  actor.action_started_ms = start_ms;
  actor.action_target_x = target_x;
  actor.action_target_y = target_y;
  actor.action_magic = true;
  actor.action_magic_effect = effect;
  actor.action_magic_effect_type = effect_type;
  world.actors[1] = actor;
  return world;
}

PoseGolden sample_pose(const std::uint64_t tick, const ActorRenderPose& pose) {
  PoseGolden out{};
  out.tick = tick;
  out.archive = pose.body_archive;
  out.body_index = pose.body_index;
  out.current_frame = pose.current_frame;
  out.rx = pose.rx;
  out.ry = pose.ry;
  out.shift_x = pose.shift_x;
  out.shift_y = pose.shift_y;
  out.dir = pose.dir;
  out.visible = pose.visible;
  out.dead = pose.dead;
  out.overlay_count = pose.overlay_count;
  out.overlay0_archive = pose.overlay_count > 0 ? pose.overlays[0].archive : ArchiveId::effect;
  out.overlay0_frame = pose.overlay_count > 0 ? pose.overlays[0].frame_index : -1;
  return out;
}

EffectGolden sample_effects(const std::uint64_t tick, const LegacyEffectManager& effects) {
  EffectGolden out{};
  out.tick = tick;
  out.ground_count = static_cast<int>(effects.ground_count());
  out.fly_count = static_cast<int>(effects.fly_count());
  const auto* effect = effects.fly_count() > 0 ? &effects.fly_effects().front()
                                               : effects.ground_count() > 0
                                                     ? &effects.ground_effects().front()
                                                     : nullptr;
  if (effect == nullptr) {
    out.archive = ArchiveId::effect;
    out.effect_base = -1;
    out.explosion_base = -1;
    out.current_frame = -1;
    out.draw_frame = -1;
    out.magic_type = LegacyMagicType::fly;
    out.dir16 = -1;
    out.target_x = -1;
    out.target_y = -1;
    out.fixed_effect = false;
    return out;
  }
  out.archive = effect->archive;
  out.effect_base = effect->effect_base;
  out.explosion_base = effect->explosion_base;
  out.current_frame = effect->current_frame;
  out.draw_frame = effect->draw_frame_index();
  out.magic_type = effect->magic_type;
  out.dir16 = effect->dir16;
  out.owner_actor_id = effect->owner_actor_id;
  out.target_x = effect->target_x;
  out.target_y = effect->target_y;
  out.fixed_effect = effect->fixed_effect;
  return out;
}

std::vector<PoseGolden> run_pose_replay(WorldViewState world, const std::uint64_t actor_id,
                                        const std::vector<std::uint64_t>& ticks) {
  AnimationManager manager;
  manager.reset(ticks.front() - 100);
  std::vector<PoseGolden> samples;
  for (const auto tick : ticks) {
    manager.update(world, tick);
    const auto pose = manager.pose_for(actor_id);
    assert(pose.has_value());
    samples.push_back(sample_pose(tick, *pose));
  }
  return samples;
}

std::vector<EffectGolden> run_effect_replay(WorldViewState world,
                                            const std::vector<std::uint64_t>& ticks) {
  AnimationManager manager;
  manager.reset(ticks.front() - 100);
  std::vector<EffectGolden> samples;
  for (const auto tick : ticks) {
    manager.update(world, tick);
    samples.push_back(sample_effects(tick, manager.effects()));
  }
  return samples;
}

std::vector<EffectGolden> run_magic_matrix_replay(LegacyEffectManager::MagicCreate create,
                                                  const std::vector<std::uint64_t>& ticks) {
  LegacyEffectManager effects;
  effects.spawn_magic_effect(create);
  std::vector<EffectGolden> samples;
  for (const auto tick : ticks) {
    effects.update(tick);
    samples.push_back(sample_effects(tick, effects));
  }
  return samples;
}

bool same_pose(const PoseGolden& a, const PoseGolden& b) {
  return a.tick == b.tick && a.archive == b.archive && a.body_index == b.body_index &&
         a.current_frame == b.current_frame && a.rx == b.rx && a.ry == b.ry &&
         a.shift_x == b.shift_x && a.shift_y == b.shift_y && a.dir == b.dir &&
         a.visible == b.visible && a.dead == b.dead && a.overlay_count == b.overlay_count &&
         a.overlay0_archive == b.overlay0_archive && a.overlay0_frame == b.overlay0_frame;
}

bool same_effect(const EffectGolden& a, const EffectGolden& b) {
  return a.tick == b.tick && a.ground_count == b.ground_count && a.fly_count == b.fly_count &&
         a.archive == b.archive && a.effect_base == b.effect_base &&
         a.explosion_base == b.explosion_base && a.current_frame == b.current_frame &&
         a.draw_frame == b.draw_frame && a.magic_type == b.magic_type && a.dir16 == b.dir16 &&
         a.owner_actor_id == b.owner_actor_id && a.target_x == b.target_x &&
         a.target_y == b.target_y && a.fixed_effect == b.fixed_effect;
}

template <std::size_t N>
void expect_pose_sequence(const std::vector<PoseGolden>& actual,
                          const std::array<PoseGolden, N>& expected) {
  assert(actual.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    assert(same_pose(actual[i], expected[i]));
  }
}

template <std::size_t N>
void expect_effect_sequence(const std::vector<EffectGolden>& actual,
                            const std::array<EffectGolden, N>& expected) {
  assert(actual.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    assert(same_effect(actual[i], expected[i]));
  }
}

LegacyEffectManager::MagicCreate matrix_magic(const int effect_type, const int effect_number,
                                              const int magic_id) {
  LegacyEffectManager::MagicCreate create;
  create.magic_id = magic_id;
  create.server_magic_id = 200 + magic_id + effect_number + effect_type;
  create.effect_type = effect_type;
  create.effect = effect_number;
  create.source_x = 10;
  create.source_y = 10;
  create.target_x = 13;
  create.target_y = 10;
  create.target_actor_id = 77;
  create.now_ms = 8000;
  return create;
}

void test_special_monster_replay() {
  const std::vector<std::uint64_t> action_ticks{10000, 10121, 10242, 10363, 10484, 10605, 10726};
  const auto skeleton_dig = run_pose_replay(
      monster_action_world(14, mir2::legacy::kSmDigUp, ActorActionKind::hit, 10000), 414,
      action_ticks);
  const auto skeleton_alive = run_pose_replay(
      monster_action_world(14, mir2::legacy::kSmAlive, ActorActionKind::hit, 10000), 414,
      action_ticks);
  const auto skeleton_now_death = run_pose_replay(
      monster_action_world(14, mir2::legacy::kSmNowDeath, ActorActionKind::struck, 10000), 414,
      action_ticks);
  const auto castle_door = run_pose_replay(
      monster_action_world(99, mir2::legacy::kSmDigDown, ActorActionKind::hit, 10000), 499,
      action_ticks);
  const auto wall = run_pose_replay(
      monster_action_world(98, mir2::legacy::kSmDigUp, ActorActionKind::hit, 10000, 901),
      1399, action_ticks);
  const auto bee_queen = run_pose_replay(
      monster_action_world(43, mir2::legacy::kSmHit, ActorActionKind::hit, 10000, 0, 5),
      443, action_ticks);
  const auto electronic_scorpion = run_pose_replay(
      monster_action_world(60, mir2::legacy::kSmLighting, ActorActionKind::hit, 10000),
      460, action_ticks);
  const auto sculpture_king = run_pose_replay(
      monster_action_world(63, mir2::legacy::kSmLighting, ActorActionKind::hit, 10000),
      463, action_ticks);

  constexpr std::array<PoseGolden, 7> kSkeletonDig{{
      {10000, archive(25), 360, 360, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10121, archive(25), 361, 361, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10242, archive(25), 362, 362, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10363, archive(25), 363, 363, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10484, archive(25), 364, 364, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10605, archive(25), 365, 365, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10726, archive(25), 366, 366, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
  }};
  constexpr std::array<PoseGolden, 7> kSkeletonAlive = kSkeletonDig;
  constexpr std::array<PoseGolden, 7> kSkeletonNowDeath{{
      {10000, archive(25), 280, 280, 30, 30, 0, 0, 2, true, false, 1, archive(25), 340},
      {10121, archive(25), 281, 281, 30, 30, 0, 0, 2, true, false, 1, archive(25), 341},
      {10242, archive(25), 282, 282, 30, 30, 0, 0, 2, true, false, 1, archive(25), 342},
      {10363, archive(25), 283, 283, 30, 30, 0, 0, 2, true, false, 1, archive(25), 343},
      {10484, archive(25), 284, 284, 30, 30, 0, 0, 2, true, false, 1, archive(25), 344},
      {10605, archive(25), 285, 285, 30, 30, 0, 0, 2, true, false, 1, archive(25), 345},
      {10726, archive(25), 286, 286, 30, 30, 0, 0, 2, true, false, 1, archive(25), 346},
  }};
  constexpr std::array<PoseGolden, 7> kCastleDoor{{
      {10000, archive(25), 64, 64, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10121, archive(25), 64, 64, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10242, archive(25), 64, 64, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10363, archive(25), 64, 64, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10484, archive(25), 64, 64, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10605, archive(25), 65, 65, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
      {10726, archive(25), 65, 65, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
  }};
  constexpr std::array<PoseGolden, 7> kWall{{
      {10000, archive(19), 168, 0, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10121, archive(19), 169, 1, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10242, archive(19), 170, 2, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10363, archive(19), 171, 3, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10484, archive(19), 172, 4, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10605, archive(19), 173, 5, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
      {10726, archive(19), 174, 6, 30, 30, 0, 0, 2, true, false, 2, archive(19), 178},
  }};
  constexpr std::array<PoseGolden, 7> kBeeQueen{{
      {10000, archive(25), 10, 10, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10121, archive(25), 11, 11, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10242, archive(25), 12, 12, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10363, archive(25), 13, 13, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10484, archive(25), 14, 14, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10605, archive(25), 15, 15, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
      {10726, archive(25), 15, 15, 30, 30, 0, 0, 5, true, false, 0, archive(19), -1},
  }};
  constexpr std::array<PoseGolden, 7> kElectronicScorpion{{
      {10000, archive(25), 360, 360, 30, 30, 0, 0, 2, true, false, 1, archive(25), 430},
      {10121, archive(25), 361, 361, 30, 30, 0, 0, 2, true, false, 1, archive(25), 431},
      {10242, archive(25), 362, 362, 30, 30, 0, 0, 2, true, false, 1, archive(25), 432},
      {10363, archive(25), 363, 363, 30, 30, 0, 0, 2, true, false, 1, archive(25), 433},
      {10484, archive(25), 364, 364, 30, 30, 0, 0, 2, true, false, 1, archive(25), 434},
      {10605, archive(25), 365, 365, 30, 30, 0, 0, 2, true, false, 1, archive(25), 435},
      {10726, archive(25), 365, 365, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
  }};
  constexpr std::array<PoseGolden, 7> kSculptureKing{{
      {10000, archive(25), 260, 260, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3220},
      {10121, archive(25), 261, 261, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3221},
      {10242, archive(25), 262, 262, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3222},
      {10363, archive(25), 263, 263, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3223},
      {10484, archive(25), 264, 264, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3224},
      {10605, archive(25), 265, 265, 30, 30, 0, 0, 2, true, false, 1, archive(29), 3225},
      {10726, archive(25), 265, 265, 30, 30, 0, 0, 2, true, false, 0, archive(19), -1},
  }};

  expect_pose_sequence(skeleton_dig, kSkeletonDig);
  expect_pose_sequence(skeleton_alive, kSkeletonAlive);
  expect_pose_sequence(skeleton_now_death, kSkeletonNowDeath);
  expect_pose_sequence(castle_door, kCastleDoor);
  expect_pose_sequence(wall, kWall);
  expect_pose_sequence(bee_queen, kBeeQueen);
  expect_pose_sequence(electronic_scorpion, kElectronicScorpion);
  expect_pose_sequence(sculpture_king, kSculptureKing);
}

void test_projectile_and_skill_replay() {
  const std::vector<std::uint64_t> projectile_ticks{21000, 21250, 21500, 21750, 22000};
  const auto dual_axe = run_effect_replay(
      monster_action_world(15, mir2::legacy::kSmFlyAxe, ActorActionKind::hit, 21000),
      projectile_ticks);
  const auto archer = run_effect_replay(
      monster_action_world(45, mir2::legacy::kSmFlyAxe, ActorActionKind::hit, 21000),
      projectile_ticks);
  const auto skeleton_king = run_effect_replay(
      monster_action_world(63, mir2::legacy::kSmFlyAxe, ActorActionKind::hit, 21000),
      projectile_ticks);

  const std::vector<std::uint64_t> spell_ticks{2000, 2061, 2122, 2183, 2244, 2305,
                                               2366, 2427, 2488, 2549, 2599, 3549};
  const auto fireball = run_effect_replay(human_spell_world(2000, 1, 1, 1), spell_ticks);
  const auto thunder = run_effect_replay(human_spell_world(2000, 11, 8, 7), spell_ticks);
  const auto fire_wall = run_effect_replay(human_spell_world(2000, 22, 20, 13, 12, 12),
                                           spell_ticks);

  const std::vector<std::uint64_t> matrix_ticks{8051, 8131, 8211, 8291};
  const auto explosion = run_magic_matrix_replay(matrix_magic(2, 3, 3), matrix_ticks);
  const auto explo_bujauk = run_magic_matrix_replay(matrix_magic(8, 10, 13), matrix_ticks);
  const auto fire_thunder = run_magic_matrix_replay(matrix_magic(14, 33, 33), matrix_ticks);

  constexpr std::array<EffectGolden, 5> kDualAxe{{
      {21000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21250, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21500, 0, 1, archive(25), 447, 617, 0, 487, magic_type(3), 4, 415, 34, 30, false},
      {21750, 0, 1, archive(25), 447, 617, 1, 488, magic_type(3), 4, 415, 34, 30, false},
      {22000, 0, 1, archive(25), 447, 617, 0, 617, magic_type(3), 4, 415, 34, 30, true},
  }};
  constexpr std::array<EffectGolden, 5> kArcher{{
      {21000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21250, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21500, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21750, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {22000, 0, 1, archive(19), 272, 442, 0, 276, magic_type(11), 4, 445, 34, 30, false},
  }};
  constexpr std::array<EffectGolden, 5> kSkeletonKing{{
      {21000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21250, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21500, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {21750, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {22000, 0, 1, archive(29), 3570, 3740, 0, 3610, magic_type(12), 4, 463, 34, 30, false},
  }};
  constexpr std::array<EffectGolden, 12> kFireball{{
      {2000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2061, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2122, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2183, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2244, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2305, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2366, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2427, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2488, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2549, 0, 1, archive(14), 0, 170, 0, 50, magic_type(1), 4, 1, 14, 10, false},
      {2599, 0, 1, archive(14), 0, 170, 0, 50, magic_type(1), 4, 1, 14, 10, false},
      {3549, 0, 1, archive(14), 0, 170, 0, 170, magic_type(1), 4, 1, 14, 10, true},
  }};
  constexpr std::array<EffectGolden, 12> kThunder{{
      {2000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2061, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2122, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2183, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2244, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2305, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2366, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2427, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2488, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2549, 0, 1, archive(15), 10, 10, 0, 10, magic_type(7), 4, 1, 14, 10, true},
      {2599, 0, 1, archive(15), 10, 10, 0, 10, magic_type(7), 4, 1, 14, 10, true},
      {3549, 0, 1, archive(15), 10, 10, 1, 11, magic_type(7), 4, 1, 14, 10, true},
  }};
  constexpr std::array<EffectGolden, 12> kFireWall{{
      {2000, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2061, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2122, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2183, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2244, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2305, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2366, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2427, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2488, 0, 0, archive(19), -1, -1, -1, -1, magic_type(1), -1, 0, -1, -1, false},
      {2549, 1, 0, archive(45), 1620, 3580, 0, 3580, magic_type(13), 6, 1, 12, 12, true},
      {2599, 1, 0, archive(45), 1620, 3580, 0, 3580, magic_type(13), 6, 1, 12, 12, true},
      {3549, 1, 0, archive(45), 1620, 3580, 1, 3581, magic_type(13), 6, 1, 12, 12, true},
  }};
  constexpr std::array<EffectGolden, 4> kExplosion{{
      {8051, 0, 1, archive(14), 400, 570, 0, 570, magic_type(2), 4, 0, 13, 10, true},
      {8131, 0, 1, archive(14), 400, 570, 1, 571, magic_type(2), 4, 0, 13, 10, true},
      {8211, 0, 1, archive(14), 400, 570, 1, 571, magic_type(2), 4, 0, 13, 10, true},
      {8291, 0, 1, archive(14), 400, 570, 2, 572, magic_type(2), 4, 0, 13, 10, true},
  }};
  constexpr std::array<EffectGolden, 4> kExploBujauk{{
      {8051, 0, 1, archive(14), 1160, 1360, 1, 1211, magic_type(8), 4, 0, 13, 10, false},
      {8131, 0, 1, archive(14), 1160, 1360, 2, 1212, magic_type(8), 4, 0, 13, 10, false},
      {8211, 0, 1, archive(14), 1160, 1360, 0, 1360, magic_type(8), 4, 0, 13, 10, true},
      {8291, 0, 1, archive(14), 1160, 1360, 1, 1361, magic_type(8), 4, 0, 13, 10, true},
  }};
  constexpr std::array<EffectGolden, 4> kFireThunder{{
      {8051, 0, 1, archive(15), 140, 140, 1, 141, magic_type(14), 4, 0, 13, 10, true},
      {8131, 0, 1, archive(15), 140, 140, 2, 142, magic_type(14), 4, 0, 13, 10, true},
      {8211, 0, 1, archive(15), 140, 140, 3, 143, magic_type(14), 4, 0, 13, 10, true},
      {8291, 0, 1, archive(15), 140, 140, 4, 144, magic_type(14), 4, 0, 13, 10, true},
  }};

  expect_effect_sequence(dual_axe, kDualAxe);
  expect_effect_sequence(archer, kArcher);
  expect_effect_sequence(skeleton_king, kSkeletonKing);
  expect_effect_sequence(fireball, kFireball);
  expect_effect_sequence(thunder, kThunder);
  expect_effect_sequence(fire_wall, kFireWall);
  expect_effect_sequence(explosion, kExplosion);
  expect_effect_sequence(explo_bujauk, kExploBujauk);
  expect_effect_sequence(fire_thunder, kFireThunder);
}

}  // namespace

int main() {
  test_special_monster_replay();
  test_projectile_and_skill_replay();
  return 0;
}
