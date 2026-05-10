#include <cassert>
#include <array>
#include <cstdint>
#include <filesystem>

#include "animation/legacy_animation.hpp"
#include "assets/asset_manager.hpp"
#include "shared/legacy/action_ids.hpp"

int main() {
  using namespace mir2::client;

  const auto feature = make_legacy_feature(0, 3, 5, 7);
  assert(legacy_race_feature(feature) == 0);
  assert(legacy_weapon_feature(feature) == 5);
  assert(legacy_hair_feature(feature) == 7);
  assert(legacy_dress_feature(feature) == 3);
  assert(legacy_appr_feature(feature) == static_cast<std::uint16_t>((3U << 8U) | 7U));

  const auto appearance = decode_legacy_human_feature(make_legacy_feature(0, 1, 2, 3));
  assert(appearance.body_offset == 600);
  assert(appearance.weapon_offset == 1200);
  assert(appearance.hair_offset == 4200);

  const auto& stand = legacy_human_action_info(LegacyHumanAction::stand);
  const auto& walk = legacy_human_action_info(LegacyHumanAction::walk);
  const auto& run = legacy_human_action_info(LegacyHumanAction::run);
  const auto& hit = legacy_human_action_info(LegacyHumanAction::hit);
  const auto& spell = legacy_human_action_info(LegacyHumanAction::spell);
  const auto& die = legacy_human_action_info(LegacyHumanAction::die);
  assert(legacy_frame_index(stand, 2, 0) == 16);
  assert(legacy_frame_index(walk, 2, 3) == 83);
  assert(legacy_frame_index(run, 4, 5) == 165);
  assert(legacy_frame_index(hit, 7, 5) == 261);
  assert(legacy_frame_index(spell, 1, 2) == 402);
  assert(legacy_frame_index(die, 3, 3) == 563);

  const auto* ma10 = legacy_monster_action_table(10, 0);
  const auto* ma14 = legacy_monster_action_table(14, 40);
  const auto* ma19 = legacy_monster_action_table(40, 120);
  assert(ma10 != nullptr && (*ma10)[static_cast<std::size_t>(LegacyMonsterAction::attack)].start == 128);
  assert(ma14 != nullptr && (*ma14)[static_cast<std::size_t>(LegacyMonsterAction::walk)].skip == 4);
  assert(ma19 != nullptr && (*ma19)[static_cast<std::size_t>(LegacyMonsterAction::die)].frame == 10);

  const auto right_start = legacy_shift(10, 10, 2, 1, 0, 6);
  assert(right_start.rx == 9 && right_start.ry == 10);
  assert(right_start.shift_x == 0 && right_start.shift_y == 0);
  const auto right_end = legacy_shift(10, 10, 2, 1, 6, 6);
  assert(right_end.rx == 10 && right_end.shift_x == 0);
  const auto run_start = legacy_shift(10, 10, 2, 2, 0, 6);
  assert(run_start.rx == 8 && run_start.shift_x == 0);
  const auto down_mid = legacy_shift(10, 10, 4, 1, 3, 6);
  assert(down_mid.ry == 10 && down_mid.shift_y == -16);

  assert(legacy_fly_direction16(0, 0, 0, -10) == 0);
  assert(legacy_fly_direction16(0, 0, 10, 0) == 4);
  assert(legacy_fly_direction16(0, 0, 0, 10) == 8);
  assert(legacy_fly_direction16(0, 0, -10, 0) == 12);
  assert(legacy_fly_direction16(0, 0, 10, -10) == 2);

  const auto fire_magic = legacy_magic_effect_base(8, 0);
  assert(fire_magic.archive == ArchiveId::magic2 && fire_magic.frame_base == 20);
  const auto mon_magic = legacy_magic_effect_base(31, 0);
  assert(mon_magic.archive == ArchiveId::mon21);
  const auto hit_magic = legacy_magic_effect_base(5, 1);
  assert(hit_magic.archive == ArchiveId::magic2 && hit_magic.frame_base == 40);
  constexpr std::array<int, 36> expected_effect_base{
      0,    200, 400, 600, 0,    900, 920, 940, 20,   940, 940, 940,
      0,    1380, 1500, 1520, 940, 1560, 1590, 1620, 1650, 1680, 0, 0,
      0,    3960, 1790, 0,    3880, 3920, 3840, 0,    40,   130, 160, 190};
  for (std::size_t index = 0; index < expected_effect_base.size(); ++index) {
    const auto base = legacy_magic_effect_base(static_cast<int>(index), 0);
    assert(base.frame_base == expected_effect_base[index]);
    if (index == 8 || index == 27 || index == 33 || index == 34 || index == 35) {
      assert(base.archive == ArchiveId::magic2);
    } else if (index == 31) {
      assert(base.archive == ArchiveId::mon21);
    } else {
      assert(base.archive == ArchiveId::magic);
    }
  }
  constexpr std::array<int, 6> expected_hit_effect_base{800, 1410, 1700, 3480, 3390, 40};
  for (std::size_t index = 0; index < expected_hit_effect_base.size(); ++index) {
    const auto base = legacy_magic_effect_base(static_cast<int>(index), 1);
    assert(base.frame_base == expected_hit_effect_base[index]);
    assert(base.archive == (index == 5 ? ArchiveId::magic2 : ArchiveId::magic));
  }

  MapCell cell;
  cell.fr_img = 100;
  cell.ani_frame = 3;
  cell.ani_tick = 1;
  assert(legacy_map_object_frame(cell, 0) == 99);
  assert(legacy_map_object_frame(cell, 1) == 99);
  assert(legacy_map_object_frame(cell, 2) == 100);
  assert(legacy_map_object_frame(cell, 5) == 101);
  cell.ani_frame = 0x83;
  assert(legacy_map_object_blend(cell));
  cell.ani_frame = 0;
  cell.door_index = 1;
  cell.door_offset = 0x82;
  assert(legacy_map_object_frame(cell, 0) == 101);

  LegacyAnimationClock clock;
  clock.reset(1000);
  clock.advance(1049);
  assert(!clock.ani_tick() && clock.main_ani_count() == 0);
  clock.advance(1050);
  assert(clock.ani_tick() && clock.main_ani_count() == 1);
  clock.advance(1100);
  assert(clock.move_tick() && clock.move_step_count() == 1);
  assert(clock.ani_tick() && clock.main_ani_count() == 2);

  WorldViewState world;
  world.self_actor_id = 1;
  ActorState actor;
  actor.actor_id = 1;
  actor.x = 10;
  actor.y = 10;
  actor.from_x = 10;
  actor.from_y = 10;
  actor.dir = 2;
  actor.feature = make_legacy_feature(0, 1, 2, 3);
  world.actors[1] = actor;

  AnimationManager animations;
  animations.reset(1000);
  animations.sync_world(world, 1000);
  auto pose = animations.pose_for(1);
  assert(pose.has_value());
  assert(pose->body_index == 600 + 16);
  assert(pose->hair_index == 4200 + 16);
  assert(pose->weapon_index == 1200 + 16);
  assert(pose->down_draw_level == 0);

  WorldViewState war_world;
  war_world.self_actor_id = 1;
  war_world.actors[1] = actor;
  AnimationManager war_animations;
  war_animations.reset(5000);
  war_animations.sync_world(war_world, 5000);
  war_world.actors[1].current_action = mir2::client_v1::ActorActionKind::hit;
  war_world.actors[1].action_started_ms = 5000;
  war_animations.update(war_world, 5000);
  war_animations.update(war_world, 5086);
  war_animations.update(war_world, 5172);
  war_animations.update(war_world, 5258);
  war_animations.update(war_world, 5344);
  war_animations.update(war_world, 5430);
  war_animations.update(war_world, 5516);
  war_animations.update(war_world, 5720);
  auto war_pose = war_animations.pose_for(1);
  assert(war_pose.has_value());
  assert(war_pose->body_index == 600 + 194);

  auto hit_body_index_for = [&](const std::uint16_t legacy_ident) {
    WorldViewState hit_world;
    hit_world.self_actor_id = 1;
    auto hit_actor = actor;
    hit_actor.current_action = mir2::client_v1::ActorActionKind::turn;
    hit_actor.legacy_action_ident = 0;
    hit_actor.action_started_ms = 0;
    hit_world.actors[1] = hit_actor;

    AnimationManager hit_animations;
    hit_animations.reset(8000);
    hit_animations.sync_world(hit_world, 8000);

    hit_actor.current_action = mir2::client_v1::ActorActionKind::hit;
    hit_actor.legacy_action_ident = legacy_ident;
    hit_actor.action_started_ms = 8100 + legacy_ident;
    hit_world.actors[1] = hit_actor;
    hit_animations.update(hit_world, hit_actor.action_started_ms);
    const auto hit_pose = hit_animations.pose_for(1);
    assert(hit_pose.has_value());
    return hit_pose->body_index - appearance.body_offset;
  };

  assert(hit_body_index_for(mir2::legacy::kSmHeavyHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::heavy_hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmBigHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::big_hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmFireHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmPowerHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmLongHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmWideHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  assert(hit_body_index_for(mir2::legacy::kSmCrossHit) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  assert(hit_body_index_for(25) ==
         legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));

  WorldViewState monster_world;
  monster_world.self_actor_id = 10;
  ActorState monster;
  monster.actor_id = 10;
  monster.actor_type = mir2::client_v1::ActorType::monster;
  monster.x = 20;
  monster.y = 20;
  monster.from_x = 20;
  monster.from_y = 20;
  monster.dir = 2;
  monster.feature = 10;
  monster.dead = true;
  monster_world.actors[10] = monster;
  AnimationManager monster_animations;
  monster_animations.reset(6000);
  monster_animations.sync_world(monster_world, 6000);
  auto monster_pose = monster_animations.pose_for(10);
  assert(monster_pose.has_value());
  assert(monster_pose->body_index == 227);
  monster_world.actors[10].skeleton = true;
  monster_animations.reset(7000);
  monster_animations.sync_world(monster_world, 7000);
  monster_pose = monster_animations.pose_for(10);
  assert(monster_pose.has_value());
  assert(monster_pose->body_index == 272);

  world.actors[1].from_x = 10;
  world.actors[1].from_y = 10;
  world.actors[1].x = 11;
  world.actors[1].y = 10;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::walk;
  world.actors[1].move_started_ms = 1100;
  animations.update(world, 1100);
  pose = animations.pose_for(1);
  assert(pose.has_value());
  assert(pose->body_index == 600 + 80);
  assert(pose->rx == 10 && pose->shift_x == 8);

  world.actors[1].current_action = mir2::client_v1::ActorActionKind::spell;
  world.actors[1].magic_id = 1;
  world.actors[1].action_started_ms = 2000;
  world.actors[1].action_target_x = 14;
  world.actors[1].action_target_y = 10;
  world.actors[1].action_target_actor_id = 0;
  world.actors[1].action_magic = true;
  animations.update(world, 2000);
  assert(animations.effects().fly_count() == 1);
  assert(animations.effects().fly_effects().front().owner_actor_id == 1);
  assert(animations.effects().fly_effects().front().target_x == 14);
  animations.update(world, 2050);
  assert(animations.effects().fly_count() == 1);

  animations.reset(3000);
  world.actors[1].x = 10;
  world.actors[1].y = 10;
  world.actors[1].from_x = 10;
  world.actors[1].from_y = 10;
  world.actors[1].magic_id = 3;
  world.actors[1].action_started_ms = 3000;
  world.actors[1].action_target_x = 12;
  world.actors[1].action_target_y = 12;
  animations.update(world, 3000);
  assert(animations.effects().ground_count() == 1);
  assert(animations.effects().ground_effects().front().effect_base == 600);

  ActorState target;
  target.actor_id = 2;
  target.x = 11;
  target.y = 10;
  target.from_x = 11;
  target.from_y = 10;
  target.dir = 6;
  target.feature = make_legacy_feature(0, 1, 0, 3);
  world.actors[2] = target;
  animations.reset(4000);
  world.actors[1].magic_id = 5;
  world.actors[1].action_started_ms = 4000;
  world.actors[1].action_target_actor_id = 2;
  world.actors[1].action_target_x = 11;
  world.actors[1].action_target_y = 10;
  animations.update(world, 4000);
  assert(animations.effects().overlay_count() == 1);

  animations.reset(4500);
  world.actors[1].magic_id = 5;
  world.actors[1].action_started_ms = 4500;
  world.actors[1].action_magic_effect_type = 1;
  world.actors[1].action_magic_effect = 3;
  world.actors[1].action_target_actor_id = 2;
  world.actors[1].action_target_x = 11;
  world.actors[1].action_target_y = 10;
  animations.update(world, 4500);
  assert(animations.effects().fly_count() == 1);
  assert(animations.effects().fly_effects().front().effect_base == 400);
  assert(animations.effects().overlay_count() == 0);

  AssetManager assets;
  const auto root = std::filesystem::temp_directory_path() / "mir2_missing_legacy_archives";
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");
  assert(assets.initialize(root));
  assert(assets.get_frame(ArchiveId::mon21, 0) == nullptr);
  assert(assets.get_frame(ArchiveId::magic3, 0) == nullptr);

  LegacyEffectManager effects;
  effects.spawn_map_effect(ArchiveId::effect, 100, 3, 10, 11, 1000, 30);
  assert(effects.ground_count() == 1);
  assert(effects.ground_effects().front().draw_frame_index() == 100);
  effects.update(1030);
  assert(effects.ground_count() == 1);
  assert(effects.ground_effects().front().current_frame == 0);
  effects.update(1031);
  assert(effects.ground_count() == 1);
  assert(effects.ground_effects().front().draw_frame_index() == 101);
  effects.update(1062);
  assert(effects.ground_count() == 1);
  assert(effects.ground_effects().front().draw_frame_index() == 102);
  effects.update(1093);
  assert(effects.ground_count() == 0);

  effects.spawn_map_effect(ArchiveId::effect, 200, 2, 10, 11, 2000, 30, 1);
  effects.update(2031);
  assert(effects.ground_count() == 1 && effects.ground_effects().front().current_frame == 1);
  effects.update(2062);
  assert(effects.ground_count() == 1 && effects.ground_effects().front().current_frame == 0);
  effects.update(2093);
  assert(effects.ground_count() == 1 && effects.ground_effects().front().current_frame == 1);
  effects.update(2124);
  assert(effects.ground_count() == 0);

  effects.spawn_char_effect(7, ArchiveId::effect, 300, 2, 3000, 30);
  assert(effects.overlay_count() == 1);
  effects.update(3031);
  assert(effects.overlay_count() == 1);
  effects.update(3062);
  assert(effects.overlay_count() == 0);

  LegacyEffectManager::MagicCreate magic;
  magic.magic_id = 1;
  magic.server_magic_id = 99;
  magic.effect_type = 1;
  magic.effect = 1;
  magic.source_x = 10;
  magic.source_y = 10;
  magic.target_x = 13;
  magic.target_y = 10;
  magic.now_ms = 4000;
  auto& fly = effects.spawn_magic_effect(magic);
  assert(effects.fly_count() == 1);
  assert(!fly.fixed_effect);
  assert(fly.dir16 == 4);
  assert(fly.draw_frame_index() == 50);
  effects.update(4051);
  assert(effects.fly_count() == 1);
  effects.del_magic(99);
  assert(effects.fly_count() == 0);

  magic.server_magic_id = 100;
  magic.now_ms = 5000;
  effects.spawn_magic_effect(magic);
  effects.update(15001);
  assert(effects.fly_count() == 0);

  return 0;
}
