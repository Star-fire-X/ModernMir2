// ============================================================
// Mir2 现代客户端 — 音效 ID 常量定义
// 职责：1:1 翻译 Delphi SoundUtil.pas 中的全部 s_* 常量、
//       BGM 路径常量、怪物/魔法音效公式
//
// 对应关系：
//   Delphi: SoundUtil.pas lines 29-206 (const 和 var 声明)
//   常量值 1-145 来自 sound.lst 中的固定映射
//   怪物ID公式: 200 + Appearance * 10 + offset
//   魔法ID公式: 10000 + MagicSerial * 10 + offset
// ============================================================
#pragma once

#include <string>

namespace mir2::client {

// ---- 脚步声 (Footstep Sounds, IDs 1-32) ----
// 地面类型: ground=地面, stone=石头, lawn=草地, rough=粗糙地面
//           wood=木头, cave=洞穴, room=室内, water=水面
// 变体: L=左声道/左脚, R=右声道/右脚
//       Walk=走路, Run=跑步(Walk+2)

constexpr int s_walk_ground_l  = 1;
constexpr int s_walk_ground_r  = 2;
constexpr int s_run_ground_l   = 3;
constexpr int s_run_ground_r   = 4;
constexpr int s_walk_stone_l   = 5;
constexpr int s_walk_stone_r   = 6;
constexpr int s_run_stone_l    = 7;
constexpr int s_run_stone_r    = 8;
constexpr int s_walk_lawn_l    = 9;
constexpr int s_walk_lawn_r    = 10;
constexpr int s_run_lawn_l     = 11;
constexpr int s_run_lawn_r     = 12;
constexpr int s_walk_rough_l   = 13;
constexpr int s_walk_rough_r   = 14;
constexpr int s_run_rough_l    = 15;
constexpr int s_run_rough_r    = 16;
constexpr int s_walk_wood_l    = 17;
constexpr int s_walk_wood_r    = 18;
constexpr int s_run_wood_l     = 19;
constexpr int s_run_wood_r     = 20;
constexpr int s_walk_cave_l    = 21;
constexpr int s_walk_cave_r    = 22;
constexpr int s_run_cave_l     = 23;
constexpr int s_run_cave_r     = 24;
constexpr int s_walk_room_l    = 25;
constexpr int s_walk_room_r    = 26;
constexpr int s_run_room_l     = 27;
constexpr int s_run_room_r     = 28;
constexpr int s_walk_water_l   = 29;
constexpr int s_walk_water_r   = 30;
constexpr int s_run_water_l    = 31;
constexpr int s_run_water_r    = 32;

// ---- 武器挥动音效 (Weapon Hit Sounds, IDs 50-57) ----
// 对应 Delphi: s_hit_short..s_hit_fist

constexpr int s_hit_short    = 50;
constexpr int s_hit_wooden   = 51;
constexpr int s_hit_sword    = 52;
constexpr int s_hit_do       = 53;
constexpr int s_hit_axe      = 54;
constexpr int s_hit_club     = 55;
constexpr int s_hit_long     = 56;
constexpr int s_hit_fist     = 57;

// ---- 被格挡音效 (Struck/Blocked Sounds, IDs 60-65) ----
// 对应 Delphi: s_struck_short..s_struck_club

constexpr int s_struck_short  = 60;
constexpr int s_struck_wooden = 61;
constexpr int s_struck_sword  = 62;
constexpr int s_struck_do     = 63;
constexpr int s_struck_axe    = 64;
constexpr int s_struck_club   = 65;

// ---- 身体被击中音效 (Body Hit Sounds, IDs 70-73) ----
// 对应 Delphi: s_struck_body_sword..s_struck_body_fist

constexpr int s_struck_body_sword     = 70;
constexpr int s_struck_body_axe       = 71;
constexpr int s_struck_body_longstick = 72;
constexpr int s_struck_body_fist      = 73;

// ---- 盔甲被击中音效 (Armor Hit Sounds, IDs 80-83) ----
// 对应 Delphi: s_struck_armor_sword..s_struck_armor_fist

constexpr int s_struck_armor_sword     = 80;
constexpr int s_struck_armor_axe       = 81;
constexpr int s_struck_armor_longstick = 82;
constexpr int s_struck_armor_fist      = 83;

// ---- 环境音效 (Environment Sounds, IDs 91-92) ----

constexpr int s_strike_stone     = 91;
constexpr int s_drop_stonepiece  = 92;

// ---- UI / 界面音效 (UI Sounds, IDs 100-118) ----
// 对应 Delphi: s_rock_door_open..s_itmclick
// Note: s_intro_theme 和 s_main_theme 在 Delphi 中都定义为 102 (别名)

constexpr int s_rock_door_open      = 100;
constexpr int s_meltstone           = 101;
constexpr int s_intro_theme         = 102;
constexpr int s_main_theme          = 102;
constexpr int s_norm_button_click   = 103;
constexpr int s_rock_button_click   = 104;
constexpr int s_glass_button_click  = 105;
constexpr int s_money               = 106;
constexpr int s_eat_drug            = 107;
constexpr int s_click_drug          = 108;
constexpr int s_spacemove_out       = 109;
constexpr int s_spacemove_in        = 110;
constexpr int s_click_weapon        = 111;
constexpr int s_click_armor         = 112;
constexpr int s_click_ring          = 113;
constexpr int s_click_armring       = 114;
constexpr int s_click_necklace      = 115;
constexpr int s_click_helmet        = 116;
constexpr int s_click_grobes        = 117;
constexpr int s_itmclick            = 118;

// ---- 角色语音 (Character Vocal Sounds, IDs 130-145) ----
// 对应 Delphi: s_yedo_man..s_wom_die

constexpr int s_yedo_man      = 130;
constexpr int s_yedo_woman    = 131;
constexpr int s_longhit       = 132;
constexpr int s_widehit       = 133;
constexpr int s_rush_l        = 134;
constexpr int s_rush_r        = 135;
constexpr int s_firehit_ready = 136;
constexpr int s_firehit       = 137;
constexpr int s_man_struck    = 138;
constexpr int s_wom_struck    = 139;
constexpr int s_man_die       = 144;
constexpr int s_wom_die       = 145;

// ---- 固化的 BGM 路径常量 (Hardcoded BGM filenames) ----
// 对应 Delphi SoundUtil.pas: bmg_intro..bmg_gameover (lines 30-33)
// 这些路径在运行时通过 resolve_wav_path 连接 asset_root

constexpr const wchar_t* bmg_intro    = L"wav\\log-in-long2.wav";
constexpr const wchar_t* bmg_select   = L"wav\\sellect-loop2.wav";
constexpr const wchar_t* bmg_field    = L"wav\\Field2.wav";
constexpr const wchar_t* bmg_gameover = L"wav\\game over2.wav";

// ---- 动态音效 ID 变量（sound.lst 加载后赋值） ----
// 对应 Delphi SoundUtil.pas lines 150-206: var 声明，初始值 -1
// 在 SoundList::append_hardcoded_extras() 之后由 AudioService 赋值
// 使用 C++17 inline 变量，定义在头文件中，所有 TU 共享同一实例

inline int s_FireFlower_1      = -1;
inline int s_FireFlower_2      = -1;
inline int s_FireFlower_3      = -1;
inline int s_HeroLogIn         = -1;
inline int s_HeroLogOut        = -1;
inline int s_hero_shield       = -1;
inline int s_SelectBoxFlash    = -1;
inline int s_Flashbox          = -1;
inline int s_Openbox           = -1;
inline int s_powerup           = -1;
inline int s_hit_ZRJF_M        = -1;
inline int s_hit_ZRJF_w        = -1;

// ---- 技能音效 (Class Skill Sounds) ----
// 战士 (ZhanShi / Warrior) 技能起始音效
inline int s_cboZs1_start_m    = -1;
inline int s_cboZs1_start_w    = -1;
inline int s_cboZs2_start      = -1;
inline int s_cboZs3_start_m    = -1;
inline int s_cboZs3_start_w    = -1;
inline int s_cboZs4_start      = -1;

// 法师 (FaShi / Wizard) 技能起始和目标音效
inline int s_cboFs1_start      = -1;
inline int s_cboFs1_target     = -1;
inline int s_cboFs2_start      = -1;
inline int s_cboFs2_target     = -1;
inline int s_cboFs3_start      = -1;
inline int s_cboFs3_target     = -1;
inline int s_cboFs4_start      = -1;
inline int s_cboFs4_target     = -1;

// 道士 (DaoShi / Taoist) 技能起始和目标音效
inline int s_cboDs1_start      = -1;
inline int s_cboDs1_target     = -1;
inline int s_cboDs2_start      = -1;
inline int s_cboDs2_target     = -1;
inline int s_cboDs3_start      = -1;
inline int s_cboDs3_target     = -1;
inline int s_cboDs4_start      = -1;
inline int s_cboDs4_target     = -1;

// ---- 怪物音效公式 (Monster Sound ID Formula) ----
// 对应 Delphi Actor.pas lines 1906-1912:
//   appearance := Appearance * 10
//   appearsound   = 200 + appearance + 0  (appear)
//   normalsound   = 200 + appearance + 1  (normal)
//   attacksound   = 200 + appearance + 2  (attack)
//   weaponsound   = 200 + appearance + 3  (weapon)
//   screamsound   = 200 + appearance + 4  (scream)
//   diesound      = 200 + appearance + 5  (die)
//   die2sound     = 200 + appearance + 6  (die2)
//
// 示例: Appearance=11 → base=310, sounds at 310-316

constexpr int sound_id_monster_base(int appearance) {
  return 200 + appearance * 10;
}

enum MonsterSoundOffset {
  monster_offset_appear = 0,
  monster_offset_normal = 1,
  monster_offset_attack = 2,
  monster_offset_weapon = 3,
  monster_offset_scream = 4,
  monster_offset_die    = 5,
  monster_offset_die2   = 6,
};

// ---- 魔法音效公式 (Magic Sound ID Formula) ----
// 对应 Delphi Actor.pas lines 1880-1882:
//   magicstartsound    = 10000 + MagicSerial * 10 + 0
//   magicfiresound     = 10000 + MagicSerial * 10 + 1
//   magicexplosionsound= 10000 + MagicSerial * 10 + 2
//
// 示例: MagicSerial=1 (Fireball) → 10010/10011/10012

constexpr int sound_id_magic_base(int magic_serial) {
  return 10000 + magic_serial * 10;
}

enum MagicSoundOffset {
  magic_offset_start    = 0,
  magic_offset_fire     = 1,
  magic_offset_explosion = 2,
};

// ---- 脚步声地形搜索辅助 ----
// 对应 Delphi Actor.pas 中的地形类型 → 脚步声常量映射
// terrain_type: 0=ground, 1=stone, 2=lawn, 3=rough,
//               4=wood, 5=cave, 6=room, 7=water

constexpr int terrain_footstep_walk_l[8] = {
  s_walk_ground_l, s_walk_stone_l, s_walk_lawn_l, s_walk_rough_l,
  s_walk_wood_l,   s_walk_cave_l,  s_walk_room_l, s_walk_water_l,
};

constexpr int terrain_footstep_run_l[8] = {
  s_run_ground_l, s_run_stone_l, s_run_lawn_l, s_run_rough_l,
  s_run_wood_l,   s_run_cave_l,  s_run_room_l, s_run_water_l,
};

// 查找地形对应的脚步声 (L) — 返回 walk 或 run 左声道 ID
// 调用方自行 +1 获得右声道，或 +2 获得 run 右声道
constexpr int footstep_for_terrain(int terrain, bool running) {
  if (terrain < 0 || terrain >= 8) terrain = 0; // fallback to ground
  return running ? terrain_footstep_run_l[terrain] : terrain_footstep_walk_l[terrain];
}

}  // namespace mir2::client
