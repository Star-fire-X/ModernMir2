// ============================================================
// Mir2 现代客户端 — 旧版动画系统声明
// 职责：兼容经典 Delphi 客户端的角色动作动画、魔法特效、
//       怪物动作表、渲染姿态计算、动画时钟和特效管理
//
// 传奇动画系统说明：
// 经典传奇（Mir2）的动画基于精灵序列帧（sprite sheet）。
// 每个角色/怪物有独立的动作表，定义了不同动作的帧范围、
// 播放速度和方向偏移。
//
// 精灵帧索引公式：
//   绝对索引 = start_offset + dir * skip + frame_number
// 其中 start_offset 是动作的起始帧偏移，dir 是方向(0-7)，
// skip 是方向间的帧间距，frame_number 是当前帧序号。
//
// 渲染层级顺序（从下到上）：
//   1. 地面背景（Tiles）
//   2. 地面物件（Objects 中间层）
//   3. 地面物品
//   4. 角色身体（Hum/Hair/Weapon）
//   5. 地面物件（Objects 前景层）
//   6. 特效（魔法/受击效果）
//   7. UI 层
// ============================================================
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "assets/asset_manager.hpp"
#include "game/game_state.hpp"
#include "shared/legacy/map_render_math.hpp"

namespace mir2::client {

/// 人类角色精灵帧跨度：每个外观（服装/武器组合）占用 600 帧
/// 在 Hum.wil 中，每 600 帧为一个完整的外观循环（10 方向 * 60 帧/方向）
constexpr int kLegacyHumanFrameSpan = 600;
/// NPC 精灵帧跨度：每个外观占用 60 帧
constexpr int kLegacyMerchantFrameSpan = 60;

class SoftwareRenderer;

/// 旧版动作信息：定义动作在精灵表中的位置和播放参数
/// 对应 Delphi 客户端的 TActorInfo 结构
struct LegacyActionInfo {
  int start{0};              ///< 精灵帧起始偏移（在 SpriteSheet 中的绝对索引）
  int frame{1};              ///< 总帧数
  int skip{0};               ///< 方向间跳过的帧数（= 每方向帧数，大部分动作 = 每方向帧数）
  std::uint64_t frame_time_ms{200};  ///< 每帧持续时间（毫秒）
  int use_tick{0};           ///< 使用时钟 tick 计数（旧版兼容，部分动作依赖 tick 而非真实时间）
};

/// 旧版人类动作枚举：对应 kHumanActions 表的索引
/// 每个枚举值对应一个动作在精灵表中的参数
enum class LegacyHumanAction : std::uint8_t {
  stand,         ///< 站立待机（4 帧循环）
  walk,          ///< 行走（6 帧，每方向 2 帧偏移）
  run,           ///< 跑步（6 帧）
  rush_left,     ///< 左冲刺（预留，用于快速闪避）
  rush_right,    ///< 右冲刺（预留）
  war_mode,      ///< 战斗姿态（持武器待机，1 帧静态）
  hit,           ///< 普通攻击（近战）
  heavy_hit,     ///< 重击
  big_hit,       ///< 大伤害攻击（如技能）
  fire_hit_ready,///< 远程攻击准备（如弓箭/魔法弹道准备动作）
  spell,         ///< 施法
  sitdown,       ///< 坐下（回复 HP/MP）
  struck,        ///< 受击（被攻击后的后仰）
  die,           ///< 死亡（倒地）
};

/// 旧版怪物动作枚举：对应怪物动作表
/// 怪物的动作比人类少（无坐下/战斗姿态等）
enum class LegacyMonsterAction : std::uint8_t {
  stand,     ///< 站立待机
  walk,      ///< 行走
  attack,    ///< 攻击
  critical,  ///< 暴击（部分怪物使用特殊的暴击动作）
  struck,    ///< 受击
  die,       ///< 死亡
  death,     ///< 死亡后骨架状态（永久静止或逐渐消失）
};

/// 旧版怪物动作表：每个怪物 race 对应一个包含 7 种动作的数组
/// 怪物的动作表由其 race（种族）和 appearance（外观编号）决定
using LegacyMonsterActionTable =
    std::array<LegacyActionInfo, static_cast<std::size_t>(LegacyMonsterAction::death) + 1U>;

/// 旧版人类外观信息：从 32 位 feature 值解码得到
/// feature 编码格式（与 Delphi 客户端的编码一致）：
///   bit 0-7:   race
///   bit 8-15:  weapon
///   bit 16-23: hair
///   bit 24-31: dress
struct LegacyHumanAppearance {
  int race{0};          ///< 种族（人/精灵等，影响动作表选择）
  int dress{0};         ///< 服装编号（Hum.wil 中外观索引）
  int weapon{0};        ///< 武器编号（Weapon.wil 中武器精灵索引）
  int hair{0};          ///< 发型编号（Hair.wil 中发型索引）
  int sex{0};           ///< 性别（0=男, 1=女）
  int appearance{0};    ///< 外观值（高 16 位的组合值，用于 NPC/怪物）
  int body_offset{0};   ///< 身体精灵在 Hum.wil 中的偏移（= dress * frame_span）
  int hair_offset{-1};  ///< 头发精灵在 Hair.wil 中的偏移（-1=无头发，如光头/头盔）
  int weapon_offset{0}; ///< 武器精灵在 Weapon.wil 中的偏移
};

/// 旧版位移偏移结果：移动插值计算的坐标和像素偏移
/// 在行走/跑步时，角色在瓦片之间的平滑移动通过此结构实现
struct LegacyShiftResult {
  int rx{0};       ///< 插值后的瓦片 X（位置取整后的坐标）
  int ry{0};       ///< 插值后的瓦片 Y
  int shift_x{0};  ///< X 方向像素偏移（子像素插值，范围为 -kUnitX 到 kUnitX）
  int shift_y{0};  ///< Y 方向像素偏移
};

/// 角色渲染姿态：由 LegacyActorAnimation 计算得出的当前渲染参数
/// 包含精灵帧索引、坐标偏移、透明度等信息
struct ActorRenderPose {
  ArchiveId body_archive{ArchiveId::hum};  ///< 身体精灵所在归档
  int rx{0};             ///< 渲染 X 坐标（瓦片）
  int ry{0};             ///< 渲染 Y 坐标
  int shift_x{0};        ///< 渲染 X 像素偏移（子像素平滑）
  int shift_y{0};        ///< 渲染 Y 像素偏移
  int down_draw_level{0}; ///< Delphi DownDrawLevel：仅影响逐行绘制归属行
  int body_index{-1};    ///< 身体帧在归档中的绝对索引
  int hair_index{-1};    ///< 头发帧索引（-1=无头发）
  int weapon_index{-1};  ///< 武器帧索引（-1=无武器）
  bool weapon_before_body{false};  ///< 武器是否绘制在身体之前（根据武器顺序位掩码判断）
  bool color_effect{false};       ///< 是否应用颜色特效（如隐身/中毒变色）
  bool dead{false};               ///< 是否死亡
  bool visible{true};             ///< 是否可见（隐身或死亡后不可见）
  std::uint8_t alpha{255};        ///< 透明度（0-255，用于隐身/渐隐效果）
  std::uint8_t dir{0};            ///< 面向方向（0-7）
  int current_frame{0};           ///< 当前帧序号（动作内的帧序号，非绝对索引）
};

/// 魔法音效事件：动画/特效系统只产生事件，不直接依赖 AudioService
enum class LegacyMagicAudioCuePhase : std::uint8_t {
  fire,
  explosion,
};

struct LegacyMagicAudioCue {
  std::uint64_t owner_actor_id{0};
  int magic_id{0};
  LegacyMagicAudioCuePhase phase{LegacyMagicAudioCuePhase::fire};
};

/// 旧版魔法类型枚举：定义魔法效果的表现形式
/// 每种魔法根据此类型决定其精灵动画的播放方式
enum class LegacyMagicType : std::uint8_t {
  ready,              ///< 准备动作（施法者身上的蓄力效果）
  fly,                ///< 飞行弹道（从施法者飞向目标的弹道）
  explosion,          ///< 爆炸效果（在目标位置爆炸）
  fly_axe,            ///< 飞行斧头（特殊弹道，旋转斧头）
  fire_wind,          ///< 火焰风（持续喷射的火焰效果）
  fire_gun,           ///< 火焰枪（直线火焰射击）
  lighting_thunder,   ///< 闪电雷击（从天而降的闪电）
  thunder,            ///< 雷击（单体雷电术）
  explo_bujauk,       ///< 爆炸（特定魔法效果）
  bujauk_ground_effect,///< 地面效果（特定魔法的地面残留）
  kyul_kai,           ///< 特定魔法类型
  fly_arrow,          ///< 飞行箭矢（弓箭手的箭矢弹道）
  fire_ball,          ///< 火球（飞行的火球弹道）
  ground_effect,      ///< 地面持续效果（如火墙的地面火焰）
  fire_thunder,       ///< 火焰雷电（组合效果）
};

/// 旧版魔法效果基础信息：精灵归档和帧基址
struct LegacyMagicEffectBase {
  ArchiveId archive{ArchiveId::magic};  ///< 魔法效果所在精灵归档
  int frame_base{0};                    ///< 帧基址偏移（在归档中的起始帧）
};

/// 从 race/dress/weapon/hair 编码为 32 位 feature 值
[[nodiscard]] std::int32_t make_legacy_feature(std::uint8_t race, std::uint8_t dress,
                                               std::uint8_t weapon, std::uint8_t face);
/// 从 feature 解码 race（低 8 位）
[[nodiscard]] std::uint8_t legacy_race_feature(std::int32_t feature);
/// 从 feature 解码 dress（bit 24-31）
[[nodiscard]] std::uint8_t legacy_dress_feature(std::int32_t feature);
/// 从 feature 解码 weapon（bit 8-15）
[[nodiscard]] std::uint8_t legacy_weapon_feature(std::int32_t feature);
/// 从 feature 解码 hair（bit 16-23）
[[nodiscard]] std::uint8_t legacy_hair_feature(std::int32_t feature);
/// 从 feature 解码 appearance（高 16 位）
[[nodiscard]] std::uint16_t legacy_appr_feature(std::int32_t feature);
/// 完整解码 feature 到 LegacyHumanAppearance 结构
[[nodiscard]] LegacyHumanAppearance decode_legacy_human_feature(std::int32_t feature);

/// 获取人类指定动作的信息（从 kHumanActions 表中查询）
[[nodiscard]] const LegacyActionInfo& legacy_human_action_info(LegacyHumanAction action);
/// 获取怪物指定 race/appearance 的动作表
[[nodiscard]] const LegacyMonsterActionTable* legacy_monster_action_table(int race, int appearance);
/// 计算动作帧在精灵表中的绝对索引
/// @param action 动作信息
/// @param dir 方向（0-7）
/// @param local_frame 动作内的帧序号（从 0 开始）
[[nodiscard]] int legacy_frame_index(const LegacyActionInfo& action, std::uint8_t dir,
                                     int local_frame);
/// 判断武器是否绘制在身体之前（根据武器顺序位掩码）
/// 有些武器（如盾牌）应在身体后方绘制，有些（如长刀）应在身体前方
[[nodiscard]] bool legacy_weapon_before_body(int sex, int current_frame);
/// 计算移动插值的瓦片偏移和像素偏移
/// @param x, y 当前瓦片坐标
/// @param dir 移动方向
/// @param step 移动步数
/// @param cur 当前插值进度
/// @param max 插值总进度
[[nodiscard]] LegacyShiftResult legacy_shift(int x, int y, std::uint8_t dir, int step,
                                             int cur, int max);
/// 计算飞行弹道的 16 方向编号
/// 传奇的魔法飞行弹道使用 16 方向（而角色使用 8 方向），
/// 以获得更精细的弹道路径
[[nodiscard]] int legacy_fly_direction16(int sx, int sy, int tx, int ty);
/// 获取魔法的效果基址和归档（根据魔法 ID 和效果类型）
[[nodiscard]] LegacyMagicEffectBase legacy_magic_effect_base(int magic_id, int effect_type);
/// 获取地图物件的动画帧索引（根据单元格的 ani_frame/ani_tick）
[[nodiscard]] int legacy_map_object_frame(const MapCell& cell, int main_ani_count);
/// 判断地图物件是否使用混合模式渲染（如水面/火焰等半透明物件）
[[nodiscard]] bool legacy_map_object_blend(const MapCell& cell);
/// 根据怪物 appearance 返回对应的精灵归档（如 Mon1.wil ~ Mon21.wil）
[[nodiscard]] ArchiveId legacy_mon_archive_for_appearance(int appearance);
/// 根据怪物 appearance 计算精灵帧偏移量
[[nodiscard]] int legacy_monster_offset(int appearance);
/// 根据 NPC appearance 计算精灵帧偏移量
[[nodiscard]] int legacy_npc_offset(int appearance);

/// 动画时钟：管理两种时钟 tick
/// 经典传奇使用基于 tick 的动画驱动方式（不依赖真实时间），
/// 本实现同时支持真实时间（毫秒）和 tick 计数
///
/// - move_tick：每 100ms 一次，驱动移动帧推进
/// - ani_tick：每 50ms 一次，驱动动画帧和地图物件动画
class LegacyAnimationClock {
 public:
  void reset(std::uint64_t now_ms = 0);
  void advance(std::uint64_t now_ms);

  [[nodiscard]] bool move_tick() const { return move_tick_; }
  [[nodiscard]] bool ani_tick() const { return ani_tick_; }
  [[nodiscard]] int move_step_count() const { return move_step_count_; }
  [[nodiscard]] int main_ani_count() const { return main_ani_count_; }

 private:
  bool initialized_{false};       ///< 是否已初始化
  bool move_tick_{false};         ///< 移动 tick 标志（当前帧是否触发了移动 tick）
  bool ani_tick_{false};          ///< 动画 tick 标志
  int move_step_count_{0};        ///< 移动步数计数（用于帧推进）
  int main_ani_count_{0};         ///< 主动画计数（用于地图物件动画帧计算）
  std::uint64_t move_time_ms_{0}; ///< 上次移动 tick 时间
  std::uint64_t ani_time_ms_{0};  ///< 上次动画 tick 时间
};

/// 旧版角色动画状态机：管理单个角色的动作转换和帧推进
///
/// 状态转换图：
///   idle（待机）←→ move（移动）←→ action（动作）
///   idle（待机）←→ action（动作）
///   任何状态都可以被新的动作/移动中断
///
/// 动作结束后自动回到 idle 状态，播放站立/待机动画。
/// 如果角色在 war_mode（战斗姿态）中，idle 状态播放
/// 持武器待机动画而非普通站立动画。
class LegacyActorAnimation {
 public:
  /// 与服务端角色状态同步（检测新动作/新移动）
  void sync_actor(const ActorState& actor, std::uint64_t now_ms);
  /// 更新动画帧（根据时钟 tick 推进帧序号）
  void update(const ActorState& actor, const LegacyAnimationClock& clock,
              std::uint64_t now_ms);

  /// 获取当前角色的渲染姿态（精灵帧索引、坐标偏移等）
  [[nodiscard]] std::optional<ActorRenderPose> pose_for(const ActorState& actor) const;
  [[nodiscard]] std::uint64_t actor_id() const { return actor_id_; }

 private:
  /// 动画运动类型（状态机的三种状态）
  enum class MotionKind {
    idle,   ///< 待机（播放 stand 动画，循环）
    move,   ///< 移动（walk/run 动画，单次播放）
    action  ///< 动作（hit/spell/struck/die 等，单次播放）
  };

  void initialize(const ActorState& actor, std::uint64_t now_ms);
  void begin_move(const ActorState& actor, std::uint64_t now_ms);
  void begin_action(const ActorState& actor, std::uint64_t now_ms);
  void begin_motion(const ActorState& actor, const LegacyActionInfo& action,
                    MotionKind kind, int move_step, std::uint64_t now_ms);
  void refresh_default_frame(const ActorState& actor, std::uint64_t now_ms);
  void reset_default_frame(const ActorState& actor, std::uint64_t now_ms);
  [[nodiscard]] const LegacyActionInfo& stand_action_for(const ActorState& actor) const;
  [[nodiscard]] const LegacyActionInfo& die_action_for(const ActorState& actor) const;
  [[nodiscard]] const LegacyActionInfo& death_action_for(const ActorState& actor) const;
  [[nodiscard]] LegacyActionInfo action_info_for(const ActorState& actor,
                                                 client_v1::ActorActionKind kind) const;
  [[nodiscard]] std::uint8_t frame_dir_for(const ActorState& actor) const;
  [[nodiscard]] int default_frame_for(const ActorState& actor) const;

  bool initialized_{false};        ///< 是否已初始化
  std::uint64_t actor_id_{0};      ///< 关联的角色 ID
  std::uint64_t last_move_started_ms_{0};    ///< 上次移动开始时间（用于检测新移动）
  std::uint64_t last_action_started_ms_{0};  ///< 上次动作开始时间（用于检测新动作）
  client_v1::ActorActionKind last_action_kind_{client_v1::ActorActionKind::turn};
  std::uint16_t last_legacy_ident_{0};  ///< 上次旧版动作标识
  std::uint16_t last_magic_id_{0};      ///< 上次魔法 ID
  bool last_dead_{false};               ///< 上次死亡状态

  MotionKind motion_kind_{MotionKind::idle};  ///< 当前运动类型
  LegacyActionInfo action_{};                  ///< 当前动作信息
  int start_frame_{0};   ///< 起始帧（精灵表中的绝对索引）
  int end_frame_{0};     ///< 结束帧（动作最后一帧的绝对索引 + 1）
  int current_frame_{0}; ///< 当前帧序号（动作内的帧偏移）
  int current_default_frame_{0};  ///< 待机动画的当前帧（循环帧序号）
  int default_frame_count_{1};    ///< 待机动画总帧数
  int move_step_{0};     ///< 移动步数（用于位移插值计算）
  std::uint64_t frame_started_ms_{0};    ///< 当前帧开始时间
  std::uint64_t default_frame_time_ms_{0}; ///< 待机帧切换时间
  std::uint64_t smooth_move_time_ms_{0};  ///< 移动结束时间（用于从移动平滑过渡到待机）
  std::uint64_t war_mode_time_ms_{0};     ///< 战斗模式开始时间
  bool war_mode_{false};  ///< 是否处于战斗模式（攻击/施法后保持持武器姿态一段时间）
  bool dead_{false};      ///< 是否死亡

  int xx_{0};             ///< 当前 X 坐标（内部副本，用于检测坐标变化）
  int yy_{0};             ///< 当前 Y 坐标
  std::uint8_t dir_{0};   ///< 当前方向
  LegacyShiftResult shift_{};  ///< 当前位移偏移（插值结果）
};

/// 旧版特效管理器：管理地面特效、角色附着特效、飞行魔法特效和叠加特效
/// 对应 Delphi 客户端的特效管理和渲染逻辑
class LegacyEffectManager {
 public:
  /// 特效类型枚举
  enum class EffectKind : std::uint8_t {
    map,            ///< 地图地面特效（如火墙的地面火焰、毒云）
    char_attached,  ///< 附着在角色上的特效（如受击光效、护盾、隐身效果）
    magic,          ///< 魔法飞行/爆炸特效（火球飞行、雷电落下）
    normal_draw,    ///< 普通叠加绘制特效（不受视角影响的辅助特效）
  };

  /// 单个特效实例
  struct Effect {
    ArchiveId archive{ArchiveId::effect};  ///< 精灵归档
    EffectKind kind{EffectKind::map};      ///< 特效类型
    LegacyMagicType magic_type{LegacyMagicType::explosion};  ///< 魔法类型
    int effect_base{0};     ///< 特效帧基址（在归档中的起始帧偏移）
    int explosion_base{0};  ///< 爆炸帧基址（飞行弹道到达目标后的爆炸帧偏移）
    int start_frame{0};     ///< 起始帧（播放开始时的帧序号）
    int current_frame{0};   ///< 当前帧序号
    int frame_count{0};     ///< 总帧数
    int x{0};               ///< 源 X（瓦片坐标，特效的起始位置）
    int y{0};               ///< 源 Y
    int rx{0};              ///< 渲染 X（瓦片坐标，飞行过程中的实时位置）
    int ry{0};              ///< 渲染 Y
    int target_x{0};        ///< 目标 X（瓦片坐标）
    int target_y{0};        ///< 目标 Y
    int fire_x{0};          ///< 发射点世界 X（像素坐标，用于弹道计算）
    int fire_y{0};          ///< 发射点世界 Y
    int fly_x{0};           ///< 飞行中世界 X（像素坐标，弹道插值用）
    int fly_y{0};           ///< 飞行中世界 Y
    int firedis_x{0};       ///< X 方向飞行位移增量（每帧移动量）
    int firedis_y{0};       ///< Y 方向飞行位移增量
    int prev_distance_x{99999};  ///< 上次距目标的 X 距离（用于越过目标的判定）
    int prev_distance_y{99999};  ///< 上次距目标的 Y 距离
    int explosion_frame_count{10};  ///< 爆炸效果帧数
    int repeat_count{0};    ///< 重复次数（循环播放的地面特效）
    int magic_id{0};        ///< 魔法 ID（用于爆炸音效回调）
    int server_magic_id{0}; ///< 服务端魔法 ID（用于与服务端同步删除特效）
    std::uint64_t owner_actor_id{0};  ///< 施法者角色 ID
    std::uint64_t target_actor_id{0}; ///< 目标角色 ID
    std::uint64_t spawned_ms{0};      ///< 生成时间戳
    std::uint64_t frame_step_ms{0};   ///< 帧更新时间戳
    std::uint64_t next_frame_ms{30};  ///< 帧间隔（毫秒，两帧之间的等待时间）
    bool fixed_effect{true};  ///< 是否固定位置（false 表示飞行弹道，位置会变化）
    bool repetition{false};   ///< 是否循环播放（如火墙持续燃烧）
    bool blend{true};         ///< 是否使用混合模式（半透明，如火焰/光效）
    bool active{true};        ///< 是否激活（false 表示播放完毕可移除）
    std::uint8_t dir16{0};    ///< 16 方向编号（飞行弹道的方向）
    int light{0};             ///< 光照值（影响周围环境的亮度）

    /// 计算当前帧在精灵表中的实际绝对索引
    [[nodiscard]] int draw_frame_index() const;
  };

  /// 魔法创建参数：用于 spawn_magic_effect 的入参聚合
  struct MagicCreate {
    int magic_id{0};           ///< 魔法 ID
    int server_magic_id{0};    ///< 服务端魔法 ID
    ArchiveId archive{ArchiveId::magic};  ///< 精灵归档
    int effect_base{-1};       ///< 效果帧基址（-1 表示自动计算）
    int source_x{0};           ///< 源 X
    int source_y{0};           ///< 源 Y
    int target_x{0};           ///< 目标 X
    int target_y{0};           ///< 目标 Y
    std::uint64_t owner_actor_id{0};   ///< 施法者 ID
    std::uint64_t target_actor_id{0};  ///< 目标角色 ID
    LegacyMagicType magic_type{LegacyMagicType::fly};  ///< 魔法类型
    bool repetition{true};     ///< 是否循环
    std::uint64_t now_ms{0};   ///< 当前时间
    std::uint64_t next_frame_ms{50};  ///< 帧间隔（毫秒）
  };

  void clear();
  void spawn_map_effect(const Effect& effect);
  void spawn_char_effect(std::uint64_t actor_id, const Effect& effect);
  void spawn_magic_effect(const Effect& effect);
  Effect& spawn_map_effect(ArchiveId archive, int effect_base, int frame_count, int x, int y,
                           std::uint64_t now_ms, std::uint64_t next_frame_ms = 30,
                           int repeat_count = 0);
  Effect& spawn_char_effect(std::uint64_t actor_id, ArchiveId archive, int effect_base,
                            int frame_count, std::uint64_t now_ms,
                            std::uint64_t next_frame_ms = 30);
  Effect& spawn_magic_effect(const MagicCreate& create);
  Effect& spawn_normal_draw_effect(ArchiveId archive, int effect_base, int frame_count, int x,
                                   int y, std::uint64_t now_ms,
                                   std::uint64_t next_frame_ms, bool blend);
  void del_magic(int server_magic_id);
  void update(std::uint64_t now_ms);
  [[nodiscard]] std::vector<LegacyMagicAudioCue> drain_magic_audio_cues();
  void render_ground(AssetManager& assets, SoftwareRenderer& renderer,
                     const legacy::LegacyMapViewport& viewport) const;
  void render_fly(AssetManager& assets, SoftwareRenderer& renderer,
                  const legacy::LegacyMapViewport& viewport, int row) const;
  void render_overlay_for_actor(std::uint64_t actor_id, const ActorRenderPose& pose,
                                AssetManager& assets, SoftwareRenderer& renderer,
                                const legacy::LegacyMapViewport& viewport) const;
  void render_overlay(AssetManager& assets, SoftwareRenderer& renderer,
                      const legacy::LegacyMapViewport& viewport) const;

  [[nodiscard]] std::size_t ground_count() const { return ground_effects_.size(); }
  [[nodiscard]] std::size_t overlay_count() const {
    return char_effects_.size() + overlay_effects_.size();
  }
  [[nodiscard]] std::size_t fly_count() const { return fly_effects_.size(); }
  [[nodiscard]] const std::vector<Effect>& ground_effects() const { return ground_effects_; }
  [[nodiscard]] const std::vector<Effect>& fly_effects() const { return fly_effects_; }

 private:
  /// 角色附着特效结构（关联到特定角色的特效）
  struct CharEffect {
    std::uint64_t actor_id{0};
    Effect effect{};
  };

  std::vector<Effect> ground_effects_{};      ///< 地面特效列表（如火墙、毒云）
  std::vector<CharEffect> char_effects_{};    ///< 角色附着特效列表（如护盾、隐身）
  std::vector<Effect> overlay_effects_{};     ///< 叠加特效列表（UI 层特效）
  std::vector<Effect> fly_effects_{};         ///< 飞行魔法特效列表（弹道）
  std::vector<LegacyMagicAudioCue> magic_audio_cues_{};
};

/// 动画管理器：整合动画时钟、角色动画和特效管理
/// 每帧调用顺序：sync_world → update → pose_for / render_*
/// 这是客户端动画系统的总入口，WorldScene 通过它驱动所有动画
class AnimationManager {
 public:
  /// 重置所有动画状态（场景切换时调用）
  void reset(std::uint64_t now_ms = 0);
  /// 与世界状态同步：根据服务端下发的角色快照更新动画状态
  void sync_world(const WorldViewState& world, std::uint64_t now_ms);
  /// 更新所有动画和特效（驱动帧推进、检查魔法效果生成等）
  void update(const WorldViewState& world, std::uint64_t now_ms);
  [[nodiscard]] std::vector<LegacyMagicAudioCue> drain_magic_audio_cues();

  /// 获取指定角色的渲染姿态（用于渲染器绘制）
  [[nodiscard]] std::optional<ActorRenderPose> pose_for(std::uint64_t actor_id) const;
  /// 获取地图物件的动画帧索引
  [[nodiscard]] int map_object_frame(const MapCell& cell) const;
  /// 判断地图物件是否使用混合模式渲染
  [[nodiscard]] bool map_object_blend(const MapCell& cell) const;
  [[nodiscard]] const LegacyAnimationClock& clock() const { return clock_; }
  [[nodiscard]] LegacyEffectManager& effects() { return effects_; }

 private:
  /// 检查并生成法术特效：当角色开始施法时创建对应的魔法效果
  void spawn_spell_effects(const WorldViewState& world, std::uint64_t now_ms);

  LegacyAnimationClock clock_{};  ///< 动画时钟（驱动所有帧推进）
  LegacyEffectManager effects_{}; ///< 特效管理器
  std::unordered_map<std::uint64_t, ActorState> actor_snapshots_{};  ///< 角色快照缓存（用于 pose_for 查询）
  std::unordered_map<std::uint64_t, LegacyActorAnimation> actors_{}; ///< 各角色的动画状态机
  std::unordered_map<std::uint64_t, std::uint64_t> spell_effect_started_ms_{};  ///< 上次生成法术特效的时间（防重复生成）
  std::vector<LegacyMagicAudioCue> magic_audio_cues_{};
};

}  // namespace mir2::client
