/**
 * @file legacy_sound_rules.cpp
 * @brief 旧版音效规则实现 —— 根据游戏事件确定应播放的音效 ID
 * @details 实现所有音效选择规则：UI 点击、物品操作、脚步声、
 *          武器挥动、攻击、受击、死亡、怪物叫声、魔法音效。
 *          所有规则与 Delphi 客户端的音效选择逻辑完全一致。
 */

#include "audio/legacy_sound_rules.hpp"

#include <algorithm>
#include <initializer_list>

#include "animation/legacy_animation.hpp"
#include "shared/legacy/action_ids.hpp"

namespace mir2::client {

namespace {

bool in_range(const int value, const int first, const int last) {
  return value >= first && value <= last;
}

bool is_one_of(const int value, std::initializer_list<int> values) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

int base_footstep_for_cell(const MapCell* cell) {
  if (cell == nullptr) {
    return s_walk_ground_l;
  }

  auto footstep = s_walk_ground_l;
  auto bidx = static_cast<int>(cell->area) * 10000 + static_cast<int>(cell->bk_img & 0x7FFFU) - 1;
  if (is_one_of(bidx, {330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341,
                       342, 343, 344, 345, 346, 347, 348, 349}) ||
      in_range(bidx, 450, 454) || in_range(bidx, 550, 554) ||
      in_range(bidx, 750, 754) || in_range(bidx, 950, 954) ||
      in_range(bidx, 1250, 1254) || in_range(bidx, 1400, 1424) ||
      in_range(bidx, 1455, 1474) || in_range(bidx, 1500, 1524) ||
      in_range(bidx, 1550, 1574)) {
    footstep = s_walk_lawn_l;
  } else if (in_range(bidx, 250, 254) || in_range(bidx, 1005, 1009) ||
             in_range(bidx, 1050, 1054) || in_range(bidx, 1060, 1064) ||
             in_range(bidx, 1450, 1454) || in_range(bidx, 1650, 1654)) {
    footstep = s_walk_rough_l;
  } else if (in_range(bidx, 605, 609) || in_range(bidx, 650, 654) ||
             in_range(bidx, 660, 664) || in_range(bidx, 2000, 2049) ||
             in_range(bidx, 3025, 3049) || in_range(bidx, 2400, 2424) ||
             in_range(bidx, 4625, 4649) || in_range(bidx, 4675, 4678)) {
    footstep = s_walk_stone_l;
  } else if (in_range(bidx, 1825, 1924) || in_range(bidx, 2150, 2174) ||
             in_range(bidx, 3075, 3099) || in_range(bidx, 3325, 3349) ||
             in_range(bidx, 3375, 3399)) {
    footstep = s_walk_cave_l;
  } else if (is_one_of(bidx, {3230, 3231, 3246, 3277}) || in_range(bidx, 3780, 3799)) {
    footstep = s_walk_wood_l;
  } else if (in_range(bidx, 3825, 4434)) {
    footstep = (bidx - 3825) % 25 == 0 ? s_walk_wood_l : s_walk_ground_l;
  } else if (in_range(bidx, 2075, 2099) || in_range(bidx, 2125, 2149)) {
    footstep = s_walk_room_l;
  } else if (in_range(bidx, 1800, 1824)) {
    footstep = s_walk_water_l;
  }

  if (in_range(bidx, 825, 1349) && ((bidx - 825) / 25) % 2 == 0) {
    footstep = s_walk_stone_l;
  }
  if (in_range(bidx, 1375, 1799) && ((bidx - 1375) / 25) % 2 == 0) {
    footstep = s_walk_cave_l;
  }
  if (is_one_of(bidx, {1385, 1386, 1391, 1392})) {
    footstep = s_walk_wood_l;
  }

  bidx = static_cast<int>(cell->mid_img & 0x7FFFU) - 1;
  if (in_range(bidx, 0, 115)) {
    footstep = s_walk_ground_l;
  } else if (in_range(bidx, 120, 124)) {
    footstep = s_walk_lawn_l;
  }

  bidx = static_cast<int>(cell->fr_img & 0x7FFFU) - 1;
  if (in_range(bidx, 221, 289) || in_range(bidx, 583, 658) ||
      in_range(bidx, 1183, 1206) || in_range(bidx, 7163, 7295) ||
      in_range(bidx, 7404, 7414)) {
    footstep = s_walk_stone_l;
  } else if (in_range(bidx, 3125, 3267) || in_range(bidx, 3757, 3948) ||
             in_range(bidx, 6030, 6999)) {
    footstep = s_walk_wood_l;
  } else if (in_range(bidx, 3316, 3589)) {
    footstep = s_walk_room_l;
  }

  return footstep;
}

LegacyActionInfo audio_action_info_for(const ActorState& actor) {
  const auto human = actor.actor_type == client_v1::ActorType::player;
  if (human) {
    if (actor.dead) {
      return legacy_human_action_info(LegacyHumanAction::die);
    }
    switch (actor.current_action) {
      case client_v1::ActorActionKind::walk:
        return legacy_human_action_info(LegacyHumanAction::walk);
      case client_v1::ActorActionKind::run:
      case client_v1::ActorActionKind::rush_kung:
        return legacy_human_action_info(LegacyHumanAction::run);
      case client_v1::ActorActionKind::rush:
        return legacy_human_action_info(LegacyHumanAction::rush_left);
      case client_v1::ActorActionKind::backstep:
      case client_v1::ActorActionKind::knockback:
        return legacy_human_action_info(LegacyHumanAction::walk);
      case client_v1::ActorActionKind::hit:
        if (actor.legacy_action_ident == legacy::kSmHeavyHit) {
          return legacy_human_action_info(LegacyHumanAction::heavy_hit);
        }
        if (actor.legacy_action_ident == legacy::kSmBigHit) {
          return legacy_human_action_info(LegacyHumanAction::big_hit);
        }
        return legacy_human_action_info(LegacyHumanAction::hit);
      case client_v1::ActorActionKind::spell:
        return legacy_human_action_info(LegacyHumanAction::spell);
      case client_v1::ActorActionKind::struck:
        return legacy_human_action_info(LegacyHumanAction::struck);
      case client_v1::ActorActionKind::turn:
      default:
        return legacy_human_action_info(LegacyHumanAction::stand);
    }
  }

  const auto* table =
      legacy_monster_action_table(legacy_race_feature(actor.feature), legacy_appr_feature(actor.feature));
  if (actor.dead) {
    return (*table)[static_cast<std::size_t>(LegacyMonsterAction::die)];
  }
  switch (actor.current_action) {
    case client_v1::ActorActionKind::walk:
    case client_v1::ActorActionKind::run:
    case client_v1::ActorActionKind::rush:
    case client_v1::ActorActionKind::rush_kung:
    case client_v1::ActorActionKind::backstep:
    case client_v1::ActorActionKind::knockback:
      return (*table)[static_cast<std::size_t>(LegacyMonsterAction::walk)];
    case client_v1::ActorActionKind::hit:
    case client_v1::ActorActionKind::spell:
      return (*table)[static_cast<std::size_t>(LegacyMonsterAction::attack)];
    case client_v1::ActorActionKind::struck:
      return (*table)[static_cast<std::size_t>(LegacyMonsterAction::struck)];
    case client_v1::ActorActionKind::turn:
    default:
      return (*table)[static_cast<std::size_t>(LegacyMonsterAction::stand)];
  }
}

}  // namespace

std::optional<int> legacy_click_sound_id(const LegacyClickSound sound) {
  switch (sound) {
    case LegacyClickSound::stone:
      return s_rock_button_click;
    case LegacyClickSound::glass:
      return s_glass_button_click;
    case LegacyClickSound::normal:
      return s_norm_button_click;
    case LegacyClickSound::none:
    default:
      return std::nullopt;
  }
}

int item_click_sound_id(const std::uint8_t std_mode, std::string_view name) {
  (void)name;
  switch (std_mode) {
    case 0:
    case 31:
      return s_click_drug;
    case 5:
    case 6:
      return s_click_weapon;
    case 10:
    case 11:
      return s_click_armor;
    case 22:
    case 23:
      return s_click_ring;
    case 24:
    case 26:
      return s_click_armring;
    case 19:
    case 20:
    case 21:
      return s_click_necklace;
    case 15:
      return s_click_helmet;
    default:
      return s_itmclick;
  }
}

std::optional<int> item_use_sound_id(const std::uint8_t std_mode) {
  switch (std_mode) {
    case 0:
      return s_click_drug;
    case 1:
    case 2:
      return s_eat_drug;
    default:
      return std::nullopt;
  }
}

int footstep_sound_id(const MapCell* cell, const bool running, const bool right_foot) {
  auto sound = base_footstep_for_cell(cell);
  if (running) {
    sound += 2;
  }
  if (right_foot) {
    sound += 1;
  }
  return sound;
}

int human_weapon_sound_id(const int weapon_feature) {
  switch (weapon_feature / 2) {
    case 6:
    case 20:
      return s_hit_short;
    case 1:
    case 27:
    case 28:
      return s_hit_wooden;
    case 2:
    case 5:
    case 9:
    case 13:
    case 14:
    case 22:
    case 25:
      return s_hit_sword;
    case 4:
    case 10:
    case 15:
    case 16:
    case 17:
    case 23:
    case 26:
    case 29:
      return s_hit_do;
    case 3:
    case 7:
    case 11:
      return s_hit_axe;
    case 24:
      return s_hit_club;
    case 8:
    case 12:
    case 18:
    case 21:
      return s_hit_long;
    default:
      return s_hit_fist;
  }
}

std::vector<int> legacy_human_attack_extra_sound_ids(const std::uint16_t legacy_ident,
                                                     const int sex) {
  switch (legacy_ident) {
    case legacy::kSmPowerHit:
      return {(sex & 1) == 0 ? s_yedo_man : s_yedo_woman};
    case legacy::kSmLongHit:
      return {s_longhit};
    case legacy::kSmWideHit:
      return {s_widehit};
    case legacy::kSmFireHit:
      return {s_firehit};
    case legacy::kSmCrossHit:
    case legacy::kSmHit:
    case legacy::kSmHeavyHit:
    case legacy::kSmBigHit:
    default:
      return {};
  }
}

std::optional<int> human_struck_weapon_sound_id(const int attacker_weapon_feature) {
  switch (attacker_weapon_feature / 2) {
    case 6:
    case 20:
      return s_struck_short;
    case 1:
      return s_struck_wooden;
    case 2:
    case 5:
    case 9:
    case 13:
    case 14:
    case 22:
      return s_struck_sword;
    case 4:
    case 10:
    case 15:
    case 16:
    case 17:
    case 23:
      return s_struck_do;
    case 3:
    case 7:
    case 11:
      return s_struck_axe;
    case 24:
      return s_struck_club;
    case 8:
    case 12:
    case 18:
    case 21:
      return s_struck_wooden;
    default:
      return std::nullopt;
  }
}

int human_struck_body_sound_id(const int defender_dress_feature,
                               const int attacker_weapon_feature) {
  const auto armor = defender_dress_feature / 2 == 3;
  switch (attacker_weapon_feature / 2) {
    case 6:
    case 1:
    case 2:
    case 4:
    case 5:
    case 9:
    case 10:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
      return armor ? s_struck_armor_sword : s_struck_body_sword;
    case 3:
    case 7:
    case 11:
      return armor ? s_struck_armor_axe : s_struck_body_axe;
    case 8:
    case 12:
    case 18:
      return armor ? s_struck_armor_longstick : s_struck_body_longstick;
    default:
      return armor ? s_struck_armor_fist : s_struck_body_fist;
  }
}

int human_struck_vocal_sound_id(const int sex) {
  return (sex & 1) == 0 ? s_man_struck : s_wom_struck;
}

int human_die_sound_id(const int sex) {
  return (sex & 1) == 0 ? s_man_die : s_wom_die;
}

int monster_sound_id(const int appearance, const MonsterSoundOffset offset) {
  return sound_id_monster_base(appearance) + static_cast<int>(offset);
}

int magic_sound_id(const int magic_serial, const LegacyMagicSoundPhase phase) {
  auto offset = magic_offset_start;
  switch (phase) {
    case LegacyMagicSoundPhase::fire:
      offset = magic_offset_fire;
      break;
    case LegacyMagicSoundPhase::explosion:
      offset = magic_offset_explosion;
      break;
    case LegacyMagicSoundPhase::start:
    default:
      offset = magic_offset_start;
      break;
  }
  return sound_id_magic_base(std::max(0, magic_serial)) + offset;
}

int actor_local_frame_for_sound(const ActorState& actor, const ActorRenderPose& pose) {
  const auto action = audio_action_info_for(actor);
  const auto dir = pose.dir % 8U;
  const auto start = legacy_frame_index(action, dir, 0);
  return pose.current_frame - start;
}

}  // namespace mir2::client
