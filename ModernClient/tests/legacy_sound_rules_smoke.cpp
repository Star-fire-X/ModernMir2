#include <cassert>
#include <iostream>
#include <optional>

#include "audio/legacy_sound_rules.hpp"
#include "shared/legacy/action_ids.hpp"

int main() {
  using namespace mir2::client;

  assert(legacy_click_sound_id(LegacyClickSound::none) == std::nullopt);
  assert(legacy_click_sound_id(LegacyClickSound::stone) == s_rock_button_click);
  assert(legacy_click_sound_id(LegacyClickSound::glass) == s_glass_button_click);
  assert(legacy_click_sound_id(LegacyClickSound::normal) == s_norm_button_click);

  assert(item_click_sound_id(0, "Potion") == s_click_drug);
  assert(item_click_sound_id(31, "Bundle") == s_click_drug);
  assert(item_click_sound_id(5, "Sword") == s_click_weapon);
  assert(item_click_sound_id(10, "Armor") == s_click_armor);
  assert(item_click_sound_id(22, "Ring") == s_click_ring);
  assert(item_click_sound_id(24, "Bracelet") == s_click_armring);
  assert(item_click_sound_id(19, "Necklace") == s_click_necklace);
  assert(item_click_sound_id(15, "Helmet") == s_click_helmet);
  assert(item_click_sound_id(99, "Other") == s_itmclick);

  assert(item_use_sound_id(0) == s_click_drug);
  assert(item_use_sound_id(1) == s_eat_drug);
  assert(item_use_sound_id(2) == s_eat_drug);
  assert(item_use_sound_id(5) == std::nullopt);

  assert(footstep_sound_id(nullptr, false, false) == s_walk_ground_l);
  assert(footstep_sound_id(nullptr, true, true) == s_run_ground_r);
  MapCell cell;
  cell.bk_img = 331;  // Delphi bidx 330 -> lawn
  assert(footstep_sound_id(&cell, false, false) == s_walk_lawn_l);
  assert(footstep_sound_id(&cell, true, true) == s_run_lawn_r);
  cell = MapCell{};
  cell.fr_img = 222;  // Delphi bidx 221 -> stone
  assert(footstep_sound_id(&cell, false, false) == s_walk_stone_l);

  assert(human_weapon_sound_id(12) == s_hit_short);
  assert(human_weapon_sound_id(2) == s_hit_wooden);
  assert(human_weapon_sound_id(4) == s_hit_sword);
  assert(human_weapon_sound_id(6) == s_hit_axe);
  assert(human_weapon_sound_id(48) == s_hit_club);
  assert(human_weapon_sound_id(16) == s_hit_long);
  assert(human_weapon_sound_id(0) == s_hit_fist);

  assert(human_struck_weapon_sound_id(12) == s_struck_short);
  assert(!human_struck_weapon_sound_id(0).has_value());
  assert(human_struck_body_sound_id(6, 6) == s_struck_armor_axe);
  assert(human_struck_body_sound_id(0, 6) == s_struck_body_axe);
  assert(human_struck_vocal_sound_id(0) == s_man_struck);
  assert(human_struck_vocal_sound_id(1) == s_wom_struck);
  assert(human_die_sound_id(0) == s_man_die);
  assert(human_die_sound_id(1) == s_wom_die);

  assert(monster_sound_id(11, monster_offset_attack) == 312);
  assert(magic_sound_id(7, LegacyMagicSoundPhase::start) == 10070);
  assert(magic_sound_id(7, LegacyMagicSoundPhase::fire) == 10071);
  assert(magic_sound_id(7, LegacyMagicSoundPhase::explosion) == 10072);

  assert(mir2::legacy::kSmFireHit == 8);
  assert(mir2::legacy::kSmHit == 14);
  assert(mir2::legacy::kSmHeavyHit == 15);
  assert(mir2::legacy::kSmBigHit == 16);
  assert(mir2::legacy::kSmPowerHit == 18);
  assert(mir2::legacy::kSmLongHit == 19);
  assert(mir2::legacy::kSmWideHit == 24);
  assert(mir2::legacy::kSmCrossHit == 35);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmPowerHit) ==
         mir2::legacy::kSmPowerHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmLongHit) ==
         mir2::legacy::kSmLongHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmWideHit) ==
         mir2::legacy::kSmWideHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmFireHit) ==
         mir2::legacy::kSmFireHit);
  assert(mir2::legacy::cm_attack_ident_to_sm(mir2::legacy::kCmCrossHit) ==
         mir2::legacy::kSmCrossHit);
  assert(mir2::legacy::sm_attack_ident_to_cm(mir2::legacy::kSmPowerHit) ==
         mir2::legacy::kCmPowerHit);
  assert(mir2::legacy::sm_attack_ident_to_cm(mir2::legacy::kSmFireHit) ==
         mir2::legacy::kCmFireHit);
  assert(mir2::legacy::normalize_attack_ident_to_sm(mir2::legacy::kCmPowerHit) ==
         mir2::legacy::kSmPowerHit);
  assert(mir2::legacy::normalize_attack_ident_to_sm(999) == mir2::legacy::kSmHit);
  assert(!mir2::legacy::is_attack_sm_ident(25));
  assert(mir2::legacy::normalize_attack_ident_to_sm(25) == mir2::legacy::kSmHit);

  auto extras = legacy_human_attack_extra_sound_ids(mir2::legacy::kSmPowerHit, 0);
  assert(extras.size() == 1 && extras.front() == s_yedo_man);
  extras = legacy_human_attack_extra_sound_ids(mir2::legacy::kSmPowerHit, 1);
  assert(extras.size() == 1 && extras.front() == s_yedo_woman);
  extras = legacy_human_attack_extra_sound_ids(mir2::legacy::kSmLongHit, 0);
  assert(extras.size() == 1 && extras.front() == s_longhit);
  extras = legacy_human_attack_extra_sound_ids(mir2::legacy::kSmWideHit, 0);
  assert(extras.size() == 1 && extras.front() == s_widehit);
  extras = legacy_human_attack_extra_sound_ids(mir2::legacy::kSmFireHit, 0);
  assert(extras.size() == 1 && extras.front() == s_firehit);
  assert(legacy_human_attack_extra_sound_ids(mir2::legacy::kSmCrossHit, 0).empty());
  assert(legacy_human_attack_extra_sound_ids(mir2::legacy::kSmHit, 0).empty());
  assert(legacy_human_attack_extra_sound_ids(mir2::legacy::kSmHeavyHit, 0).empty());
  assert(legacy_human_attack_extra_sound_ids(mir2::legacy::kSmBigHit, 0).empty());
  assert(legacy_human_attack_extra_sound_ids(25, 0).empty());
  assert(legacy_human_attack_extra_sound_ids(999, 0).empty());

  ActorState actor;
  actor.actor_type = mir2::client_v1::ActorType::player;
  actor.current_action = mir2::client_v1::ActorActionKind::hit;
  actor.feature = make_legacy_feature(0, 0, 4, 2);
  ActorRenderPose pose;
  pose.dir = 0;
  pose.current_frame = legacy_frame_index(legacy_human_action_info(LegacyHumanAction::hit), 0, 2);
  assert(actor_local_frame_for_sound(actor, pose) == 2);

  std::cout << "legacy_sound_rules_smoke ok\n";
  return 0;
}
