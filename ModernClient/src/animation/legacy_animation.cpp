// ============================================================
// Mir2 现代客户端 — 旧版动画系统实现
// 职责：兼容经典 Delphi 客户端的角色动作/帧计算、怪物动作表、
//       移动插值偏移、魔法特效飞行与爆炸、动画时钟驱动
//
// 本文件是客户端动画系统的核心实现，包含：
//   1. 人类和怪物的动作表定义（kHumanActions / kMA* 系列）
//   2. 精灵帧索引计算（方向偏移 + 帧内偏移的复合公式）
//   3. 移动插值偏移计算（legacy_shift，8 方向独立公式）
//   4. 魔法弹道飞行与爆炸（16 方向弹道、线性插值、超时回收）
//   5. 地图物件动画帧计算（ani_frame/ani_tick 取模驱动）
//   6. 动画时钟（move_tick 每 100ms / ani_tick 每 50ms）
//   7. 角色动画状态机（idle → move → action 状态转换）
//   8. 特效管理器（地面/角色附着/飞行/叠加四种特效生命周期）
//   9. AnimationManager 整合（sync_world → update → pose_for）
//
// 与经典 Delphi 客户端的差异：
//   - 经典客户端使用基于 tick 的动画（响应时间片中断），
//     本实现基于真实时间（std::uint64_t now_ms 毫秒时间戳）
//   - 方向计算公式完全复现 Delphi 的 nearbyint 取整行为
//   - 怪物动作表（race→table 映射）与 Delphi 完全一致
// ============================================================

#include "animation/legacy_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "render/software_renderer.hpp"
#include "shared/legacy/action_ids.hpp"
#include "shared/legacy/movement_rules.hpp"

namespace mir2::client {

namespace {

bool legacy_anim_trace_flag_cached(const char* name) {
  const auto* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void legacy_anim_trace_write_line(const std::string& line) {
  const auto* file_path = std::getenv("MIR2_ANIM_TRACE_FILE");
  if (file_path == nullptr || file_path[0] == '\0') {
    file_path = std::getenv("MIR2_LEGACY_TRACE_FILE");
  }
  if (file_path == nullptr || file_path[0] == '\0') {
    return;
  }
  std::ofstream file(file_path, std::ios::app | std::ios::binary);
  file << line;
}

}  // namespace

bool legacy_anim_trace_enabled() {
  static const bool enabled =
      legacy_anim_trace_flag_cached("MIR2_ANIM_TRACE") ||
      legacy_anim_trace_flag_cached("MIR2_LEGACY_TRACE");
  return enabled;
}

void legacy_anim_trace_record(const LegacyAnimTraceRecord& record) {
  if (!legacy_anim_trace_enabled()) {
    return;
  }
  std::ostringstream out;
  out << "[mir2-anim]"
      << " frame=" << record.frame_index
      << " t=" << record.now_ms
      << " stage=" << record.stage
      << " reason=" << record.reason
      << " actor=" << record.actor_id
      << " actor_type=" << record.actor_type
      << " action=" << record.action_state
      << " dir=" << record.direction
      << " logical_pos=" << record.logical_x << "," << record.logical_y
      << " render_offset=" << record.render_offset_x << "," << record.render_offset_y
      << " animation_start_time=" << record.animation_start_time
      << " animation_frame_index=" << record.animation_frame_index
      << " animation_frame_interval=" << record.animation_frame_interval
      << " action_finished=" << (record.action_finished ? 1 : 0)
      << " effect_id=" << record.effect_id
      << " effect_type=" << record.effect_type
      << " effect_owner=" << record.effect_owner
      << " effect_target=" << record.effect_target
      << " effect_pos=" << record.effect_x << "," << record.effect_y
      << " effect_frame_index=" << record.effect_frame_index
      << " effect_create_frame=" << record.effect_create_frame
      << " effect_destroy_frame=" << record.effect_destroy_frame
      << " render_layer=" << record.render_layer
      << " same_frame_visible=" << (record.same_frame_visible ? 1 : 0)
      << '\n';
  legacy_anim_trace_write_line(out.str());
}

namespace {

// ====================================================================
// 人类动作表（kHumanActions）
// 每个元素：{起始偏移, 帧数, 方向跳帧, 每帧毫秒, 时钟 tick}
// 索引对应 LegacyHumanAction 枚举值
//
// 精灵表布局说明（以 Hum.wil/Hair.wil/Weapon.wil 为例）：
//   每 600 帧（kLegacyHumanFrameSpan）为一个完整的外观循环。
//   每个外观内，各动作按固定偏移排列，偏移值从 0 到 599。
//   帧索引公式：绝对索引 = dress * 600 + action_start + dir * (frame + skip) + local_frame
//
// 方向跳帧（skip）的作用：
//   skip > 0 时，每个方向占用 (frame + skip) 个帧槽，但实际有效帧只有 frame 个。
//   多余的帧槽填充空白帧，用于在精灵表中对齐不同动作的长度。
//   例如 walk: frame=6, skip=2 → 每方向占 8 个帧槽，其中 6 个有效帧+2 个空白。
//
// 帧时间差异：
//   stand (200ms) 最慢，spell (60ms) 最快，体现了不同动作的节奏差异。
//   use_tick > 0 时（如 walk/run），由时钟 tick 驱动而非真实时间，
//   这是 Delphi 客户端的遗留机制，本实现在 update() 中兼容处理。
// ====================================================================
constexpr LegacyActionInfo kHumanActions[] = {
    {0, 4, 4, 200, 0},     // stand: 偏移0, 4帧, 方向间隔4帧, 200ms/帧
    {64, 6, 2, 90, 2},     // walk: 偏移64, 6帧, 方向间隔2帧, 90ms/帧, tick驱动
    {128, 6, 2, 120, 3},   // run: 偏移128, 6帧, 方向间隔2帧, 120ms/帧, tick驱动
    {128, 3, 5, 120, 3},   // rush_left: 偏移128, 3帧（左闪避，与 run 共享偏移区）
    {131, 3, 5, 120, 3},   // rush_right: 偏移131, 3帧（右闪避，紧接 rush_left）
    {192, 1, 0, 200, 0},   // war_mode: 偏移192, 1帧（静止持武器姿态，skip=0 无方向偏移）
    {200, 6, 2, 85, 0},    // hit: 偏移200, 6帧, 85ms/帧（普通攻击，最常用的动作）
    {264, 6, 2, 90, 0},    // heavy_hit: 偏移264, 6帧（SM_HEAVYHIT=15）
    {328, 8, 0, 70, 0},    // big_hit: 偏移328, 8帧, 70ms/帧（SM_BIGHIT=16, skip=0 无方向偏移）
    {192, 6, 4, 70, 0},    // fire_hit_ready: 偏移192, 6帧（远程攻击准备，与 war_mode 共享偏移区）
    {392, 6, 2, 60, 0},    // spell: 偏移392, 6帧, 60ms/帧（施法，最快的动作）
    {456, 2, 0, 300, 0},   // sitdown: 偏移456, 2帧, 300ms/帧（坐下回复，skip=0 无方向偏移）
    {472, 3, 5, 70, 0},    // struck: 偏移472, 3帧, 70ms/帧（受击后仰，快速闪白）
    {536, 4, 4, 120, 0},   // die: 偏移536, 4帧, 120ms/帧（死亡倒地，最后展示骨架）
};

// ====================================================================
// 怪物动作表（kMA* 系列）
// 每种怪物 race 有独立的动作表，由 legacy_monster_action_table() 选择。
// 每个表包含 7 个 LegacyActionInfo，对应 LegacyMonsterAction 枚举的 7 种动作。
//
// 格式：{stand, walk, attack, critical, struck, die, death}
// 每个动作：{起始偏移(frame), 帧数, 方向跳帧(skip), 每帧毫秒, 时钟 tick}
//
// 与人类动作表的差异：
//   - 怪物没有坐下/战斗姿态，但有 death（骨架永久状态）和 critical（暴击）
//   - 大部分怪物没有 critical 动作（{0,0,0,0,0} 表示无此动作）
//   - walk 多使用 tick=3 驱动（与时钟同步），attack 使用真实时间
//   - 帧跨度（每方向占用帧槽数）因怪物类型而异（280/360/430/440/350 等）
//
// 各怪物表的主要差异说明：
//   MA9:  最简表，多数动作只有 1 帧，attack 复用 walk 的帧区
//   MA10: 独立帧区的多帧站立（4帧呼吸），attack 独立（4帧）
//   MA11: 6帧攻击（挥舞更完整），10帧死亡（倒地动画更长）
//   MA12: 标准表，各动作帧分布均衡
//   MA13: walk 仅 2 帧（移动更快），death 有独立动画
//   MA14: 大怪物用，death 长达 10 帧
//   MA15: 类似 MA14，death 固定 1 帧
//   MA16: die 使用 4 帧方向动画（与 stand 结构类似）
//   MA17: stand 仅 60ms/帧（快速呼吸，如小怪），attack 使用独立帧区
//   MA19: 通用标准表（60+ 种 race 共用），是最常见的怪物动画模式
//   MA20: death 10 帧 170ms（慢速消散动画）
//   MA21: 同 MA19 但 frame_time 微调
//   MA50: NPC/商人用，所有动作 4 帧 200ms
//   MA51-52: 特殊 NPC appearance 专用表
// ====================================================================

// MA9：通用怪物表 1（多数怪物使用的基础表）
constexpr LegacyMonsterActionTable kMA9 = {{{0, 1, 7, 200, 0},    // stand
                                            {64, 6, 2, 120, 3},   // walk
                                            {64, 6, 2, 150, 0},   // attack
                                            {0, 0, 0, 0, 0},      // critical（无）
                                            {64, 6, 2, 100, 0},   // struck
                                            {0, 1, 7, 140, 0},    // die
                                            {0, 1, 7, 0, 0}}};    // death

// MA10：多帧站立、4帧攻击
constexpr LegacyMonsterActionTable kMA10 = {{{0, 4, 4, 200, 0},
                                             {64, 6, 2, 120, 3},
                                             {128, 4, 4, 150, 0},
                                             {0, 0, 0, 0, 0},
                                             {192, 2, 0, 100, 0},
                                             {208, 4, 4, 140, 0},
                                             {272, 1, 0, 0, 0}}};

// MA11：6帧攻击、10帧死亡
constexpr LegacyMonsterActionTable kMA11 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 120, 3},
                                             {160, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 140, 0},
                                             {340, 1, 0, 0, 0}}};

// MA12：包括 race 24 也使用此表
constexpr LegacyMonsterActionTable kMA12 = {{{0, 4, 4, 200, 0},
                                             {64, 6, 2, 120, 3},
                                             {128, 6, 2, 150, 0},
                                             {0, 0, 0, 0, 0},
                                             {192, 2, 0, 150, 0},
                                             {208, 4, 4, 160, 0},
                                             {272, 1, 0, 0, 0}}};

// MA13：walk 仅 2 帧死亡动画长，death 有独立帧
constexpr LegacyMonsterActionTable kMA13 = {{{0, 4, 6, 200, 0},
                                             {10, 8, 2, 160, 0},
                                             {30, 6, 4, 120, 0},
                                             {0, 0, 0, 0, 0},
                                             {110, 2, 0, 100, 0},
                                             {130, 10, 0, 120, 0},
                                             {20, 9, 0, 150, 0}}};

// MA14：包括 race 17/18/23 也使用此表，death 帧 10 帧
constexpr LegacyMonsterActionTable kMA14 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 120, 0},
                                             {340, 10, 0, 100, 0}}};

// MA15：包括 race 22
constexpr LegacyMonsterActionTable kMA15 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 120, 0},
                                             {1, 1, 0, 100, 0}}};

// MA16：death 为 4 帧
constexpr LegacyMonsterActionTable kMA16 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 160, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 4, 6, 160, 0},
                                             {0, 1, 0, 160, 0}}};

// MA17：包括 race 30/31，stand 60ms/帧（快速待机）
constexpr LegacyMonsterActionTable kMA17 = {{{0, 4, 6, 60, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 100, 0},
                                             {340, 1, 0, 140, 0}}};

// MA19：多数怪物使用的通用表（包括 race 37/40/45/52/53/64-69 等）
constexpr LegacyMonsterActionTable kMA19 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 140, 0},
                                             {340, 1, 0, 140, 0}}};

// MA20：race 41/42，death 10 帧 170ms/帧
constexpr LegacyMonsterActionTable kMA20 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 120, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 100, 0},
                                             {340, 10, 0, 170, 0}}};

// MA21：race 43
constexpr LegacyMonsterActionTable kMA21 = {{{0, 4, 6, 200, 0},
                                             {0, 0, 0, 0, 0},
                                             {10, 6, 4, 120, 0},
                                             {0, 0, 0, 0, 0},
                                             {20, 2, 0, 100, 0},
                                             {30, 10, 0, 160, 0},
                                             {0, 0, 0, 0, 0}}};

// MA22：race 47（石像怪/石像将军），stand 起始于 80
constexpr LegacyMonsterActionTable kMA22 = {{{80, 4, 6, 200, 0},
                                             {160, 6, 4, 160, 3},
                                             {240, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {320, 2, 0, 100, 0},
                                             {340, 10, 0, 160, 0},
                                             {0, 6, 4, 170, 0}}};

// MA23：race 48/49（石像王/Boss），stand 起始于 20
constexpr LegacyMonsterActionTable kMA23 = {{{20, 4, 6, 200, 0},
                                             {100, 6, 4, 160, 3},
                                             {180, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {260, 2, 0, 100, 0},
                                             {280, 10, 0, 160, 0},
                                             {0, 20, 0, 100, 0}}};

// MA24：race 32（蝎子 2 型），含 critical 动作
constexpr LegacyMonsterActionTable kMA24 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 160, 3},
                                             {160, 6, 4, 100, 0},
                                             {240, 6, 4, 100, 0},
                                             {320, 2, 0, 100, 0},
                                             {340, 10, 0, 140, 0},
                                             {420, 1, 0, 140, 0}}};

// MA25：race 33（蜈蚣王），walk 10 帧无方向跳帧
constexpr LegacyMonsterActionTable kMA25 = {{{0, 4, 6, 200, 0},
                                             {70, 10, 0, 200, 3},
                                             {20, 6, 4, 120, 0},
                                             {10, 6, 4, 120, 0},
                                             {50, 2, 0, 100, 0},
                                             {60, 10, 0, 200, 0},
                                             {80, 10, 0, 200, 3}}};

// MA26：race 99（城堡门），可移动门
constexpr LegacyMonsterActionTable kMA26 = {{{0, 1, 7, 200, 0},
                                             {0, 0, 0, 160, 0},
                                             {56, 6, 2, 500, 0},
                                             {64, 6, 2, 500, 0},
                                             {0, 4, 4, 100, 0},
                                             {24, 10, 0, 120, 0},
                                             {0, 0, 0, 150, 0}}};

// MA27：race 98（城墙结构），不可移动
constexpr LegacyMonsterActionTable kMA27 = {{{0, 1, 7, 200, 0},
                                             {0, 0, 0, 160, 0},
                                             {0, 0, 0, 250, 0},
                                             {0, 0, 0, 250, 0},
                                             {0, 0, 0, 100, 0},
                                             {0, 10, 0, 120, 0},
                                             {0, 0, 0, 150, 0}}};

// MA28：race 54（小精灵怪物/女）
constexpr LegacyMonsterActionTable kMA28 = {{{80, 4, 6, 200, 0},
                                             {160, 6, 4, 160, 3},
                                             {0, 6, 4, 100, 0},
                                             {0, 0, 0, 0, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 120, 0},
                                             {0, 10, 0, 100, 0}}};

// MA29：race 55（战士精灵怪物/男），attack 含 critical
constexpr LegacyMonsterActionTable kMA29 = {{{80, 4, 6, 200, 0},
                                             {160, 6, 4, 160, 3},
                                             {240, 6, 4, 100, 0},
                                             {0, 10, 0, 100, 0},
                                             {320, 2, 0, 100, 0},
                                             {340, 10, 0, 120, 0},
                                             {0, 10, 0, 100, 0}}};

// MA30：race 34（大心脏怪物/巨虫），walk 10 帧无方向跳帧
constexpr LegacyMonsterActionTable kMA30 = {{{0, 4, 6, 200, 0},
                                             {0, 10, 0, 200, 3},
                                             {10, 6, 4, 120, 0},
                                             {10, 6, 4, 120, 0},
                                             {20, 2, 0, 100, 0},
                                             {30, 20, 0, 150, 0},
                                             {0, 10, 0, 200, 3}}};

// MA31：race 35（蜘蛛屋怪物）
constexpr LegacyMonsterActionTable kMA31 = {{{0, 4, 6, 200, 0},
                                             {0, 10, 0, 200, 3},
                                             {10, 6, 4, 120, 0},
                                             {0, 6, 4, 120, 0},
                                             {0, 2, 8, 100, 0},
                                             {20, 10, 0, 200, 0},
                                             {0, 10, 0, 200, 3}}};

// MA32：race 36（爆炸蜘蛛），stand 1 帧
constexpr LegacyMonsterActionTable kMA32 = {{{0, 1, 9, 200, 0},
                                             {0, 6, 4, 200, 3},
                                             {0, 6, 4, 120, 0},
                                             {0, 6, 4, 120, 0},
                                             {0, 2, 8, 100, 0},
                                             {80, 10, 0, 80, 0},
                                             {80, 10, 0, 200, 3}}};

// MA33：race 60/61/62/70/71/72（电子蝎子/Boss猪/石像王(大)/般若守卫系列）
constexpr LegacyMonsterActionTable kMA33 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 200, 3},
                                             {160, 6, 4, 120, 0},
                                             {340, 6, 4, 120, 0},
                                             {240, 2, 0, 100, 0},
                                             {260, 10, 0, 200, 0},
                                             {260, 10, 0, 200, 0}}};

// MA34：race 63（骷髅王），die/death 各 20 帧
constexpr LegacyMonsterActionTable kMA34 = {{{0, 4, 6, 200, 0},
                                             {80, 6, 4, 200, 3},
                                             {160, 6, 4, 120, 0},
                                             {320, 6, 4, 120, 0},
                                             {400, 2, 0, 100, 0},
                                             {420, 20, 0, 200, 0},
                                             {420, 20, 0, 200, 0}}};

// MA50：race 50（NPC/商人）
constexpr LegacyMonsterActionTable kMA50 = {{{0, 4, 6, 200, 0},
                                             {0, 0, 0, 0, 0},
                                             {30, 10, 0, 150, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 1, 9, 0, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 0, 0, 0, 0}}};

// MA51：race 50 的特殊外观（appearance==23）
constexpr LegacyMonsterActionTable kMA51 = {{{0, 4, 6, 200, 0},
                                             {0, 0, 0, 0, 0},
                                             {30, 20, 0, 150, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 1, 9, 0, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 0, 0, 0, 0}}};

// MA52：race 50 的特殊外观（appearance==24/25）
constexpr LegacyMonsterActionTable kMA52 = {{{30, 4, 6, 200, 0},
                                             {0, 0, 0, 0, 0},
                                             {30, 4, 6, 150, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 1, 9, 0, 0},
                                             {0, 0, 0, 0, 0},
                                             {0, 0, 0, 0, 0}}};

// ====================================================================
// 武器顺序位掩码
// 两个字符串（索引 0=男性, 1=女性），每个字符对应 kLegacyHumanFrameSpan(600) 帧中的一帧。
// '0' = 武器在身体之前绘制（武器在角色身后，如盾牌/短兵器）
// '1' = 武器在身体之后绘制（武器在角色身前，如长刀/法杖）
//
// 经典传奇中，武器精灵与身体精灵的绘制顺序取决于当前帧：
//   - 正面/侧面行走的某些帧需要武器遮住身体（长刀向前挥舞）
//   - 背向帧需要身体遮住武器（避免武器穿透身体）
// 这个位掩码通过逐帧标记精确控制了绘制顺序，无需运行时计算。
// ====================================================================
constexpr std::string_view kWeaponOrder[2] = {
    "000000001111111111111111111111110000000000001111000011110000111100000000"
    "111111111111111111111111000000000000000100000001000000010000000011111111"
    "111111111111111100111111001110010000000100000001011100001110001111100000"
    "111000001111111111111111111000000000000011110011011000110110001111100000"
    "111001111111111101111111000111000111101111010000110000001111100011001000"
    "111000010110000000001110111110000000001100000011000000111000011111111111"
    "011111110011001100010011001011111100010000011111111111111111111111111111"
    "000111110001111100011111000111110011111101111111111111111111111100011111"
    "000111110001111100011111",
    "000000001111111111111111111111110000000000001111000011110000111100000000"
    "111111111111111111111111000000000000000100000001000000010000000011111111"
    "111111111111111100111111001110010000000100000001111100001110001111100000"
    "111000001111111111111111111000000000000011110011011000110110001111100000"
    "111001111111111101111111000111000111101111010000110000001111100011001000"
    "111000010110000000001110111110000000001100000011000000111000011111111111"
    "011111110011001100010011001011111100010000011111111111111111111111111111"
    "000111110001111100011111000111110011111101111111111111111111111100011111"
    "000111110001111100011111"};
static_assert(kWeaponOrder[0].size() == kLegacyHumanFrameSpan);
static_assert(kWeaponOrder[1].size() == kLegacyHumanFrameSpan);

// ====================================================================
// 坐标系统和常量
//
// 传奇使用等距（isometric）风格的 2D 坐标系统：
//   瓦片宽度 48 像素（X 轴方向），瓦片高度 32 像素（Y 轴方向）。
//   地图向右下方倾斜，每个瓦片两个菱形边分别是 48×32 的矩形对角线。
//
// 世界坐标 → 屏幕坐标的映射：
//   screen_x = world_x - left * 48 + kLegacyDefX
//   screen_y = world_y - top  * 32 + kLegacyDefY
// 其中 (left, top) 为当前可视区域左上角瓦片坐标。
// ====================================================================

constexpr int kLegacyUnitX = 48;   ///< 每个瓦片的像素宽度（经典客户端标准值）
constexpr int kLegacyUnitY = 32;   ///< 每个瓦片的像素高度
constexpr int kLegacyDefX = -66;   ///< 默认 X 偏移（将地图原点对齐到屏幕左上角）
constexpr int kLegacyDefY = -64;   ///< 默认 Y 偏移
constexpr int kMagicFlyBase = 10;       ///< 魔法飞行帧基址偏移（effect_base + 10 开始飞行帧）
constexpr int kMagicExplosionBase = 170; ///< 魔法爆炸帧基址偏移（effect_base + 170 开始爆炸帧）
constexpr std::uint64_t kMagicTimeoutMs = 10000;  ///< 魔法特效超时时间（10秒，防止永久残留）
constexpr int kDefaultSpellFrame = 10;

constexpr int kDeathEffectBase = 340;
constexpr int kDeathFireEffectBase = 2860;
constexpr int kFlyOmaAxeBase = 447;
constexpr int kThornBase = 2967;
constexpr int kArcherBase = 2607;
constexpr int kArcherBase2 = 272;
constexpr int kKuDeGiGasBase = 1445;
constexpr int kCowMonFireBase = 1800;
constexpr int kCowMonLightBase = 1900;
constexpr int kZombiLightingBase = 350;
constexpr int kZombiDieBase = 340;
constexpr int kSculptureFireBase = 1680;
constexpr int kMothPoisonGasBase = 3590;
constexpr int kDungPoisonGasBase = 3590;
constexpr int kSuperiorGuardEffectBase = 760;
constexpr int kElectronicScolpionEffectBase = 430;
constexpr int kKingBigEffectBase = 860;
constexpr int kKingOfSculpureKingEffectBase = 1380;
constexpr int kKingOfSculpureKingDeathEffectBase = 1470;
constexpr int kKingOfSculpureKingAttackEffectBase = 1490;
constexpr int kSkeletonKingEffect1Base = 2980;
constexpr int kSkeletonKingEffect2Base = 3060;
constexpr int kSkeletonKingEffect3Base = 3140;
constexpr int kSkeletonKingEffect4Base = 3220;
constexpr int kSkeletonKingEffect5Base = 3300;
constexpr int kSkeletonKingEffect6Base = 3380;
constexpr int kSkeletonKingEffect7Base = 3400;
constexpr int kSkeletonKingEffect8Base = 3570;
constexpr int kDeadCowKingHitBase = 3490;
constexpr int kDeadCowKingFlyBase = 3580;
constexpr int kDoorDeathEffectBase = 120;
constexpr int kWallLeftBrokenEffectBase = 224;
constexpr int kWallRightBrokenEffectBase = 240;

/// 魔法效果帧基址表：索引 = MagicDB.Effect - 1
/// 值为 0 的项表示该特效无独立精灵区（复用其他特效的帧）
constexpr std::array<int, 36> kEffectBase = {
    0,    200, 400, 600, 0,    900, 920, 940, 20,   940, 940, 940,
    0,    1380, 1500, 1520, 940, 1560, 1590, 1620, 1650, 1680, 0, 0,
    0,    3960, 1790, 0,    3880, 3920, 3840, 0,    40,   130, 160, 190};

/// 命中效果帧基址表（针对武器命中特效）
/// 索引 = HitEffectNumber - 1
constexpr std::array<int, 6> kHitEffectBase = {800, 1410, 1700, 3480, 3390, 40};

// ====================================================================
// 魔法特效参数表（kMagicEffectParams）
// 索引 = MagicDB.Effect - 1，用于覆盖爆炸帧偏移、帧间隔、爆炸帧数和光照。
// 值与 Delphi PlayScn.pas NewMagic() 中 mtExplosion 分支的硬编码参数一致。
// 字段为 0 时表示使用通用默认值（explosion_base=effect_base+170, light=1, etc.）。
// ====================================================================

/// 单个魔法的特效参数（覆盖 spawn_magic_effect 中的通用默认值）
struct LegacyMagicEffectParams {
  int explosion_base{0};                ///< 爆炸帧在归档中的绝对偏移 (0=effect_base+170)
  std::uint64_t next_frame_ms{0};       ///< 帧间隔毫秒 (0=默认50)
  int explosion_frame_count{0};         ///< 爆炸动画帧数 (0=默认10)
  int light{0};                         ///< 光照强度 (0=不覆盖, 1=默认, 2=中, 3=高)
};

/// 魔法特效参数表：索引 = MagicDB.Effect - 1
/// 未列出的特效使用全零默认值（即沿用通用参数）
constexpr std::array<LegacyMagicEffectParams, 36> kMagicEffectParams = {{
    {}, {}, {}, {}, {}, {}, {}, {},        // 1-8
    {}, {}, {}, {}, {}, {}, {}, {}, {},    // 9-17
    {1570, 80, 10, 1},                     // 18
    {}, {},                                // 19-20
    {1660, 80, 20, 3},                     // 21
    {}, {}, {}, {},                        // 22-25
    {3990, 80, 10, 2},                     // 26
    {1800, 80, 10, 3},                     // 27
    {}, {},                                // 28-29
    {3930, 80, 16, 3},                     // 30
    {3850, 80, 20, 3},                     // 31
    {}, {}, {}, {}, {}                     // 32-36
}};

// ====================================================================
// 内部工具函数
// 提供坐标转换、角色类型判断、魔法类型映射等底层工具
// ====================================================================

/// Delphi 风格的最近取整
/// Delphi 的 Round() 使用"银行家舍入"（banker's rounding），即 nearest 偶数的规则。
/// C++ 中 std::nearbyint() 在默认舍入模式（FE_TONEAREST）下的行为与此一致：
///   小数部分正好为 0.5 时舍入到最近的偶数（如 2.5→2, 3.5→4）。
/// 这与 std::round()（远离零舍入）不同，确保移动偏移计算与 Delphi 客户端完全一致。
int delphi_round(const double value) {
  return static_cast<int>(std::nearbyint(value));
}

/// 返回相反方向（方向 + 4 再模 8）
/// 8 方向中，相反方向正好相差 4（180 度旋转）：
///   0(上)↔4(下), 1(右上)↔5(左下), 2(右)↔6(左), 3(右下)↔7(左上)
std::uint8_t opposite_dir(const std::uint8_t dir) {
  return static_cast<std::uint8_t>((dir + 4U) & 7U);
}

/// 判断角色是否为人类（玩家）
/// 玩家角色使用 Hum.wil + Hair.wil + Weapon.wil 三个精灵层叠，
/// NPC/怪物使用单独的精灵归档（Npc.wil / Mon*.wil）
bool actor_is_human(const ActorState& actor) {
  return actor.actor_type == client_v1::ActorType::player;
}

/// 判断角色是否为 NPC
/// NPC 的特殊之处：
///   - 使用 Npc.wil 精灵归档（而非 Mon*.wil）
///   - 使用 3 方向（正面/左/右）而非 8 方向
///   - race == 50 是 Delphi 客户端的硬编码 NPC race 标识
bool actor_is_npc(const ActorState& actor) {
  return actor.actor_type == client_v1::ActorType::npc ||
         (actor.actor_type != client_v1::ActorType::player &&
          legacy_race_feature(actor.feature) == 50);
}

/// 根据方向（0-7）返回瓦片坐标增量（Δx, Δy）
/// 用于根据角色面向方向推算行动目标格的位置。
/// 方向编码与网络协议和精灵帧方向一致：
///   kDirUp=0（北）, 顺时针旋转，kDirUpRight=1, ..., kDirUpLeft=7
std::pair<int, int> dir_tile_delta(const std::uint8_t dir) {
  switch (dir % 8U) {
    case legacy::kDirUp:        return {0, -1};   // 正北
    case legacy::kDirUpRight:   return {1, -1};   // 东北
    case legacy::kDirRight:     return {1, 0};    // 正东
    case legacy::kDirDownRight: return {1, 1};    // 东南
    case legacy::kDirDown:      return {0, 1};    // 正南
    case legacy::kDirDownLeft:  return {-1, 1};   // 西南
    case legacy::kDirLeft:      return {-1, 0};   // 正西
    case legacy::kDirUpLeft:    return {-1, -1};  // 西北
    default:                    return {0, 0};
  }
}

bool legacy_move_action(const client_v1::ActorActionKind kind) {
  return kind == client_v1::ActorActionKind::walk ||
         kind == client_v1::ActorActionKind::run ||
         kind == client_v1::ActorActionKind::rush ||
         kind == client_v1::ActorActionKind::rush_kung ||
         kind == client_v1::ActorActionKind::backstep ||
         kind == client_v1::ActorActionKind::knockback;
}

const char* actor_type_name(const client_v1::ActorType type) {
  switch (type) {
    case client_v1::ActorType::player:
      return "player";
    case client_v1::ActorType::monster:
      return "monster";
    case client_v1::ActorType::npc:
      return "npc";
    default:
      return "unknown";
  }
}

const char* action_kind_name(const client_v1::ActorActionKind kind) {
  switch (kind) {
    case client_v1::ActorActionKind::turn:
      return "turn";
    case client_v1::ActorActionKind::walk:
      return "walk";
    case client_v1::ActorActionKind::run:
      return "run";
    case client_v1::ActorActionKind::hit:
      return "hit";
    case client_v1::ActorActionKind::spell:
      return "spell";
    case client_v1::ActorActionKind::struck:
      return "struck";
    case client_v1::ActorActionKind::rush:
      return "rush";
    case client_v1::ActorActionKind::rush_kung:
      return "rush_kung";
    case client_v1::ActorActionKind::backstep:
      return "backstep";
    case client_v1::ActorActionKind::knockback:
      return "knockback";
    default:
      return "unknown";
  }
}

const char* effect_kind_name(const LegacyEffectManager::EffectKind kind) {
  switch (kind) {
    case LegacyEffectManager::EffectKind::map:
      return "map";
    case LegacyEffectManager::EffectKind::char_attached:
      return "char_attached";
    case LegacyEffectManager::EffectKind::magic:
      return "magic";
    case LegacyEffectManager::EffectKind::normal_draw:
      return "normal_draw";
    default:
      return "unknown";
  }
}

/// 根据魔法 ID 和施法者/目标位置判断弹道类型
///   - 同格施法（施法者=目标）：直接爆炸（无飞行弹道）
///   - 不同格施法：飞行弹道（从施法者飞向目标）
///   - 特殊魔法：冰咆哮（fire_thunder）、火墙（ground_effect）
LegacyMagicType spell_magic_type(const int magic_id, const bool same_tile) {
  if (magic_id == 33) {
    return LegacyMagicType::fire_thunder;  // 冰咆哮：固定位置爆发
  }
  if (magic_id == 22) {
    return LegacyMagicType::ground_effect; // 火墙：地面持续效果
  }
  return same_tile ? LegacyMagicType::explosion : LegacyMagicType::fly;
}

LegacyMagicType magic_type_from_effect_type(const int effect_type,
                                            const LegacyMagicType fallback) {
  if (effect_type >= 0 && effect_type < 15) {
    return static_cast<LegacyMagicType>(effect_type);
  }
  return fallback;
}

/// 从怪物动作表中取出指定动作
const LegacyActionInfo& monster_action(const LegacyMonsterActionTable& table,
                                       const LegacyMonsterAction action) {
  return table[static_cast<std::size_t>(action)];
}

/// 规范化动作信息：确保帧数 >= 1，帧时间非零
LegacyActionInfo normalized_action(LegacyActionInfo action) {
  if (action.frame <= 0) {
    action.frame = 1;
  }
  if (action.frame_time_ms == 0) {
    action.frame_time_ms = 200;
  }
  return action;
}

LegacyActionInfo single_frame_action(const int frame, const std::uint64_t frame_time_ms) {
  return LegacyActionInfo{frame, 1, 0, frame_time_ms, 0};
}

bool direction_independent_default(const LegacySpecialActorProfile profile) {
  const auto info = legacy_special_actor_profile_info(profile);
  return info.direction_independent_stand;
}

bool direction_independent_death_default(const LegacySpecialActorProfile profile) {
  const auto info = legacy_special_actor_profile_info(profile);
  return info.direction_independent_death ||
         profile == LegacySpecialActorProfile::killing_herb ||
         profile == LegacySpecialActorProfile::centipede_king;
}

void add_pose_overlay(ActorRenderPose& pose, const ArchiveId archive, const int frame_index,
                      const bool blend = true) {
  if (frame_index < 0 || pose.overlay_count >= pose.overlays.size()) {
    return;
  }
  pose.overlays[pose.overlay_count++] = ActorOverlaySprite{archive, frame_index, blend};
}

/// 地图瓦片 X → 世界坐标 X
int map_to_world_x(const int x) {
  return x * kLegacyUnitX + kLegacyUnitX / 2;
}

/// 地图瓦片 Y → 世界坐标 Y
int map_to_world_y(const int y) {
  return y * kLegacyUnitY + kLegacyUnitY / 2;
}

/// 世界坐标 X → 屏幕坐标 X
int world_to_screen_x(const int world_x, const legacy::LegacyMapViewport& viewport) {
  return world_x - viewport.left * kLegacyUnitX + viewport.draw_origin_x;
}

/// 世界坐标 Y → 屏幕坐标 Y
int world_to_screen_y(const int world_y, const legacy::LegacyMapViewport& viewport) {
  return world_y - viewport.top * kLegacyUnitY + viewport.draw_origin_y;
}

/// 获取特效的渲染行（按 Y 坐标排序用）
int effect_row(const LegacyEffectManager::Effect& effect) {
  if (effect.fixed_effect) {
    return effect.ry;
  }
  return effect.fly_y / kLegacyUnitY;
}

bool axis_reached_or_passed(const int current, const int target, const int delta) {
  if (delta > 0) {
    return current >= target;
  }
  if (delta < 0) {
    return current <= target;
  }
  return current == target;
}

bool over_through(const int old_dir, const int new_dir) {
  if (std::abs(old_dir - new_dir) < 2) {
    return false;
  }
  return !((old_dir == 0 && new_dir == 15) || (old_dir == 15 && new_dir == 0));
}

int spell_frame_count_for_effect(const int effect) {
  switch (effect) {
    case 22:
      return 10;
    case 26:
      return 20;
    case 35:
      return 15;
    case 43:
      return 20;
    default:
      return kDefaultSpellFrame;
  }
}

std::pair<int, int> firedis_toward(const int from_x, const int from_y,
                                   const int target_x, const int target_y) {
  const auto tax = std::max(1, std::abs(target_x - from_x));
  const auto tay = std::max(1, std::abs(target_y - from_y));
  if (std::abs(from_x - target_x) > std::abs(from_y - target_y)) {
    return {delphi_round(static_cast<double>(target_x - from_x) * (500.0 / tax)),
            delphi_round(static_cast<double>(target_y - from_y) * (500.0 / tax))};
  }
  return {delphi_round(static_cast<double>(target_x - from_x) * (500.0 / tay)),
          delphi_round(static_cast<double>(target_y - from_y) * (500.0 / tay))};
}

int approach_firedis(const int current, const int target) {
  if (current < target) {
    return current + std::max(1, (target - current) / 10);
  }
  if (current > target) {
    return current - std::max(1, (current - target) / 10);
  }
  return current;
}

/// 安全获取帧数（最小为 0）
int frame_safe_count(const LegacyEffectManager::Effect& effect) {
  return std::max(0, effect.frame_count);
}

/// 规范化基础特效参数
LegacyEffectManager::Effect normalize_basic_effect(LegacyEffectManager::Effect effect,
                                                   const LegacyEffectManager::EffectKind kind) {
  effect.kind = kind;
  if (effect.effect_base == 0 && effect.start_frame != 0) {
    effect.effect_base = effect.start_frame;
    effect.start_frame = 0;
  }
  effect.start_frame = std::max(0, effect.start_frame);
  effect.current_frame = std::max(0, effect.current_frame);
  if (effect.next_frame_ms == 0) {
    effect.next_frame_ms = 30;
  }
  if (effect.frame_step_ms == 0) {
    effect.frame_step_ms = effect.spawned_ms;
  }
  effect.rx = effect.x;
  effect.ry = effect.y;
  effect.fly_x = effect.fly_x == 0 ? map_to_world_x(effect.x) : effect.fly_x;
  effect.fly_y = effect.fly_y == 0 ? map_to_world_y(effect.y) : effect.fly_y;
  effect.fixed_effect = true;
  effect.active = true;
  return effect;
}

/// 绘制特效帧到屏幕
void draw_effect_frame(AssetManager& assets, SoftwareRenderer& renderer,
                       const LegacyEffectManager::Effect& effect, const int screen_x,
                       const int screen_y, const int frame_index) {
  const auto frame = assets.get_frame(effect.archive, frame_index);
  if (frame == nullptr || frame->empty()) {
    return;
  }
  const auto draw_x = screen_x + frame->hotspot_x - kLegacyUnitX / 2;
  const auto draw_y = screen_y + frame->hotspot_y - kLegacyUnitY / 2;
  if (effect.blend) {
    renderer.surface().blit_rgba_legacy_blend(draw_x, draw_y, frame->width, frame->height,
                                              frame->pixels.data());
    return;
  }
  renderer.surface().blit_rgba(draw_x, draw_y, frame->width, frame->height, frame->pixels.data());
}

/// 推进特效帧（返回 false 表示特效结束应被移除）
///
/// 帧推进策略：
///   1. 按 next_frame_ms 间隔推进 current_frame
///   2. 播放完最后一帧后：
///      - 如果 repetition=true：回到 start_frame 循环播放
///      - 如果 repeat_count>0：递减计数器后循环（如地面特效指定次数）
///      - 否则：停在最后一帧，返回 false 标记移除
bool advance_effect_frame(LegacyEffectManager::Effect& effect, const std::uint64_t now_ms) {
  if (effect.frame_count <= 0) {
    return false;
  }
  if (elapsed_ms(now_ms, effect.frame_step_ms) <= effect.next_frame_ms) {
    return true;  // 尚未到下一帧时间，保持当前帧
  }

  effect.frame_step_ms = now_ms;
  ++effect.current_frame;
  if (effect.current_frame < frame_safe_count(effect)) {
    return true;  // 帧推进后仍在范围内
  }

  // 循环播放：回到起始帧继续循环
  if (effect.repetition) {
    effect.current_frame = effect.start_frame;
    return true;
  }
  // 地面特效有指定重复次数（如火墙燃烧 N 次后熄灭）
  if (effect.kind == LegacyEffectManager::EffectKind::map && effect.repeat_count > 0) {
    --effect.repeat_count;
    effect.current_frame = effect.start_frame;
    return true;
  }

  // 播放完毕，停在最后一帧
  effect.current_frame = std::max(0, effect.frame_count - 1);
  return false;
}

/// 开始魔法爆炸效果：将飞行特效状态转换为固定位置的爆炸状态
///
/// 飞行弹道到达目标后调用此函数：
///   - fixed_effect 从 false → true（位置锁定在目标点，不再移动）
///   - frame_count 替换为 explosion_frame_count（切换到爆炸帧序列）
///   - 爆炸帧从 0 开始重新计数
void begin_magic_explosion(LegacyEffectManager::Effect& effect,
                           std::vector<LegacyMagicAudioCue>& audio_cues) {
  if (!effect.fixed_effect && effect.magic_id > 0) {
    audio_cues.push_back(LegacyMagicAudioCue{
        effect.owner_actor_id,
        effect.magic_id,
        LegacyMagicAudioCuePhase::explosion,
    });
  }
  effect.fixed_effect = true;
  effect.repetition = false;
  effect.start_frame = 0;
  effect.current_frame = 0;
  effect.frame_count = effect.magic_type == LegacyMagicType::ready
      ? effect.explosion_frame_count
      : std::max(1, effect.explosion_frame_count);
  effect.ry = effect.target_y;
  effect.fly_x = map_to_world_x(effect.target_x);
  effect.fly_y = map_to_world_y(effect.target_y);
}

void lock_effect_to_target_pose(
    LegacyEffectManager::Effect& effect,
    const std::unordered_map<std::uint64_t, ActorRenderPose>& actor_poses) {
  if (effect.target_actor_id == 0) {
    effect.rx = effect.target_x;
    effect.ry = effect.target_y;
    effect.fly_x = map_to_world_x(effect.target_x);
    effect.fly_y = map_to_world_y(effect.target_y);
    return;
  }
  const auto target = actor_poses.find(effect.target_actor_id);
  if (target == actor_poses.end()) {
    effect.rx = effect.target_x;
    effect.ry = effect.target_y;
    effect.fly_x = map_to_world_x(effect.target_x);
    effect.fly_y = map_to_world_y(effect.target_y);
    return;
  }
  effect.rx = target->second.rx;
  effect.ry = target->second.ry;
  effect.target_x = target->second.rx;
  effect.target_y = target->second.ry;
  effect.fly_x = map_to_world_x(target->second.rx) + target->second.shift_x;
  effect.fly_y = map_to_world_y(target->second.ry) + target->second.shift_y;
}

/// 运行魔法飞行特效：更新飞行位置，到达目标时触发爆炸
///
/// 飞行弹道的工作原理：
///   1. 每帧（50ms）更新一次飞行位置（线性插值）
///   2. 从发射点 (fire_x, fire_y) 到目标点按 900ms 的标准飞行时间插值
///   3. firedis_x/firedis_y 在 spawn 时预计算，确保总位移在 900ms 内完成
///   4. 到达目标附近（距离<15）或已越过目标时，触发爆炸（begin_magic_explosion）
///   5. 超过 10 秒未完成飞行（kMagicTimeoutMs），自动移除防止内存泄漏
///
/// @return false 表示特效已结束或超时，应从列表中移除
bool run_magic_effect(LegacyEffectManager::Effect& effect, const std::uint64_t now_ms,
                      std::vector<LegacyMagicAudioCue>& audio_cues,
                      const std::unordered_map<std::uint64_t, ActorRenderPose>& actor_poses) {
  if (elapsed_ms(now_ms, effect.spawned_ms) > kMagicTimeoutMs) {
    return false;  // 超时移除（防止卡住的飞行弹道永久残留）
  }

  const auto ready_zero_frame =
      effect.magic_type == LegacyMagicType::ready && effect.frame_count <= 0;
  if (!ready_zero_frame && !advance_effect_frame(effect, now_ms)) {
    return false;  // 帧播放完毕
  }

  if (effect.fixed_effect) {
    // 固定特效（已爆炸）：有目标角色时继续贴 Actor pose。
    lock_effect_to_target_pose(effect, actor_poses);
    return true;
  }

  auto crash = false;
  auto target_world_x = map_to_world_x(effect.target_x);
  auto target_world_y = map_to_world_y(effect.target_y);
  const auto target_pose = effect.target_actor_id != 0
      ? actor_poses.find(effect.target_actor_id)
      : actor_poses.end();
  const auto has_target_actor = target_pose != actor_poses.end();
  const auto ready_without_target =
      effect.magic_type == LegacyMagicType::ready && !has_target_actor;
  if (effect.target_actor_id != 0 && !ready_without_target) {
    if (has_target_actor) {
      target_world_x = map_to_world_x(target_pose->second.rx) + target_pose->second.shift_x;
      target_world_y = map_to_world_y(target_pose->second.ry) + target_pose->second.shift_y;
      effect.target_x = target_pose->second.rx;
      effect.target_y = target_pose->second.ry;
    }
    const auto ms = elapsed_ms(now_ms, effect.move_step_ms);
    effect.move_step_ms = now_ms;
    const auto [target_firedis_x, target_firedis_y] =
        firedis_toward(effect.fly_x, effect.fly_y, target_world_x, target_world_y);
    effect.firedis_x = approach_firedis(effect.firedis_x, target_firedis_x);
    effect.firedis_y = approach_firedis(effect.firedis_y, target_firedis_y);
    effect.fly_xf += (static_cast<double>(effect.firedis_x) / 700.0) *
                     static_cast<double>(ms);
    effect.fly_yf += (static_cast<double>(effect.firedis_y) / 700.0) *
                     static_cast<double>(ms);
    effect.fly_x = delphi_round(effect.fly_xf);
    effect.fly_y = delphi_round(effect.fly_yf);

    const auto pass_dir16 = legacy_fly_direction16(effect.fly_x, effect.fly_y,
                                                   target_world_x, target_world_y);
    const auto distance_x = std::abs(target_world_x - effect.fly_x);
    const auto distance_y = std::abs(target_world_y - effect.fly_y);
    crash = (distance_x <= 15 && distance_y <= 15) ||
            (distance_x >= effect.prev_distance_x && distance_y >= effect.prev_distance_y) ||
            over_through(effect.old_dir16, pass_dir16);
    effect.prev_distance_x = distance_x;
    effect.prev_distance_y = distance_y;
    effect.old_dir16 = static_cast<std::uint8_t>(pass_dir16);
  } else {
    // 飞行弹道：固定目标使用 Delphi 的 /900 时间基准。
    const auto elapsed = static_cast<double>(elapsed_ms(now_ms, effect.spawned_ms));
    effect.fly_x = effect.fire_x +
        delphi_round((static_cast<double>(effect.firedis_x) / 900.0) * elapsed);
    effect.fly_y = effect.fire_y +
        delphi_round((static_cast<double>(effect.firedis_y) / 900.0) * elapsed);
  }
  effect.rx = effect.fly_x / kLegacyUnitX;
  effect.ry = effect.fly_y / kLegacyUnitY;

  const auto distance_x = std::abs(target_world_x - effect.fly_x);
  const auto distance_y = std::abs(target_world_y - effect.fly_y);
  const auto passed_target =
      axis_reached_or_passed(effect.fly_x, target_world_x, effect.firedis_x) &&
      axis_reached_or_passed(effect.fly_y, target_world_y, effect.firedis_y);
  if (effect.target_actor_id == 0) {
    effect.prev_distance_x = distance_x;
    effect.prev_distance_y = distance_y;
  }

  // 到达目标附近或已越过目标 → 触发爆炸
  if (!ready_without_target && ((distance_x < 15 && distance_y < 15) || passed_target || crash)) {
    begin_magic_explosion(effect, audio_cues);
    lock_effect_to_target_pose(effect, actor_poses);
  }
  return true;
}

}  // namespace

// ====================================================================
// Feature 编解码函数
// 32 位 feature 编码格式：
// bit 0-7:   race（种族）
// bit 8-15:  weapon（武器编号）
// bit 16-23: hair/face（头发/面部）
// bit 24-31: dress（服装编号）
// ====================================================================

std::int32_t make_legacy_feature(const std::uint8_t race, const std::uint8_t dress,
                                 const std::uint8_t weapon, const std::uint8_t face) {
  return static_cast<std::int32_t>(race) | (static_cast<std::int32_t>(weapon) << 8) |
         (static_cast<std::int32_t>(face) << 16) |
         (static_cast<std::int32_t>(dress) << 24);
}

std::uint8_t legacy_race_feature(const std::int32_t feature) {
  return static_cast<std::uint8_t>(static_cast<std::uint32_t>(feature) & 0xFFU);
}

std::uint8_t legacy_dress_feature(const std::int32_t feature) {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(feature) >> 24U) & 0xFFU);
}

std::uint8_t legacy_weapon_feature(const std::int32_t feature) {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(feature) >> 8U) & 0xFFU);
}

std::uint8_t legacy_hair_feature(const std::int32_t feature) {
  return static_cast<std::uint8_t>((static_cast<std::uint32_t>(feature) >> 16U) & 0xFFU);
}

std::uint16_t legacy_appr_feature(const std::int32_t feature) {
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(feature) >> 16U) & 0xFFFFU);
}

/// 完整解码人类外观信息
/// body_offset = dress * 600（每个服装占用 600 帧）
/// hair_offset = 发型 * 2 + 性别（-1 表示无头发）
/// weapon_offset = weapon * 600
LegacyHumanAppearance decode_legacy_human_feature(const std::int32_t feature) {
  LegacyHumanAppearance appearance;
  appearance.race = legacy_race_feature(feature);
  appearance.dress = legacy_dress_feature(feature);
  appearance.weapon = legacy_weapon_feature(feature);
  appearance.hair = legacy_hair_feature(feature);
  appearance.sex = appearance.dress & 1;
  appearance.appearance = legacy_appr_feature(feature);
  appearance.body_offset = kLegacyHumanFrameSpan * appearance.dress;
  const auto hair_pair = appearance.hair * 2;
  appearance.hair_offset =
      hair_pair > 1 ? kLegacyHumanFrameSpan * (hair_pair + appearance.sex) : -1;
  appearance.weapon_offset = kLegacyHumanFrameSpan * appearance.weapon;
  return appearance;
}

// ====================================================================
// 动作表和帧计算函数
// ====================================================================

const LegacyActionInfo& legacy_human_action_info(const LegacyHumanAction action) {
  return kHumanActions[static_cast<std::size_t>(action)];
}

/// 根据 race 和 appearance 选择怪物动作表
/// 映射规则基于经典 Delph 客户端的魔数（magic numbers）
const LegacyMonsterActionTable* legacy_monster_action_table(const int race,
                                                            const int appearance) {
  switch (race) {
    case 9:                    return &kMA9;
    case 10:                   return &kMA10;
    case 11:                   return &kMA11;
    case 12:
    case 24:                   return &kMA12;
    case 13:                   return &kMA13;
    case 14:
    case 17:
    case 18:
    case 23:                   return &kMA14;
    case 15:
    case 22:                   return &kMA15;
    case 16:                   return &kMA16;
    case 30:
    case 31:                   return &kMA17;
    case 32:                   return &kMA24;
    case 33:                   return &kMA25;
    case 34:                   return &kMA30;
    case 35:                   return &kMA31;
    case 36:                   return &kMA32;
    case 19:
    case 20:
    case 21:
    case 37:
    case 40:
    case 45:                   return &kMA19;
    case 47:                   return &kMA22;
    case 48:
    case 49:                   return &kMA23;
    case 52:
    case 53:                   return &kMA19;
    case 54:                   return &kMA28;
    case 55:                   return &kMA29;
    case 41:
    case 42:                   return &kMA20;
    case 43:                   return &kMA21;
    case 60:
    case 61:
    case 62:                   return &kMA33;
    case 63:                   return &kMA34;
    case 64:
    case 65:
    case 66:
    case 67:
    case 68:
    case 69:                   return &kMA19;
    case 70:
    case 71:
    case 72:                   return &kMA33;
    case 98:                   return &kMA27;
    case 99:                   return &kMA26;
    case 50:
      if (appearance == 23)    return &kMA51;
      if (appearance == 24 || appearance == 25) return &kMA52;
      return &kMA50;
    default:                   return &kMA19;
  }
}

LegacySpecialActorProfile legacy_special_actor_profile_for(const int race, const int /*appearance*/) {
  switch (race) {
    case 0:  return LegacySpecialActorProfile::human_actor;
    case 9:  return LegacySpecialActorProfile::soccer_ball;
    case 13: return LegacySpecialActorProfile::killing_herb;
    case 14: return LegacySpecialActorProfile::skeleton_oma;
    case 15:
    case 22: return LegacySpecialActorProfile::dual_axe_oma;
    case 16: return LegacySpecialActorProfile::gas_ku_de_gi;
    case 17:
    case 19:
    case 30:
    case 31: return LegacySpecialActorProfile::cat_mon;
    case 18: return LegacySpecialActorProfile::hu_su_abi;
    case 20: return LegacySpecialActorProfile::fire_cow_face_mon;
    case 21: return LegacySpecialActorProfile::cow_face_king;
    case 23: return LegacySpecialActorProfile::white_skeleton;
    case 24: return LegacySpecialActorProfile::superior_guard;
    case 32: return LegacySpecialActorProfile::scorpion_mon;
    case 33: return LegacySpecialActorProfile::centipede_king;
    case 34: return LegacySpecialActorProfile::big_heart;
    case 35: return LegacySpecialActorProfile::spider_house;
    case 36: return LegacySpecialActorProfile::explosion_spider;
    case 37: return LegacySpecialActorProfile::flying_spider;
    case 40: return LegacySpecialActorProfile::zombi_lighting;
    case 41: return LegacySpecialActorProfile::zombi_dig_out;
    case 42: return LegacySpecialActorProfile::zombi_zilkin;
    case 43: return LegacySpecialActorProfile::bee_queen;
    case 45: return LegacySpecialActorProfile::archer_mon;
    case 47:
    case 48: return LegacySpecialActorProfile::sculpture_mon;
    case 49: return LegacySpecialActorProfile::sculpture_king;
    case 50: return LegacySpecialActorProfile::npc_actor;
    case 52:
    case 53:
    case 64: return LegacySpecialActorProfile::gas_ku_de_gi;
    case 54: return LegacySpecialActorProfile::small_elf_monster;
    case 55: return LegacySpecialActorProfile::warrior_elf_monster;
    case 60: return LegacySpecialActorProfile::electronic_scolpion;
    case 61: return LegacySpecialActorProfile::boss_pig;
    case 62: return LegacySpecialActorProfile::king_of_sculpure_king;
    case 63: return LegacySpecialActorProfile::skeleton_king;
    case 65: return LegacySpecialActorProfile::samurai;
    case 66:
    case 67:
    case 68: return LegacySpecialActorProfile::skeleton_soldier;
    case 69: return LegacySpecialActorProfile::skeleton_archer;
    case 70:
    case 71:
    case 72: return LegacySpecialActorProfile::banya_guard;
    case 98: return LegacySpecialActorProfile::wall_structure;
    case 99: return LegacySpecialActorProfile::castle_door;
    default: return LegacySpecialActorProfile::base_actor;
  }
}

LegacySpecialActorProfileInfo legacy_special_actor_profile_info(
    const LegacySpecialActorProfile profile) {
  LegacySpecialActorProfileInfo info;
  info.profile = profile;

  const auto skeleton_family = [&]() {
    info.supports_dig_up = true;
    info.supports_dig_down = true;
    info.supports_fly_axe = true;
    info.supports_lighting = true;
    info.supports_alive = true;
    info.supports_skeleton = true;
    info.supports_now_death = true;
    info.has_body_overlay = true;
    info.death_effect_base = kDeathEffectBase;
  };
  const auto gas_family = [&]() {
    info.supports_lighting = true;
    info.supports_skeleton = true;
    info.supports_now_death = true;
    info.has_body_overlay = true;
  };
  const auto herb_family = [&]() {
    info.direction_independent_stand = true;
    info.supports_dig_up = true;
    info.supports_dig_down = true;
    info.supports_now_death = true;
  };

  switch (profile) {
    case LegacySpecialActorProfile::human_actor:
      info.delphi_class_name = "THumActor";
      break;
    case LegacySpecialActorProfile::soccer_ball:
      info.delphi_class_name = "TSoccerBall";
      break;
    case LegacySpecialActorProfile::killing_herb:
      info.delphi_class_name = "TKillingHerb";
      herb_family();
      break;
    case LegacySpecialActorProfile::bee_queen:
      info.delphi_class_name = "TBeeQueen";
      info.direction_independent_stand = true;
      info.direction_independent_attack = true;
      info.direction_independent_death = true;
      info.supports_now_death = true;
      break;
    case LegacySpecialActorProfile::centipede_king:
      info.delphi_class_name = "TCentipedeKingMon";
      herb_family();
      info.direction_independent_attack = true;
      info.has_body_overlay = true;
      break;
    case LegacySpecialActorProfile::big_heart:
      info.delphi_class_name = "TBigHeartMon";
      herb_family();
      info.direction_independent_attack = true;
      info.direction_independent_death = true;
      break;
    case LegacySpecialActorProfile::spider_house:
      info.delphi_class_name = "TSpiderHouseMon";
      herb_family();
      info.direction_independent_attack = true;
      info.direction_independent_death = true;
      break;
    case LegacySpecialActorProfile::skeleton_oma:
      info.delphi_class_name = "TSkeletonOma";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::dual_axe_oma:
      info.delphi_class_name = "TDualAxeOma";
      skeleton_family();
      info.has_projectile_trigger = true;
      info.projectile_base = kFlyOmaAxeBase;
      info.alternate_projectile_base = kThornBase;
      info.projectile_trigger_frame = 2;
      break;
    case LegacySpecialActorProfile::cat_mon:
      info.delphi_class_name = "TCatMon";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::archer_mon:
      info.delphi_class_name = "TArcherMon";
      skeleton_family();
      info.has_projectile_trigger = true;
      info.projectile_base = kArcherBase;
      info.alternate_projectile_base = kArcherBase2;
      info.projectile_trigger_frame = 4;
      break;
    case LegacySpecialActorProfile::scorpion_mon:
      info.delphi_class_name = "TScorpionMon";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::hu_su_abi:
      info.delphi_class_name = "THuSuABi";
      skeleton_family();
      info.has_death_effect = true;
      info.death_effect_base = kDeathFireEffectBase;
      break;
    case LegacySpecialActorProfile::zombi_dig_out:
      info.delphi_class_name = "TZombiDigOut";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::zombi_zilkin:
      info.delphi_class_name = "TZombiZilkin";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::white_skeleton:
      info.delphi_class_name = "TWhiteSkeleton";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::gas_ku_de_gi:
      info.delphi_class_name = "TGasKuDeGi";
      gas_family();
      break;
    case LegacySpecialActorProfile::fire_cow_face_mon:
      info.delphi_class_name = "TFireCowFaceMon";
      gas_family();
      break;
    case LegacySpecialActorProfile::cow_face_king:
      info.delphi_class_name = "TCowFaceKing";
      gas_family();
      break;
    case LegacySpecialActorProfile::zombi_lighting:
      info.delphi_class_name = "TZombiLighting";
      gas_family();
      info.has_death_effect = true;
      break;
    case LegacySpecialActorProfile::superior_guard:
      info.delphi_class_name = "TSuperiorGuard";
      gas_family();
      info.effect_base = kSuperiorGuardEffectBase;
      break;
    case LegacySpecialActorProfile::explosion_spider:
      info.delphi_class_name = "TExplosionSpider";
      gas_family();
      info.has_death_effect = true;
      break;
    case LegacySpecialActorProfile::flying_spider:
      info.delphi_class_name = "TFlyingSpider";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::sculpture_mon:
      info.delphi_class_name = "TSculptureMon";
      skeleton_family();
      info.effect_base = kKingOfSculpureKingEffectBase;
      break;
    case LegacySpecialActorProfile::sculpture_king:
      info.delphi_class_name = "TSculptureKingMon";
      skeleton_family();
      info.effect_base = kKingOfSculpureKingEffectBase;
      break;
    case LegacySpecialActorProfile::small_elf_monster:
      info.delphi_class_name = "TSmallElfMonster";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::warrior_elf_monster:
      info.delphi_class_name = "TWarriorElfMonster";
      skeleton_family();
      break;
    case LegacySpecialActorProfile::electronic_scolpion:
      info.delphi_class_name = "TElectronicScolpionMon";
      gas_family();
      info.effect_base = kElectronicScolpionEffectBase;
      break;
    case LegacySpecialActorProfile::boss_pig:
      info.delphi_class_name = "TBossPigMon";
      gas_family();
      info.effect_base = kKingBigEffectBase;
      break;
    case LegacySpecialActorProfile::king_of_sculpure_king:
      info.delphi_class_name = "TKingOfSculpureKingMon";
      gas_family();
      info.effect_base = kKingOfSculpureKingEffectBase;
      info.alternate_effect_base = kKingOfSculpureKingAttackEffectBase;
      info.death_effect_base = kKingOfSculpureKingDeathEffectBase;
      break;
    case LegacySpecialActorProfile::skeleton_king:
      info.delphi_class_name = "TSkeletonKingMon";
      gas_family();
      info.supports_fly_axe = true;
      info.has_projectile_trigger = true;
      info.projectile_base = kSkeletonKingEffect8Base;
      info.projectile_trigger_frame = 4;
      break;
    case LegacySpecialActorProfile::samurai:
      info.delphi_class_name = "TSamuraiMon";
      gas_family();
      info.has_death_effect = true;
      break;
    case LegacySpecialActorProfile::skeleton_soldier:
      info.delphi_class_name = "TSkeletonSoldierMon";
      gas_family();
      info.has_death_effect = true;
      break;
    case LegacySpecialActorProfile::skeleton_archer:
      info.delphi_class_name = "TSkeletonArcherMon";
      skeleton_family();
      info.has_death_effect = true;
      info.has_projectile_trigger = true;
      info.projectile_base = kArcherBase;
      info.alternate_projectile_base = kArcherBase2;
      info.projectile_trigger_frame = 4;
      break;
    case LegacySpecialActorProfile::banya_guard:
      info.delphi_class_name = "TBanyaGuardMon";
      skeleton_family();
      info.supports_lighting = true;
      info.has_projectile_trigger = true;
      info.projectile_base = kDeadCowKingFlyBase;
      info.effect_base = kDeadCowKingHitBase;
      info.projectile_trigger_frame = 4;
      break;
    case LegacySpecialActorProfile::npc_actor:
      info.delphi_class_name = "TNpcActor";
      break;
    case LegacySpecialActorProfile::castle_door:
      info.delphi_class_name = "TCastleDoor";
      info.direction_independent_attack = true;
      info.supports_dig_up = true;
      info.supports_dig_down = true;
      info.supports_now_death = true;
      info.has_body_overlay = true;
      info.has_death_effect = true;
      info.has_structure_animation = true;
      info.death_effect_base = kDoorDeathEffectBase;
      break;
    case LegacySpecialActorProfile::wall_structure:
      info.delphi_class_name = "TWallStructure";
      info.supports_dig_up = true;
      info.supports_now_death = true;
      info.has_body_overlay = true;
      info.has_structure_animation = true;
      info.effect_base = kWallLeftBrokenEffectBase;
      info.alternate_effect_base = kWallRightBrokenEffectBase;
      break;
    case LegacySpecialActorProfile::base_actor:
    default:
      info.delphi_class_name = "TActor";
      break;
  }
  return info;
}

/// 计算帧在精灵表中的绝对索引
/// 公式：start + dir * (frame + skip) + local_frame
/// 其中 (frame + skip) 是每个方向占用的总帧槽数
int legacy_frame_index(const LegacyActionInfo& action, const std::uint8_t dir,
                       const int local_frame) {
  const auto frame_count = std::max(1, action.frame);
  const auto local = std::clamp(local_frame, 0, frame_count - 1);
  return action.start + static_cast<int>(dir % 8U) * (frame_count + action.skip) + local;
}

/// 判断武器是否绘制在身体之前
/// 根据武器顺序位掩码 kWeaponOrder[sex] 的对应位
/// '0'=武器在前 '1'=武器在后
bool legacy_weapon_before_body(const int sex, const int current_frame) {
  if (current_frame < 0 || current_frame >= kLegacyHumanFrameSpan) {
    return false;
  }
  const auto order = kWeaponOrder[sex & 1][static_cast<std::size_t>(current_frame)];
  return order == '0';
}

// ====================================================================
// 移动插值偏移计算（legacy_shift）
//
// 经典 Delphi 客户端使用复杂的逐方向偏移公式实现平滑的瓦片间移动。
// 核心原理：
//   移动不是逐格跳转，而是在两个瓦片之间进行像素级平滑插值。
//   每帧根据当前移动进度（cur/max）重新计算渲染位置，
//   使角色在屏幕上的位移看起来连续流畅。
//
// 参数说明：
//   @param x, y  当前瓦片坐标（移动的目标瓦片）
//   @param dir   移动方向（0-7）
//   @param step  移动步数（格数，1=相邻格子）
//   @param cur   当前插值进度（从 0 到 max-1）
//   @param max   插值总进度（移动帧数 = end_frame - start_frame + 1）
//
// 返回值 LegacyShiftResult：
//   rx, ry     插值后的瓦片坐标（逻辑坐标，已取整）
//   shift_x/y  像素偏移（-48 ~ +48 / -32 ~ +32），当前帧内的子像素偏移
//
// 每方向的公式由 Delphi 客户端逆向得出，ss 变量控制瓦片级切换，
// shift_x/y 控制像素级偏移，二者叠加实现平滑过渡。
// v=2 的修正项仅当 max>=6 时生效，用于处理长距离移动的边界情况。
// ====================================================================

LegacyShiftResult legacy_shift(const int x, const int y, const std::uint8_t dir, const int step,
                               int cur, const int max) {
  LegacyShiftResult result{x, y, 0, 0};
  if (max <= 0) {
    return result;
  }
  cur = std::clamp(cur, 0, max);
  const auto unx = 48 * step;  // X 方向总像素偏移
  const auto uny = 32 * step;  // Y 方向总像素偏移
  auto ss = delphi_round(static_cast<double>(max - cur - 1) / static_cast<double>(max)) * step;
  auto v = 0;
  switch (dir % 8U) {
    case legacy::kDirUp:
      // 向上：瓦片 Y 增加，shift_y 为负
      ss = delphi_round(static_cast<double>(max - cur) / static_cast<double>(max)) * step;
      result.ry = y + ss;
      result.shift_y = ss == step ? -delphi_round(static_cast<double>(uny) / max * cur)
                                  : delphi_round(static_cast<double>(uny) / max * (max - cur));
      break;
    case legacy::kDirUpRight:
      // 右上：瓦片 X 减少、Y 增加，双方向偏移
      v = max >= 6 ? 2 : 0;
      ss = delphi_round(static_cast<double>(max - cur + v) / static_cast<double>(max)) * step;
      result.rx = x - ss;
      result.ry = y + ss;
      if (ss == step) {
        result.shift_x = delphi_round(static_cast<double>(unx) / max * cur);
        result.shift_y = -delphi_round(static_cast<double>(uny) / max * cur);
      } else {
        result.shift_x = -delphi_round(static_cast<double>(unx) / max * (max - cur));
        result.shift_y = delphi_round(static_cast<double>(uny) / max * (max - cur));
      }
      break;
    case legacy::kDirRight:
      // 右：瓦片 X 减少，shift_x 为正
      ss = delphi_round(static_cast<double>(max - cur) / static_cast<double>(max)) * step;
      result.rx = x - ss;
      result.shift_x = ss == step ? delphi_round(static_cast<double>(unx) / max * cur)
                                  : -delphi_round(static_cast<double>(unx) / max * (max - cur));
      break;
    case legacy::kDirDownRight:
      // 右下：双方向正偏移
      v = max >= 6 ? 2 : 0;
      ss = delphi_round(static_cast<double>(max - cur - v) / static_cast<double>(max)) * step;
      result.rx = x - ss;
      result.ry = y - ss;
      if (ss == step) {
        result.shift_x = delphi_round(static_cast<double>(unx) / max * cur);
        result.shift_y = delphi_round(static_cast<double>(uny) / max * cur);
      } else {
        result.shift_x = -delphi_round(static_cast<double>(unx) / max * (max - cur));
        result.shift_y = -delphi_round(static_cast<double>(uny) / max * (max - cur));
      }
      break;
    case legacy::kDirDown:
      // 下：瓦片 Y 减少，shift_y 为正
      v = max >= 6 ? 1 : 0;
      ss = delphi_round(static_cast<double>(max - cur - v) / static_cast<double>(max)) * step;
      result.ry = y - ss;
      result.shift_y = ss == step ? delphi_round(static_cast<double>(uny) / max * cur)
                                  : -delphi_round(static_cast<double>(uny) / max * (max - cur));
      break;
    case legacy::kDirDownLeft:
      // 左下：瓦片 X 增加、Y 减少
      v = max >= 6 ? 2 : 0;
      ss = delphi_round(static_cast<double>(max - cur - v) / static_cast<double>(max)) * step;
      result.rx = x + ss;
      result.ry = y - ss;
      if (ss == step) {
        result.shift_x = -delphi_round(static_cast<double>(unx) / max * cur);
        result.shift_y = delphi_round(static_cast<double>(uny) / max * cur);
      } else {
        result.shift_x = delphi_round(static_cast<double>(unx) / max * (max - cur));
        result.shift_y = -delphi_round(static_cast<double>(uny) / max * (max - cur));
      }
      break;
    case legacy::kDirLeft:
      // 左：瓦片 X 增加，shift_x 为负
      ss = delphi_round(static_cast<double>(max - cur) / static_cast<double>(max)) * step;
      result.rx = x + ss;
      result.shift_x = ss == step ? -delphi_round(static_cast<double>(unx) / max * cur)
                                  : delphi_round(static_cast<double>(unx) / max * (max - cur));
      break;
    case legacy::kDirUpLeft:
      // 左上：瓦片 X 增加、Y 增加
      v = max >= 6 ? 2 : 0;
      ss = delphi_round(static_cast<double>(max - cur + v) / static_cast<double>(max)) * step;
      result.rx = x + ss;
      result.ry = y + ss;
      if (ss == step) {
        result.shift_x = -delphi_round(static_cast<double>(unx) / max * cur);
        result.shift_y = -delphi_round(static_cast<double>(uny) / max * cur);
      } else {
        result.shift_x = delphi_round(static_cast<double>(unx) / max * (max - cur));
        result.shift_y = delphi_round(static_cast<double>(uny) / max * (max - cur));
      }
      break;
    default:
      break;
  }
  return result;
}

// ====================================================================
// 飞行方向计算（16 方向）
//
// 魔法的飞行弹道使用 16 方向（而非角色的 8 方向），以获得更精细的飞行路径。
// 方向 0=上, 4=右, 8=下, 12=左，奇数方向为斜向。
//
// 方向判定算法：
//   将平面分为四个象限（以源点为原点），在每个象限内：
//   1. 以 |dy/dx| 的比值与 5 个阈值比较（1/4, 1/1.9, 1.4, 4）
//   2. 阈值来自 Delphi 客户端的硬编码魔数
//   3. 比值越大（弹道越陡），方向越接近正上/正下
//   4. 差值越大（弹道越平），方向越接近正左/正右
//
// 精灵帧映射：
//   飞行弹道帧索引 = effect_base + 10 + dir16 * 10 + current_frame
//   即每方向预留 10 帧飞行精灵（如不同的飞行角度/形态）。
// ====================================================================

int legacy_fly_direction16(const int sx, const int sy, const int tx, const int ty) {
  const auto fx = tx - sx;
  const auto fy = ty - sy;
  if (fx == 0) {
    return fy < 0 ? 0 : 8;  // 正上或正下
  }
  if (fy == 0) {
    return fx < 0 ? 12 : 4; // 正左或正右
  }

  auto result = 0;
  if (fx > 0 && fy < 0) {
    // 右上象限（方向 0-4）
    result = 4;
    if (-fy > fx / 4.0)       result = 3;
    if (-fy > fx / 1.9)       result = 2;
    if (-fy > fx * 1.4)       result = 1;
    if (-fy > fx * 4.0)       result = 0;
  } else if (fx > 0 && fy > 0) {
    // 右下象限（方向 4-8）
    result = 4;
    if (fy > fx / 4.0)        result = 5;
    if (fy > fx / 1.9)        result = 6;
    if (fy > fx * 1.4)        result = 7;
    if (fy > fx * 4.0)        result = 8;
  } else if (fx < 0 && fy > 0) {
    // 左下象限（方向 8-12）
    result = 12;
    if (fy > -fx / 4.0)       result = 11;
    if (fy > -fx / 1.9)       result = 10;
    if (fy > -fx * 1.4)       result = 9;
    if (fy > -fx * 4.0)       result = 8;
  } else if (fx < 0 && fy < 0) {
    // 左上象限（方向 12-15, 0）
    result = 12;
    if (-fy > -fx / 4.0)      result = 13;
    if (-fy > -fx / 1.9)      result = 14;
    if (-fy > -fx * 1.4)      result = 15;
    if (-fy > -fx * 4.0)      result = 0;
  }
  return result;
}

// ====================================================================
// 魔法效果基址查询
//
// 根据 MagicDB.Effect - 1 和效果表类型返回精灵归档和帧基址。
// 经典传奇中，不同的魔法效果分布在不同的 WIL 归档中：
//   Magic.wil:  大多数魔法的飞行+爆炸效果（主归档）
//   Magic2.wil: 高级魔法的效果（EffectBase 索引 8,27,33-35）
//   Mon21.wil:  特殊地面效果（EffectBase 索引 31，复用怪物精灵）
//
// effect_kind 含义：
//   0: 魔法本身的飞行/爆炸效果（飞行弹道+命中后爆炸）
//   1: 武器/命中特效表
// ====================================================================

LegacyMagicEffectBase legacy_magic_effect_base(const int effect_index, const int effect_kind) {
  LegacyMagicEffectBase result;
  if (effect_kind == 1) {
    // 命中特效：目标身上的受击光效；HitEffectNumber=6 使用 Magic2.wil。
    result.archive = effect_index == 5 ? ArchiveId::magic2 : ArchiveId::magic;
    if (effect_index >= 0 && effect_index < static_cast<int>(kHitEffectBase.size())) {
      result.frame_base = kHitEffectBase[static_cast<std::size_t>(effect_index)];
    }
    return result;
  }

  // 一般魔法特效：按 Delphi GetEffectBase 的 EffectBase 索引选择归档。
  if (effect_index == 33 || effect_index == 34 || effect_index == 35 || effect_index == 8 ||
      effect_index == 27) {
    result.archive = ArchiveId::magic2;
  } else if (effect_index == 31) {
    result.archive = ArchiveId::mon21;
  } else {
    result.archive = ArchiveId::magic;
  }
  if (effect_index >= 0 && effect_index < static_cast<int>(kEffectBase.size())) {
    result.frame_base = kEffectBase[static_cast<std::size_t>(effect_index)];
  }
  return result;
}

// ====================================================================
// 地图物件动画帧计算
// ====================================================================

/// 计算地图物件的当前动画帧索引
/// 规则：
/// 1. 从 fr_img 取低 15 位作为基础帧索引
/// 2. 如果 ani_frame 高位(0x80)有动画标志，根据 ani_tick 计算帧偏移
/// 3. 门偏移(door_offset 高位 0x80)影响帧索引
int legacy_map_object_frame(const MapCell& cell, const int main_ani_count) {
  auto frame_index = static_cast<int>(cell.fr_img & 0x7FFFU);
  if (frame_index <= 0) {
    return -1;
  }
  auto ani = static_cast<int>(cell.ani_frame);
  if ((ani & 0x80) != 0) {
    ani &= 0x7F;  // 清除动画标志位
  }
  if (ani > 0) {
    // 有动画：根据主动画计数取模计算帧偏移
    const auto ani_tick = static_cast<int>(cell.ani_tick);
    const auto span = ani + ani * ani_tick;
    if (span > 0) {
      frame_index += (std::max(0, main_ani_count) % span) / (1 + ani_tick);
    }
  }
  // 门动画偏移
  if ((cell.door_offset & 0x80U) != 0U && (cell.door_index & 0x7FU) > 0U) {
    frame_index += static_cast<int>(cell.door_offset & 0x7FU);
  }
  return frame_index - 1;  // 返回 0-based 索引
}

bool legacy_map_object_blend(const MapCell& cell) {
  return (cell.ani_frame & 0x80U) != 0U;  // ani_frame 高位为混合标志
}

// ====================================================================
// 精灵归档和偏移查询
//
// 怪物精灵的归档选择规则：
//   appearance / 10 → Mon1~Mon21.wil（每个归档约 10 种外观）
//   appearance % 10 → 该归档内的位置编号，决定帧偏移量
//
// 特殊 case：appearance/10 == 90 使用 Effect.wil（部分特效复用怪物外观）
// ====================================================================

/// 根据 appearance 值选择怪物精灵归档
/// appearance/10 = 0 → Mon1.wil, 1 → Mon2.wil, ..., 20 → Mon21.wil
ArchiveId legacy_mon_archive_for_appearance(const int appearance) {
  switch (appearance / 10) {
    case 0:  return ArchiveId::mon1;
    case 1:  return ArchiveId::mon2;
    case 2:  return ArchiveId::mon3;
    case 3:  return ArchiveId::mon4;
    case 4:  return ArchiveId::mon5;
    case 5:  return ArchiveId::mon6;
    case 6:  return ArchiveId::mon7;
    case 7:  return ArchiveId::mon8;
    case 8:  return ArchiveId::mon9;
    case 9:  return ArchiveId::mon10;
    case 10: return ArchiveId::mon11;
    case 11: return ArchiveId::mon12;
    case 12: return ArchiveId::mon13;
    case 13: return ArchiveId::mon14;
    case 14: return ArchiveId::mon15;
    case 15: return ArchiveId::mon16;
    case 16: return ArchiveId::mon17;
    case 17: return ArchiveId::mon18;
    case 18: return ArchiveId::mon19;
    case 19: return ArchiveId::mon20;
    case 20: return ArchiveId::mon21;
    case 90: return ArchiveId::effect;
    default: return ArchiveId::mon1;
  }
}

/// 计算怪物精灵在归档中的帧偏移量
/// 每种怪物类型占用不同的帧跨度（280/360/430/440/350 等）
int legacy_monster_offset(const int appearance) {
  const auto race = appearance / 10;
  const auto pos = appearance % 10;
  switch (race) {
    case 0:   return pos * 280;
    case 1:   return pos * 230;
    case 2:
    case 3:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 15:
    case 16:  return pos * 360;
    case 13:
      if (pos == 1) return 360;
      if (pos == 2) return 440;
      if (pos == 3) return 550;
      return pos * 360;
    case 4:   return pos == 1 ? 600 : pos * 360;
    case 5:   return pos * 430;
    case 6:   return pos * 440;
    case 17:  return pos * 350;
    case 18:
      if (pos == 1) return 520;
      if (pos == 2) return 950;
      return 0;
    case 19:
      switch (pos) {
        case 1: return 370;   case 2: return 810;
        case 3: return 1250;  case 4: return 1630;
        case 5: return 2010;  case 6: return 2390;
        default: return 0;
      }
    case 20:
      switch (pos) {
        case 1: return 360;   case 2: return 720;
        case 3: return 1080;  case 4: return 1440;
        case 5: return 1800;  case 6: return 2350;
        case 7: return 3060;
        default: return 0;
      }
    case 90:
      switch (pos) {
        case 0: return 80;    case 1: return 168;
        case 2: return 184;   case 3: return 200;
        default: return 0;
      }
    default:  return 0;
  }
}

/// 计算 NPC 精灵在 Npc.wil 中的帧偏移量
///
/// Npc.wil 的布局与怪物归档不同：
///   appearance 0-22: 使用 kLegacyMerchantFrameSpan(60) 作为每 NPC 的帧跨度
///      这是因为 NPC 通常只有 2-4 个动作角度的站立帧（10方向不需要）
///   appearance=23: 硬编码偏移 1380（特殊 NPC 如沙城守卫等）
///   appearance>=24: 从偏移 1470 开始，按 60 帧间距排列
int legacy_npc_offset(const int appearance) {
  if (appearance >= 0 && appearance <= 22) {
    return kLegacyMerchantFrameSpan * appearance;
  }
  if (appearance == 23) {
    return 1380;
  }
  return 1470 + kLegacyMerchantFrameSpan * (appearance - 24);
}

// ====================================================================
// LegacyAnimationClock（动画时钟）
// ====================================================================

/// 重置时钟（清零所有 tick 和计数）
void LegacyAnimationClock::reset(const std::uint64_t now_ms) {
  initialized_ = now_ms != 0;
  move_tick_ = false;
  ani_tick_ = false;
  move_step_count_ = 0;
  main_ani_count_ = 0;
  move_time_ms_ = now_ms;
  ani_time_ms_ = now_ms;
}

/// 推进时钟：每 100ms 产生一个 move_tick，每 50ms 产生一个 ani_tick
void LegacyAnimationClock::advance(const std::uint64_t now_ms) {
  move_tick_ = false;
  ani_tick_ = false;
  if (!initialized_) {
    initialized_ = true;
    move_time_ms_ = now_ms;
    ani_time_ms_ = now_ms;
    return;
  }
  if (elapsed_ms(now_ms, move_time_ms_) >= 100U) {
    move_time_ms_ = now_ms;
    move_tick_ = true;
    ++move_step_count_;
    if (move_step_count_ > 1) {
      move_step_count_ = 0;
    }
  }
  if (elapsed_ms(now_ms, ani_time_ms_) >= 50U) {
    ani_time_ms_ = now_ms;
    ani_tick_ = true;
    ++main_ani_count_;
    if (main_ani_count_ > 1000000) {
      main_ani_count_ = 0;  // 防止溢出
    }
  }
}

// ====================================================================
// LegacyActorAnimation（角色动画状态机）
// ====================================================================

/// 初始化动画状态机：记录角色初始状态并设置待机帧
void LegacyActorAnimation::initialize(const ActorState& actor, const std::uint64_t now_ms) {
  initialized_ = true;
  actor_id_ = actor.actor_id;
  xx_ = actor.x;
  yy_ = actor.y;
  dir_ = actor.dir % 8U;
  shift_ = LegacyShiftResult{xx_, yy_, 0, 0};
  dead_ = actor.dead;
  motion_kind_ = MotionKind::idle;
  current_default_frame_ = 0;
  default_frame_count_ = std::max(1, stand_action_for(actor).frame);
  current_frame_ = default_frame_for(actor);
  frame_started_ms_ = now_ms;
  motion_started_ms_ = now_ms;
  default_frame_time_ms_ = now_ms;
  smooth_move_time_ms_ = now_ms;
  last_move_started_ms_ = actor.move_started_ms;
  last_action_started_ms_ = actor.action_started_ms;
  last_legacy_event_sequence_ = 0;
  last_action_kind_ = actor.current_action;
  last_legacy_ident_ = actor.legacy_action_ident;
  last_magic_id_ = actor.magic_id;
  last_dead_ = actor.dead;
  active_action_started_ms_ = actor.action_started_ms;
  pending_actions_.clear();
  lock_end_frame_ = false;
  action_reverse_ = false;
  action_lock_single_frame_ = false;
  spell_active_ = false;
  spell_effect_ready_started_ms_.reset();
  spell_effect_spawned_started_ms_ = 0;
  special_effect_events_.clear();
  last_special_event_action_started_ms_ = 0;
  last_special_event_local_frame_ = -1;
  last_state_change_frame_ = 0;
}

/// 与服务端角色状态同步：检测新移动、新动作、新死亡
void LegacyActorAnimation::sync_actor(const ActorState& actor, const std::uint64_t now_ms,
                                      const bool self_actor,
                                      const std::uint64_t trace_frame_index) {
  const auto was_initialized = initialized_;
  if (!initialized_) {
    initialize(actor, now_ms);
  }
  actor_id_ = actor.actor_id;
  self_actor_ = self_actor;
  dir_ = actor.dir % 8U;
  auto trace_state_changed = !was_initialized;
  auto trace_reason = std::string_view{"initialize"};

  // 检测是否有新的移动开始
  const auto new_move = actor.move_started_ms != 0 &&
                        (!was_initialized || actor.move_started_ms != last_move_started_ms_) &&
                        (actor.x != actor.from_x || actor.y != actor.from_y);
  // 检测是否有新的动作开始
  const auto new_action =
      actor.action_started_ms != 0 &&
      (!was_initialized ||
       actor.action_started_ms != last_action_started_ms_ ||
       actor.current_action != last_action_kind_ ||
       actor.legacy_action_ident != last_legacy_ident_ || actor.magic_id != last_magic_id_);
  // 检测是否刚死亡
  const auto newly_dead = actor.dead && !last_dead_;
  const auto revived = !actor.dead && last_dead_;
  auto queued_legacy_event = false;
  auto newest_legacy_event_sequence = last_legacy_event_sequence_;
  std::vector<std::pair<std::uint64_t, std::uint16_t>> hurry_spell_keys;
  auto remember_hurry_spell = [&](const ActorState& event) {
    if (event.current_action != client_v1::ActorActionKind::spell || event.action_started_ms == 0) {
      return;
    }
    const auto key = std::make_pair(event.action_started_ms, event.magic_id);
    if (std::find(hurry_spell_keys.begin(), hurry_spell_keys.end(), key) == hurry_spell_keys.end()) {
      hurry_spell_keys.push_back(key);
    }
  };
  auto superseded_by_hurry_spell = [&](const ActorState& event) {
    if (event.current_action != client_v1::ActorActionKind::spell || event.action_started_ms == 0) {
      return false;
    }
    const auto key = std::make_pair(event.action_started_ms, event.magic_id);
    return std::find(hurry_spell_keys.begin(), hurry_spell_keys.end(), key) != hurry_spell_keys.end();
  };

  auto queue_events_by_priority = [&](const LegacyEventPriority priority) {
    for (const auto& event : actor.legacy_pending_actions) {
      if (event.legacy_event_sequence <= last_legacy_event_sequence_) {
        continue;
      }
      if (event.legacy_event_priority != priority) {
        continue;
      }
      const auto hurry = priority == LegacyEventPriority::hurry;
      if (!hurry && superseded_by_hurry_spell(event)) {
        continue;
      }
      queue_or_begin(event, legacy_move_action(event.current_action), now_ms, hurry);
      if (hurry) {
        remember_hurry_spell(event);
      }
      newest_legacy_event_sequence =
          std::max(newest_legacy_event_sequence, event.legacy_event_sequence);
      queued_legacy_event = true;
    }
  };
  queue_events_by_priority(LegacyEventPriority::hurry);
  queue_events_by_priority(LegacyEventPriority::normal);

  if (newly_dead) {
    pending_actions_.clear();
    dead_ = true;
    spell_active_ = false;
    spell_effect_ready_started_ms_.reset();
    spell_effect_spawned_started_ms_ = 0;
    if (actor.legacy_death_mode == LegacyDeathMode::play_death_anim) {
      begin_motion(actor, die_action_for(actor), MotionKind::action, 0, now_ms);
      trace_reason = "dead_play_anim";
    } else {
      motion_kind_ = MotionKind::idle;
      smooth_move_time_ms_ = 0;
      xx_ = actor.x;
      yy_ = actor.y;
      shift_ = LegacyShiftResult{xx_, yy_, 0, 0};
      lock_end_frame_ = false;
      reset_default_frame(actor, now_ms);
      current_frame_ = default_frame_for(actor);
      motion_started_ms_ = now_ms;
      trace_reason = "dead_instant_corpse";
    }
    trace_state_changed = true;
  } else if (revived) {
    pending_actions_.clear();
    dead_ = false;
    spell_active_ = false;
    spell_effect_ready_started_ms_.reset();
    spell_effect_spawned_started_ms_ = 0;
    motion_kind_ = MotionKind::idle;
    smooth_move_time_ms_ = 0;
    xx_ = actor.x;
    yy_ = actor.y;
    shift_ = LegacyShiftResult{xx_, yy_, 0, 0};
    lock_end_frame_ = false;
    reset_default_frame(actor, now_ms);
    current_frame_ = default_frame_for(actor);
    motion_started_ms_ = now_ms;
    trace_state_changed = true;
    trace_reason = "revive_to_idle";
  } else if (queued_legacy_event) {
    // Queued legacy action snapshots preserve intermediate move segments that
    // arrived in the same network poll before the renderer observed them.
    trace_state_changed = true;
    trace_reason = "queue_legacy_event";
  } else if (new_move) {
    queue_or_begin(actor, true, now_ms);
    trace_state_changed = true;
    trace_reason = "new_move";
  } else if (new_action) {
    if (legacy_move_action(actor.current_action)) {
      queue_or_begin(actor, true, now_ms);
      trace_reason = "new_move_action";
    } else {
      queue_or_begin(actor, false, now_ms);
      trace_reason = "new_action";
    }
    trace_state_changed = true;
  } else if (motion_kind_ == MotionKind::idle && (xx_ != actor.x || yy_ != actor.y)) {
    // 空闲时坐标变化，直接跟随
    xx_ = actor.x;
    yy_ = actor.y;
    shift_ = LegacyShiftResult{xx_, yy_, 0, 0};
    current_frame_ = default_frame_for(actor);
    trace_state_changed = true;
    trace_reason = "idle_snap_position";
  }

  if (trace_state_changed) {
    last_state_change_frame_ = trace_frame_index;
    trace_actor_state(actor, trace_frame_index, now_ms, "state_change", trace_reason);
  }

  last_move_started_ms_ = actor.move_started_ms;
  last_action_started_ms_ = actor.action_started_ms;
  last_legacy_event_sequence_ =
      std::max(newest_legacy_event_sequence, actor.legacy_event_sequence);
  last_action_kind_ = actor.current_action;
  last_legacy_ident_ = actor.legacy_action_ident;
  last_magic_id_ = actor.magic_id;
  last_dead_ = actor.dead;
}

void LegacyActorAnimation::queue_or_begin(const ActorState& actor, const bool is_move,
                                          const std::uint64_t now_ms, const bool hurry) {
  const auto spell_key_match = [&](const ActorState& queued) {
    return actor.current_action == client_v1::ActorActionKind::spell &&
           queued.current_action == client_v1::ActorActionKind::spell &&
           actor.action_started_ms != 0 &&
           actor.action_started_ms == queued.action_started_ms &&
           actor.magic_id == queued.magic_id;
  };
  if (hurry && actor.current_action == client_v1::ActorActionKind::spell) {
    pending_actions_.erase(
        std::remove_if(
            pending_actions_.begin(),
            pending_actions_.end(),
            [&](const ActorState& queued) {
              return queued.legacy_event_priority == LegacyEventPriority::normal &&
                     spell_key_match(queued);
            }),
        pending_actions_.end());
    const auto active_same_spell =
        motion_kind_ == MotionKind::action && spell_active_ &&
        active_action_started_ms_ == actor.action_started_ms && last_magic_id_ == actor.magic_id;
    if (active_same_spell) {
      return;
    }
  }
  if (motion_kind_ == MotionKind::idle && !lock_end_frame_) {
    if (is_move) {
      begin_move(actor, now_ms);
    } else {
      begin_action(actor, now_ms);
    }
    return;
  }
  auto queued_actor = actor;
  queued_actor.legacy_event_priority =
      hurry ? LegacyEventPriority::hurry : LegacyEventPriority::normal;
  if (hurry) {
    auto insert_pos = pending_actions_.begin();
    while (insert_pos != pending_actions_.end() &&
           insert_pos->legacy_event_priority == LegacyEventPriority::hurry) {
      ++insert_pos;
    }
    pending_actions_.insert(insert_pos, queued_actor);
    return;
  }
  pending_actions_.push_back(queued_actor);
}

void LegacyActorAnimation::begin_queued_or_idle(const ActorState& fallback_actor,
                                                const std::uint64_t now_ms) {
  if (!pending_actions_.empty()) {
    const auto next = pending_actions_.front();
    pending_actions_.erase(pending_actions_.begin());
    if (legacy_move_action(next.current_action)) {
      begin_move(next, now_ms);
    } else {
      begin_action(next, now_ms);
    }
    return;
  }
  motion_kind_ = MotionKind::idle;
  motion_started_ms_ = now_ms;
  smooth_move_time_ms_ = now_ms;
  reset_default_frame(fallback_actor, now_ms);
}

void LegacyActorAnimation::finish_motion(const ActorState& actor, const std::uint64_t now_ms,
                                         const std::uint64_t trace_frame_index) {
  last_state_change_frame_ = trace_frame_index;
  spell_active_ = false;
  if (actor.dead || dead_) {
    motion_kind_ = MotionKind::idle;
    motion_started_ms_ = now_ms;
    current_frame_ = end_frame_;
    return;
  }
  begin_queued_or_idle(actor, now_ms);
}

bool LegacyActorAnimation::is_legacy_idle() const {
  return motion_kind_ == MotionKind::idle && pending_actions_.empty();
}

/// 开始移动动画：选择 walk 或 run 的动作信息
void LegacyActorAnimation::begin_move(const ActorState& actor, const std::uint64_t now_ms) {
  const auto action = resolved_action_for(actor, actor.current_action);
  auto target_x = actor.x;
  auto target_y = actor.y;
  const auto move_backwards = actor.current_action == client_v1::ActorActionKind::backstep ||
                              actor.current_action == client_v1::ActorActionKind::knockback;
  const auto rush_kung_move = actor.current_action == client_v1::ActorActionKind::rush_kung;
  if (rush_kung_move && actor.action_target_x >= 0 && actor.action_target_y >= 0) {
    target_x = actor.action_target_x;
    target_y = actor.action_target_y;
  }
  const auto distance =
      std::max(std::abs(target_x - actor.from_x), std::abs(target_y - actor.from_y));
  begin_motion(actor, action, MotionKind::move, std::max(1, distance), now_ms);
  move_backwards_ = move_backwards;
  rush_kung_move_ = rush_kung_move;
  xx_ = target_x;
  yy_ = target_y;
  move_return_x_ = actor.from_x;
  move_return_y_ = actor.from_y;
  move_shift_dir_ = move_backwards_ ? opposite_dir(frame_dir_for(actor)) : frame_dir_for(actor);
  current_frame_ = move_backwards_ ? end_frame_ + 1 : start_frame_ - 1;
  shift_ = legacy_shift(xx_, yy_, move_shift_dir_, move_step_, 0,
                        std::max(1, end_frame_ - start_frame_ + 1));
  lock_end_frame_ = false;
}

/// 开始动作动画（攻击/施法/受击等），并设置战斗模式
void LegacyActorAnimation::begin_action(const ActorState& actor, const std::uint64_t now_ms) {
  begin_motion(actor, resolved_action_for(actor, actor.current_action), MotionKind::action, 0, now_ms);
  shift_ = legacy_shift(xx_, yy_, frame_dir_for(actor), 0, 0, 1);
  if (actor.current_action == client_v1::ActorActionKind::spell) {
    setup_spell_runtime(actor, now_ms);
  } else {
    spell_active_ = false;
  }
  if (actor_is_human(actor) && (actor.current_action == client_v1::ActorActionKind::hit ||
                                actor.current_action == client_v1::ActorActionKind::spell)) {
    war_mode_ = true;            // 进入战斗姿态
    war_mode_time_ms_ = now_ms;
  }
}

/// 开始一个动画动作：设置动作信息、计算帧范围
void LegacyActorAnimation::begin_motion(const ActorState& actor, const LegacyActionInfo& action,
                                        const MotionKind kind, const int move_step,
                                        const std::uint64_t now_ms) {
  begin_motion(actor, ResolvedAction{action, false, false, false}, kind, move_step, now_ms);
}

void LegacyActorAnimation::begin_motion(const ActorState& actor, const ResolvedAction& resolved,
                                        const MotionKind kind, const int move_step,
                                        const std::uint64_t now_ms) {
  action_ = normalized_action(resolved.action);
  motion_kind_ = kind;
  move_step_ = move_step;
  move_backwards_ = false;
  action_reverse_ = resolved.reverse;
  action_lock_single_frame_ = resolved.lock_single_frame;
  rush_kung_move_ = false;
  move_shift_dir_ = frame_dir_for(actor);
  move_return_x_ = actor.from_x;
  move_return_y_ = actor.from_y;
  xx_ = actor.x;
  yy_ = actor.y;
  dir_ = actor.dir % 8U;
  const auto dir = resolved.direction_independent ? 0U : frame_dir_for(actor);
  start_frame_ = action_.start + static_cast<int>(dir) * (action_.frame + action_.skip);
  end_frame_ = start_frame_ + action_.frame - 1;
  current_frame_ = action_reverse_ ? end_frame_ : start_frame_;
  frame_started_ms_ = now_ms;
  motion_started_ms_ = now_ms;
  default_frame_time_ms_ = now_ms;
  default_frame_count_ = std::max(1, stand_action_for(actor).frame);
  dead_ = actor.dead;
  active_action_started_ms_ = actor.action_started_ms;
  lock_end_frame_ = false;
}

void LegacyActorAnimation::setup_spell_runtime(const ActorState& actor,
                                               const std::uint64_t now_ms) {
  spell_active_ = true;
  cur_eff_frame_ = 0;
  spell_frame_ = spell_frame_count_for_effect(actor.action_magic_effect);
  wait_magic_request_ms_ = now_ms;
  spell_effect_ready_started_ms_.reset();
  auto spell_action = legacy_human_action_info(LegacyHumanAction::spell);
  if (actor.action_magic_effect == 26) {
    spell_action.frame_time_ms /= 2U;
  }
  action_.frame_time_ms = std::max<std::uint64_t>(1U, spell_action.frame_time_ms);
}

bool LegacyActorAnimation::spell_magic_ready(const ActorState& actor,
                                             const std::uint64_t now_ms) const {
  const auto timeout_ms = self_actor_ ? 3000U : 2000U;
  return actor.action_magic_effect_type >= 0 ||
         actor.action_magic_failed ||
         elapsed_ms(now_ms, wait_magic_request_ms_) > timeout_ms;
}

bool LegacyActorAnimation::should_hold_for_server_ack(
    const ActorState& actor, const bool wait_server_accept) const {
  if (!self_actor_ || !wait_server_accept || actor.dead || dead_) {
    return false;
  }
  return actor.current_action == client_v1::ActorActionKind::hit ||
         actor.current_action == client_v1::ActorActionKind::spell;
}

void LegacyActorAnimation::trace_actor_state(const ActorState& actor,
                                             const std::uint64_t trace_frame_index,
                                             const std::uint64_t now_ms,
                                             const std::string_view stage,
                                             const std::string_view reason,
                                             const bool action_finished) const {
  if (!legacy_anim_trace_enabled()) {
    return;
  }
  LegacyAnimTraceRecord record;
  record.frame_index = trace_frame_index;
  record.now_ms = now_ms;
  record.stage = std::string(stage);
  record.reason = std::string(reason);
  record.actor_id = actor.actor_id;
  record.actor_type = actor_type_name(actor.actor_type);
  record.action_state =
      motion_kind_ == MotionKind::idle ? "idle" : action_kind_name(actor.current_action);
  record.direction = static_cast<int>(dir_);
  record.logical_x = xx_;
  record.logical_y = yy_;
  record.render_offset_x = shift_.shift_x;
  record.render_offset_y = shift_.shift_y;
  record.animation_start_time = motion_started_ms_;
  record.animation_frame_index = current_frame_;
  record.animation_frame_interval =
      action_.frame_time_ms > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
          ? std::numeric_limits<int>::max()
          : static_cast<int>(action_.frame_time_ms);
  record.action_finished = action_finished;
  legacy_anim_trace_record(record);
}

/// 更新动画：根据 motion_kind 推进帧
/// - move：根据 move_tick 推进，到最后一帧回到 idle
/// - action：按 frame_time_ms 间隔推进，到最后一帧回到 idle
/// - idle：播放待机动画（带呼吸效果）
void LegacyActorAnimation::update(const ActorState& actor, const LegacyAnimationClock& clock,
                                  const std::uint64_t now_ms, const bool wait_server_accept,
                                  const std::uint64_t trace_frame_index) {
  if (!initialized_) {
    initialize(actor, now_ms);
  }
  if (war_mode_ && elapsed_ms(now_ms, war_mode_time_ms_) > 4000U) {
    war_mode_ = false;  // 战斗模式 4 秒后自动退出
  }
  auto trace_anim = [&](const std::string_view reason, const bool action_finished = false) {
    trace_actor_state(actor, trace_frame_index, now_ms, "anim", reason, action_finished);
  };

  if (motion_kind_ == MotionKind::move) {
    // 移动动画：由 move_tick 驱动帧推进
    if (!clock.move_tick()) {
      trace_anim("move_wait_tick");
      return;
    }
    if (!move_backwards_ && (current_frame_ < start_frame_ || current_frame_ > end_frame_)) {
      current_frame_ = start_frame_ - 1;
    }
    if (move_backwards_ && (current_frame_ < start_frame_ || current_frame_ > end_frame_)) {
      current_frame_ = end_frame_ + 1;
    }
    if (move_backwards_) {
      if (current_frame_ > start_frame_) {
        --current_frame_;
        if (pending_actions_.size() >= 2 && current_frame_ > start_frame_) {
          --current_frame_;
        }
        const auto cur_step = end_frame_ - current_frame_ + 1;
        const auto max_step = end_frame_ - start_frame_ + 1;
        shift_ = legacy_shift(xx_, yy_, move_shift_dir_, move_step_, cur_step, max_step);
      }
      if (current_frame_ <= start_frame_) {
        lock_end_frame_ = true;
        begin_queued_or_idle(actor, now_ms);
        trace_anim("move_back_finish");
        return;
      }
      trace_anim("move_back_step");
      return;
    }
    if (current_frame_ < end_frame_) {
      ++current_frame_;
      if (pending_actions_.size() >= 2 && current_frame_ < end_frame_) {
        ++current_frame_;
      }
      const auto cur_step = current_frame_ - start_frame_ + 1;
      const auto max_step = end_frame_ - start_frame_ + 1;
      shift_ = legacy_shift(xx_, yy_, move_shift_dir_, move_step_, cur_step, max_step);
    }
    if (rush_kung_move_ && current_frame_ >= end_frame_ - 3) {
      shift_ = LegacyShiftResult{move_return_x_, move_return_y_, 0, 0};
      lock_end_frame_ = true;
      begin_queued_or_idle(actor, now_ms);
      trace_anim("rush_kung_finish");
      return;
    }
    if (current_frame_ >= end_frame_) {
      lock_end_frame_ = true;
      begin_queued_or_idle(actor, now_ms);
      trace_anim("move_finish");
      return;
    }
    trace_anim("move_step");
    return;
  }

  if (motion_kind_ == MotionKind::action) {
    const auto hold_for_ack = should_hold_for_server_ack(actor, wait_server_accept);
    // 动作动画：按 frame_time_ms 间隔推进帧
    if (current_frame_ < start_frame_ || current_frame_ > end_frame_) {
      current_frame_ = action_reverse_ ? end_frame_ : start_frame_;
    }
    if (elapsed_ms(now_ms, frame_started_ms_) <= action_.frame_time_ms) {
      trace_anim("action_wait_frame");
      return;
    }
    if (action_lock_single_frame_) {
      finish_motion(actor, now_ms, trace_frame_index);
      trace_anim("action_lock_single_finish", true);
      return;
    }
    if (spell_active_) {
      if ((cur_eff_frame_ == spell_frame_ - 2) && !spell_magic_ready(actor, now_ms)) {
        trace_anim("spell_wait_magic_ready");
        return;
      }
      if (current_frame_ < end_frame_) {
        if (current_frame_ < end_frame_ - 1 || cur_eff_frame_ >= spell_frame_ - 2) {
          ++current_frame_;
        }
      }
      ++cur_eff_frame_;
      frame_started_ms_ = now_ms;
      if (cur_eff_frame_ == spell_frame_ - 1 &&
          spell_effect_ready_started_ms_ != active_action_started_ms_) {
        spell_effect_ready_started_ms_ = active_action_started_ms_;
      }
      auto spell_finished = false;
      if (current_frame_ >= end_frame_ && cur_eff_frame_ >= spell_frame_) {
        if (hold_for_ack) {
          frame_started_ms_ = now_ms;
          maybe_emit_special_frame_event(actor);
          trace_anim("spell_hold_server_ack");
          return;
        }
        finish_motion(actor, now_ms, trace_frame_index);
        spell_finished = true;
      }
      maybe_emit_special_frame_event(actor);
      trace_anim(spell_finished ? "spell_finish" : "spell_step", spell_finished);
      return;
    }
    if (action_reverse_) {
      auto reverse_finished = false;
      if (current_frame_ > start_frame_) {
        --current_frame_;
        frame_started_ms_ = now_ms;
      } else {
        finish_motion(actor, now_ms, trace_frame_index);
        reverse_finished = true;
      }
      maybe_emit_special_frame_event(actor);
      trace_anim(reverse_finished ? "action_reverse_finish" : "action_reverse_step",
                 reverse_finished);
      return;
    }
    auto action_finished = false;
    if (current_frame_ < end_frame_) {
      ++current_frame_;
      frame_started_ms_ = now_ms;
    } else {
      if (hold_for_ack) {
        frame_started_ms_ = now_ms;
        maybe_emit_special_frame_event(actor);
        trace_anim("action_hold_server_ack");
        return;
      }
      finish_motion(actor, now_ms, trace_frame_index);
      action_finished = true;
    }
    maybe_emit_special_frame_event(actor);
    trace_anim(action_finished ? "action_finish" : "action_step", action_finished);
    return;
  }

  // 待机动画：播放 stand 动画，带呼吸闪烁
  refresh_default_frame(actor, now_ms);
  trace_anim("idle_refresh");
}

/// 更新待机帧：死亡状态显示死亡帧，否则按 500ms 间隔切换待机子帧
void LegacyActorAnimation::refresh_default_frame(const ActorState& actor,
                                                 const std::uint64_t now_ms) {
  if (actor.dead) {
    current_frame_ = default_frame_for(actor);
    shift_ = LegacyShiftResult{actor.x, actor.y, 0, 0};
    return;
  }
  if (elapsed_ms(now_ms, smooth_move_time_ms_) <= 200U) {
    return;  // 移动结束 200ms 内不切换待机帧（平滑过渡）
  }
  default_frame_count_ = std::max(1, stand_action_for(actor).frame);
  if (elapsed_ms(now_ms, default_frame_time_ms_) > 500U) {
    default_frame_time_ms_ = now_ms;
    ++current_default_frame_;
    if (current_default_frame_ >= default_frame_count_) {
      current_default_frame_ = 0;
    }
  }
  xx_ = actor.x;
  yy_ = actor.y;
  shift_ = LegacyShiftResult{xx_, yy_, 0, 0};
  current_frame_ = default_frame_for(actor);
}

std::optional<std::uint64_t> LegacyActorAnimation::spell_effect_ready_started_ms() const {
  if (spell_effect_ready_started_ms_.has_value() &&
      spell_effect_ready_started_ms_.value() != spell_effect_spawned_started_ms_) {
    return spell_effect_ready_started_ms_;
  }
  return std::nullopt;
}

void LegacyActorAnimation::mark_spell_effect_spawned(const std::uint64_t action_started_ms) {
  spell_effect_spawned_started_ms_ = action_started_ms;
}

std::vector<LegacySpecialEffectEvent> LegacyActorAnimation::drain_special_effect_events() {
  auto events = std::move(special_effect_events_);
  special_effect_events_.clear();
  return events;
}

/// 重置待机帧计数器
void LegacyActorAnimation::reset_default_frame(const ActorState& actor,
                                               const std::uint64_t now_ms) {
  current_default_frame_ = 0;
  default_frame_count_ = std::max(1, stand_action_for(actor).frame);
  default_frame_time_ms_ = now_ms;
}

/// 获取待机动作为人类或怪物的 stand
const LegacyActionInfo& LegacyActorAnimation::stand_action_for(const ActorState& actor) const {
  if (actor_is_human(actor)) {
    return legacy_human_action_info(LegacyHumanAction::stand);
  }
  const auto* table =
      legacy_monster_action_table(legacy_race_feature(actor.feature), legacy_appr_feature(actor.feature));
  return monster_action(*table, LegacyMonsterAction::stand);
}

/// 获取死亡动作
const LegacyActionInfo& LegacyActorAnimation::die_action_for(const ActorState& actor) const {
  if (actor_is_human(actor)) {
    return legacy_human_action_info(LegacyHumanAction::die);
  }
  const auto* table =
      legacy_monster_action_table(legacy_race_feature(actor.feature), legacy_appr_feature(actor.feature));
  return monster_action(*table, LegacyMonsterAction::die);
}

/// 获取死亡后骨架动作（怪物专用）
const LegacyActionInfo& LegacyActorAnimation::death_action_for(const ActorState& actor) const {
  const auto* table =
      legacy_monster_action_table(legacy_race_feature(actor.feature), legacy_appr_feature(actor.feature));
  return monster_action(*table, LegacyMonsterAction::death);
}

/// 根据动作类型选择对应的 LegacyActionInfo
/// 对人类和怪物分别映射
LegacyActionInfo LegacyActorAnimation::action_info_for(const ActorState& actor,
                                                       const client_v1::ActorActionKind kind) const {
  if (actor_is_human(actor)) {
    switch (kind) {
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
        return actor.dead ? legacy_human_action_info(LegacyHumanAction::die)
                          : legacy_human_action_info(LegacyHumanAction::struck);
      case client_v1::ActorActionKind::turn:
      default:
        return legacy_human_action_info(LegacyHumanAction::stand);
    }
  }

  // 怪物动作映射
  const auto* table =
      legacy_monster_action_table(legacy_race_feature(actor.feature), legacy_appr_feature(actor.feature));
  switch (kind) {
    case client_v1::ActorActionKind::walk:
    case client_v1::ActorActionKind::run:
    case client_v1::ActorActionKind::rush:
    case client_v1::ActorActionKind::rush_kung:
    case client_v1::ActorActionKind::backstep:
    case client_v1::ActorActionKind::knockback:
      return monster_action(*table, LegacyMonsterAction::walk);
    case client_v1::ActorActionKind::hit:
    case client_v1::ActorActionKind::spell:
      return monster_action(*table, LegacyMonsterAction::attack);
    case client_v1::ActorActionKind::struck:
      return actor.dead ? monster_action(*table, LegacyMonsterAction::die)
                        : monster_action(*table, LegacyMonsterAction::struck);
    case client_v1::ActorActionKind::turn:
    default:
      return monster_action(*table, LegacyMonsterAction::stand);
  }
}

LegacyActorAnimation::ResolvedAction LegacyActorAnimation::resolved_action_for(
    const ActorState& actor, const client_v1::ActorActionKind kind) const {
  if (actor_is_human(actor)) {
    return ResolvedAction{action_info_for(actor, kind), false, false, false};
  }

  const auto race = legacy_race_feature(actor.feature);
  const auto appearance = legacy_appr_feature(actor.feature);
  const auto* table = legacy_monster_action_table(race, appearance);
  const auto profile = legacy_special_actor_profile_for(race, appearance);
  const auto info = legacy_special_actor_profile_info(profile);
  const auto stand = monster_action(*table, LegacyMonsterAction::stand);
  const auto walk = monster_action(*table, LegacyMonsterAction::walk);
  const auto attack = monster_action(*table, LegacyMonsterAction::attack);
  const auto critical = monster_action(*table, LegacyMonsterAction::critical);
  const auto struck = monster_action(*table, LegacyMonsterAction::struck);
  const auto die = monster_action(*table, LegacyMonsterAction::die);
  const auto death = monster_action(*table, LegacyMonsterAction::death);
  const auto ident = actor.legacy_action_ident;

  if (ident == legacy::kSmDigUp && profile == LegacySpecialActorProfile::killing_herb) {
    return ResolvedAction{walk, true, false, false};
  }
  if (ident == legacy::kSmDigDown &&
      (profile == LegacySpecialActorProfile::killing_herb ||
       profile == LegacySpecialActorProfile::centipede_king)) {
    return ResolvedAction{death, true, false, false};
  }

  if (profile == LegacySpecialActorProfile::castle_door) {
    switch (ident) {
      case legacy::kSmDigUp:
        return ResolvedAction{attack, true, false, false};
      case legacy::kSmDigDown:
        return ResolvedAction{critical, true, false, false};
      case legacy::kSmNowDeath:
        return ResolvedAction{die, true, false, false};
      default:
        break;
    }
    if (actor.dead) {
      return ResolvedAction{single_frame_action(die.start + std::max(0, die.frame - 1),
                                                die.frame_time_ms),
                            true, false, true};
    }
  }

  if (profile == LegacySpecialActorProfile::wall_structure) {
    switch (ident) {
      case legacy::kSmDigUp:
      case legacy::kSmNowDeath:
        return ResolvedAction{die, true, false, false};
      default:
        break;
    }
    if (actor.dead) {
      return ResolvedAction{single_frame_action(die.start + std::max(0, die.frame - 1),
                                                die.frame_time_ms),
                            true, false, true};
    }
  }

  if (info.direction_independent_stand || info.direction_independent_attack ||
      info.direction_independent_death) {
    switch (kind) {
      case client_v1::ActorActionKind::turn:
        if (info.direction_independent_stand) {
          return ResolvedAction{stand, true, false, false};
        }
        break;
      case client_v1::ActorActionKind::hit:
      case client_v1::ActorActionKind::spell:
        if (profile == LegacySpecialActorProfile::centipede_king) {
          return ResolvedAction{critical, true, false, false};
        }
        if (info.direction_independent_attack) {
          return ResolvedAction{attack, true, false, false};
        }
        break;
      case client_v1::ActorActionKind::struck:
        if (actor.dead && direction_independent_death_default(profile)) {
          return ResolvedAction{die, true, false, false};
        }
        if (info.direction_independent_attack) {
          return ResolvedAction{struck, true, false, false};
        }
        break;
      default:
        break;
    }
  }

  switch (ident) {
    case legacy::kSmDigUp:
      if (profile == LegacySpecialActorProfile::sculpture_mon ||
          profile == LegacySpecialActorProfile::sculpture_king) {
        const auto no_dir = race == 48 || race == 49;
        return ResolvedAction{death, no_dir, false, false};
      }
      if (info.supports_dig_up) {
        const auto no_dir = race == 23;
        return ResolvedAction{death, no_dir, false, false};
      }
      break;
    case legacy::kSmDigDown:
      if (race == 55) {
        return ResolvedAction{critical, false, true, false};
      }
      break;
    case legacy::kSmFlyAxe:
      if (profile == LegacySpecialActorProfile::skeleton_king) {
        return ResolvedAction{critical, false, false, false};
      }
      if (info.supports_fly_axe) {
        return ResolvedAction{attack, false, false, false};
      }
      break;
    case legacy::kSmLighting:
      if (race == 60 || race == 62 || race == 70 || race == 71 || race == 72) {
        return ResolvedAction{critical, false, false, false};
      }
      if (profile == LegacySpecialActorProfile::skeleton_king) {
        auto shifted_attack = attack;
        shifted_attack.start += 80;
        return ResolvedAction{shifted_attack, false, false, false};
      }
      if (info.supports_lighting) {
        return ResolvedAction{attack, false, false, false};
      }
      break;
    case legacy::kSmAlive:
      if (info.supports_alive) {
        return ResolvedAction{death, false, false, false};
      }
      break;
    case legacy::kSmSkeleton:
      if (info.supports_skeleton) {
        return ResolvedAction{death, true, false, false};
      }
      break;
    case legacy::kSmNowDeath:
      if (info.supports_now_death) {
        const auto no_dir = direction_independent_death_default(profile);
        return ResolvedAction{die, no_dir, false, false};
      }
      break;
    default:
      break;
  }

  return ResolvedAction{action_info_for(actor, kind), false, false, false};
}

void LegacyActorAnimation::maybe_emit_special_frame_event(const ActorState& actor) {
  if (motion_kind_ != MotionKind::action || actor.action_started_ms == 0) {
    return;
  }
  const auto local_frame = current_frame_ - start_frame_;
  if (local_frame < 0 || actor.action_started_ms == last_special_event_action_started_ms_ &&
                             local_frame == last_special_event_local_frame_) {
    return;
  }

  const auto race = legacy_race_feature(actor.feature);
  const auto appearance = legacy_appr_feature(actor.feature);
  const auto profile = legacy_special_actor_profile_for(race, appearance);
  LegacySpecialEffectEvent event;
  event.actor_id = actor.actor_id;
  event.action_started_ms = actor.action_started_ms;
  event.target_actor_id = actor.action_target_actor_id;
  event.source_x = actor.x;
  event.source_y = actor.y;
  event.target_x = actor.action_target_x;
  event.target_y = actor.action_target_y;

  auto emit_projectile = [&](const ArchiveId archive, const int base,
                             const LegacyMagicType magic_type, const int frame_count,
                             const int explosion_frame_count, const int ready_distance,
                             const int frame_offset, const int frame_stride,
                             const std::uint64_t next_frame_ms) {
    event.kind = LegacySpecialEffectEvent::Kind::projectile;
    event.archive = archive;
    event.effect_base = base;
    event.magic_type = magic_type;
    event.frame_count = frame_count;
    event.explosion_frame_count = explosion_frame_count;
    event.ready_distance = ready_distance;
    event.fly_frame_offset = frame_offset;
    event.fly_frame_stride = frame_stride;
    event.next_frame_ms = next_frame_ms;
    special_effect_events_.push_back(event);
    last_special_event_action_started_ms_ = actor.action_started_ms;
    last_special_event_local_frame_ = local_frame;
  };

  if (actor.legacy_action_ident == legacy::kSmFlyAxe) {
    if (profile == LegacySpecialActorProfile::dual_axe_oma && local_frame == 2) {
      const auto base = race == 22 ? kThornBase : kFlyOmaAxeBase;
      emit_projectile(legacy_mon_archive_for_appearance(appearance), base,
                      LegacyMagicType::fly_axe, 3, 3, 65, 0, 10, 50);
      return;
    }
    if ((profile == LegacySpecialActorProfile::archer_mon ||
         profile == LegacySpecialActorProfile::skeleton_archer) &&
        local_frame == 4) {
      emit_projectile(ArchiveId::effect, kArcherBase2, LegacyMagicType::fly_arrow,
                      1, 1, 40, 0, 1, 30);
      return;
    }
    if (profile == LegacySpecialActorProfile::skeleton_king && local_frame == 4) {
      emit_projectile(ArchiveId::mon5, kSkeletonKingEffect8Base,
                      LegacyMagicType::fire_ball, 6, 1, 15, 0, 10, 40);
      return;
    }
  }

  if (actor.legacy_action_ident == legacy::kSmLighting &&
      profile == LegacySpecialActorProfile::banya_guard && local_frame == 4) {
    event.kind = LegacySpecialEffectEvent::Kind::magic_projectile;
    if (race == 70) {
      event.magic_id = 8;
      event.effect = 8;
      event.magic_type = LegacyMagicType::thunder;
      event.next_frame_ms = 30;
    } else if (race == 71) {
      event.magic_id = 1;
      event.effect = 1;
      event.magic_type = LegacyMagicType::fly;
      event.next_frame_ms = 30;
    } else if (race == 72) {
      event.magic_id = 11;
      event.effect = 32;
      event.magic_type = LegacyMagicType::ground_effect;
      event.next_frame_ms = 30;
    } else {
      return;
    }
    special_effect_events_.push_back(event);
    last_special_event_action_started_ms_ = actor.action_started_ms;
    last_special_event_local_frame_ = local_frame;
  }
}

/// 获取方向对应的帧方向：NPC 使用 3 方向（正面/左/右），其他使用 8 方向
std::uint8_t LegacyActorAnimation::frame_dir_for(const ActorState& actor) const {
  if (actor_is_npc(actor)) {
    return static_cast<std::uint8_t>(dir_ % 3U);
  }
  return static_cast<std::uint8_t>(dir_ % 8U);
}

/// 计算默认帧（待机/死亡/战斗姿态）
int LegacyActorAnimation::default_frame_for(const ActorState& actor) const {
  const auto dir = frame_dir_for(actor);
  if (actor.dead || dead_) {
    if (!actor_is_human(actor) && actor.skeleton) {
      return death_action_for(actor).start;  // 怪物骨架状态
    }
    const auto die = die_action_for(actor);
    if (!actor_is_human(actor)) {
      const auto race = legacy_race_feature(actor.feature);
      const auto appearance = legacy_appr_feature(actor.feature);
      const auto profile = legacy_special_actor_profile_for(race, appearance);
      if (profile == LegacySpecialActorProfile::castle_door ||
          profile == LegacySpecialActorProfile::wall_structure ||
          direction_independent_death_default(profile)) {
        return die.start + std::max(0, die.frame - 1);
      }
    }
    return legacy_frame_index(die, dir, std::max(0, die.frame - 1));  // 死亡最后一帧
  }
  if (actor_is_human(actor) && war_mode_) {
    return legacy_frame_index(legacy_human_action_info(LegacyHumanAction::war_mode), dir, 0);
  }
  const auto& stand = stand_action_for(actor);
  if (!actor_is_human(actor)) {
    const auto race = legacy_race_feature(actor.feature);
    const auto appearance = legacy_appr_feature(actor.feature);
    const auto profile = legacy_special_actor_profile_for(race, appearance);
    if (profile == LegacySpecialActorProfile::castle_door && actor.dir >= 3) {
      const auto* table = legacy_monster_action_table(race, appearance);
      return monster_action(*table, LegacyMonsterAction::critical).start;
    }
    if (profile == LegacySpecialActorProfile::wall_structure) {
      return stand.start + static_cast<int>(dir);
    }
    if (direction_independent_default(profile)) {
      return stand.start + current_default_frame_;
    }
  }
  return legacy_frame_index(stand, dir, current_default_frame_);
}

/// 生成渲染姿态：包含身体/头发/武器的精灵索引和偏移
std::optional<ActorRenderPose> LegacyActorAnimation::pose_for(const ActorState& actor) const {
  if (!initialized_) {
    return std::nullopt;
  }

  ActorRenderPose pose;
  pose.rx = shift_.rx;
  pose.ry = shift_.ry;
  pose.shift_x = shift_.shift_x;
  pose.shift_y = shift_.shift_y;
  pose.down_draw_level = 0;
  pose.dir = dir_;
  pose.current_frame = current_frame_;
  pose.dead = actor.dead || dead_;
  pose.alpha = 255;
  pose.visible = true;
  const auto render_frame =
      motion_kind_ == MotionKind::move && current_frame_ < start_frame_ ? start_frame_
                                                                        : current_frame_;
  pose.current_frame = render_frame;

  if (actor_is_human(actor)) {
    const auto appearance = decode_legacy_human_feature(actor.feature);
    pose.body_archive = ArchiveId::hum;
    pose.body_index = appearance.body_offset + render_frame;
    pose.hair_index = appearance.hair_offset >= 0 ? appearance.hair_offset + render_frame : -1;
    pose.weapon_index =
        appearance.weapon >= 2 ? appearance.weapon_offset + render_frame : -1;
    pose.weapon_before_body = pose.weapon_index >= 0 &&
                              legacy_weapon_before_body(appearance.sex, render_frame);
    if (motion_kind_ == MotionKind::action) {
      const auto local_frame = std::max(0, render_frame - start_frame_);
      if (actor.current_action == client_v1::ActorActionKind::spell &&
          actor.action_magic_effect > 0 && spell_active_) {
        const auto effect_index = actor.action_magic_effect - 1;
        if (effect_index >= 0) {
          const auto effect = legacy_magic_effect_base(effect_index, 0);
          if (effect.frame_base > 0 && spell_frame_ > 0) {
            const auto effect_frame =
                effect.frame_base + std::clamp(cur_eff_frame_, 0, spell_frame_ - 1);
            add_pose_overlay(pose, effect.archive, effect_frame);
          }
        }
      }
      if (actor.current_action == client_v1::ActorActionKind::struck) {
        const auto hit_effect_index = actor.last_damage_magic ? 5 : 0;
        const auto hit_effect = legacy_magic_effect_base(hit_effect_index, 1);
        if (hit_effect.frame_base > 0) {
          add_pose_overlay(pose, hit_effect.archive, hit_effect.frame_base + local_frame);
        }
      }
    }
    return pose;
  }

  // 怪物或 NPC
  const auto appearance = legacy_appr_feature(actor.feature);
  if (actor_is_npc(actor)) {
    pose.body_archive = ArchiveId::npc;
    pose.body_index = legacy_npc_offset(appearance) + render_frame;
  } else {
    pose.body_archive = legacy_mon_archive_for_appearance(appearance);
    pose.body_index = legacy_monster_offset(appearance) + render_frame;
  }
  const auto race = legacy_race_feature(actor.feature);
  const auto profile = legacy_special_actor_profile_for(race, appearance);
  if (profile == LegacySpecialActorProfile::castle_door) {
    pose.down_draw_level = (pose.dead || actor.dir >= 3) ? 2 : 1;
  } else if (profile == LegacySpecialActorProfile::skeleton_oma && pose.dead &&
             ((appearance >= 30 && appearance <= 34) || appearance == 151)) {
    pose.down_draw_level = 1;
  }
  const auto local_frame = motion_kind_ == MotionKind::action ? render_frame - start_frame_ : 0;
  const auto dir = static_cast<int>(pose.dir);
  const auto actor_archive = pose.body_archive;

  if (motion_kind_ == MotionKind::action && local_frame >= 0) {
    if (actor.legacy_action_ident == legacy::kSmNowDeath) {
      if (profile == LegacySpecialActorProfile::skeleton_oma ||
          profile == LegacySpecialActorProfile::dual_axe_oma ||
          profile == LegacySpecialActorProfile::cat_mon ||
          profile == LegacySpecialActorProfile::hu_su_abi ||
          profile == LegacySpecialActorProfile::zombi_dig_out ||
          profile == LegacySpecialActorProfile::zombi_zilkin ||
          profile == LegacySpecialActorProfile::white_skeleton ||
          profile == LegacySpecialActorProfile::flying_spider) {
        const auto base = profile == LegacySpecialActorProfile::hu_su_abi
                              ? kDeathFireEffectBase
                              : kDeathEffectBase;
        add_pose_overlay(pose, actor_archive, base + local_frame);
      } else if (profile == LegacySpecialActorProfile::zombi_lighting) {
        add_pose_overlay(pose, ArchiveId::mon5, kZombiDieBase + local_frame);
      } else if (profile == LegacySpecialActorProfile::king_of_sculpure_king) {
        add_pose_overlay(pose, ArchiveId::mon5,
                         kKingOfSculpureKingDeathEffectBase + local_frame);
      } else if (profile == LegacySpecialActorProfile::castle_door) {
        add_pose_overlay(pose, actor_archive, kDoorDeathEffectBase + local_frame);
      } else if (profile == LegacySpecialActorProfile::wall_structure) {
        const auto base = appearance == 901 ? kWallLeftBrokenEffectBase
                                            : kWallRightBrokenEffectBase;
        add_pose_overlay(pose, actor_archive, legacy_monster_offset(appearance) + 8 + dir, false);
        add_pose_overlay(pose, actor_archive, base + local_frame);
      }
    }

    if (actor.legacy_action_ident == legacy::kSmDigUp &&
        profile == LegacySpecialActorProfile::wall_structure) {
      const auto base = appearance == 901 ? kWallLeftBrokenEffectBase
                                          : kWallRightBrokenEffectBase;
      add_pose_overlay(pose, actor_archive, legacy_monster_offset(appearance) + 8 + dir, false);
      add_pose_overlay(pose, actor_archive, base + local_frame);
    }

    if (actor.legacy_action_ident == legacy::kSmLighting ||
        actor.legacy_action_ident == legacy::kSmHit ||
        actor.legacy_action_ident == legacy::kSmFlyAxe) {
      switch (race) {
        case 16:
          add_pose_overlay(pose, ArchiveId::mon3, kKuDeGiGasBase - 1 + dir * 10 + local_frame);
          break;
        case 20:
          add_pose_overlay(pose, ArchiveId::mon4, kCowMonFireBase + dir * 10 + local_frame);
          break;
        case 21:
          add_pose_overlay(pose, ArchiveId::mon4, kCowMonLightBase + dir * 10 + local_frame);
          break;
        case 24:
          add_pose_overlay(pose, ArchiveId::mon1, kSuperiorGuardEffectBase + dir * 8 + local_frame);
          break;
        case 40:
          add_pose_overlay(pose, ArchiveId::mon5, kZombiLightingBase + dir * 20 + local_frame);
          break;
        case 48:
        case 49:
          add_pose_overlay(pose, ArchiveId::mon7, kSculptureFireBase + dir * 10 + local_frame);
          break;
        case 52:
          add_pose_overlay(pose, ArchiveId::mon4, kMothPoisonGasBase + dir * 10 + local_frame);
          break;
        case 53:
          add_pose_overlay(pose, ArchiveId::mon3, kDungPoisonGasBase + dir * 10 + local_frame);
          break;
        case 60:
          add_pose_overlay(pose, actor_archive, kElectronicScolpionEffectBase + local_frame);
          break;
        case 61:
          add_pose_overlay(pose, actor_archive, kKingBigEffectBase + local_frame);
          break;
        case 62:
          add_pose_overlay(pose, ArchiveId::mon5,
                           (actor.legacy_action_ident == legacy::kSmLighting
                                ? kKingOfSculpureKingEffectBase
                                : kKingOfSculpureKingAttackEffectBase) +
                               local_frame);
          break;
        case 63:
          if (actor.legacy_action_ident == legacy::kSmFlyAxe) {
            add_pose_overlay(pose, ArchiveId::mon5, kSkeletonKingEffect5Base + local_frame);
          } else if (actor.legacy_action_ident == legacy::kSmLighting) {
            add_pose_overlay(pose, ArchiveId::mon5, kSkeletonKingEffect4Base + local_frame);
          } else {
            add_pose_overlay(pose, ArchiveId::mon5, kSkeletonKingEffect3Base + local_frame);
          }
          break;
        case 71:
          add_pose_overlay(pose, actor_archive, kDeadCowKingHitBase + local_frame);
          break;
        default:
          break;
      }
    }
  }
  return pose;
}

// ====================================================================
// LegacyEffectManager（特效管理器）
// ====================================================================

/// 计算特效帧在精灵表中的实际绝对索引
/// 魔法类型：飞行弹道使用 effect_base + 10 + dir16*10 + current_frame
///           爆炸使用 explosion_base + current_frame
/// 其他类型：effect_base + current_frame
int LegacyEffectManager::Effect::draw_frame_index() const {
  if (kind == EffectKind::magic) {
    if (!fixed_effect) {
      return effect_base + fly_frame_offset + static_cast<int>(dir16) * fly_frame_stride +
             current_frame;
    }
    return explosion_base + current_frame;
  }
  return effect_base + current_frame;
}

void LegacyEffectManager::clear() {
  ground_effects_.clear();
  char_effects_.clear();
  overlay_effects_.clear();
  fly_effects_.clear();
  next_trace_effect_id_ = 1;
}

void LegacyEffectManager::spawn_map_effect(const Effect& effect) {
  auto normalized = normalize_basic_effect(effect, EffectKind::map);
  normalized.trace_effect_id = next_trace_effect_id_++;
  normalized.trace_create_frame = 0;
  ground_effects_.push_back(normalized);
}

void LegacyEffectManager::spawn_char_effect(const std::uint64_t actor_id, const Effect& effect) {
  auto normalized = normalize_basic_effect(effect, EffectKind::char_attached);
  normalized.target_actor_id = actor_id;
  normalized.trace_effect_id = next_trace_effect_id_++;
  normalized.trace_create_frame = 0;
  char_effects_.push_back(CharEffect{actor_id, normalized});
}

/// 生成魔法特效：初始化飞行/爆炸参数，计算弹道方向
void LegacyEffectManager::spawn_magic_effect(const Effect& effect) {
  auto normalized = effect;
  normalized.kind = EffectKind::magic;
  normalized.archive = effect.archive;
  if (normalized.effect_base == 0 && normalized.start_frame != 0) {
    normalized.effect_base = normalized.start_frame;
    normalized.start_frame = 0;
  }
  normalized.explosion_base =
      normalized.explosion_base > 0 ? normalized.explosion_base
                                    : normalized.effect_base + kMagicExplosionBase;
  if (normalized.next_frame_ms == 0) {
    normalized.next_frame_ms = 50;
  }
  if (normalized.frame_step_ms == 0) {
    normalized.frame_step_ms = normalized.spawned_ms;
  }
  if (normalized.move_step_ms == 0) {
    normalized.move_step_ms = normalized.spawned_ms;
  }
  if (normalized.fire_x == 0 && normalized.fire_y == 0) {
    normalized.fire_x = map_to_world_x(normalized.x);
    normalized.fire_y = map_to_world_y(normalized.y);
  }
  if (normalized.fly_x == 0 && normalized.fly_y == 0) {
    normalized.fly_x = normalized.fixed_effect ? map_to_world_x(normalized.target_x)
                                                : normalized.fire_x;
    normalized.fly_y = normalized.fixed_effect ? map_to_world_y(normalized.target_y)
                                                : normalized.fire_y;
  }
  normalized.active = true;
  normalized.fly_xf = static_cast<double>(normalized.fly_x);
  normalized.fly_yf = static_cast<double>(normalized.fly_y);
  normalized.old_dir16 = normalized.dir16;
  normalized.trace_effect_id = next_trace_effect_id_++;
  normalized.trace_create_frame = 0;
  fly_effects_.push_back(normalized);
}

/// 生成地图地面特效（便捷接口）
LegacyEffectManager::Effect& LegacyEffectManager::spawn_map_effect(
    const ArchiveId archive, const int effect_base, const int frame_count, const int x,
    const int y, const std::uint64_t now_ms, const std::uint64_t next_frame_ms,
    const int repeat_count) {
  Effect effect;
  effect.archive = archive;
  effect.kind = EffectKind::map;
  effect.effect_base = effect_base;
  effect.frame_count = frame_count;
  effect.x = x;
  effect.y = y;
  effect.rx = x;
  effect.ry = y;
  effect.fly_x = map_to_world_x(x);
  effect.fly_y = map_to_world_y(y);
  effect.spawned_ms = now_ms;
  effect.frame_step_ms = now_ms;
  effect.next_frame_ms = next_frame_ms;
  effect.repeat_count = repeat_count;
  effect.fixed_effect = true;
  effect.blend = true;
  effect.trace_effect_id = next_trace_effect_id_++;
  effect.trace_create_frame = 0;
  ground_effects_.push_back(effect);
  if (legacy_anim_trace_enabled()) {
    LegacyAnimTraceRecord trace;
    trace.frame_index = effect.trace_create_frame;
    trace.now_ms = now_ms;
    trace.stage = "effect_create";
    trace.reason = "spawn_map_effect";
    trace.effect_id = effect.trace_effect_id;
    trace.effect_type = effect_kind_name(effect.kind);
    trace.effect_x = effect.x;
    trace.effect_y = effect.y;
    trace.effect_frame_index = effect.current_frame;
    trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
    trace.render_layer = "ground_effect";
    legacy_anim_trace_record(trace);
  }
  return ground_effects_.back();
}

/// 生成角色附着特效（便捷接口）
LegacyEffectManager::Effect& LegacyEffectManager::spawn_char_effect(
    const std::uint64_t actor_id, const ArchiveId archive, const int effect_base,
    const int frame_count, const std::uint64_t now_ms, const std::uint64_t next_frame_ms) {
  Effect effect;
  effect.archive = archive;
  effect.kind = EffectKind::char_attached;
  effect.effect_base = effect_base;
  effect.frame_count = frame_count;
  effect.spawned_ms = now_ms;
  effect.frame_step_ms = now_ms;
  effect.next_frame_ms = next_frame_ms;
  effect.target_actor_id = actor_id;
  effect.fixed_effect = true;
  effect.blend = true;
  effect.trace_effect_id = next_trace_effect_id_++;
  effect.trace_create_frame = 0;
  char_effects_.push_back(CharEffect{actor_id, effect});
  if (legacy_anim_trace_enabled()) {
    LegacyAnimTraceRecord trace;
    trace.frame_index = effect.trace_create_frame;
    trace.now_ms = now_ms;
    trace.stage = "effect_create";
    trace.reason = "spawn_char_effect";
    trace.effect_id = effect.trace_effect_id;
    trace.effect_type = effect_kind_name(effect.kind);
    trace.effect_target = actor_id;
    trace.effect_frame_index = effect.current_frame;
    trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
    trace.render_layer = "actor_overlay";
    legacy_anim_trace_record(trace);
  }
  return char_effects_.back().effect;
}

/// 生成飞行魔法特效（完整接口）：计算弹道参数、帧数、爆炸帧数
LegacyEffectManager::Effect& LegacyEffectManager::spawn_magic_effect(const MagicCreate& create) {
  const auto effect_index = create.effect > 0 ? create.effect - 1 : create.magic_id;
  auto base = legacy_magic_effect_base(effect_index, 0);
  const auto magic_type = magic_type_from_effect_type(create.effect_type, create.magic_type);
  const auto explicit_effect_base = create.effect_base >= 0;

  Effect effect;
  effect.archive = explicit_effect_base ? create.archive : base.archive;
  effect.kind = EffectKind::magic;
  effect.magic_type = magic_type;
  effect.effect_base = explicit_effect_base ? create.effect_base : base.frame_base;
  effect.explosion_base = effect.effect_base + kMagicExplosionBase;
  effect.magic_id = create.magic_id;
  effect.server_magic_id = create.server_magic_id;
  effect.owner_actor_id = create.owner_actor_id;
  effect.target_actor_id = create.target_actor_id;
  effect.x = create.source_x;
  effect.y = create.source_y;
  effect.rx = create.source_x;
  effect.ry = create.source_y;
  effect.target_x = create.target_x;
  effect.target_y = create.target_y;
  effect.fire_x = map_to_world_x(create.source_x);
  effect.fire_y = map_to_world_y(create.source_y);
  effect.fly_x = effect.fire_x;
  effect.fly_y = effect.fire_y;
  effect.fly_xf = static_cast<double>(effect.fly_x);
  effect.fly_yf = static_cast<double>(effect.fly_y);
  effect.spawned_ms = create.now_ms;
  effect.frame_step_ms = create.now_ms;
  effect.move_step_ms = create.now_ms;
  effect.next_frame_ms = create.next_frame_ms == 0 ? 50 : create.next_frame_ms;
  effect.dir16 = static_cast<std::uint8_t>(
      legacy_fly_direction16(effect.fire_x, effect.fire_y, map_to_world_x(create.target_x),
                             map_to_world_y(create.target_y)));
  effect.old_dir16 = effect.dir16;
  effect.light = 1;
  effect.blend = true;
  effect.ready_distance = create.ready_distance;
  effect.fly_frame_offset = create.fly_frame_offset;
  effect.fly_frame_stride = create.fly_frame_stride;
  effect.trace_effect_id = next_trace_effect_id_++;
  effect.trace_create_frame = create.trace_frame_index;

  auto force_fixed_base = [&](const ArchiveId archive, const int frame_base,
                              const int frame_count, const int explosion_frame_count,
                              const std::uint64_t next_frame_ms) {
    if (!explicit_effect_base) {
      effect.archive = archive;
      effect.effect_base = frame_base;
    }
    effect.explosion_base = effect.effect_base;
    effect.frame_count = frame_count;
    effect.fixed_effect = true;
    effect.repetition = false;
    effect.explosion_frame_count = explosion_frame_count;
    effect.next_frame_ms = next_frame_ms;
  };

  // 根据魔法类型设置帧数和飞行/固定模式
  switch (magic_type) {
    case LegacyMagicType::fly:
      effect.frame_count = 6;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = 10;
      break;
    case LegacyMagicType::fire_ball:
      effect.frame_count = 6;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = 1;
      break;
    case LegacyMagicType::fly_axe:
      effect.frame_count = 3;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = 3;
      break;
    case LegacyMagicType::fly_arrow:
      effect.frame_count = 1;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = 1;
      break;
    case LegacyMagicType::ground_effect:
      if (!explicit_effect_base) {
        effect.archive = ArchiveId::mon21;
      }
      effect.explosion_base = 3580;
      effect.frame_count = 20;
      effect.fixed_effect = true;
      effect.repetition = false;
      effect.explosion_frame_count = 20;
      effect.light = 3;
      break;
    case LegacyMagicType::fire_thunder:
      force_fixed_base(ArchiveId::magic2, 140, 10, 10, effect.next_frame_ms);
      break;
    case LegacyMagicType::thunder:
      force_fixed_base(ArchiveId::magic2, 10, 6, 6, effect.next_frame_ms);
      break;
    case LegacyMagicType::lighting_thunder:
      force_fixed_base(ArchiveId::magic, 970, 10, 10, effect.next_frame_ms);
      break;
    case LegacyMagicType::fire_gun:
      force_fixed_base(ArchiveId::magic, 930, 6, 6, effect.next_frame_ms);
      break;
    case LegacyMagicType::explosion:
      effect.frame_count = 10;
      effect.fixed_effect = true;
      effect.repetition = false;
      effect.explosion_frame_count = 10;
      effect.next_frame_ms = 80;
      break;
    case LegacyMagicType::explo_bujauk:
      if (!explicit_effect_base) {
        effect.effect_base = 1160;
        effect.explosion_base = create.effect == 17 ? 1540 : 1360;
      }
      effect.frame_count = 6;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = 10;
      break;
    case LegacyMagicType::bujauk_ground_effect:
      if (!explicit_effect_base) {
        effect.effect_base = 1160;
        effect.explosion_base = effect.effect_base + kMagicExplosionBase;
      }
      effect.frame_count = 6;
      effect.fixed_effect = false;
      effect.repetition = create.repetition;
      effect.explosion_frame_count = (create.effect == 11 || create.effect == 12) ? 16 : 10;
      break;
    case LegacyMagicType::ready:
      effect.frame_count = 0;
      effect.fixed_effect = false;
      effect.repetition = false;
      effect.explosion_frame_count = 0;
      break;
    case LegacyMagicType::fire_wind:
    case LegacyMagicType::kyul_kai:
    default:
      effect.frame_count = 6;
      effect.fixed_effect = true;
      effect.repetition = false;
      effect.explosion_frame_count = 6;
      break;
  }

  // 查找魔法特定参数，覆盖按类型分配的通用默认值
  // 这些参数来自 Delphi PlayScn.pas NewMagic() 中的硬编码值
  if (effect_index >= 0 && effect_index < static_cast<int>(kMagicEffectParams.size())) {
    const auto& params = kMagicEffectParams[static_cast<std::size_t>(effect_index)];
    if (params.explosion_base > 0) {
      effect.explosion_base = params.explosion_base;
    }
    if (params.next_frame_ms > 0) {
      effect.next_frame_ms = params.next_frame_ms;
    }
    if (params.explosion_frame_count > 0) {
      effect.explosion_frame_count = params.explosion_frame_count;
    }
    if (params.light > 0) {
      effect.light = params.light;
    }
  }
  if (create.frame_count > 0) {
    effect.frame_count = create.frame_count;
  }
  if (create.explosion_frame_count > 0) {
    effect.explosion_frame_count = create.explosion_frame_count;
  }
  if (magic_type == LegacyMagicType::explosion &&
      (create.effect == 21 || create.effect == 27 || create.effect == 31)) {
    effect.target_actor_id = 0;
  }
  if (magic_type == LegacyMagicType::thunder || magic_type == LegacyMagicType::fire_thunder ||
      magic_type == LegacyMagicType::fire_gun) {
    effect.target_actor_id = 0;
  }

  if (effect.fixed_effect) {
    effect.rx = create.target_x;
    effect.ry = create.target_y;
    effect.fly_x = map_to_world_x(create.target_x);
    effect.fly_y = map_to_world_y(create.target_y);
  }

  // 计算飞行弹道的位移增量
  // 标准飞行时间 900ms，总位移 = tax（或 tay）个世界坐标单位。
  // firedis_x/y 预计算为 500ms 的位移量（而非 900ms），这是 Delphi 客户端的约定：
  //   实际飞行插值使用 firedis / 900 * elapsed，使得总飞行在 900ms 内完成。
  // 500.0 是 Delphi 端硬编码的魔数（与 kMagicFlyBase=10 没有直接关系）。
  const auto target_world_x = map_to_world_x(create.target_x);
  const auto target_world_y = map_to_world_y(create.target_y);
  const auto tax = std::abs(target_world_x - effect.fire_x);
  const auto tay = std::abs(target_world_y - effect.fire_y);
  if (tax == 0 && tay == 0) {
    effect.firedis_x = 0;
    effect.firedis_y = 0;
  } else if (tax > tay) {
    effect.firedis_x =
        delphi_round(static_cast<double>(target_world_x - effect.fire_x) * (500.0 / tax));
    effect.firedis_y =
        delphi_round(static_cast<double>(target_world_y - effect.fire_y) * (500.0 / tax));
  } else {
    effect.firedis_x =
        delphi_round(static_cast<double>(target_world_x - effect.fire_x) * (500.0 / tay));
    effect.firedis_y =
        delphi_round(static_cast<double>(target_world_y - effect.fire_y) * (500.0 / tay));
  }

  if (magic_type == LegacyMagicType::ground_effect) {
    ground_effects_.push_back(effect);
    if (legacy_anim_trace_enabled()) {
      LegacyAnimTraceRecord trace;
      trace.frame_index = effect.trace_create_frame;
      trace.now_ms = create.now_ms;
      trace.stage = "effect_create";
      trace.reason = "spawn_magic_ground";
      trace.actor_id = effect.owner_actor_id;
      trace.effect_id = effect.trace_effect_id;
      trace.effect_type = effect_kind_name(effect.kind);
      trace.effect_owner = effect.owner_actor_id;
      trace.effect_target = effect.target_actor_id;
      trace.effect_x = effect.target_x;
      trace.effect_y = effect.target_y;
      trace.effect_frame_index = effect.current_frame;
      trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
      trace.render_layer = "ground_effect";
      legacy_anim_trace_record(trace);
    }
    return ground_effects_.back();
  }
  fly_effects_.push_back(effect);
  if (legacy_anim_trace_enabled()) {
    LegacyAnimTraceRecord trace;
    trace.frame_index = effect.trace_create_frame;
    trace.now_ms = create.now_ms;
    trace.stage = "effect_create";
    trace.reason = "spawn_magic_effect";
    trace.actor_id = effect.owner_actor_id;
    trace.effect_id = effect.trace_effect_id;
    trace.effect_type = effect_kind_name(effect.kind);
    trace.effect_owner = effect.owner_actor_id;
    trace.effect_target = effect.target_actor_id;
    trace.effect_x = effect.x;
    trace.effect_y = effect.y;
    trace.effect_frame_index = effect.current_frame;
    trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
    trace.render_layer = "fly_effect";
    legacy_anim_trace_record(trace);
  }
  return fly_effects_.back();
}

/// 生成普通叠加特效（用于 UI 或其他覆盖层效果）
LegacyEffectManager::Effect& LegacyEffectManager::spawn_normal_draw_effect(
    const ArchiveId archive, const int effect_base, const int frame_count, const int x,
    const int y, const std::uint64_t now_ms, const std::uint64_t next_frame_ms,
    const bool blend) {
  Effect effect;
  effect.archive = archive;
  effect.kind = EffectKind::normal_draw;
  effect.effect_base = effect_base;
  effect.frame_count = frame_count;
  effect.x = x;
  effect.y = y;
  effect.rx = x;
  effect.ry = y;
  effect.fly_x = map_to_world_x(x);
  effect.fly_y = map_to_world_y(y);
  effect.spawned_ms = now_ms;
  effect.frame_step_ms = now_ms;
  effect.next_frame_ms = next_frame_ms;
  effect.fixed_effect = true;
  effect.blend = blend;
  effect.trace_effect_id = next_trace_effect_id_++;
  effect.trace_create_frame = 0;
  overlay_effects_.push_back(effect);
  if (legacy_anim_trace_enabled()) {
    LegacyAnimTraceRecord trace;
    trace.frame_index = effect.trace_create_frame;
    trace.now_ms = now_ms;
    trace.stage = "effect_create";
    trace.reason = "spawn_normal_draw_effect";
    trace.effect_id = effect.trace_effect_id;
    trace.effect_type = effect_kind_name(effect.kind);
    trace.effect_x = effect.x;
    trace.effect_y = effect.y;
    trace.effect_frame_index = effect.current_frame;
    trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
    trace.render_layer = "overlay_effect";
    legacy_anim_trace_record(trace);
  }
  return overlay_effects_.back();
}

/// 删除指定 server_magic_id 的飞行魔法特效
void LegacyEffectManager::del_magic(const int server_magic_id) {
  fly_effects_.erase(std::remove_if(fly_effects_.begin(), fly_effects_.end(),
                                    [server_magic_id](const Effect& effect) {
                                      return effect.server_magic_id == server_magic_id;
                                    }),
                     fly_effects_.end());
}

/// 更新所有特效：移除已结束的特效
void LegacyEffectManager::update(const std::uint64_t now_ms,
                                 const std::uint64_t trace_frame_index) {
  update(now_ms, {}, trace_frame_index);
}

void LegacyEffectManager::update(
    const std::uint64_t now_ms,
    const std::unordered_map<std::uint64_t, ActorRenderPose>& actor_poses,
    const std::uint64_t trace_frame_index) {
  const auto trace_enabled = legacy_anim_trace_enabled();
  auto trace_destroy = [&](const Effect& effect, const std::string_view reason) {
    if (!trace_enabled) {
      return;
    }
    LegacyAnimTraceRecord trace;
    trace.frame_index = trace_frame_index;
    trace.now_ms = now_ms;
    trace.stage = "effect_destroy";
    trace.reason = std::string(reason);
    trace.actor_id = effect.owner_actor_id;
    trace.effect_id = effect.trace_effect_id;
    trace.effect_type = effect_kind_name(effect.kind);
    trace.effect_owner = effect.owner_actor_id;
    trace.effect_target = effect.target_actor_id;
    trace.effect_x = effect.rx;
    trace.effect_y = effect.ry;
    trace.effect_frame_index = effect.current_frame;
    trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
    trace.effect_destroy_frame = static_cast<std::int64_t>(trace_frame_index);
    trace.same_frame_visible = effect.trace_create_frame == trace_frame_index;
    legacy_anim_trace_record(trace);
  };
  ground_effects_.erase(
      std::remove_if(ground_effects_.begin(), ground_effects_.end(),
                     [now_ms, &trace_destroy](Effect& effect) {
                       const auto keep = advance_effect_frame(effect, now_ms);
                       if (!keep) {
                         trace_destroy(effect, "ground_effect_end");
                       }
                       return !keep;
                     }),
      ground_effects_.end());
  char_effects_.erase(
      std::remove_if(char_effects_.begin(), char_effects_.end(),
                     [now_ms, &trace_destroy](CharEffect& effect) {
                       const auto keep = advance_effect_frame(effect.effect, now_ms);
                       if (!keep) {
                         trace_destroy(effect.effect, "char_effect_end");
                       }
                       return !keep;
                     }),
      char_effects_.end());
  overlay_effects_.erase(
      std::remove_if(overlay_effects_.begin(), overlay_effects_.end(),
                     [now_ms, &trace_destroy](Effect& effect) {
                       const auto keep = advance_effect_frame(effect, now_ms);
                       if (!keep) {
                         trace_destroy(effect, "overlay_effect_end");
                       }
                       return !keep;
                     }),
      overlay_effects_.end());
  fly_effects_.erase(
      std::remove_if(fly_effects_.begin(), fly_effects_.end(),
                     [this, now_ms, &actor_poses, &trace_destroy](Effect& effect) {
                       const auto keep =
                           run_magic_effect(effect, now_ms, magic_audio_cues_, actor_poses);
                       if (!keep) {
                         trace_destroy(effect, "fly_effect_end");
                       }
                       return !keep;
                     }),
      fly_effects_.end());
}

std::vector<LegacyMagicAudioCue> LegacyEffectManager::drain_magic_audio_cues() {
  auto cues = std::move(magic_audio_cues_);
  magic_audio_cues_.clear();
  return cues;
}

/// 渲染所有地面特效
void LegacyEffectManager::render_ground(AssetManager& assets, SoftwareRenderer& renderer,
                                        const legacy::LegacyMapViewport& viewport,
                                        const std::uint64_t trace_frame_index,
                                        const std::uint64_t now_ms) const {
  const auto trace_enabled = legacy_anim_trace_enabled();
  for (const auto& effect : ground_effects_) {
    if (trace_enabled) {
      LegacyAnimTraceRecord trace;
      trace.frame_index = trace_frame_index;
      trace.now_ms = now_ms;
      trace.stage = "render";
      trace.reason = "render_ground_effect";
      trace.actor_id = effect.owner_actor_id;
      trace.effect_id = effect.trace_effect_id;
      trace.effect_type = effect_kind_name(effect.kind);
      trace.effect_owner = effect.owner_actor_id;
      trace.effect_target = effect.target_actor_id;
      trace.effect_x = effect.rx;
      trace.effect_y = effect.ry;
      trace.effect_frame_index = effect.current_frame;
      trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
      trace.render_layer = "ground_effect";
      trace.same_frame_visible = effect.trace_create_frame == trace_frame_index;
      legacy_anim_trace_record(trace);
    }
    const auto screen_x = world_to_screen_x(effect.fly_x, viewport);
    const auto screen_y = world_to_screen_y(effect.fly_y, viewport);
    draw_effect_frame(assets, renderer, effect, screen_x, screen_y, effect.draw_frame_index());
  }
}

/// 渲染指定行的飞行魔法特效（按 Y 行排序绘制）
void LegacyEffectManager::render_fly(AssetManager& assets, SoftwareRenderer& renderer,
                                     const legacy::LegacyMapViewport& viewport,
                                     const int row,
                                     const std::uint64_t trace_frame_index,
                                     const std::uint64_t now_ms) const {
  const auto trace_enabled = legacy_anim_trace_enabled();
  for (const auto& effect : fly_effects_) {
    if (effect_row(effect) != row) {
      continue;
    }
    // 跳过刚发射的飞行弹道（避免在发射点闪烁）
    if (!effect.fixed_effect && std::abs(effect.fly_x - effect.fire_x) <= effect.ready_distance &&
        std::abs(effect.fly_y - effect.fire_y) <= effect.ready_distance) {
      continue;
    }
    if (trace_enabled) {
      LegacyAnimTraceRecord trace;
      trace.frame_index = trace_frame_index;
      trace.now_ms = now_ms;
      trace.stage = "render";
      trace.reason = "render_fly_effect";
      trace.actor_id = effect.owner_actor_id;
      trace.effect_id = effect.trace_effect_id;
      trace.effect_type = effect_kind_name(effect.kind);
      trace.effect_owner = effect.owner_actor_id;
      trace.effect_target = effect.target_actor_id;
      trace.effect_x = effect.rx;
      trace.effect_y = effect.ry;
      trace.effect_frame_index = effect.current_frame;
      trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
      trace.render_layer = "fly_effect";
      trace.same_frame_visible = effect.trace_create_frame == trace_frame_index;
      legacy_anim_trace_record(trace);
    }
    const auto screen_x = world_to_screen_x(effect.fly_x, viewport);
    const auto screen_y = world_to_screen_y(effect.fly_y, viewport);
    draw_effect_frame(assets, renderer, effect, screen_x, screen_y, effect.draw_frame_index());
  }
}

/// 渲染指定角色的附着特效
void LegacyEffectManager::render_overlay_for_actor(const std::uint64_t actor_id,
                                                   const ActorRenderPose& pose,
                                                   AssetManager& assets, SoftwareRenderer& renderer,
                                                   const legacy::LegacyMapViewport& viewport,
                                                   const std::uint64_t trace_frame_index,
                                                   const std::uint64_t now_ms) const {
  const auto trace_enabled = legacy_anim_trace_enabled();
  const auto screen_x =
      legacy::legacy_tile_draw_x(viewport, pose.rx) + pose.shift_x + kLegacyUnitX / 2;
  const auto screen_y =
      legacy::legacy_ground_mid_y(viewport, pose.ry) + pose.shift_y + kLegacyUnitY / 2;
  for (const auto& effect : char_effects_) {
    if (effect.actor_id != actor_id) {
      continue;
    }
    if (trace_enabled) {
      LegacyAnimTraceRecord trace;
      trace.frame_index = trace_frame_index;
      trace.now_ms = now_ms;
      trace.stage = "render";
      trace.reason = "render_char_effect";
      trace.actor_id = actor_id;
      trace.action_state = "actor_overlay";
      trace.direction = static_cast<int>(pose.dir);
      trace.logical_x = pose.rx;
      trace.logical_y = pose.ry;
      trace.render_offset_x = pose.shift_x;
      trace.render_offset_y = pose.shift_y;
      trace.effect_id = effect.effect.trace_effect_id;
      trace.effect_type = effect_kind_name(effect.effect.kind);
      trace.effect_owner = effect.effect.owner_actor_id;
      trace.effect_target = effect.effect.target_actor_id;
      trace.effect_x = pose.rx;
      trace.effect_y = pose.ry;
      trace.effect_frame_index = effect.effect.current_frame;
      trace.effect_create_frame = static_cast<std::int64_t>(effect.effect.trace_create_frame);
      trace.render_layer = "actor_overlay";
      trace.same_frame_visible = effect.effect.trace_create_frame == trace_frame_index;
      legacy_anim_trace_record(trace);
    }
    draw_effect_frame(assets, renderer, effect.effect, screen_x, screen_y,
                      effect.effect.draw_frame_index());
  }
}

/// 渲染所有叠加特效
void LegacyEffectManager::render_overlay(AssetManager& assets, SoftwareRenderer& renderer,
                                         const legacy::LegacyMapViewport& viewport,
                                         const std::uint64_t trace_frame_index,
                                         const std::uint64_t now_ms) const {
  const auto trace_enabled = legacy_anim_trace_enabled();
  for (const auto& effect : overlay_effects_) {
    if (trace_enabled) {
      LegacyAnimTraceRecord trace;
      trace.frame_index = trace_frame_index;
      trace.now_ms = now_ms;
      trace.stage = "render";
      trace.reason = "render_overlay_effect";
      trace.actor_id = effect.owner_actor_id;
      trace.effect_id = effect.trace_effect_id;
      trace.effect_type = effect_kind_name(effect.kind);
      trace.effect_owner = effect.owner_actor_id;
      trace.effect_target = effect.target_actor_id;
      trace.effect_x = effect.rx;
      trace.effect_y = effect.ry;
      trace.effect_frame_index = effect.current_frame;
      trace.effect_create_frame = static_cast<std::int64_t>(effect.trace_create_frame);
      trace.render_layer = "overlay_effect";
      trace.same_frame_visible = effect.trace_create_frame == trace_frame_index;
      legacy_anim_trace_record(trace);
    }
    const auto screen_x = world_to_screen_x(effect.fly_x, viewport);
    const auto screen_y = world_to_screen_y(effect.fly_y, viewport);
    draw_effect_frame(assets, renderer, effect, screen_x, screen_y, effect.draw_frame_index());
  }
}

// ====================================================================
// AnimationManager（动画管理器）
// ====================================================================

void AnimationManager::reset(const std::uint64_t now_ms) {
  clock_.reset(now_ms);
  effects_.clear();
  actor_snapshots_.clear();
  actors_.clear();
  spell_effect_started_ms_.clear();
  special_effect_started_ms_.clear();
  trace_frame_index_ = 0;
  trace_now_ms_ = now_ms;
}

/// 与世界状态同步：更新角色快照、同步动画状态机、移除已离开的角色
void AnimationManager::sync_world(const WorldViewState& world, const std::uint64_t now_ms) {
  actor_snapshots_ = world.actors;
  for (const auto& [actor_id, actor] : world.actors) {
    auto& animation = actors_[actor_id];
    animation.sync_actor(actor, now_ms, actor_id == world.self_actor_id, trace_frame_index_);
  }
  for (auto it = actors_.begin(); it != actors_.end();) {
    if (world.actors.find(it->first) == world.actors.end()) {
      spell_effect_started_ms_.erase(it->first);
      it = actors_.erase(it);
    } else {
      ++it;
    }
  }
}

/// 更新所有动画和特效：推进时钟、同步世界、更新角色动画、生成法术特效
void AnimationManager::update(const WorldViewState& world, const std::uint64_t now_ms) {
  ++trace_frame_index_;
  trace_now_ms_ = now_ms;
  clock_.advance(now_ms);
  sync_world(world, now_ms);
  for (auto& [actor_id, animation] : actors_) {
    const auto found = world.actors.find(actor_id);
    if (found != world.actors.end()) {
      const auto wait_server_accept =
          actor_id == world.self_actor_id && action_lock_active(world, now_ms);
      animation.update(found->second, clock_, now_ms, wait_server_accept, trace_frame_index_);
      actor_snapshots_[actor_id] = found->second;
    }
  }
  spawn_spell_effects(world, now_ms);
  spawn_special_effect_events(world, now_ms);
  std::unordered_map<std::uint64_t, ActorRenderPose> poses;
  for (const auto& [actor_id, actor] : actor_snapshots_) {
    const auto animation = actors_.find(actor_id);
    if (animation == actors_.end()) {
      continue;
    }
    if (auto pose = animation->second.pose_for(actor); pose.has_value()) {
      poses.emplace(actor_id, *pose);
    }
  }
  effects_.update(now_ms, poses, trace_frame_index_);
}

std::vector<LegacyMagicAudioCue> AnimationManager::drain_magic_audio_cues() {
  auto cues = std::move(magic_audio_cues_);
  magic_audio_cues_.clear();
  auto effect_cues = effects_.drain_magic_audio_cues();
  cues.insert(cues.end(), effect_cues.begin(), effect_cues.end());
  return cues;
}

/// 检测并生成施法特效
///
/// 每帧遍历所有角色，检测新开始的施法动作（spell action）。
/// 使用 action_started_ms 作为去重键：同一角色的同一时间戳只生成一次特效。
///
/// 根据魔法 ID 分三种特效类型：
///   1. 角色附着特效（如火球/雷电）：直接出现在目标角色身上
///   2. 地图地面特效（如火墙）：在目标地图格上生成地面火焰
///   3. 飞行弹道（大多数攻击魔法）：从施法者飞向目标，到达后爆炸
///
/// server_magic_id 的生成（用于服务端控制特效删除）：
///   server_magic_id = ((actor_id & 0x7FFF) << 16) ^ (action_started_ms & 0xFFFF)
///   将角色 ID 的低 15 位和时间戳的低 16 位混合，形成全局唯一标识。
///   服务端通过此 ID 通知客户端删除特定魔法特效。
void AnimationManager::spawn_spell_effects(const WorldViewState& world,
                                           const std::uint64_t now_ms) {
  for (const auto& [actor_id, actor] : world.actors) {
    const auto animation = actors_.find(actor_id);
    if (animation == actors_.end()) {
      continue;
    }
    const auto ready_started = animation->second.spell_effect_ready_started_ms();
    if (!ready_started.has_value()) {
      continue;
    }
    if (spell_effect_started_ms_[actor_id] == ready_started.value()) {
      continue;
    }
    spell_effect_started_ms_[actor_id] = ready_started.value();
    animation->second.mark_spell_effect_spawned(ready_started.value());
    spawn_spell_effect_for_actor(world, actor, actor_id, now_ms);
  }
}

void AnimationManager::spawn_spell_effect_for_actor(const WorldViewState& world,
                                                    const ActorState& actor,
                                                    const std::uint64_t actor_id,
                                                    const std::uint64_t now_ms) {
  if (actor.current_action != client_v1::ActorActionKind::spell || actor.magic_id == 0 ||
      actor.action_started_ms == 0) {
    return;
  }
  if (actor.action_magic_failed || actor.action_magic_effect_type < 0 ||
      actor.action_magic_effect <= 0) {
    return;
  }

  auto target_actor_id = actor.action_target_actor_id;
  auto target_x = actor.action_target_x;
  auto target_y = actor.action_target_y;
  if (target_actor_id != 0) {
    if (const auto target = world.actors.find(target_actor_id); target != world.actors.end()) {
      target_x = target->second.x;
      target_y = target->second.y;
    }
  }
  if (target_x < 0 || target_y < 0) {
    const auto [dx, dy] = dir_tile_delta(actor.dir);
    target_x = actor.x + dx;
    target_y = actor.y + dy;
  }
  target_x = std::clamp(target_x, 0, std::max(0, world.width - 1));
  target_y = std::clamp(target_y, 0, std::max(0, world.height - 1));

  const auto magic_id = static_cast<int>(actor.magic_id);
  const auto has_effect = actor.action_magic_effect > 0;
  const auto effect = has_effect ? actor.action_magic_effect : magic_id;
  magic_audio_cues_.push_back(LegacyMagicAudioCue{
      actor_id,
      magic_id,
      LegacyMagicAudioCuePhase::fire,
  });

  LegacyEffectManager::MagicCreate create;
  create.magic_id = magic_id;
  create.server_magic_id = static_cast<int>(
      ((actor_id & 0x7FFFU) << 16U) ^ (actor.action_started_ms & 0xFFFFU));
  create.effect_type = actor.action_magic_effect_type;
  create.effect = effect;
  create.source_x = actor.x;
  create.source_y = actor.y;
  create.target_x = target_x;
  create.target_y = target_y;
  create.owner_actor_id = actor_id;
  create.target_actor_id = target_actor_id;
  create.magic_type = spell_magic_type(magic_id, actor.x == target_x && actor.y == target_y);
  create.repetition = true;
  create.now_ms = now_ms;
  create.trace_frame_index = trace_frame_index_;
  create.next_frame_ms = 50;
  effects_.spawn_magic_effect(create);
}

void AnimationManager::spawn_special_effect_events(const WorldViewState& world,
                                                   const std::uint64_t now_ms) {
  for (auto& [actor_id, animation] : actors_) {
    auto events = animation.drain_special_effect_events();
    for (const auto& event : events) {
      const auto key = (event.action_started_ms << 8U) ^
                       static_cast<std::uint64_t>(event.effect_base + event.magic_id);
      if (special_effect_started_ms_[actor_id] == key) {
        continue;
      }
      special_effect_started_ms_[actor_id] = key;
      spawn_special_effect_event(world, event, now_ms);
    }
  }
}

void AnimationManager::spawn_special_effect_event(const WorldViewState& world,
                                                  const LegacySpecialEffectEvent& event,
                                                  const std::uint64_t now_ms) {
  auto target_x = event.target_x;
  auto target_y = event.target_y;
  if (event.target_actor_id != 0) {
    if (const auto target = world.actors.find(event.target_actor_id); target != world.actors.end()) {
      target_x = target->second.x;
      target_y = target->second.y;
    }
  }
  if (target_x < 0 || target_y < 0) {
    if (const auto actor = world.actors.find(event.actor_id); actor != world.actors.end()) {
      const auto [dx, dy] = dir_tile_delta(actor->second.dir);
      target_x = event.source_x + dx;
      target_y = event.source_y + dy;
    } else {
      target_x = event.source_x;
      target_y = event.source_y;
    }
  }
  target_x = std::clamp(target_x, 0, std::max(0, world.width - 1));
  target_y = std::clamp(target_y, 0, std::max(0, world.height - 1));

  LegacyEffectManager::MagicCreate create;
  create.magic_id = event.magic_id;
  create.server_magic_id = static_cast<int>(
      ((event.actor_id & 0x7FFFU) << 16U) ^ (event.action_started_ms & 0xFFFFU) ^
      static_cast<std::uint64_t>(event.effect_base + event.magic_id));
  create.effect_type = static_cast<int>(event.magic_type);
  create.effect = event.effect;
  create.archive = event.archive;
  create.effect_base = event.effect_base > 0 ? event.effect_base : -1;
  create.source_x = event.source_x;
  create.source_y = event.source_y;
  create.target_x = target_x;
  create.target_y = target_y;
  create.owner_actor_id = event.actor_id;
  create.target_actor_id = event.target_actor_id;
  create.magic_type = event.magic_type;
  create.repetition = true;
  create.now_ms = now_ms;
  create.trace_frame_index = trace_frame_index_;
  create.next_frame_ms = event.next_frame_ms;
  create.frame_count = event.frame_count;
  create.explosion_frame_count = event.explosion_frame_count;
  create.ready_distance = event.ready_distance;
  create.fly_frame_offset = event.fly_frame_offset;
  create.fly_frame_stride = event.fly_frame_stride;
  effects_.spawn_magic_effect(create);
  if (event.kind == LegacySpecialEffectEvent::Kind::magic_projectile && event.magic_id > 0) {
    magic_audio_cues_.push_back(LegacyMagicAudioCue{
        event.actor_id,
        event.magic_id,
        LegacyMagicAudioCuePhase::fire,
    });
  }
}

/// 获取指定角色的渲染姿态
std::optional<ActorRenderPose> AnimationManager::pose_for(const std::uint64_t actor_id) const {
  const auto actor = actor_snapshots_.find(actor_id);
  if (actor == actor_snapshots_.end()) {
    return std::nullopt;
  }
  const auto animation = actors_.find(actor_id);
  if (animation == actors_.end()) {
    return std::nullopt;
  }
  return animation->second.pose_for(actor->second);
}

bool AnimationManager::is_actor_legacy_idle(const std::uint64_t actor_id) const {
  const auto animation = actors_.find(actor_id);
  if (animation == actors_.end()) {
    return false;
  }
  return animation->second.is_legacy_idle();
}

bool AnimationManager::actor_state_changed_this_frame(const std::uint64_t actor_id) const {
  const auto animation = actors_.find(actor_id);
  if (animation == actors_.end()) {
    return false;
  }
  return animation->second.last_state_change_frame() == trace_frame_index_;
}

int AnimationManager::map_object_frame(const MapCell& cell) const {
  return legacy_map_object_frame(cell, clock_.main_ani_count());
}

bool AnimationManager::map_object_blend(const MapCell& cell) const {
  return legacy_map_object_blend(cell);
}

}  // namespace mir2::client
