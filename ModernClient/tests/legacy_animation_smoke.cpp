#include <cassert>
#include <array>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

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
  const auto& struck = legacy_human_action_info(LegacyHumanAction::struck);
  const auto& die = legacy_human_action_info(LegacyHumanAction::die);
  assert(legacy_frame_index(stand, 2, 0) == 16);
  assert(legacy_frame_index(walk, 2, 3) == 83);
  assert(legacy_frame_index(run, 4, 5) == 165);
  assert(legacy_frame_index(hit, 7, 5) == 261);
  assert(legacy_frame_index(spell, 1, 2) == 402);
  assert(legacy_frame_index(struck, 5, 1) == 513);
  assert(legacy_frame_index(die, 3, 3) == 563);
  assert(mir2::legacy::kSmDigUp == 20);
  assert(mir2::legacy::kSmDigDown == 21);
  assert(mir2::legacy::kSmFlyAxe == 22);
  assert(mir2::legacy::kSmLighting == 23);
  assert(mir2::legacy::kSmAlive == 27);
  assert(mir2::legacy::kSmSkeleton == 33);
  assert(mir2::legacy::kSmNowDeath == 34);

  const auto* ma10 = legacy_monster_action_table(10, 0);
  const auto* ma14 = legacy_monster_action_table(14, 40);
  const auto* ma19 = legacy_monster_action_table(40, 120);
  const auto* ma21 = legacy_monster_action_table(43, 0);
  const auto* ma50 = legacy_monster_action_table(50, 0);
  const auto* ma51 = legacy_monster_action_table(50, 23);
  const auto* ma52 = legacy_monster_action_table(50, 24);
  assert(ma10 != nullptr && (*ma10)[static_cast<std::size_t>(LegacyMonsterAction::attack)].start == 128);
  assert(ma14 != nullptr && (*ma14)[static_cast<std::size_t>(LegacyMonsterAction::walk)].skip == 4);
  assert(ma19 != nullptr && (*ma19)[static_cast<std::size_t>(LegacyMonsterAction::die)].frame == 10);
  assert(ma21 != nullptr && (*ma21)[static_cast<std::size_t>(LegacyMonsterAction::walk)].frame == 0);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::attack)].start == 10);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame == 6);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame_time_ms == 120);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::struck)].start == 20);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::die)].start == 30);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::die)].frame == 10);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::die)].frame_time_ms == 160);
  assert((*ma21)[static_cast<std::size_t>(LegacyMonsterAction::death)].frame == 0);
  assert(ma50 != nullptr && (*ma50)[static_cast<std::size_t>(LegacyMonsterAction::stand)].skip == 6);
  assert((*ma50)[static_cast<std::size_t>(LegacyMonsterAction::walk)].frame == 0);
  assert((*ma50)[static_cast<std::size_t>(LegacyMonsterAction::attack)].start == 30);
  assert((*ma50)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame == 10);
  assert((*ma50)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame_time_ms == 150);
  assert(ma51 != nullptr && (*ma51)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame == 20);
  assert(ma52 != nullptr && (*ma52)[static_cast<std::size_t>(LegacyMonsterAction::stand)].start == 30);
  assert((*ma52)[static_cast<std::size_t>(LegacyMonsterAction::attack)].frame == 4);
  assert((*ma52)[static_cast<std::size_t>(LegacyMonsterAction::attack)].skip == 6);

  struct ExpectedProfile {
    int race;
    LegacySpecialActorProfile profile;
  };
  constexpr std::array<ExpectedProfile, 50> expected_profiles{{
      {0, LegacySpecialActorProfile::human_actor},
      {9, LegacySpecialActorProfile::soccer_ball},
      {13, LegacySpecialActorProfile::killing_herb},
      {14, LegacySpecialActorProfile::skeleton_oma},
      {15, LegacySpecialActorProfile::dual_axe_oma},
      {16, LegacySpecialActorProfile::gas_ku_de_gi},
      {17, LegacySpecialActorProfile::cat_mon},
      {18, LegacySpecialActorProfile::hu_su_abi},
      {19, LegacySpecialActorProfile::cat_mon},
      {20, LegacySpecialActorProfile::fire_cow_face_mon},
      {21, LegacySpecialActorProfile::cow_face_king},
      {22, LegacySpecialActorProfile::dual_axe_oma},
      {23, LegacySpecialActorProfile::white_skeleton},
      {24, LegacySpecialActorProfile::superior_guard},
      {30, LegacySpecialActorProfile::cat_mon},
      {31, LegacySpecialActorProfile::cat_mon},
      {32, LegacySpecialActorProfile::scorpion_mon},
      {33, LegacySpecialActorProfile::centipede_king},
      {34, LegacySpecialActorProfile::big_heart},
      {35, LegacySpecialActorProfile::spider_house},
      {36, LegacySpecialActorProfile::explosion_spider},
      {37, LegacySpecialActorProfile::flying_spider},
      {40, LegacySpecialActorProfile::zombi_lighting},
      {41, LegacySpecialActorProfile::zombi_dig_out},
      {42, LegacySpecialActorProfile::zombi_zilkin},
      {43, LegacySpecialActorProfile::bee_queen},
      {45, LegacySpecialActorProfile::archer_mon},
      {47, LegacySpecialActorProfile::sculpture_mon},
      {48, LegacySpecialActorProfile::sculpture_mon},
      {49, LegacySpecialActorProfile::sculpture_king},
      {50, LegacySpecialActorProfile::npc_actor},
      {52, LegacySpecialActorProfile::gas_ku_de_gi},
      {53, LegacySpecialActorProfile::gas_ku_de_gi},
      {54, LegacySpecialActorProfile::small_elf_monster},
      {55, LegacySpecialActorProfile::warrior_elf_monster},
      {60, LegacySpecialActorProfile::electronic_scolpion},
      {61, LegacySpecialActorProfile::boss_pig},
      {62, LegacySpecialActorProfile::king_of_sculpure_king},
      {63, LegacySpecialActorProfile::skeleton_king},
      {64, LegacySpecialActorProfile::gas_ku_de_gi},
      {65, LegacySpecialActorProfile::samurai},
      {66, LegacySpecialActorProfile::skeleton_soldier},
      {67, LegacySpecialActorProfile::skeleton_soldier},
      {68, LegacySpecialActorProfile::skeleton_soldier},
      {69, LegacySpecialActorProfile::skeleton_archer},
      {70, LegacySpecialActorProfile::banya_guard},
      {71, LegacySpecialActorProfile::banya_guard},
      {72, LegacySpecialActorProfile::banya_guard},
      {98, LegacySpecialActorProfile::wall_structure},
      {99, LegacySpecialActorProfile::castle_door},
  }};
  for (const auto& expected : expected_profiles) {
    assert(legacy_special_actor_profile_for(expected.race, 0) == expected.profile);
  }
  assert(legacy_special_actor_profile_for(88, 0) == LegacySpecialActorProfile::base_actor);

  const auto skeleton_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::skeleton_oma);
  assert(skeleton_profile.supports_dig_up && skeleton_profile.supports_dig_down);
  assert(skeleton_profile.supports_alive && skeleton_profile.supports_skeleton);
  assert(skeleton_profile.supports_now_death && skeleton_profile.has_body_overlay);
  assert(skeleton_profile.death_effect_base == 340);
  const auto dual_axe_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::dual_axe_oma);
  assert(dual_axe_profile.has_projectile_trigger);
  assert(dual_axe_profile.projectile_base == 447);
  assert(dual_axe_profile.alternate_projectile_base == 2967);
  assert(dual_axe_profile.projectile_trigger_frame == 2);
  const auto archer_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::archer_mon);
  assert(archer_profile.has_projectile_trigger);
  assert(archer_profile.projectile_base == 2607);
  assert(archer_profile.alternate_projectile_base == 272);
  assert(archer_profile.projectile_trigger_frame == 4);
  const auto gas_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::gas_ku_de_gi);
  assert(gas_profile.supports_lighting && gas_profile.supports_skeleton);
  assert(!gas_profile.supports_dig_up);
  const auto hu_su_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::hu_su_abi);
  assert(hu_su_profile.has_death_effect && hu_su_profile.death_effect_base == 2860);
  const auto superior_guard_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::superior_guard);
  assert(superior_guard_profile.effect_base == 760);
  const auto banya_guard_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::banya_guard);
  assert(banya_guard_profile.has_projectile_trigger);
  assert(banya_guard_profile.effect_base == 3490);
  assert(banya_guard_profile.projectile_base == 3580);
  const auto door_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::castle_door);
  assert(door_profile.supports_dig_up && door_profile.supports_dig_down);
  assert(door_profile.has_structure_animation && door_profile.death_effect_base == 120);
  const auto wall_profile =
      legacy_special_actor_profile_info(LegacySpecialActorProfile::wall_structure);
  assert(wall_profile.supports_dig_up && !wall_profile.supports_dig_down);
  assert(wall_profile.has_structure_animation);
  assert(wall_profile.effect_base == 224);
  assert(wall_profile.alternate_effect_base == 240);

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
  assert(animations.is_actor_legacy_idle(1));
  assert(!animations.is_actor_legacy_idle(999));

  auto down_draw_level_for = [](ActorState sample_actor) {
    WorldViewState sample_world;
    sample_world.self_actor_id = sample_actor.actor_id;
    sample_world.actors[sample_actor.actor_id] = sample_actor;
    AnimationManager sample_animations;
    sample_animations.reset(16000);
    sample_animations.sync_world(sample_world, 16000);
    const auto sample_pose = sample_animations.pose_for(sample_actor.actor_id);
    assert(sample_pose.has_value());
    return sample_pose->down_draw_level;
  };

  ActorState castle_door_actor;
  castle_door_actor.actor_id = 2001;
  castle_door_actor.actor_type = mir2::client_v1::ActorType::monster;
  castle_door_actor.x = 30;
  castle_door_actor.y = 30;
  castle_door_actor.from_x = 30;
  castle_door_actor.from_y = 30;
  castle_door_actor.dir = 2;
  castle_door_actor.feature = make_legacy_feature(99, 0, 0, 0);
  assert(down_draw_level_for(castle_door_actor) == 1);
  castle_door_actor.dir = 3;
  assert(down_draw_level_for(castle_door_actor) == 2);
  castle_door_actor.dead = true;
  castle_door_actor.dir = 0;
  assert(down_draw_level_for(castle_door_actor) == 2);

  ActorState skeleton_actor;
  skeleton_actor.actor_id = 2002;
  skeleton_actor.actor_type = mir2::client_v1::ActorType::monster;
  skeleton_actor.x = 32;
  skeleton_actor.y = 32;
  skeleton_actor.from_x = 32;
  skeleton_actor.from_y = 32;
  skeleton_actor.feature = make_legacy_feature(14, 0, 0, 30);
  skeleton_actor.dead = true;
  assert(down_draw_level_for(skeleton_actor) == 1);
  skeleton_actor.feature = make_legacy_feature(14, 0, 0, 151);
  assert(down_draw_level_for(skeleton_actor) == 1);
  skeleton_actor.feature = make_legacy_feature(14, 0, 0, 35);
  assert(down_draw_level_for(skeleton_actor) == 0);

  auto idle_actor = actor;
  WorldViewState idle_world;
  idle_world.self_actor_id = 1;
  idle_world.actors[1] = idle_actor;
  AnimationManager idle_animations;
  idle_animations.reset(12000);
  idle_animations.sync_world(idle_world, 12000);
  assert(idle_animations.is_actor_legacy_idle(1));
  idle_actor.current_action = mir2::client_v1::ActorActionKind::hit;
  idle_actor.action_started_ms = 12100;
  idle_world.actors[1] = idle_actor;
  idle_animations.sync_world(idle_world, 12100);
  assert(!idle_animations.is_actor_legacy_idle(1));
  auto idle_queued_actor = idle_actor;
  idle_queued_actor.action_started_ms = 12200;
  idle_world.actors[1] = idle_queued_actor;
  idle_animations.sync_world(idle_world, 12200);
  assert(!idle_animations.is_actor_legacy_idle(1));
  for (std::uint64_t tick = 12300; tick <= 15000; tick += 100) {
    idle_animations.update(idle_world, tick);
  }
  assert(idle_animations.is_actor_legacy_idle(1));

  WorldViewState move_idle_world;
  move_idle_world.self_actor_id = 1;
  auto moving_actor = actor;
  moving_actor.from_x = 10;
  moving_actor.from_y = 10;
  moving_actor.x = 11;
  moving_actor.y = 10;
  moving_actor.current_action = mir2::client_v1::ActorActionKind::walk;
  moving_actor.move_started_ms = 14000;
  move_idle_world.actors[1] = moving_actor;
  AnimationManager move_idle_animations;
  move_idle_animations.reset(13900);
  move_idle_animations.sync_world(move_idle_world, 14000);
  assert(!move_idle_animations.is_actor_legacy_idle(1));

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

  WorldViewState struck_world;
  struck_world.self_actor_id = 1;
  auto struck_actor = actor;
  struck_world.actors[1] = struck_actor;
  AnimationManager struck_animations;
  struck_animations.reset(8900);
  struck_animations.sync_world(struck_world, 8900);
  struck_actor.current_action = mir2::client_v1::ActorActionKind::struck;
  struck_actor.action_started_ms = 9000;
  struck_world.actors[1] = struck_actor;
  struck_animations.sync_world(struck_world, 9000);
  struck_animations.update(struck_world, 9000);
  const auto struck_pose = struck_animations.pose_for(1);
  assert(struck_pose.has_value());
  assert(struck_pose->body_index ==
         appearance.body_offset +
             legacy_frame_index(legacy_human_action_info(LegacyHumanAction::struck), 2, 0));
  assert(struck_pose->overlay_count == 1);
  assert(struck_pose->overlays[0].archive == ArchiveId::magic);
  assert(struck_pose->overlays[0].frame_index == 800);

  WorldViewState death_mode_world;
  death_mode_world.self_actor_id = 1;
  auto death_mode_actor = actor;
  death_mode_world.actors[1] = death_mode_actor;
  AnimationManager death_mode_animations;
  death_mode_animations.reset(9000);
  death_mode_animations.update(death_mode_world, 9000);

  death_mode_actor.dead = true;
  death_mode_actor.legacy_death_mode = LegacyDeathMode::instant_corpse;
  death_mode_actor.legacy_action_ident = legacy_sm::kDeath;
  death_mode_actor.action_started_ms = 9100;
  death_mode_world.actors[1] = death_mode_actor;
  death_mode_animations.update(death_mode_world, 9100);
  auto death_pose = death_mode_animations.pose_for(1);
  assert(death_pose.has_value());
  assert(death_pose->dead);
  assert(death_mode_animations.is_actor_legacy_idle(1));
  const auto death_action = legacy_human_action_info(LegacyHumanAction::die);
  assert(death_pose->body_index ==
         appearance.body_offset +
             legacy_frame_index(death_action, 2, std::max(0, death_action.frame - 1)));

  death_mode_animations.reset(9200);
  death_mode_actor = actor;
  death_mode_world.actors[1] = death_mode_actor;
  death_mode_animations.update(death_mode_world, 9200);
  death_mode_actor.dead = true;
  death_mode_actor.legacy_death_mode = LegacyDeathMode::play_death_anim;
  death_mode_actor.legacy_action_ident = mir2::legacy::kSmNowDeath;
  death_mode_actor.action_started_ms = 9300;
  death_mode_world.actors[1] = death_mode_actor;
  death_mode_animations.update(death_mode_world, 9300);
  death_pose = death_mode_animations.pose_for(1);
  assert(death_pose.has_value());
  assert(death_pose->dead);
  assert(!death_mode_animations.is_actor_legacy_idle(1));
  assert(death_pose->body_index ==
         appearance.body_offset +
             legacy_frame_index(death_action, 2, 0));

  death_mode_actor.dead = false;
  death_mode_actor.hp = 10;
  death_mode_actor.legacy_action_ident = legacy_sm::kTurn;
  death_mode_world.actors[1] = death_mode_actor;
  death_mode_animations.update(death_mode_world, 9361);
  death_pose = death_mode_animations.pose_for(1);
  assert(death_pose.has_value());
  assert(!death_pose->dead);
  assert(death_mode_animations.is_actor_legacy_idle(1));

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

  auto monster_action_frame = [](const std::uint8_t race, const std::uint16_t ident,
                                 const mir2::client_v1::ActorActionKind kind,
                                 const std::uint8_t dir = 2) {
    WorldViewState action_world;
    ActorState action_actor;
    action_actor.actor_id = 90 + race;
    action_actor.actor_type = mir2::client_v1::ActorType::monster;
    action_actor.x = 30;
    action_actor.y = 30;
    action_actor.from_x = 30;
    action_actor.from_y = 30;
    action_actor.dir = dir;
    action_actor.feature = race;
    action_world.actors[action_actor.actor_id] = action_actor;

    AnimationManager action_animations;
    action_animations.reset(10000);
    action_animations.sync_world(action_world, 10000);
    action_actor.current_action = kind;
    action_actor.legacy_action_ident = ident;
    action_actor.action_started_ms = 10100 + ident + race;
    action_world.actors[action_actor.actor_id] = action_actor;
    action_animations.update(action_world, action_actor.action_started_ms);
    const auto action_pose = action_animations.pose_for(action_actor.actor_id);
    assert(action_pose.has_value());
    return action_pose->body_index - legacy_monster_offset(0);
  };

  auto monster_default_frame = [](const std::uint8_t race, const std::uint8_t dir,
                                  const bool dead, const bool skeleton = false) {
    WorldViewState default_world;
    ActorState default_actor;
    default_actor.actor_id = 300 + race;
    default_actor.actor_type = mir2::client_v1::ActorType::monster;
    default_actor.x = 31;
    default_actor.y = 30;
    default_actor.from_x = 31;
    default_actor.from_y = 30;
    default_actor.dir = dir;
    default_actor.feature = race;
    default_actor.dead = dead;
    default_actor.skeleton = skeleton;
    default_world.actors[default_actor.actor_id] = default_actor;

    AnimationManager default_animations;
    default_animations.reset(11000);
    default_animations.sync_world(default_world, 11000);
    const auto default_pose = default_animations.pose_for(default_actor.actor_id);
    assert(default_pose.has_value());
    return default_pose->body_index - legacy_monster_offset(0);
  };

  const auto* ma14_special = legacy_monster_action_table(14, 0);
  const auto ma14_death = (*ma14_special)[static_cast<std::size_t>(LegacyMonsterAction::death)];
  const auto ma14_die = (*ma14_special)[static_cast<std::size_t>(LegacyMonsterAction::die)];
  assert(monster_action_frame(14, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma14_death, 2, 0));
  assert(monster_action_frame(23, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit) == ma14_death.start);
  assert(monster_action_frame(14, mir2::legacy::kSmSkeleton,
                              mir2::client_v1::ActorActionKind::hit) == ma14_death.start);
  assert(monster_action_frame(14, mir2::legacy::kSmAlive,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma14_death, 2, 0));
  assert(monster_action_frame(14, mir2::legacy::kSmNowDeath,
                              mir2::client_v1::ActorActionKind::struck) ==
         legacy_frame_index(ma14_die, 2, 0));
  assert(monster_default_frame(14, 2, true, true) == ma14_death.start);

  const auto* ma15_special = legacy_monster_action_table(15, 0);
  const auto ma15_attack = (*ma15_special)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  assert(monster_action_frame(15, mir2::legacy::kSmFlyAxe,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma15_attack, 2, 0));

  const auto* ma16_special = legacy_monster_action_table(16, 0);
  const auto ma16_attack = (*ma16_special)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  assert(monster_action_frame(16, mir2::legacy::kSmLighting,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma16_attack, 2, 0));

  const auto* ma33_special = legacy_monster_action_table(60, 0);
  const auto ma33_critical = (*ma33_special)[static_cast<std::size_t>(LegacyMonsterAction::critical)];
  assert(monster_action_frame(60, mir2::legacy::kSmLighting,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma33_critical, 2, 0));
  assert(monster_action_frame(70, mir2::legacy::kSmLighting,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma33_critical, 2, 0));

  const auto* ma34_special = legacy_monster_action_table(63, 0);
  const auto ma34_critical = (*ma34_special)[static_cast<std::size_t>(LegacyMonsterAction::critical)];
  const auto ma34_attack = (*ma34_special)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  assert(monster_action_frame(63, mir2::legacy::kSmFlyAxe,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma34_critical, 2, 0));
  assert(monster_action_frame(63, mir2::legacy::kSmLighting,
                              mir2::client_v1::ActorActionKind::hit) ==
         legacy_frame_index(ma34_attack, 2, 0) + 80);

  const auto* ma13_special = legacy_monster_action_table(13, 0);
  const auto ma13_stand = (*ma13_special)[static_cast<std::size_t>(LegacyMonsterAction::stand)];
  const auto ma13_walk = (*ma13_special)[static_cast<std::size_t>(LegacyMonsterAction::walk)];
  assert(monster_default_frame(13, 5, false) == ma13_stand.start);
  assert(monster_action_frame(13, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit, 5) == ma13_walk.start);

  const auto* ma21_special = legacy_monster_action_table(43, 0);
  const auto ma21_attack = (*ma21_special)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  const auto ma21_die = (*ma21_special)[static_cast<std::size_t>(LegacyMonsterAction::die)];
  assert(monster_default_frame(43, 5, false) ==
         (*ma21_special)[static_cast<std::size_t>(LegacyMonsterAction::stand)].start);
  assert(monster_action_frame(43, mir2::legacy::kSmHit,
                              mir2::client_v1::ActorActionKind::hit, 5) == ma21_attack.start);
  assert(monster_default_frame(43, 5, true) == ma21_die.start + ma21_die.frame - 1);

  const auto* ma25_special = legacy_monster_action_table(33, 0);
  const auto ma25_critical = (*ma25_special)[static_cast<std::size_t>(LegacyMonsterAction::critical)];
  const auto ma25_death = (*ma25_special)[static_cast<std::size_t>(LegacyMonsterAction::death)];
  assert(monster_action_frame(33, mir2::legacy::kSmHit,
                              mir2::client_v1::ActorActionKind::hit, 5) == ma25_critical.start);
  assert(monster_action_frame(33, mir2::legacy::kSmDigDown,
                              mir2::client_v1::ActorActionKind::hit, 5) == ma25_death.start);

  const auto* ma23_special = legacy_monster_action_table(48, 0);
  const auto ma23_death = (*ma23_special)[static_cast<std::size_t>(LegacyMonsterAction::death)];
  assert(monster_action_frame(48, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit, 5) == ma23_death.start);

  const auto* ma26_special = legacy_monster_action_table(99, 0);
  const auto ma26_stand = (*ma26_special)[static_cast<std::size_t>(LegacyMonsterAction::stand)];
  const auto ma26_attack = (*ma26_special)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  const auto ma26_critical = (*ma26_special)[static_cast<std::size_t>(LegacyMonsterAction::critical)];
  const auto ma26_die = (*ma26_special)[static_cast<std::size_t>(LegacyMonsterAction::die)];
  assert(monster_default_frame(99, 2, false) == legacy_frame_index(ma26_stand, 2, 0));
  assert(monster_default_frame(99, 5, false) == ma26_critical.start);
  assert(monster_action_frame(99, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit, 2) == ma26_attack.start);
  assert(monster_action_frame(99, mir2::legacy::kSmDigDown,
                              mir2::client_v1::ActorActionKind::hit, 2) == ma26_critical.start);
  assert(monster_default_frame(99, 2, true) == ma26_die.start + ma26_die.frame - 1);

  const auto* ma27_special = legacy_monster_action_table(98, 0);
  const auto ma27_stand = (*ma27_special)[static_cast<std::size_t>(LegacyMonsterAction::stand)];
  const auto ma27_die = (*ma27_special)[static_cast<std::size_t>(LegacyMonsterAction::die)];
  assert(monster_default_frame(98, 2, false) == ma27_stand.start + 2);
  assert(monster_action_frame(98, mir2::legacy::kSmDigUp,
                              mir2::client_v1::ActorActionKind::hit, 2) == ma27_die.start);
  assert(monster_default_frame(98, 2, true) == ma27_die.start + ma27_die.frame - 1);

  const auto* ma19_default = legacy_monster_action_table(88, 0);
  const auto ma19_default_attack =
      (*ma19_default)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
  assert(monster_action_frame(88, mir2::legacy::kSmLighting,
                              mir2::client_v1::ActorActionKind::hit, 2) ==
         legacy_frame_index(ma19_default_attack, 2, 0));

  auto special_effect_world = [](const std::uint8_t race, const std::uint16_t ident,
                                 const mir2::client_v1::ActorActionKind kind,
                                 const int target_x = 34, const int target_y = 30) {
    WorldViewState effect_world;
    effect_world.width = 80;
    effect_world.height = 80;
    ActorState effect_actor;
    effect_actor.actor_id = 400 + race;
    effect_actor.actor_type = mir2::client_v1::ActorType::monster;
    effect_actor.x = 30;
    effect_actor.y = 30;
    effect_actor.from_x = 30;
    effect_actor.from_y = 30;
    effect_actor.dir = 2;
    effect_actor.feature = race;
    effect_actor.current_action = kind;
    effect_actor.legacy_action_ident = ident;
    effect_actor.action_started_ms = 20000 + race + ident;
    effect_actor.action_target_x = target_x;
    effect_actor.action_target_y = target_y;
    effect_world.actors[effect_actor.actor_id] = effect_actor;
    return effect_world;
  };

  auto advance_special_effect = [](AnimationManager& manager, WorldViewState& effect_world,
                                   const std::uint64_t start_ms, const int steps) {
    manager.reset(start_ms - 100);
    manager.update(effect_world, start_ms);
    for (int step = 1; step <= steps; ++step) {
      manager.update(effect_world, start_ms + static_cast<std::uint64_t>(step) * 250U);
    }
  };

  auto axe_world = special_effect_world(15, mir2::legacy::kSmFlyAxe,
                                        mir2::client_v1::ActorActionKind::hit);
  AnimationManager axe_animations;
  advance_special_effect(axe_animations, axe_world, 21000, 2);
  assert(axe_animations.effects().fly_count() == 1);
  const auto& axe_fly = axe_animations.effects().fly_effects().front();
  assert(axe_fly.archive == legacy_mon_archive_for_appearance(0));
  assert(axe_fly.effect_base == 447);
  assert(axe_fly.magic_type == LegacyMagicType::fly_axe);
  assert(axe_fly.owner_actor_id == 415);
  assert(axe_fly.dir16 == 4);
  assert(axe_fly.ready_distance == 65);
  axe_animations.update(axe_world, 21500);
  assert(axe_animations.effects().fly_count() == 1);
  axe_world.actors[415].action_started_ms = 25000;
  AnimationManager second_axe_animations;
  advance_special_effect(second_axe_animations, axe_world, 25000, 2);
  assert(second_axe_animations.effects().fly_count() == 1);

  auto thorn_world = special_effect_world(22, mir2::legacy::kSmFlyAxe,
                                          mir2::client_v1::ActorActionKind::hit);
  AnimationManager thorn_animations;
  advance_special_effect(thorn_animations, thorn_world, 22000, 2);
  assert(thorn_animations.effects().fly_count() == 1);
  assert(thorn_animations.effects().fly_effects().front().effect_base == 2967);

  auto archer_world = special_effect_world(45, mir2::legacy::kSmFlyAxe,
                                           mir2::client_v1::ActorActionKind::hit);
  AnimationManager archer_animations;
  advance_special_effect(archer_animations, archer_world, 23000, 4);
  assert(archer_animations.effects().fly_count() == 1);
  const auto& arrow_fly = archer_animations.effects().fly_effects().front();
  assert(arrow_fly.archive == ArchiveId::effect);
  assert(arrow_fly.effect_base == 272);
  assert(arrow_fly.magic_type == LegacyMagicType::fly_arrow);
  assert(arrow_fly.fly_frame_stride == 1);
  assert(arrow_fly.ready_distance == 40);

  auto skel_king_world = special_effect_world(63, mir2::legacy::kSmFlyAxe,
                                              mir2::client_v1::ActorActionKind::hit);
  AnimationManager skel_king_animations;
  advance_special_effect(skel_king_animations, skel_king_world, 24000, 4);
  assert(skel_king_animations.effects().fly_count() == 1);
  const auto& skel_king_fly = skel_king_animations.effects().fly_effects().front();
  assert(skel_king_fly.archive == ArchiveId::mon5);
  assert(skel_king_fly.effect_base == 3570);
  assert(skel_king_fly.magic_type == LegacyMagicType::fire_ball);
  assert(skel_king_fly.next_frame_ms == 40);

  auto guard_world = special_effect_world(72, mir2::legacy::kSmLighting,
                                          mir2::client_v1::ActorActionKind::hit);
  AnimationManager guard_animations;
  advance_special_effect(guard_animations, guard_world, 25000, 4);
  assert(guard_animations.effects().ground_count() == 1);
  assert(guard_animations.effects().ground_effects().front().magic_type ==
         LegacyMagicType::ground_effect);
  assert(guard_animations.effects().ground_effects().front().owner_actor_id == 472);

  auto ordinary_world = special_effect_world(88, mir2::legacy::kSmLighting,
                                             mir2::client_v1::ActorActionKind::hit);
  AnimationManager ordinary_animations;
  advance_special_effect(ordinary_animations, ordinary_world, 26000, 4);
  assert(ordinary_animations.effects().fly_count() == 0);
  assert(ordinary_animations.effects().ground_count() == 0);

  auto overlay_pose_for = [&](const std::uint8_t race, const std::uint16_t ident,
                              const mir2::client_v1::ActorActionKind kind) {
    auto overlay_world = special_effect_world(race, ident, kind);
    AnimationManager overlay_animations;
    overlay_animations.reset(30000);
    overlay_animations.update(overlay_world, 30100);
    const auto pose = overlay_animations.pose_for(400 + race);
    assert(pose.has_value());
    return *pose;
  };

  const auto gas_overlay = overlay_pose_for(16, mir2::legacy::kSmLighting,
                                            mir2::client_v1::ActorActionKind::hit);
  assert(gas_overlay.overlay_count == 1);
  assert(gas_overlay.overlays[0].archive == ArchiveId::mon3);
  assert(gas_overlay.overlays[0].frame_index == 1444 + 20);

  const auto zombi_overlay = overlay_pose_for(40, mir2::legacy::kSmLighting,
                                              mir2::client_v1::ActorActionKind::hit);
  assert(zombi_overlay.overlay_count == 1);
  assert(zombi_overlay.overlays[0].archive == ArchiveId::mon5);
  assert(zombi_overlay.overlays[0].frame_index == 350 + 40);

  const auto sculpture_overlay = overlay_pose_for(49, mir2::legacy::kSmHit,
                                                  mir2::client_v1::ActorActionKind::hit);
  assert(sculpture_overlay.overlay_count == 1);
  assert(sculpture_overlay.overlays[0].archive == ArchiveId::mon7);
  assert(sculpture_overlay.overlays[0].frame_index == 1680 + 20);

  const auto skel_overlay = overlay_pose_for(63, mir2::legacy::kSmFlyAxe,
                                             mir2::client_v1::ActorActionKind::hit);
  assert(skel_overlay.overlay_count == 1);
  assert(skel_overlay.overlays[0].archive == ArchiveId::mon5);
  assert(skel_overlay.overlays[0].frame_index == 3300);

  const auto banya_overlay = overlay_pose_for(71, mir2::legacy::kSmLighting,
                                              mir2::client_v1::ActorActionKind::hit);
  assert(banya_overlay.overlay_count == 1);
  assert(banya_overlay.overlays[0].frame_index == 3490);

  const auto door_overlay = overlay_pose_for(99, mir2::legacy::kSmNowDeath,
                                             mir2::client_v1::ActorActionKind::struck);
  assert(door_overlay.overlay_count == 1);
  assert(door_overlay.overlays[0].frame_index == 120);

  auto wall_world = special_effect_world(98, mir2::legacy::kSmDigUp,
                                         mir2::client_v1::ActorActionKind::hit);
  wall_world.actors[498].feature = (901 << 16) | 98;
  AnimationManager wall_animations;
  wall_animations.reset(31000);
  wall_animations.update(wall_world, 31100);
  const auto wall_overlay = wall_animations.pose_for(498);
  assert(wall_overlay.has_value());
  assert(wall_overlay->overlay_count == 2);
  assert(wall_overlay->overlays[0].frame_index == legacy_monster_offset(901) + 10);
  assert(wall_overlay->overlays[1].frame_index == 224);

  WorldViewState npc_world;
  ActorState merchant;
  merchant.actor_id = 30;
  merchant.actor_type = mir2::client_v1::ActorType::npc;
  merchant.x = 20;
  merchant.y = 21;
  merchant.from_x = 20;
  merchant.from_y = 21;
  merchant.dir = 5;
  merchant.feature = make_legacy_feature(50, 0, 0, 0);
  npc_world.self_actor_id = 1;
  npc_world.actors[30] = merchant;
  AnimationManager npc_animations;
  npc_animations.reset(7100);
  npc_animations.sync_world(npc_world, 7100);
  const auto npc_pose = npc_animations.pose_for(30);
  assert(npc_pose.has_value());
  assert(npc_pose->body_archive == ArchiveId::npc);
  assert(npc_pose->body_index == legacy_npc_offset(0) + 20);

  merchant.actor_id = 31;
  merchant.feature = make_legacy_feature(50, 0, 0, 24);
  npc_world.actors[31] = merchant;
  npc_animations.reset(7200);
  npc_animations.sync_world(npc_world, 7200);
  const auto npc_special_pose = npc_animations.pose_for(31);
  assert(npc_special_pose.has_value());
  assert(npc_special_pose->body_archive == ArchiveId::npc);
  assert(npc_special_pose->body_index == legacy_npc_offset(24) + 50);

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

  WorldViewState forced_world;
  forced_world.self_actor_id = 1;
  ActorState forced_actor;
  forced_actor.actor_id = 1;
  forced_actor.actor_type = mir2::client_v1::ActorType::player;
  forced_actor.feature = make_legacy_feature(0, 1, 2, 3);
  forced_actor.dir = 2;
  forced_actor.from_x = 10;
  forced_actor.from_y = 10;
  forced_actor.x = 11;
  forced_actor.y = 10;
  forced_actor.current_action = mir2::client_v1::ActorActionKind::rush;
  forced_actor.legacy_action_ident = mir2::legacy::kSmRush;
  forced_actor.move_started_ms = 1000;
  forced_actor.action_started_ms = 1000;
  forced_world.actors[1] = forced_actor;
  AnimationManager forced_animations;
  forced_animations.reset(900);
  forced_animations.update(forced_world, 1000);
  auto forced_pose = forced_animations.pose_for(1);
  assert(forced_pose.has_value());
  assert(forced_pose->body_index == 600 + 144);
  assert(forced_pose->rx == 10 && forced_pose->shift_x == 16);

  forced_actor.current_action = mir2::client_v1::ActorActionKind::rush_kung;
  forced_actor.legacy_action_ident = mir2::legacy::kSmRushKung;
  forced_actor.x = 10;
  forced_actor.y = 10;
  forced_actor.from_x = 10;
  forced_actor.from_y = 10;
  forced_actor.action_target_x = 11;
  forced_actor.action_target_y = 10;
  forced_world.actors[1] = forced_actor;
  forced_animations.reset(900);
  forced_animations.update(forced_world, 1000);
  forced_pose = forced_animations.pose_for(1);
  assert(forced_pose.has_value());
  assert(forced_pose->rx == 10 && forced_pose->shift_x == 8);
  forced_animations.update(forced_world, 1100);
  forced_animations.update(forced_world, 1200);
  forced_pose = forced_animations.pose_for(1);
  assert(forced_pose.has_value());
  assert(forced_pose->rx == 10 && forced_pose->shift_x == 0);

  forced_actor.current_action = mir2::client_v1::ActorActionKind::backstep;
  forced_actor.legacy_action_ident = mir2::legacy::kSmBackStep;
  forced_actor.x = 9;
  forced_actor.y = 10;
  forced_actor.from_x = 10;
  forced_actor.from_y = 10;
  forced_actor.action_target_x = -1;
  forced_actor.action_target_y = -1;
  forced_world.actors[1] = forced_actor;
  forced_animations.reset(900);
  forced_animations.update(forced_world, 1000);
  forced_pose = forced_animations.pose_for(1);
  const auto expected_backstep = legacy_shift(9, 10, 6, 1, 1, 6);
  assert(forced_pose.has_value());
  assert(forced_pose->rx == expected_backstep.rx);
  assert(forced_pose->shift_x == expected_backstep.shift_x);

  GameStateStore queued_store;
  queued_store.world.self_actor_id = 1;
  ActorState queued_move_actor;
  queued_move_actor.actor_id = 1;
  queued_move_actor.actor_type = mir2::client_v1::ActorType::player;
  queued_move_actor.feature = make_legacy_feature(0, 1, 2, 3);
  queued_move_actor.x = 10;
  queued_move_actor.y = 10;
  queued_move_actor.from_x = 10;
  queued_move_actor.from_y = 10;
  queued_move_actor.dir = 2;
  queued_store.world.actors[1] = queued_move_actor;
  queued_store.apply(mir2::client_v1::ActorAction{
      1, mir2::client_v1::ActorActionKind::walk, 11, 10, 2, 0, 0,
      mir2::legacy::kSmWalk, 0, false, 0});
  queued_store.apply(mir2::client_v1::ActorAction{
      1, mir2::client_v1::ActorActionKind::walk, 12, 10, 2, 0, 0,
      mir2::legacy::kSmWalk, 0, false, 0});
  queued_store.process_legacy_actor_queues(900);
  AnimationManager queued_animations;
  queued_animations.reset(900);
  queued_animations.update(queued_store.world, 1000);
  auto queued_pose = queued_animations.pose_for(1);
  assert(queued_pose.has_value());
  assert(queued_pose->rx == 10 && queued_pose->shift_x == 8);

  animations.reset(2000);
  world.actors[1].x = 10;
  world.actors[1].y = 10;
  world.actors[1].from_x = 10;
  world.actors[1].from_y = 10;
  world.actors[1].move_started_ms = 0;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::spell;
  world.actors[1].magic_id = 1;
  world.actors[1].action_started_ms = 2000;
  world.actors[1].action_target_x = 14;
  world.actors[1].action_target_y = 10;
  world.actors[1].action_target_actor_id = 0;
  world.actors[1].action_magic = true;
  world.actors[1].action_magic_effect = 1;
  world.actors[1].action_magic_effect_type = 1;
  animations.update(world, 2000);
  assert(animations.effects().fly_count() == 0);
  for (std::uint64_t now = 2061; now <= 2488; now += 61) {
    animations.update(world, now);
    assert(animations.effects().fly_count() == 0);
  }
  animations.update(world, 2549);
  assert(animations.effects().fly_count() == 1);
  assert(animations.effects().fly_effects().front().owner_actor_id == 1);
  assert(animations.effects().fly_effects().front().target_x == 14);
  animations.update(world, 2599);
  assert(animations.effects().fly_count() == 1);

  animations.reset(2600);
  world.actors[1].action_started_ms = 2600;
  world.actors[1].action_magic_effect = 3;
  world.actors[1].action_magic_effect_type = -1;
  animations.update(world, 2600);
  auto saw_spell_overlay = false;
  for (std::uint64_t now = 2661; now <= 3149; now += 61) {
    animations.update(world, now);
    if (const auto spell_pose = animations.pose_for(1); spell_pose.has_value()) {
      saw_spell_overlay = saw_spell_overlay || spell_pose->overlay_count > 0;
    }
    assert(animations.effects().fly_count() == 0);
  }
  assert(saw_spell_overlay);
  world.actors[1].action_magic_effect_type = 1;
  animations.update(world, 3210);
  assert(animations.effects().fly_count() == 1);
  world.actors[1].action_magic_effect = 0;
  world.actors[1].action_magic_effect_type = -1;

  animations.reset(6000);
  auto queued_actor = actor;
  queued_actor.current_action = mir2::client_v1::ActorActionKind::hit;
  queued_actor.action_started_ms = 6000;
  world.actors[1] = queued_actor;
  animations.update(world, 6000);
  queued_actor.current_action = mir2::client_v1::ActorActionKind::spell;
  queued_actor.magic_id = 1;
  queued_actor.action_started_ms = 6061;
  queued_actor.action_target_x = 14;
  queued_actor.action_target_y = 10;
  world.actors[1] = queued_actor;
  animations.update(world, 6061);
  pose = animations.pose_for(1);
  assert(pose.has_value());
  assert(pose->body_index ==
         appearance.body_offset +
             legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 2, 0));
  for (std::uint64_t now = 6147; now <= 6600; now += 86) {
    animations.update(world, now);
  }
  animations.update(world, 6664);
  pose = animations.pose_for(1);
  assert(pose.has_value());
  assert(pose->body_index ==
         appearance.body_offset +
             legacy_frame_index(legacy_human_action_info(LegacyHumanAction::spell), 2, 1));

  auto prioritized_spell_actor = actor;
  prioritized_spell_actor.current_action = mir2::client_v1::ActorActionKind::spell;
  prioritized_spell_actor.magic_id = 1;
  prioritized_spell_actor.action_started_ms = 6800;
  prioritized_spell_actor.action_target_x = 14;
  prioritized_spell_actor.action_target_y = 10;
  prioritized_spell_actor.action_magic = true;
  prioritized_spell_actor.action_magic_effect = 1;
  prioritized_spell_actor.action_magic_effect_type = 1;
  prioritized_spell_actor.legacy_event_sequence = 2;
  auto normal_spell_event = prioritized_spell_actor;
  normal_spell_event.legacy_event_sequence = 1;
  normal_spell_event.legacy_event_priority = LegacyEventPriority::normal;
  auto hurry_spell_event = prioritized_spell_actor;
  hurry_spell_event.legacy_event_sequence = 2;
  hurry_spell_event.legacy_event_priority = LegacyEventPriority::hurry;
  auto hurry_only_actor = prioritized_spell_actor;
  hurry_only_actor.legacy_pending_actions.clear();
  hurry_only_actor.legacy_pending_actions.push_back(hurry_spell_event);
  auto hurry_plus_normal_actor = prioritized_spell_actor;
  hurry_plus_normal_actor.legacy_pending_actions.clear();
  hurry_plus_normal_actor.legacy_pending_actions.push_back(normal_spell_event);
  hurry_plus_normal_actor.legacy_pending_actions.push_back(hurry_spell_event);
  auto hurry_only_world = world;
  auto hurry_plus_normal_world = world;
  hurry_only_world.action_locked = false;
  hurry_only_world.action_lock_started_ms = 0;
  hurry_plus_normal_world.action_locked = false;
  hurry_plus_normal_world.action_lock_started_ms = 0;
  hurry_only_world.actors[1] = hurry_only_actor;
  hurry_plus_normal_world.actors[1] = hurry_plus_normal_actor;
  AnimationManager hurry_only_animations;
  hurry_only_animations.reset(6800);
  AnimationManager hurry_plus_normal_animations;
  hurry_plus_normal_animations.reset(6800);
  hurry_only_animations.update(hurry_only_world, 6800);
  hurry_plus_normal_animations.update(hurry_plus_normal_world, 6800);
  for (std::uint64_t now = 6886; now <= 9300; now += 86) {
    hurry_only_animations.update(hurry_only_world, now);
    hurry_plus_normal_animations.update(hurry_plus_normal_world, now);
    assert(hurry_only_animations.is_actor_legacy_idle(1) ==
           hurry_plus_normal_animations.is_actor_legacy_idle(1));
    const auto hurry_only_pose = hurry_only_animations.pose_for(1);
    const auto hurry_plus_normal_pose = hurry_plus_normal_animations.pose_for(1);
    assert(hurry_only_pose.has_value() == hurry_plus_normal_pose.has_value());
    if (hurry_only_pose.has_value() && hurry_plus_normal_pose.has_value()) {
      assert(hurry_only_pose->body_index == hurry_plus_normal_pose->body_index);
    }
  }

  animations.reset(6000);
  auto delayed_spell_actor = actor;
  delayed_spell_actor.current_action = mir2::client_v1::ActorActionKind::hit;
  delayed_spell_actor.legacy_action_ident = mir2::legacy::kSmHit;
  delayed_spell_actor.action_started_ms = 6000;
  world.actors[1] = delayed_spell_actor;
  world.action_locked = false;
  world.action_lock_started_ms = 0;
  animations.update(world, 6000);

  delayed_spell_actor.current_action = mir2::client_v1::ActorActionKind::spell;
  delayed_spell_actor.legacy_action_ident = 17;
  delayed_spell_actor.magic_id = 1;
  delayed_spell_actor.action_started_ms = 6061;
  delayed_spell_actor.action_target_x = 14;
  delayed_spell_actor.action_target_y = 10;
  delayed_spell_actor.action_magic = true;
  delayed_spell_actor.action_magic_effect = 1;
  delayed_spell_actor.action_magic_effect_type = -1;
  delayed_spell_actor.legacy_event_sequence = 1;
  auto delayed_normal_event = delayed_spell_actor;
  delayed_normal_event.legacy_event_priority = LegacyEventPriority::normal;
  delayed_normal_event.legacy_event_sequence = 1;
  delayed_spell_actor.legacy_pending_actions.clear();
  delayed_spell_actor.legacy_pending_actions.push_back(delayed_normal_event);
  world.actors[1] = delayed_spell_actor;
  animations.update(world, 6061);

  auto delayed_hurry_event = delayed_spell_actor;
  delayed_hurry_event.legacy_event_priority = LegacyEventPriority::hurry;
  delayed_hurry_event.legacy_event_sequence = 2;
  delayed_hurry_event.action_magic_effect_type = 1;
  delayed_spell_actor.legacy_pending_actions.push_back(delayed_hurry_event);
  delayed_spell_actor.legacy_event_sequence = 2;
  delayed_spell_actor.action_magic_effect_type = 1;
  world.actors[1] = delayed_spell_actor;
  animations.update(world, 6122);

  for (std::uint64_t now = 6208; now <= 8400; now += 86) {
    animations.update(world, now);
  }
  assert(animations.is_actor_legacy_idle(1));

  animations.reset(7000);
  world.actors[1] = actor;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::hit;
  world.actors[1].legacy_action_ident = mir2::legacy::kSmHit;
  world.actors[1].action_started_ms = 7000;
  world.action_locked = true;
  animations.update(world, 7000);
  for (std::uint64_t now = 7086; now <= 7600; now += 86) {
    animations.update(world, now);
  }
  assert(!animations.is_actor_legacy_idle(1));
  world.action_locked = false;
  animations.update(world, 7700);
  assert(animations.is_actor_legacy_idle(1));
  world.action_lock_started_ms = 0;

  animations.reset(12000);
  world.actors[1] = actor;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::hit;
  world.actors[1].legacy_action_ident = mir2::legacy::kSmHit;
  world.actors[1].action_started_ms = 12000;
  world.action_locked = true;
  world.action_lock_started_ms = 1000;
  animations.update(world, 12000);
  for (std::uint64_t now = 12086; now <= 12600; now += 86) {
    animations.update(world, now);
  }
  assert(animations.is_actor_legacy_idle(1));
  world.action_locked = false;
  world.action_lock_started_ms = 0;

  animations.reset(3000);
  world.actors[1] = actor;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::spell;
  world.actors[1].x = 10;
  world.actors[1].y = 10;
  world.actors[1].from_x = 10;
  world.actors[1].from_y = 10;
  world.actors[1].magic_id = 22;
  world.actors[1].action_started_ms = 3000;
  world.actors[1].action_target_x = 12;
  world.actors[1].action_target_y = 12;
  world.actors[1].action_magic_effect = 20;
  world.actors[1].action_magic_effect_type = 13;
  animations.update(world, 3000);
  assert(animations.effects().ground_count() == 0);
  for (std::uint64_t now = 3061; now <= 3549; now += 61) {
    animations.update(world, now);
  }
  assert(animations.effects().ground_count() == 1);
  assert(animations.effects().ground_effects().front().archive == ArchiveId::mon21);
  assert(animations.effects().ground_effects().front().explosion_base == 3580);

  animations.reset(3600);
  world.actors[1] = actor;
  world.actors[1].current_action = mir2::client_v1::ActorActionKind::spell;
  world.actors[1].magic_id = 3;
  world.actors[1].action_started_ms = 3600;
  world.actors[1].action_target_x = 12;
  world.actors[1].action_target_y = 12;
  world.actors[1].action_magic_effect = 0;
  world.actors[1].action_magic_effect_type = -1;
  animations.update(world, 3600);
  for (std::uint64_t now = 3661; now <= 4400; now += 61) {
    animations.update(world, now);
  }
  assert(animations.effects().ground_count() == 0);
  assert(animations.effects().fly_count() == 0);

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
  assert(animations.effects().overlay_count() == 0);
  for (std::uint64_t now = 4061; now <= 4549; now += 61) {
    animations.update(world, now);
  }
  assert(animations.effects().overlay_count() == 0);
  assert(animations.effects().fly_count() == 0);

  animations.reset(4500);
  world.actors[1].magic_id = 5;
  world.actors[1].action_started_ms = 4500;
  world.actors[1].action_magic_effect_type = 1;
  world.actors[1].action_magic_effect = 3;
  world.actors[1].action_target_actor_id = 2;
  world.actors[1].action_target_x = 11;
  world.actors[1].action_target_y = 10;
  animations.update(world, 4500);
  assert(animations.effects().fly_count() == 0);
  for (std::uint64_t now = 4561; now <= 5049; now += 61) {
    animations.update(world, now);
  }
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

  LegacyEffectManager tracking_effects;
  magic.server_magic_id = 101;
  magic.now_ms = 6000;
  magic.target_actor_id = 42;
  auto& tracking = tracking_effects.spawn_magic_effect(magic);
  assert(tracking_effects.fly_count() == 1);
  std::unordered_map<std::uint64_t, ActorRenderPose> target_poses;
  ActorRenderPose target_pose;
  target_pose.rx = 13;
  target_pose.ry = 10;
  target_pose.shift_x = 0;
  target_pose.shift_y = 0;
  target_poses.emplace(42, target_pose);
  tracking_effects.update(6051, target_poses);
  const auto first_track_x = tracking.fly_x;
  target_poses[42].rx = 15;
  tracking_effects.update(6102, target_poses);
  assert(tracking_effects.fly_count() == 1);
  assert(tracking.fly_x > first_track_x);

  LegacyEffectManager explosion_pose_effects;
  magic.server_magic_id = 102;
  magic.now_ms = 7000;
  magic.source_x = 10;
  magic.source_y = 10;
  magic.target_x = 11;
  magic.target_y = 10;
  magic.target_actor_id = 77;
  explosion_pose_effects.spawn_magic_effect(magic);
  std::unordered_map<std::uint64_t, ActorRenderPose> shifted_target_poses;
  ActorRenderPose shifted_target_pose;
  shifted_target_pose.rx = 11;
  shifted_target_pose.ry = 10;
  shifted_target_pose.shift_x = 9;
  shifted_target_pose.shift_y = -6;
  shifted_target_poses.emplace(77, shifted_target_pose);
  for (std::uint64_t now = 7051; now <= 7600; now += 51) {
    explosion_pose_effects.update(now, shifted_target_poses);
  }
  assert(explosion_pose_effects.fly_count() == 1);
  const auto& shifted_explosion = explosion_pose_effects.fly_effects().front();
  assert(shifted_explosion.fixed_effect);
  assert(shifted_explosion.fly_x == 11 * 48 + 24 + 9);
  assert(shifted_explosion.fly_y == 10 * 32 + 16 - 6);

  auto spawn_matrix_magic = [](const int effect_type, const int effect_number,
                               const int magic_id = 1) {
    LegacyEffectManager manager;
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
    manager.spawn_magic_effect(create);
    return manager;
  };

  auto fireball_matrix = spawn_matrix_magic(1, 1, 1);
  assert(fireball_matrix.fly_count() == 1);
  const auto& fireball_effect = fireball_matrix.fly_effects().front();
  assert(fireball_effect.archive == ArchiveId::magic);
  assert(fireball_effect.effect_base == 0);
  assert(fireball_effect.explosion_base == 170);
  assert(!fireball_effect.fixed_effect);
  assert(fireball_effect.frame_count == 6);

  auto firegun_matrix = spawn_matrix_magic(5, 9, 9);
  assert(firegun_matrix.fly_count() == 1);
  const auto& firegun_effect = firegun_matrix.fly_effects().front();
  assert(firegun_effect.archive == ArchiveId::magic);
  assert(firegun_effect.effect_base == 930);
  assert(firegun_effect.explosion_base == 930);
  assert(firegun_effect.fixed_effect);
  assert(firegun_effect.target_actor_id == 0);

  auto thunder_matrix = spawn_matrix_magic(7, 8, 11);
  assert(thunder_matrix.fly_count() == 1);
  const auto& thunder_effect = thunder_matrix.fly_effects().front();
  assert(thunder_effect.archive == ArchiveId::magic2);
  assert(thunder_effect.effect_base == 10);
  assert(thunder_effect.explosion_base == 10);
  assert(thunder_effect.explosion_frame_count == 6);

  auto explosion_matrix = spawn_matrix_magic(2, 3, 3);
  assert(explosion_matrix.fly_count() == 1);
  const auto& explosion_effect = explosion_matrix.fly_effects().front();
  assert(explosion_effect.effect_base == 400);
  assert(explosion_effect.explosion_base == 570);
  assert(explosion_effect.next_frame_ms == 80);
  assert(explosion_effect.fixed_effect);
  assert(explosion_effect.target_actor_id == 77);

  auto fire_thunder_matrix = spawn_matrix_magic(14, 33, 33);
  assert(fire_thunder_matrix.fly_count() == 1);
  const auto& fire_thunder_effect = fire_thunder_matrix.fly_effects().front();
  assert(fire_thunder_effect.archive == ArchiveId::magic2);
  assert(fire_thunder_effect.effect_base == 140);
  assert(fire_thunder_effect.explosion_base == 140);
  assert(fire_thunder_effect.explosion_frame_count == 10);

  auto explo_bujauk_matrix = spawn_matrix_magic(8, 10, 13);
  assert(explo_bujauk_matrix.fly_count() == 1);
  const auto& explo_bujauk_effect = explo_bujauk_matrix.fly_effects().front();
  assert(explo_bujauk_effect.effect_base == 1160);
  assert(explo_bujauk_effect.explosion_base == 1360);
  assert(!explo_bujauk_effect.fixed_effect);

  auto explo_bujauk_alt_matrix = spawn_matrix_magic(8, 17, 19);
  assert(explo_bujauk_alt_matrix.fly_count() == 1);
  assert(explo_bujauk_alt_matrix.fly_effects().front().explosion_base == 1540);

  auto bujauk_matrix = spawn_matrix_magic(9, 11, 14);
  assert(bujauk_matrix.fly_count() == 1);
  const auto& bujauk_effect = bujauk_matrix.fly_effects().front();
  assert(bujauk_effect.effect_base == 1160);
  assert(bujauk_effect.explosion_frame_count == 16);
  assert(!bujauk_effect.fixed_effect);

  auto ground_matrix = spawn_matrix_magic(13, 32, 22);
  assert(ground_matrix.ground_count() == 1);
  const auto& ground_magic_effect = ground_matrix.ground_effects().front();
  assert(ground_magic_effect.archive == ArchiveId::mon21);
  assert(ground_magic_effect.explosion_base == 3580);
  assert(ground_magic_effect.explosion_frame_count == 20);
  assert(ground_magic_effect.light == 3);
  assert(ground_magic_effect.fixed_effect);

  const auto assert_explosion_special = [&](const int effect_number, const int explosion_base,
                                            const int explosion_frame_count, const int light,
                                            const bool tracks_target) {
    auto special_matrix = spawn_matrix_magic(2, effect_number, effect_number);
    assert(special_matrix.fly_count() == 1);
    const auto& special_effect = special_matrix.fly_effects().front();
    assert(special_effect.explosion_base == explosion_base);
    assert(special_effect.explosion_frame_count == explosion_frame_count);
    assert(special_effect.next_frame_ms == 80);
    assert(special_effect.light == light);
    assert((special_effect.target_actor_id != 0) == tracks_target);
  };
  assert_explosion_special(18, 1570, 10, 1, true);
  assert_explosion_special(21, 1660, 20, 3, false);
  assert_explosion_special(26, 3990, 10, 2, true);
  assert_explosion_special(27, 1800, 10, 3, false);
  assert_explosion_special(30, 3930, 16, 3, true);
  assert_explosion_special(31, 3850, 20, 3, false);

  LegacyEffectManager explicit_matrix;
  LegacyEffectManager::MagicCreate explicit_create;
  explicit_create.effect_type = 12;
  explicit_create.effect = 63;
  explicit_create.archive = ArchiveId::mon5;
  explicit_create.effect_base = 3570;
  explicit_create.source_x = 10;
  explicit_create.source_y = 10;
  explicit_create.target_x = 13;
  explicit_create.target_y = 10;
  explicit_create.now_ms = 9000;
  explicit_create.frame_count = 6;
  explicit_create.explosion_frame_count = 1;
  explicit_matrix.spawn_magic_effect(explicit_create);
  assert(explicit_matrix.fly_count() == 1);
  const auto& explicit_effect = explicit_matrix.fly_effects().front();
  assert(explicit_effect.archive == ArchiveId::mon5);
  assert(explicit_effect.effect_base == 3570);
  assert(explicit_effect.explosion_frame_count == 1);

  return 0;
}
