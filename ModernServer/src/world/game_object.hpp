/**
 * @file game_object.hpp
 * @brief 游戏对象模型头文件，定义了游戏中所有核心实体类型
 * @details 该文件是游戏服务器的实体模型层，定义了以下核心类型：
 *          - LegacyRuntimeTrace: 运行时追踪数据结构
 *          - LegacyEventType/LegacyEventRecord: 事件系统
 *          - LegacyBuffContainer: 状态效果容器
 *          - RuntimeDispatch: 运行时调度输出（所有阶段输出的汇总）
 *          - DamagResult/StatusTickResult: 战斗结算数据
 *          - GameObject 基类：所有游戏实体的基类
 *          - Player 类：玩家角色（完整的角色状态和行为）
 *          - Monster 类：怪物实体（AI、战斗、掉落）
 *          - Npc 类：NPC/商人（交易、任务、对话框）
 *          - EventObject 类：地图事件对象
 *
 *          枚举类型包括：LegacyPlayerState（玩家生命周期状态）、
 *          LegacyBuffKind（Buff 类型）、LegacyRepairMode（修理模式）等。
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"

namespace mir2 {

/**
 * @enum GameObjectKind
 * @brief 游戏对象种类枚举
 * @details 区分四种不同的游戏实体类型，用于运行时类型识别
 *          和帧处理阶段的调度决策。
 */
enum class GameObjectKind {
  player,       ///< 玩家角色
  monster,      ///< 怪物
  npc,          ///< 非玩家角色（商人、任务 NPC）
  event_object  ///< 事件对象（火墙、圣言帷幕等）
};

/**
 * @struct LegacyRuntimeTrace
 * @brief 运行时追踪记录结构
 * @details 用于调试和性能分析的追踪数据结构。每个追踪记录包含
 *          阶段名称、操作描述、关联对象信息、时间戳、随机数状态
 *          和成功/失败状态。在 RuntimeDispatch 中以 legacy_traces
 *          向量形式传递到帧输出。
 */
struct LegacyRuntimeTrace {
  std::string stage{};              ///< 追踪阶段名称
  std::string action{};             ///< 操作描述
  std::string map_id{};             ///< 关联地图 ID
  std::string object_name{};        ///< 关联对象名称
  std::uint64_t actor_id{0};        ///< 关联角色 ID
  std::uint64_t now_ms{0};          ///< 追踪时系统时间（毫秒）
  std::uint64_t current_tick{0};    ///< 追踪时逻辑 tick
  std::size_t cursor{0};            ///< 当前游标位置（用于迭代追踪）
  std::size_t sub_cursor{0};        ///< 子游标位置
  std::uint64_t elapsed_ms{0};      ///< 操作耗时（毫秒）
  std::uint64_t target_actor_id{0}; ///< 目标角色 ID
  std::string command{};            ///< 触发命令
  std::string label{};              ///< 文本标签
  std::uint32_t rng_before{0};      ///< 操作前随机数状态
  std::uint32_t rng_after{0};       ///< 操作后随机数状态
  std::int32_t value{0};            ///< 关联数值
  std::int32_t damage{0};           ///< 伤害数值
  bool success{false};              ///< 操作是否成功
};

/**
 * @enum LegacyEventType
 * @brief 地图事件类型枚举
 * @details 定义地图上可发生的各种事件类型，用于事件对象
 *          创建、检测和生命周期管理。每种事件类型有不同的
 *          行为和交互规则。
 */
enum class LegacyEventType {
  stone_mine,     ///< 矿石采集事件
  digout_zombi,   ///< 挖出僵尸事件（用于传送门洞口检测 need_hole）
  pile_stones,    ///< 堆石事件（阻挡通行）
  holy_curtain,   ///< 圣言帷幕事件（神圣防护领域）
  fire_burn,      ///< 火墙燃烧事件（范围持续伤害）
  sculp_piece     ///< 雕塑碎片事件
};

/**
 * @struct LegacyEventRecord
 * @brief 地图事件记录结构
 * @details 描述一个地图事件的完整状态，包括位置、类型、
 *          开启/关闭时间、运行间隔、关联角色、伤害参数等。
 *          是 LegacyEventManager 管理的基本单位。
 */
struct LegacyEventRecord {
  std::uint64_t id{0};                           ///< 事件唯一标识
  std::string map_id{};                          ///< 所在地图 ID
  std::int32_t x{0};                             ///< 事件 X 坐标
  std::int32_t y{0};                             ///< 事件 Y 坐标
  LegacyEventType type{LegacyEventType::stone_mine}; ///< 事件类型
  std::uint64_t open_start_ms{0};                ///< 开启时间（毫秒）
  std::uint64_t continue_ms{0};                  ///< 持续时长（毫秒）
  std::uint64_t close_time_ms{0};                ///< 自动关闭时间（毫秒）
  std::uint64_t run_start_ms{0};                 ///< 上次运行时间（毫秒）
  std::uint64_t run_tick_ms{500};                ///< 运行间隔（毫秒）
  std::uint64_t owner_actor_id{0};               ///< 拥有者角色 ID
  std::uint64_t holy_group_id{0};                ///< 圣言帷幕组 ID
  std::uint64_t last_damage_ms{0};               ///< 上次造成伤害时间（毫秒）
  std::int32_t event_param{0};                   ///< 事件参数（如火墙强度）
  std::int32_t damage{0};                        ///< 事件伤害值
  bool blocks_walk{false};                       ///< 是否阻挡行走
  bool skip_if_occupied{false};                  ///< 位置被占用时是否跳过
  bool active{true};                             ///< 是否处于激活状态
  bool closed{false};                            ///< 是否已关闭
};

/**
 * @struct LegacyHolyCurtainGroup
 * @brief 圣言帷幕组结构
 * @details 圣言帷幕是一个神圣防护领域效果，由多个事件对象组成一个组。
 *          当敌对角色进入帷幕范围时，会被记录在 seized_actor_ids 中。
 */
struct LegacyHolyCurtainGroup {
  std::uint64_t id{0};                           ///< 帷幕组 ID
  std::string map_id{};                          ///< 所在地图 ID
  std::uint64_t open_start_ms{0};                ///< 开启时间（毫秒）
  std::uint64_t seize_ms{0};                     ///< 捕捉间隔（毫秒）
  std::vector<std::uint64_t> event_ids{};        ///< 关联的事件 ID 列表
  std::vector<std::uint64_t> seized_actor_ids{}; ///< 被捕捉的敌对角色 ID 列表
};

/**
 * @struct LegacyRandomSpaceMoveRequest
 * @brief 随机空间移动请求结构
 * @details 用于请求将指定角色随机传送到地图上的可行走位置。
 *          由 NPC 脚本的 RANDOMMOVE 命令或特定物品触发。
 */
struct LegacyRandomSpaceMoveRequest {
  std::string source_map_id{};   ///< 源地图 ID
  std::string target_map_id{};   ///< 目标地图 ID（跨地图随机传送）
  std::uint64_t actor_id{0};     ///< 目标角色 ID
  std::int32_t magic_id{0};      ///< 关联魔法 ID
};

/**
 * @enum LegacyTimeRecallRequestKind
 * @brief 时间召回请求类型枚举
 */
enum class LegacyTimeRecallRequestKind {
  schedule,  ///< 安排定时召回
  cancel     ///< 取消定时召回
};

/**
 * @struct LegacyTimeRecallRequest
 * @brief 时间召回请求结构
 * @details 定时召回是一种游戏机制，在指定延迟后将玩家传送回
 *          记录的位置。用于地牢逃脱卷轴、回城卷轴等场景。
 *          支持安排和取消两种操作。
 */
struct LegacyTimeRecallRequest {
  LegacyTimeRecallRequestKind kind{LegacyTimeRecallRequestKind::schedule}; ///< 请求类型
  std::uint64_t session_id{0};  ///< 玩家会话 ID
  std::uint64_t actor_id{0};    ///< 角色 ID
  std::string map_id{};         ///< 召回目标地图
  std::int32_t x{0};            ///< 召回目标 X
  std::int32_t y{0};            ///< 召回目标 Y
  std::uint64_t delay_ticks{1}; ///< 延迟 tick 数
};

/**
 * @enum LegacyBatchMoveRequestKind
 * @brief 批次移动请求类型枚举
 */
enum class LegacyBatchMoveRequestKind {
  random_actor_to_map, ///< 将角色随机移动到指定地图
  recall_map,          ///< 召回地图上的所有玩家
  exchange_map         ///< 交换两个地图的所有玩家
};

/**
 * @struct LegacyBatchMoveRequest
 * @brief 批次移动请求结构
 * @details 用于 NPC 脚本的批量地图移动操作。支持随机传送、
 *          整地图召回和两地玩家互换三种模式。操作会延迟指定
 *          tick 数后执行。
 */
struct LegacyBatchMoveRequest {
  LegacyBatchMoveRequestKind kind{LegacyBatchMoveRequestKind::random_actor_to_map}; ///< 移动类型
  std::uint64_t actor_id{0};        ///< 目标角色 ID
  std::string source_map_id{};      ///< 源地图 ID
  std::string target_map_id{};      ///< 目标地图 ID
  std::uint64_t delay_ticks{1};     ///< 执行延迟（tick 数）
};

/**
 * @struct RuntimeDispatch
 * @brief 运行时调度输出结构
 * @details 这是帧处理流水线的核心输出结构，包含了所有阶段的处理结果。
 *          每帧各个阶段（网络、解码、引擎、事件、消息）产生的结果最终
 *          合并到同一个 RuntimeDispatch 中，然后由帧驱动器统一处理。
 *
 *          包含的输出类型：
 *          - session_events: 发送给客户端的事件包
 *          - audit_events: 审计追踪事件
 *          - persist_requests: 持久化保存请求
 *          - cross_map_mails: 跨地图传递的 ActorMail
 *          - legacy_event_creates: 需创建的地图事件
 *          - legacy_holy_curtain_groups: 圣言帷幕组状态更新
 *          - legacy_random_space_moves: 随机空间移动请求
 *          - legacy_time_recall_requests: 时间召回请求
 *          - legacy_batch_move_requests: 批次移动请求
 *          - legacy_traces: 运行时追踪记录
 */
struct RuntimeDispatch {
  std::vector<SessionEvent> session_events{};                    ///< 客户端事件包
  std::vector<AuditEvent> audit_events{};                        ///< 审计事件
  std::vector<PersistRequest> persist_requests{};                ///< 持久化请求
  std::vector<ActorMail> cross_map_mails{};                      ///< 跨地图邮件
  std::vector<LegacyEventRecord> legacy_event_creates{};         ///< 待创建的地图事件
  std::vector<LegacyHolyCurtainGroup> legacy_holy_curtain_groups{}; ///< 圣言帷幕组
  std::vector<LegacyRandomSpaceMoveRequest> legacy_random_space_moves{}; ///< 随机空间移动
  std::vector<LegacyTimeRecallRequest> legacy_time_recall_requests{}; ///< 时间召回请求
  std::vector<LegacyBatchMoveRequest> legacy_batch_move_requests{}; ///< 批次移动请求
  std::vector<LegacyRuntimeTrace> legacy_traces{};               ///< 运行时追踪
};

/**
 * @struct ExperienceResult
 * @brief 经验值结算结果结构
 */
struct ExperienceResult {
  std::int32_t gained{0};          ///< 实际获得的经验值
  std::int32_t display_exp{0};     ///< 显示的经验值（可能受倍数影响）
  bool leveled_up{false};          ///< 是否升级
};

/**
 * @struct DamageResult
 * @brief 伤害结算结果结构
 * @details 包含 HP 伤害、MP 伤害、吸收伤害量以及护盾破碎信息。
 *          由 Player::apply_damage 返回，用于后续广播和处理。
 */
struct DamageResult {
  std::int32_t hp_damage{0};           ///< HP 伤害量
  std::int32_t mp_damage{0};           ///< MP 伤害量
  std::int32_t absorbed_damage{0};     ///< 被护盾吸收的伤害量
  bool shield_broken{false};           ///< 护盾是否破碎
  std::string shield_name{};           ///< 护盾名称
};

/**
 * @struct LegacyEquipmentSpecials
 * @brief 装备特殊属性结构
 * @details 统计玩家装备中所有的特殊属性加成，包括幸运、诅咒、
 *          毒物抗性、不死系克制、魔法护盾、复活、透明等效果。
 *          每次装备变更后通过 refresh_derived_state 重新计算。
 */
struct LegacyEquipmentSpecials {
  std::int32_t luck{0};                   ///< 幸运值
  std::int32_t unluck{0};                 ///< 诅咒值
  std::int32_t anti_poison{0};            ///< 毒物抗性
  std::int32_t undead_power{0};           ///< 不死系克制强度
  std::int32_t mana_to_health{0};         ///< 魔法值转生命值比率
  std::int32_t suck_health_rate{0};       ///< 吸血比率
  bool make_stone{false};                 ///< 是否拥有石化攻击
  bool revival{false};                    ///< 是否拥有复活能力（复活戒指）
  bool magic_shield{false};               ///< 是否拥有魔法护盾（护身戒指）
  bool equipment_transparent{false};       ///< 是否拥有透明效果（隐身戒指）
};

/**
 * @struct TimedStatusEffect
 * @brief 定时状态效果结构
 * @details 用于持续伤害（DOT）、持续治疗（HOT）、减速和护盾效果。
 *          每个效果有独立的 tick 间隔和过期时间，由 tick_status_effects
 *          驱动处理。
 */
struct TimedStatusEffect {
  std::uint64_t source_actor_id{0}; ///< 效果来源角色 ID
  std::uint64_t expire_tick{0};     ///< 过期 tick
  std::uint64_t next_tick{0};       ///< 下次触发 tick
  std::uint64_t tick_interval{0};   ///< 触发间隔（tick 数）
  std::string effect_name{};        ///< 效果名称
  std::int32_t damage_per_tick{0};  ///< 每 tick 伤害值
  std::int32_t slow_percent{0};     ///< 减速百分比
  std::int32_t heal_per_tick{0};    ///< 每 tick 治疗量
  std::int32_t shield_points{0};    ///< 护盾值（吸收伤害）
};

/**
 * @struct StatusTickResult
 * @brief 状态效果 tick 结算结果结构
 * @details 由 tick_status_effects 返回，汇总一次状态效果处理
 *          中的所有变化，包括伤害、治疗、护盾状态和属性变化。
 */
struct StatusTickResult {
  std::int32_t damage{0};              ///< 本次 tick 总伤害
  std::int32_t heal{0};                ///< 本次 tick 总治疗
  std::int32_t absorbed_damage{0};     ///< 护盾吸收的伤害
  std::uint64_t source_actor_id{0};    ///< 伤害来源角色 ID
  bool shield_broken{false};           ///< 护盾是否被击碎
  bool shield_expired{false};          ///< 护盾是否自然消失
  bool ability_changed{false};         ///< 属性是否发生变化
  bool legacy_status_changed{false};   ///< 传统状态位是否变化
  std::string shield_name{};           ///< 护盾效果名称
};

/**
 * @struct LegacyHealthSpellTickResult
 * @brief 生命/魔法自动恢复 tick 结果
 */
struct LegacyHealthSpellTickResult {
  std::int32_t hp{0};     ///< 本次恢复的 HP
  std::int32_t mp{0};     ///< 本次恢复的 MP
  bool changed{false};    ///< 是否有变化
};

/**
 * @enum LegacyBuffKind
 * @brief 传统 Buff 类型枚举
 * @details 定义了游戏中所有状态效果的枚举值。包括各种毒药效果、
 *          透明、防御提升、魔法防御提升、气泡防御和攻击力提升。
 */
enum class LegacyBuffKind : std::int32_t {
  poison_dechealth = 0,   ///< 减血毒（持续 HP 减少）
  poison_damage_armor = 1, ///< 损装备毒（降低装备耐久）
  poison_dont_move = 4,   ///< 定身毒（无法移动）
  poison_stone = 5,       ///< 石化（完全无法行动）
  transparent = 8,        ///< 透明/隐身效果
  defence_up = 9,         ///< 物理防御提升
  magic_defence_up = 10,  ///< 魔法防御提升
  bubble_defence_up = 11, ///< 气泡防御（魔法盾）
  dc_up = 12              ///< 攻击力提升
};

/**
 * @enum LegacyBuffClearPolicy
 * @brief Buff 清除策略枚举
 * @details 定义 Buff 在何种情况下被自动清除。
 *          不同策略影响不同的 Buff 子集。
 */
enum class LegacyBuffClearPolicy {
  death,      ///< 死亡时清除
  leave_map,  ///< 离开地图时清除
  logout      ///< 登出时清除
};

/**
 * @struct LegacyBuffState
 * @brief 单个 Buff 的状态结构
 * @details 描述一个激活中的 Buff 的完整状态，包括类型、
 *          过期时间、作用间隔、等级、来源等。
 */
struct LegacyBuffState {
  LegacyBuffKind kind{LegacyBuffKind::poison_dechealth}; ///< Buff 类型
  std::uint64_t expire_tick{0};       ///< 过期 tick
  std::uint64_t next_tick{0};         ///< 下次触发 tick
  std::uint64_t tick_interval{0};     ///< 触发间隔
  std::int32_t level{0};              ///< 效果等级
  std::uint64_t source_actor_id{0};   ///< 来源角色 ID
  std::int32_t status_bit{0};         ///< 对应的传统状态位
  bool affects_ability{false};        ///< 是否影响面板属性
  bool negative{false};               ///< 是否为负面效果
  bool clear_on_death{true};          ///< 死亡时是否清除
};

/**
 * @struct LegacyBuffClearResult
 * @brief Buff 清除操作的结果
 */
struct LegacyBuffClearResult {
  bool status_changed{false};   ///< 状态位是否发生变化
  bool ability_changed{false};  ///< 面板属性是否发生变化
};

/**
 * @class LegacyBuffContainer
 * @brief Buff 容器类，管理角色身上的所有状态效果
 * @details 提供 Buff 的添加、刷新、查询、清除和 tick 处理功能。
 *          内部使用 vector 存储状态，支持按类型查找和遍历。
 *          用于 Player 和 Monster 的 Buff 管理。
 */
class LegacyBuffContainer {
 public:
  /// @brief 激活或刷新一个 Buff。如果已存在相同类型的 Buff，延长其持续时间
  [[nodiscard]] bool activate_or_refresh(LegacyBuffState state, std::uint64_t current_tick);
  /// @brief 检查指定类型的 Buff 是否在有效期内
  [[nodiscard]] bool active(LegacyBuffKind kind, std::uint64_t current_tick) const;
  /// @brief 检查是否拥有指定类型的 Buff（不论是否过期）
  [[nodiscard]] bool has(LegacyBuffKind kind) const;
  /// @brief 清除指定类型的 Buff
  [[nodiscard]] bool clear(LegacyBuffKind kind);
  /// @brief 按策略清除 Buff（死亡/离开地图/登出）
  [[nodiscard]] LegacyBuffClearResult clear_by_policy(LegacyBuffClearPolicy policy);
  /// @brief 获取指定类型中到期待 tick 的 Buff
  [[nodiscard]] LegacyBuffState* tick_due(LegacyBuffKind kind, std::uint64_t current_tick);
  /// @brief 获取所有过期的 Buff（自动移除并返回）
  [[nodiscard]] std::vector<LegacyBuffState> expire_due(std::uint64_t current_tick);
  /// @brief 获取指定 Buff 的剩余 tick 数
  [[nodiscard]] std::uint64_t remaining_ticks(LegacyBuffKind kind,
                                              std::uint64_t current_tick) const;
  /// @brief 获取所有 Buff 的快照
  [[nodiscard]] std::vector<LegacyBuffState> snapshot() const { return states_; }
  /// @brief 获取指定 Buff（const 版本）
  [[nodiscard]] const LegacyBuffState* get(LegacyBuffKind kind) const;
  /// @brief 获取指定 Buff（可变版本）
  [[nodiscard]] LegacyBuffState* get(LegacyBuffKind kind);

 private:
  std::vector<LegacyBuffState> states_{}; ///< Buff 状态列表
};

/**
 * @struct LegacyMoveThrottleResult
 * @brief 移动限速检查结果
 */
struct LegacyMoveThrottleResult {
  bool allowed{true};       ///< 是否允许移动
  bool disconnect{false};   ///< 是否需要断开连接（检测到加速作弊）
};

/**
 * @struct LegacySpellThrottleResult
 * @brief 技能释放限速检查结果
 */
struct LegacySpellThrottleResult {
  bool allowed{true};       ///< 是否允许释放
  bool disconnect{false};   ///< 是否需要断开连接
  std::int32_t over_count{0}; ///< 超限次数
};

/**
 * @struct LegacyAttackThrottleResult
 * @brief 攻击限速检查结果
 */
struct LegacyAttackThrottleResult {
  bool allowed{true};       ///< 是否允许攻击
  bool disconnect{false};   ///< 是否需要断开连接
  std::int32_t over_count{0}; ///< 超限次数
};

/**
 * @brief 计算传统服务端攻击间隔
 * @param hit_speed 攻击速度值
 * @return 攻击间隔（毫秒）
 * @details 公式：900 - hit_speed * 60，速度值越高攻击越快。
 */
[[nodiscard]] inline std::int32_t legacy_server_attack_interval_ms(
    const std::int32_t hit_speed) {
  return 900 - hit_speed * 60;
}

/**
 * @enum LegacyPlayerState
 * @brief 玩家生命周期状态枚举
 * @details 定义玩家从连入到断开的完整生命周期状态序列：
 *          loading -> ready -> notice_pending -> initialize_pending
 *          -> running -> ghost -> closed。
 *          状态转换由 dispatch_legacy_run_notice、dispatch_legacy_initialize、
 *          legacy_operate_player_running 和 dispatch_legacy_close 驱动。
 */
enum class LegacyPlayerState {
  loading,              ///< 加载中（初始状态）
  ready,                ///< 就绪（等待接入地图）
  notice_pending,       ///< 待发送登录通知（审计事件 world.run_notice）
  initialize_pending,   ///< 待初始化（出生物品、登录序列包发送）
  running,              ///< 正常运行中（每帧处理输入）
  ghost,                ///< 鬼魂状态（尸体过期，等待清理）
  closed                ///< 已关闭（最终状态，对象即将销毁）
};

/**
 * @enum LegacyRepairMode
 * @brief 装备修理模式枚举
 */
enum class LegacyRepairMode {
  normal,  ///< 普通修理
  special  ///< 特殊修理（使用特殊材料）
};

/**
 * @enum LegacyNpcItemMode
 * @brief NPC 交互模式枚举
 * @details 玩家与 NPC 进行物品交互时的模式状态。
 *          用于限制玩家同时只能进行一种交易操作。
 */
enum class LegacyNpcItemMode {
  none,     ///< 无交互
  buy,      ///< 购买模式
  sell,     ///< 出售模式
  repair,   ///< 修理模式
  storage   ///< 仓库存储模式
};

/**
 * @struct LegacyQueuedCommand
 * @brief 排队的玩家命令结构
 * @details 当邮件从帧外到达（如 cross_map_mail）时，会先放入
 *          玩家的 legacy_inbox_ 队列中。在下一帧由
 *          legacy_operate_player_running 消费处理。保证消息顺序
 *          和帧安全。
 */
struct LegacyQueuedCommand {
  ActorMail mail{};                    ///< 待处理的邮件
  std::uint64_t received_ms{0};        ///< 接收时间（毫秒）
  std::uint64_t sequence{0};           ///< 序列号（保证 FIFO 顺序）
};

/**
 * @class MapContext
 * @brief 地图上下文，在邮件处理中传递的上下文信息
 * @details 当 MapActor 派发邮件到 GameObject 时，提供当前 tick、
 *          地图 ID、调度输出指针和配置数据。GameObject.on_mail()
 *          使用 MapContext 来发送回包、审计事件和持久化请求。
 */
class MapContext {
 public:
  std::uint64_t tick{0};                                              ///< 当前逻辑 tick
  std::string map_id{};                                               ///< 当前地图 ID
  RuntimeDispatch* dispatch{nullptr};                                 ///< 运行时调度输出指针
  const std::unordered_map<std::int32_t, ItemConfig>* items{nullptr}; ///< 物品配置表
  const std::unordered_map<std::int32_t, MagicConfig>* magics{nullptr}; ///< 魔法配置表

  /// @brief 通过上下文发送数据包到客户端
  void send_packet(std::uint64_t session_id, LegacyPacket packet) const;
  /// @brief 通过上下文物发审计事件
  void emit_audit(std::string category, std::string message) const;
  /// @brief 通过上下文发起持久化请求
  void request_persist(PersistRequest request) const;
  /// @brief 通过上下文发送跨地图邮件
  void post_cross_map_mail(ActorMail mail) const;
};

/**
 * @class GameObject
 * @brief 游戏对象基类
 * @details 所有游戏实体的基类。提供基本的身份标识（ID、名称、种类）、
 *          空间位置（地图 ID、XY 坐标）和调度信息（下次到期 tick）。
 *          子类包括 Player、Monster、Npc、EventObject。
 *
 *          使用虚拟方法 on_mail 和 on_tick 实现多态行为。
 */
class GameObject {
 public:
  /**
   * @brief 构造 GameObject 实例
   * @param id 对象唯一标识
   * @param kind 对象种类
   * @param name 对象名称
   * @param map_id 所在地图 ID
   * @param x X 坐标
   * @param y Y 坐标
   */
  GameObject(std::uint64_t id, GameObjectKind kind, std::string name, std::string map_id,
             std::int32_t x, std::int32_t y);
  virtual ~GameObject() = default;

  /// @name 查询方法
  //@{
  [[nodiscard]] std::uint64_t id() const { return id_; }
  [[nodiscard]] GameObjectKind kind() const { return kind_; }
  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::string& map_id() const { return map_id_; }
  [[nodiscard]] std::int32_t x() const { return x_; }
  [[nodiscard]] std::int32_t y() const { return y_; }
  [[nodiscard]] std::uint64_t next_due_tick() const { return next_due_tick_; }
  //@}

  /**
   * @brief 处理收到的邮件（虚拟方法，子类重写）
   * @param mail 收到的邮件
   * @param context 地图上下文
   */
  virtual void on_mail(const ActorMail& mail, MapContext& context);

  /**
   * @brief 处理 tick 事件（虚拟方法，子类重写）
   * @param context 地图上下文
   */
  virtual void on_tick(MapContext& context);

 protected:
  /// @brief 设置位置坐标
  void set_position(std::int32_t x, std::int32_t y);
  /// @brief 设置下次到期 tick
  void set_next_due_tick(std::uint64_t next_due_tick);

 private:
  std::uint64_t id_{0};                             ///< 对象 ID
  GameObjectKind kind_{GameObjectKind::event_object}; ///< 对象种类
  std::string name_{};                              ///< 对象名称
  std::string map_id_{};                            ///< 所在地图 ID
  std::int32_t x_{0};                               ///< X 坐标
  std::int32_t y_{0};                               ///< Y 坐标
  std::uint64_t next_due_tick_{1};                  ///< 下次到期 tick
};

/**
 * @class Player
 * @brief 玩家角色类
 * @details 最复杂的游戏实体类，管理玩家的完整状态：
 *
 *          **角色数据管理：**
 *          - CharacterRecord：角色属性、背包、装备、魔法等
 *          - 快照（snapshot/persistent_snapshot）：保存和传输用
 *
 *          **物品操作：**
 *          - 背包管理（添加、移除、查找、压缩）
 *          - 装备管理（穿戴、卸下、耐久度）
 *          - 仓库管理（存取）
 *          - 金币管理
 *
 *          **战斗系统：**
 *          - 伤害计算与吸收
 *          - 攻击限速防加速
 *          - 特殊攻击（PowerHit、FireHit、Rush、CrossHit、WideHit）
 *          - 武器技能（SwordSkill 蓄力）
 *
 *          **状态效果：**
 *          - HP/MP 自动恢复
 *          - Buff 系统（中毒、防御、透明、魔法盾等）
 *          - 定时状态效果（DOT/HOT/护盾）
 *
 *          **生命周期：**
 *          - 状态机（notice_pending -> initialize_pending -> running -> ghost -> closed）
 *          - 死亡与复活（含复活戒指）
 *          - PK 值管理
 *          - 自动保存
 */
class Player : public GameObject {
 public:
  /**
   * @brief 构造 Player 实例
   * @param id 角色 ID
   * @param session_id 网关会话 ID
   * @param character 角色数据记录
   */
  Player(std::uint64_t id, std::uint64_t session_id, CharacterRecord character);

  /// @name 基础查询方法
  //@{
  [[nodiscard]] std::uint64_t session_id() const { return session_id_; }
  [[nodiscard]] const CharacterRecord& character() const { return character_; }
  [[nodiscard]] CharacterRecord snapshot() const;
  [[nodiscard]] CharacterRecord persistent_snapshot() const;
  [[nodiscard]] bool is_dead() const;
  [[nodiscard]] bool in_safe_zone() const { return in_safe_zone_; }
  [[nodiscard]] bool has_free_bag_slot() const;
  [[nodiscard]] bool has_free_storage_slot() const;
  //@}

  /// @name 物品查询方法
  //@{
  [[nodiscard]] bool can_add_bag_item(
      const LegacyUserItem& item,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] LegacyUserItem* bag_item_mutable(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] const LegacyUserItem* bag_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] std::optional<std::size_t> bag_item_index(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] const LegacyUserItem* storage_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] const LegacyUserItem* equipped_item(std::size_t slot) const;
  [[nodiscard]] LegacyUserItem* equipped_item_mutable(std::size_t slot);
  [[nodiscard]] const LegacyUseMagicInfo* learned_magic(std::int32_t magic_id) const;
  [[nodiscard]] LegacyUseMagicInfo* learned_magic_mutable(std::int32_t magic_id);
  //@}

  /// @name 魔法管理
  //@{
  [[nodiscard]] bool add_legacy_magic(std::int32_t magic_id, char key, std::uint8_t level,
                                      std::int32_t cur_train);
  [[nodiscard]] bool remove_legacy_magic(std::int32_t magic_id);
  //@}

  /// @name 金币操作
  //@{
  [[nodiscard]] bool can_spend_gold(std::int32_t amount) const;
  //@}

  /// @name 战斗属性查询
  //@{
  [[nodiscard]] std::int32_t accuracy_point() const { return accuracy_point_; }
  [[nodiscard]] std::int32_t speed_point() const { return speed_point_; }
  [[nodiscard]] std::int32_t legacy_hit_speed() const { return legacy_hit_speed_; }
  [[nodiscard]] const LegacyEquipmentSpecials& legacy_equipment_specials() const {
    return legacy_equipment_specials_;
  }
  [[nodiscard]] std::int32_t legacy_luck() const {
    return legacy_equipment_specials_.luck - legacy_equipment_specials_.unluck;
  }
  [[nodiscard]] std::int32_t legacy_anti_poison() const {
    return legacy_equipment_specials_.anti_poison;
  }
  [[nodiscard]] std::int32_t legacy_undead_power() const {
    return legacy_equipment_specials_.undead_power;
  }
  [[nodiscard]] bool legacy_make_stone() const {
    return legacy_equipment_specials_.make_stone;
  }
  [[nodiscard]] bool legacy_revival_active() const {
    return legacy_equipment_specials_.revival;
  }
  [[nodiscard]] bool legacy_magic_shield_active() const {
    return legacy_equipment_specials_.magic_shield;
  }
  [[nodiscard]] bool legacy_equipment_transparent_active() const {
    return legacy_equipment_specials_.equipment_transparent;
  }
  [[nodiscard]] bool legacy_revival_available(std::uint64_t now_ms) const;
  void mark_legacy_revival(std::uint64_t now_ms);
  [[nodiscard]] std::int32_t apply_legacy_suck_health(std::int32_t damage);
  [[nodiscard]] std::uint8_t attack_mode() const { return character_.attack_mode; }
  [[nodiscard]] std::uint64_t legacy_group_id() const { return legacy_group_id_; }
  [[nodiscard]] std::int32_t pk_point() const { return character_.pk_point; }
  [[nodiscard]] std::int32_t pk_level() const;
  [[nodiscard]] std::int32_t body_luck_level() const;
  [[nodiscard]] std::uint8_t legacy_name_color() const { return legacy_name_color_; }
  [[nodiscard]] std::uint64_t death_time_ms() const { return character_.death_time_ms; }
  [[nodiscard]] std::uint8_t quest_mark(std::int32_t index) const;
  [[nodiscard]] std::uint8_t quest_open_unit(std::int32_t index) const;
  [[nodiscard]] std::uint8_t quest_unit(std::int32_t index) const;
  [[nodiscard]] std::int32_t script_param(std::int32_t index) const;
  [[nodiscard]] std::int32_t script_dice_param(std::int32_t index) const;
  [[nodiscard]] const std::array<std::int32_t, 10>& script_dice_params() const {
    return script_dice_params_;
  }
  [[nodiscard]] std::uint32_t daily_quest() const { return character_.daily_quest; }
  [[nodiscard]] const std::vector<std::uint64_t>& slave_actor_ids() const {
    return slave_actor_ids_;
  }
  //@}

  /// @name 奴隶/宠物管理
  //@{
  void add_slave_actor_id(std::uint64_t actor_id);
  void remove_slave_actor_id(std::uint64_t actor_id);
  void prune_slave_actor_ids(const std::unordered_set<std::uint64_t>& live_slave_ids);
  //@}

  /// @name 物品操作
  //@{
  [[nodiscard]] std::optional<LegacyUserItem> remove_bag_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] std::optional<LegacyUserItem> remove_bag_item_at(std::size_t slot);
  [[nodiscard]] std::optional<LegacyUserItem> remove_storage_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] std::optional<LegacyUserItem> remove_equipped_item(
      std::size_t slot, std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] bool add_bag_item(const LegacyUserItem& item);
  [[nodiscard]] bool add_storage_item(const LegacyUserItem& item);
  //@}

  /// @name 战斗属性计算
  //@{
  [[nodiscard]] std::int32_t melee_power() const;
  [[nodiscard]] std::int32_t legacy_dc_up_bonus() const;
  [[nodiscard]] std::int32_t spell_power(std::int32_t base_power) const;
  [[nodiscard]] std::int32_t physical_defense() const;
  [[nodiscard]] std::int32_t magic_defense() const;
  [[nodiscard]] std::int32_t current_shield_points(std::uint64_t current_tick) const;
  [[nodiscard]] bool has_active_shield(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t current_slow_percent(std::uint64_t current_tick) const;
  [[nodiscard]] bool can_move_at(std::uint64_t current_tick) const;
  //@}

  /// @name 限速检查
  //@{
  [[nodiscard]] LegacyMoveThrottleResult begin_move_attempt(std::uint64_t current_tick,
                                                            std::uint32_t tick_ms);
  [[nodiscard]] LegacySpellThrottleResult begin_spell_attempt(std::uint64_t now_ms,
                                                              std::int32_t delay_time_ms,
                                                              bool sword_skill);
  [[nodiscard]] LegacyAttackThrottleResult begin_attack_attempt(std::uint64_t now_ms);
  void reset_move_throttle();
  //@}

  /// @name 伤害/治疗/状态
  //@{
  [[nodiscard]] DamageResult apply_damage(std::int32_t amount, std::uint64_t current_tick);
  [[nodiscard]] std::int32_t apply_heal(std::int32_t amount);
  [[nodiscard]] std::int32_t apply_spell(std::int32_t amount);
  void queue_legacy_health_spell(std::int32_t hp, std::int32_t mp, std::int32_t healing,
                                 std::uint64_t current_tick,
                                 std::uint64_t tick_interval);
  void queue_legacy_healing(std::int32_t amount, std::uint64_t current_tick,
                            std::uint64_t tick_interval);
  [[nodiscard]] bool legacy_healing_pending() const;
  [[nodiscard]] LegacyHealthSpellTickResult tick_legacy_health_spell(std::uint64_t current_tick);
  [[nodiscard]] bool spend_mp(std::int32_t amount);
  [[nodiscard]] ExperienceResult gain_experience(std::int32_t amount);
  void add_status_effect(TimedStatusEffect effect);
  [[nodiscard]] bool apply_legacy_poison(std::int32_t poison_kind,
                                         std::uint64_t duration_ticks,
                                         std::int32_t poison_level,
                                         std::uint64_t poison_tick_interval,
                                         std::uint64_t source_actor_id,
                                         std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_defence_up(std::uint64_t duration_ticks,
                                                std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                                      std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_dc_up(std::uint64_t duration_ticks,
                                           std::uint64_t current_tick,
                                           std::int32_t bonus);
  [[nodiscard]] bool legacy_transparent_active(std::uint64_t current_tick) const;
  [[nodiscard]] bool activate_legacy_transparent(std::uint64_t duration_ticks,
                                                 std::uint64_t current_tick);
  [[nodiscard]] bool clear_legacy_transparent(std::uint64_t current_tick);
  [[nodiscard]] bool legacy_poison_damage_armor_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::size_t clear_negative_status_effects(std::uint64_t current_tick);
  [[nodiscard]] std::size_t clear_negative_legacy_buffs(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_death(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_leave_map(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_logout(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult tick_status_effects(std::uint64_t current_tick);
  [[nodiscard]] std::vector<LegacyBuffTransferState> legacy_buffs_for_transfer(
      std::uint64_t current_tick) const;
  void restore_legacy_buffs_from_transfer(const std::vector<LegacyBuffTransferState>& states,
                                          std::uint64_t current_tick);
  void consume_move_action(std::uint64_t current_tick, bool running, std::uint32_t tick_ms);
  void restore_full_vitals();
  void equip_item(std::size_t slot, const LegacyUserItem& item);
  void apply_consumable(const ItemConfig& item_config);
  void add_gold(std::int32_t amount);
  void spend_gold(std::int32_t amount);
  void set_legacy_level(std::int32_t level, std::int32_t max_level);
  void set_legacy_exp(std::int32_t exp);
  void set_pk_point(std::int32_t value);
  void set_body_luck_value(double value);
  void set_hair(std::int32_t value);
  void set_job(std::int32_t value);
  void toggle_sex();
  void set_legacy_name_color(std::int32_t value);
  void set_legacy_group_id(std::uint64_t value) { legacy_group_id_ = value; }
  void mark_birth_items_granted();
  void set_guild_membership(std::string guild_name, std::string guild_title);
  void clear_guild_membership();
  bool set_quest_mark(std::int32_t index, std::uint8_t value);
  bool set_quest_open_unit(std::int32_t index, std::uint8_t value);
  bool set_quest_unit(std::int32_t index, std::uint8_t value);
  bool set_script_param(std::int32_t index, std::int32_t value);
  bool set_script_dice_param(std::int32_t index, std::int32_t value);
  void set_daily_quest(std::uint32_t value);
  void refresh_derived_state(const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] StatusTickResult mark_dead(std::uint64_t now_ms);
  [[nodiscard]] bool legacy_death_drop_settled() const { return legacy_death_drop_settled_; }
  void mark_legacy_death_drop_settled() { legacy_death_drop_settled_ = true; }
  void revive_at(std::string map_id, std::int32_t x, std::int32_t y,
                 std::uint16_t hp, std::uint16_t mp);
  void inc_pk_point(std::int32_t amount);
  void add_body_luck(double amount);
  void record_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms);
  [[nodiscard]] bool has_recent_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) const;
  //@}

  /// @name 安全区与状态
  //@{
  void set_in_safe_zone(bool value) { in_safe_zone_ = value; }
  [[nodiscard]] LegacyPlayerState legacy_state() const { return legacy_state_; }
  [[nodiscard]] bool legacy_login_sign() const { return login_sign_; }
  [[nodiscard]] bool legacy_ready_run() const { return ready_run_; }
  [[nodiscard]] bool legacy_ghost() const { return ghost_; }
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const { return run_next_tick_ms_; }
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_last_save_time_ms() const { return last_save_time_ms_; }
  [[nodiscard]] std::size_t legacy_inbox_size() const { return legacy_inbox_.size(); }
  [[nodiscard]] std::vector<std::uint64_t> legacy_inbox_session_sequences() const;
  [[nodiscard]] bool legacy_has_commands() const { return !legacy_inbox_.empty(); }
  [[nodiscard]] bool legacy_see_health_gauge() const { return legacy_see_health_gauge_; }
  void set_legacy_see_health_gauge(bool value) { legacy_see_health_gauge_ = value; }
  [[nodiscard]] bool legacy_slave_relax() const { return slave_relax_; }
  void set_legacy_slave_relax(bool value) { slave_relax_ = value; }
  [[nodiscard]] LegacyRepairMode legacy_repair_mode() const { return legacy_repair_mode_; }
  void set_legacy_repair_mode(LegacyRepairMode mode) { legacy_repair_mode_ = mode; }
  [[nodiscard]] LegacyNpcItemMode legacy_npc_item_mode() const { return legacy_npc_item_mode_; }
  [[nodiscard]] std::uint64_t legacy_npc_item_actor_id() const {
    return legacy_npc_item_actor_id_;
  }
  void set_legacy_npc_item_mode(LegacyNpcItemMode mode, std::uint64_t actor_id) {
    legacy_npc_item_mode_ = mode;
    legacy_npc_item_actor_id_ = mode == LegacyNpcItemMode::none ? 0 : actor_id;
  }
  void clear_legacy_npc_item_mode() {
    set_legacy_npc_item_mode(LegacyNpcItemMode::none, 0);
  }
  //@}

  /// @name 特殊能力状态
  //@{
  [[nodiscard]] bool legacy_magic_bubble_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t legacy_magic_bubble_level() const;
  [[nodiscard]] bool legacy_poison_stone_active(std::uint64_t current_tick) const;
  [[nodiscard]] bool activate_legacy_magic_bubble(std::int32_t level,
                                                  std::uint64_t current_tick,
                                                  std::uint64_t expire_tick);
  void damage_legacy_magic_bubble(std::uint64_t current_tick, std::uint64_t ticks);
  void prepare_legacy_sword_skill(std::int32_t magic_id, std::uint64_t expire_tick);
  [[nodiscard]] std::int32_t pending_legacy_sword_skill(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t consume_legacy_sword_skill(std::uint64_t current_tick);
  void clear_legacy_sword_skill();
  [[nodiscard]] bool legacy_power_hit_ready() const { return legacy_power_hit_ready_; }
  [[nodiscard]] bool consume_legacy_power_hit();
  [[nodiscard]] bool legacy_power_hit_counter_matches(std::int32_t level) const;
  void reset_legacy_power_hit_counter(std::int32_t level, std::int32_t random_point);
  [[nodiscard]] bool advance_legacy_power_hit_counter();
  [[nodiscard]] bool legacy_power_hit_counter_expired() const;
  [[nodiscard]] bool legacy_fire_hit_ready(std::uint64_t now_ms) const;
  void mark_legacy_fire_hit(std::uint64_t now_ms);
  [[nodiscard]] bool legacy_rush_ready(std::uint64_t now_ms) const;
  void mark_legacy_rush(std::uint64_t now_ms);
  [[nodiscard]] bool legacy_item_change_ready(std::uint64_t now_ms) const;
  void mark_legacy_item_change(std::uint64_t now_ms);
  [[nodiscard]] bool legacy_long_hit_enabled() const { return legacy_long_hit_enabled_; }
  void set_legacy_long_hit_enabled(bool value) { legacy_long_hit_enabled_ = value; }
  [[nodiscard]] bool legacy_wide_hit_enabled() const { return legacy_wide_hit_enabled_; }
  void set_legacy_wide_hit_enabled(bool value) { legacy_wide_hit_enabled_ = value; }
  [[nodiscard]] bool legacy_cross_hit_enabled() const { return legacy_cross_hit_enabled_; }
  void set_legacy_cross_hit_enabled(bool value) { legacy_cross_hit_enabled_ = value; }
  [[nodiscard]] bool legacy_open_health_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::uint64_t legacy_open_health_expire_tick() const {
    return legacy_open_health_expire_tick_;
  }
  void activate_legacy_open_health(std::uint64_t expire_tick);
  [[nodiscard]] std::uint32_t legacy_magic_lvexp_generation(std::int32_t magic_id) const;
  std::uint32_t advance_legacy_magic_lvexp_generation(std::int32_t magic_id);
  //@}

  /// @name 生命周期管理
  //@{
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_player_search_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_line_notice_due(std::uint64_t now_ms) const;
  [[nodiscard]] std::size_t legacy_line_notice_index() const { return line_notice_index_; }
  void set_legacy_state(LegacyPlayerState state);
  void mark_legacy_notice_done(std::uint64_t now_ms);
  void mark_legacy_initialize_done(std::uint64_t now_ms);
  void mark_legacy_running_time(std::uint64_t now_ms);
  void mark_legacy_player_search_time(std::uint64_t now_ms);
  void mark_legacy_line_notice_time(std::uint64_t now_ms);
  void mark_legacy_autosaved(std::uint64_t now_ms);
  void mark_legacy_ghost(std::uint64_t now_ms);
  void mark_legacy_closed();
  void rewind_legacy_run_time(std::uint64_t delta_ms);
  //@}

  /// @name 命令队列
  //@{
  void enqueue_legacy_command(ActorMail mail, std::uint64_t now_ms);
  [[nodiscard]] std::optional<LegacyQueuedCommand> pop_legacy_command();
  //@}

  /// @name 虚拟方法重写
  //@{
  void on_mail(const ActorMail& mail, MapContext& context) override;
  void on_tick(MapContext& context) override;
  //@}

 private:
  /**
   * @struct PkHiterInfo
   * @brief PK 攻击者记录结构
   * @details 记录攻击过该玩家的角色 ID 和时间，用于 PK 值计算
   *          和正当防卫判断。在 5 分钟内攻击过该玩家的角色被击杀
   *          不会增加 PK 值。
   */
  struct PkHiterInfo {
    std::uint64_t actor_id{0};    ///< 攻击者角色 ID
    std::uint64_t hit_time_ms{0}; ///< 攻击时间（毫秒）
  };

  std::uint64_t session_id_{0};                           ///< 网关会话 ID
  CharacterRecord character_{};                            ///< 角色数据记录
  LegacyAbility base_ability_{};                           ///< 基础能力值（不含装备加成）
  bool in_safe_zone_{false};                               ///< 是否在安全区
  std::int32_t accuracy_point_{10};                        ///< 准确度点数
  std::int32_t speed_point_{10};                           ///< 速度点数
  LegacyEquipmentSpecials legacy_equipment_specials_{};    ///< 装备特殊属性
  double legacy_suck_health_accumulator_{0.0};             ///< 吸血累积器
  std::uint64_t latest_legacy_revival_time_ms_{0};         ///< 最近复活时间
  std::vector<PkHiterInfo> pk_hiters_{};                   ///< PK 攻击者列表
  std::vector<TimedStatusEffect> status_effects_{};        ///< 定时状态效果列表
  std::uint64_t next_move_tick_{0};                        ///< 下次允许移动的 tick
  std::uint64_t latest_walk_tick_{0};                      ///< 最近行走 tick
  std::int32_t walk_time_over_count_{0};                   ///< 行走超限计数
  std::int32_t walk_time_over_sum_{0};                     ///< 行走超限总和
  std::int32_t speed_hack_timer_over_count_{0};            ///< 加速检测超限计数
  std::uint64_t latest_spell_time_ms_{0};                  ///< 最近施法时间
  std::int32_t latest_spell_delay_ms_{0};                  ///< 最近施法延迟
  std::int32_t spell_time_over_count_{0};                  ///< 施法超限计数
  std::int32_t spell_speed_hack_timer_over_count_{0};      ///< 施法加速检测超限
  std::uint64_t latest_hit_time_ms_{0};                    ///< 最近击中时间
  std::int32_t hit_time_over_count_{0};                    ///< 攻击超限计数
  std::int32_t hit_time_over_sum_{0};                      ///< 攻击超限总和
  std::int32_t hit_speed_hack_timer_over_count_{0};        ///< 攻击加速检测超限
  std::int32_t legacy_hit_speed_{0};                       ///< 传统攻击速度值
  LegacyPlayerState legacy_state_{LegacyPlayerState::notice_pending}; ///< 玩家状态
  bool login_sign_{false};                                 ///< 登录标记
  bool ready_run_{false};                                  ///< 就绪运行标记
  bool ghost_{false};                                      ///< 鬼魂标记
  bool legacy_death_drop_settled_{false};                   ///< 死亡掉落已结算标记
  bool legacy_see_health_gauge_{false};                     ///< 是否可见血量条
  bool slave_relax_{false};                                ///< 宠物放松标记（不攻击）
  LegacyRepairMode legacy_repair_mode_{LegacyRepairMode::normal}; ///< 修理模式
  LegacyNpcItemMode legacy_npc_item_mode_{LegacyNpcItemMode::none}; ///< NPC 交互模式
  std::uint64_t legacy_npc_item_actor_id_{0};              ///< NPC 交互目标 ID
  std::uint8_t legacy_name_color_{255};                    ///< 名字颜色
  std::uint64_t legacy_group_id_{0};                       ///< 队伍 ID
  LegacyBuffContainer legacy_buffs_{};                     ///< Buff 容器
  std::int32_t legacy_prepared_sword_magic_id_{0};         ///< 蓄力中的剑术魔法 ID
  std::uint64_t legacy_prepared_sword_expire_tick_{0};     ///< 蓄力过期 tick
  bool legacy_power_hit_ready_{false};                     ///< 重击就绪
  std::int32_t legacy_power_hit_count_{0};                 ///< 重击计数
  std::int32_t legacy_power_hit_point_count_{0};           ///< 重击点数计数
  std::int32_t legacy_power_hit_level_{-1};                ///< 重击等级
  std::uint64_t legacy_latest_fire_hit_time_ms_{0};        ///< 最近火焰攻击时间
  std::uint64_t legacy_latest_rush_time_ms_{0};            ///< 最近冲锋时间
  std::uint64_t legacy_item_change_time_ms_{0};            ///< 最近物品更换时间
  bool legacy_long_hit_enabled_{false};                    ///< 长距攻击启用
  bool legacy_wide_hit_enabled_{false};                    ///< 范围攻击启用
  bool legacy_cross_hit_enabled_{false};                   ///< 十字攻击启用
  std::uint64_t legacy_open_health_expire_tick_{0};        ///< 开天斩过期 tick
  std::int32_t legacy_inc_health_{0};                      ///< 递增生命值
  std::int32_t legacy_inc_spell_{0};                       ///< 递增魔法值
  std::int32_t legacy_inc_healing_{0};                     ///< 递增治疗量
  std::uint64_t legacy_next_health_spell_tick_{0};         ///< 下次 HP/MP 恢复 tick
  std::uint64_t legacy_health_spell_tick_interval_{1};     ///< HP/MP 恢复间隔
  std::int64_t run_time_ms_{0};                            ///< 运行时间累计（毫秒，可回拨）
  std::uint64_t run_next_tick_ms_{250};                    ///< 下次运行 tick
  std::uint64_t search_time_ms_{0};
  std::uint64_t search_rate_ms_{1000};
  std::uint64_t line_notice_time_ms_{0};
  std::size_t line_notice_index_{0};
  std::uint64_t last_save_time_ms_{0};                     ///< 上次保存时间
  std::uint64_t ghost_time_ms_{0};                         ///< 进入鬼魂状态时间
  std::uint64_t legacy_command_sequence_{0};               ///< 命令序列号计数器
  std::deque<LegacyQueuedCommand> legacy_inbox_{};         ///< 命令队列
  std::unordered_map<std::int32_t, std::uint32_t> legacy_magic_lvexp_generations_{}; ///< 魔法等级代次
  std::vector<std::uint64_t> slave_actor_ids_{};           ///< 宠物/奴隶 ID 列表
  std::array<std::int32_t, 10> script_dice_params_{};      ///< 脚本骰子参数数组
};

/**
 * @struct MonsterSnapshot
 * @brief 怪物快照结构
 * @details 包含怪物的完整状态信息，用于持久化和跨地图传输。
 *          包括基本属性、战斗参数、AI 状态、可见性、目标等信息。
 */
struct MonsterSnapshot {
  std::uint64_t id{0};                          ///< 怪物 ID
  std::string name{};                           ///< 怪物名称
  std::string map_id{};                         ///< 所在地图 ID
  std::int32_t x{0};                            ///< X 坐标
  std::int32_t y{0};                            ///< Y 坐标
  std::uint8_t dir{0};                          ///< 朝向
  std::int32_t level{1};                        ///< 等级
  std::int32_t hp{0};                           ///< 当前生命值
  std::int32_t max_hp{0};                       ///< 最大生命值
  std::int32_t mp{0};                           ///< 当前魔法值
  std::int32_t max_mp{0};                       ///< 最大魔法值
  std::int32_t dc_min{0};                       ///< 最小攻击力
  std::int32_t dc_max{0};                       ///< 最大攻击力
  std::int32_t attack_power{0};                 ///< 攻击强度
  std::int32_t defense{0};                      ///< 物理防御
  std::int32_t magic_defense{0};                ///< 魔法防御
  std::int32_t mc{0};                           ///< 魔法力
  std::int32_t sc{0};                           ///< 道术力
  std::int32_t exp_reward{0};                   ///< 经验奖励
  std::int32_t life_attrib{0};                  ///< 生命属性（0=普通, 1=不死）
  std::int32_t race_server{0};                  ///< 服务端种族
  std::int32_t race_image{0};                   ///< 客户端显示种族
  std::int32_t appearance{0};                   ///< 外观
  std::int32_t cool_eye{0};                     ///< 特殊效果
  std::int32_t speed_point{0};                  ///< 速度点数
  std::int32_t accuracy_point{0};               ///< 准确度点数
  std::int32_t walk_speed_ms{0};                ///< 行走速度（毫秒）
  std::int32_t walk_step{0};                    ///< 行走步幅
  std::int32_t walk_wait_ms{0};                 ///< 行走等待（毫秒）
  std::int32_t attack_speed_ms{0};              ///< 攻击速度（毫秒）
  std::int64_t legacy_run_time_ms{0};           ///< 运行时间
  std::uint64_t legacy_run_next_tick_ms{0};     ///< 下次运行 tick
  std::uint64_t legacy_search_time_ms{0};       ///< 搜索时间
  std::uint64_t legacy_search_rate_ms{0};       ///< 搜索频率
  std::vector<std::uint64_t> legacy_visible_actor_ids{}; ///< 可见角色列表
  std::uint64_t target_actor_id{0};             ///< 目标角色 ID
  std::uint64_t target_focus_time_ms{0};        ///< 目标关注时间
  std::int32_t target_x{-1};                    ///< 目标 X 坐标
  std::int32_t target_y{-1};                    ///< 目标 Y 坐标
  std::uint64_t walk_time_ms{0};                ///< 行走时间
  std::uint64_t hit_time_ms{0};                 ///< 攻击时间
  std::uint64_t search_enemy_time_ms{0};        ///< 搜索敌人时间
  std::uint64_t think_time_ms{0};               ///< AI 思考时间
  std::uint64_t last_hitter_id{0};              ///< 上次攻击者 ID
  std::uint64_t last_hit_time_ms{0};            ///< 上次被击时间
  std::uint64_t exp_hitter_id{0};               ///< 经验归属者 ID
  std::uint64_t exp_hit_time_ms{0};             ///< 经验归属时间
  std::uint64_t death_time_ms{0};               ///< 死亡时间
  std::uint64_t ghost_time_ms{0};               ///< 鬼魂时间
  bool walk_wait_mode{false};                   ///< 行走等待模式
  bool dup_mode{false};                         ///< 是否分身
  bool ghosted{false};                          ///< 是否已鬼魂化
  bool death_settled{false};                    ///< 死亡是否已结算
  std::int32_t chain_shot{0};                   ///< 连锁射击计数
  std::int32_t chain_shot_count{0};             ///< 连锁射击总次数
  bool hide_mode{false};                        ///< 隐藏模式
  bool stick_mode{false};                       ///< 粘附模式
  std::int32_t dig_up_range{0};                 ///< 挖掘上范围
  std::int32_t dig_down_range{0};               ///< 挖掘下范围
  std::uint64_t appear_time_ms{0};              ///< 出现时间
  std::size_t child_actor_count{0};             ///< 子角色数量
  std::int32_t summon_limit{0};                 ///< 召唤限制
  std::uint64_t master_actor_id{0};             ///< 主人角色 ID
  bool is_slave{false};                         ///< 是否为奴隶/宠物
  std::int32_t slave_exp{0};                    ///< 宠物经验
  std::int32_t slave_make_level{0};             ///< 宠物制造等级
  std::int32_t slave_exp_level{0};              ///< 宠物经验等级
  std::uint64_t master_royalty_time_ms{0};      ///< 主人忠诚时间
  std::uint64_t slave_life_time_ms{0};          ///< 宠物存活时间
  bool no_item{false};                          ///< 是否无掉落物品
  bool tameable{true};                          ///< 是否可驯服
};

/**
 * @class Monster
 * @brief 怪物实体类
 * @details 实现怪物的完整生命周期和行为：
 *
 *          **核心属性：** 等级、HP/MP、攻击力、防御力、魔法力、经验值等
 *
 *          **AI 系统：** 基于 MonsterAiProfile 的行为模式，包括搜索、
 *          移动、攻击、特殊技能（远程、束缚、召唤、分身等）
 *
 *          **种族系统：** 支持多种服务端种族（race_server），每种有
 *          不同的行为特性（蜘蛛、弓箭手、僵尸、骷髅王等）
 *
 *          **奴隶系统：** 可被驯服为宠物，有等级经验、忠诚度、存活时间
 *
 *          **战斗系统：** 伤害计算、仇恨系统、命中记录、经验归属
 */
class Monster : public GameObject {
 public:
  /**
   * @brief 构造 Monster 实例
   * @param id 怪物 ID
   * @param name 怪物名称
   * @param map_id 所在地图 ID
   * @param x X 坐标
   * @param y Y 坐标
   * @param level 等级
   * @param max_hp 最大生命值
   * @param attack_power 攻击强度
   * @param dc_min 最小攻击力
   * @param dc_max 最大攻击力
   * @param defense 物理防御
   * @param magic_defense 魔法防御
   * @param mc 魔法力
   * @param sc 道术力
   * @param exp_reward 经验奖励
   * @param life_attrib 生命属性 (0=普通, 1=不死)
   * @param max_mp 最大魔法值
   * @param race_server 服务端种族
   * @param race_image 客户端显示种族
   * @param appearance 外观
   * @param cool_eye 特殊效果
   * @param speed 速度
   * @param accuracy 准确度
   * @param walk_speed_ms 行走速度（毫秒）
   * @param walk_step 行走步幅
   * @param walk_wait_ms 行走等待（毫秒）
   * @param attack_speed_ms 攻击速度（毫秒）
   * @param ai_profile AI 行为模式
   * @param search_rate_ms 搜索频率（毫秒）
   * @param home_x 出生点 X
   * @param home_y 出生点 Y
   * @param home_area 活动范围
   * @param legacy_spawn_group 是否为传统分组刷新
   * @param master_actor_id 主人 ID
   * @param is_slave 是否为宠物
   * @param slave_exp 宠物经验
   * @param slave_make_level 宠物制造等级
   * @param slave_exp_level 宠物经验等级
   * @param master_royalty_time_ms 主人忠诚时间
   * @param slave_life_time_ms 宠物存活时间
   * @param no_item 是否无掉落
   * @param tameable 是否可驯服
   * @param drop_items 掉落物品列表
   * @param drop_gold 掉落金币量
   */
  Monster(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
          std::int32_t level, std::int32_t max_hp, std::int32_t attack_power,
          std::int32_t dc_min, std::int32_t dc_max, std::int32_t defense,
          std::int32_t magic_defense, std::int32_t mc, std::int32_t sc,
          std::int32_t exp_reward,
          std::int32_t life_attrib = 0, std::int32_t max_mp = 0,
          std::int32_t race_server = 0, std::int32_t race_image = 0,
          std::int32_t appearance = 0, std::int32_t cool_eye = 0,
          std::int32_t speed = 0, std::int32_t accuracy = 0,
          std::int32_t walk_speed_ms = 20, std::int32_t walk_step = 1,
          std::int32_t walk_wait_ms = 0, std::int32_t attack_speed_ms = 100,
          MonsterAiProfile ai_profile = MonsterAiProfile::basic,
          std::uint64_t search_rate_ms = 0,
          std::int32_t home_x = 0, std::int32_t home_y = 0, std::int32_t home_area = 0,
          bool legacy_spawn_group = false,
          std::uint64_t master_actor_id = 0, bool is_slave = false,
          std::int32_t slave_exp = 0, std::int32_t slave_make_level = 0,
          std::int32_t slave_exp_level = 0,
          std::uint64_t master_royalty_time_ms = 0,
          std::uint64_t slave_life_time_ms = 0,
          bool no_item = false,
          bool tameable = true,
          std::vector<LegacyUserItem> drop_items = {}, std::int32_t drop_gold = 0);

  /// @name 基础查询方法
  //@{
  [[nodiscard]] bool is_dead() const;
  [[nodiscard]] MonsterSnapshot snapshot() const;
  [[nodiscard]] std::int32_t hp() const { return hp_; }
  [[nodiscard]] std::int32_t max_hp() const { return max_hp_; }
  [[nodiscard]] std::int32_t mp() const { return mp_; }
  [[nodiscard]] std::int32_t max_mp() const { return max_mp_; }
  [[nodiscard]] std::int32_t level() const { return level_; }
  [[nodiscard]] std::int32_t attack_power() const { return attack_power_; }
  [[nodiscard]] std::int32_t dc_min() const { return dc_min_; }
  [[nodiscard]] std::int32_t dc_max() const { return dc_max_ + legacy_dc_up_bonus(); }
  [[nodiscard]] std::int32_t legacy_dc_up_bonus() const;
  [[nodiscard]] std::int32_t physical_defense() const;
  [[nodiscard]] std::int32_t magical_defense() const;
  [[nodiscard]] std::int32_t mc() const { return mc_; }
  [[nodiscard]] std::int32_t sc() const { return sc_; }
  [[nodiscard]] std::int32_t exp_reward() const { return exp_reward_; }
  [[nodiscard]] std::int32_t life_attrib() const { return life_attrib_; }
  [[nodiscard]] bool legacy_undead() const { return life_attrib_ == 1; }
  [[nodiscard]] std::int32_t race_server() const { return race_server_; }
  [[nodiscard]] std::int32_t race_image() const { return race_image_; }
  [[nodiscard]] std::int32_t appearance() const { return appearance_; }
  [[nodiscard]] std::int32_t cool_eye() const { return cool_eye_; }
  [[nodiscard]] std::uint8_t dir() const { return dir_; }
  [[nodiscard]] std::int32_t speed_point() const { return speed_point_; }
  [[nodiscard]] std::int32_t accuracy_point() const { return accuracy_point_; }
  [[nodiscard]] MonsterAiProfile ai_profile() const { return ai_profile_; }
  [[nodiscard]] std::int32_t walk_speed_ms() const { return walk_speed_ms_; }
  [[nodiscard]] std::int32_t walk_step() const { return walk_step_; }
  [[nodiscard]] std::int32_t walk_wait_ms() const { return walk_wait_ms_; }
  [[nodiscard]] std::int32_t attack_speed_ms() const { return attack_speed_ms_; }
  [[nodiscard]] std::int32_t home_x() const { return home_x_; }
  [[nodiscard]] std::int32_t home_y() const { return home_y_; }
  [[nodiscard]] std::int32_t home_area() const { return home_area_; }
  [[nodiscard]] bool legacy_spawn_group() const { return legacy_spawn_group_; }
  [[nodiscard]] std::int32_t drop_gold() const { return drop_gold_; }
  [[nodiscard]] const std::vector<LegacyUserItem>& drop_items() const { return drop_items_; }
  //@}

  /// @name 效果和仇恨
  //@{
  [[nodiscard]] bool legacy_open_health_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::uint64_t legacy_open_health_expire_tick() const;
  void activate_legacy_open_health(std::uint64_t expire_tick);
  [[nodiscard]] std::uint64_t last_hitter_id() const { return last_hitter_id_; }
  [[nodiscard]] std::uint64_t last_hit_time_ms() const { return last_hit_time_ms_; }
  [[nodiscard]] std::uint64_t exp_hitter_id() const { return exp_hitter_id_; }
  [[nodiscard]] std::uint64_t exp_hit_time_ms() const { return exp_hit_time_ms_; }
  [[nodiscard]] std::uint64_t death_time_ms() const { return death_time_ms_; }
  [[nodiscard]] std::uint64_t ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] bool legacy_ghosted() const { return ghosted_; }
  [[nodiscard]] bool death_settled() const { return death_settled_; }
  [[nodiscard]] std::int32_t chain_shot() const { return chain_shot_; }
  [[nodiscard]] std::int32_t chain_shot_count() const { return chain_shot_count_; }
  [[nodiscard]] bool hide_mode() const { return hide_mode_; }
  [[nodiscard]] bool stick_mode() const { return stick_mode_; }
  [[nodiscard]] std::int32_t dig_up_range() const { return dig_up_range_; }
  [[nodiscard]] std::int32_t dig_down_range() const { return dig_down_range_; }
  [[nodiscard]] std::uint64_t appear_time_ms() const { return appear_time_ms_; }
  [[nodiscard]] const std::vector<std::uint64_t>& child_actor_ids() const;
  [[nodiscard]] std::int32_t summon_limit() const { return summon_limit_; }
  [[nodiscard]] const std::string& summon_monster_name() const;
  [[nodiscard]] std::uint64_t summon_delay_ms() const;
  [[nodiscard]] std::uint64_t master_actor_id() const { return master_actor_id_; }
  [[nodiscard]] bool is_slave() const { return is_slave_; }
  [[nodiscard]] std::int32_t slave_exp() const { return slave_exp_; }
  [[nodiscard]] std::int32_t slave_make_level() const { return slave_make_level_; }
  [[nodiscard]] std::int32_t slave_exp_level() const { return slave_exp_level_; }
  [[nodiscard]] std::uint64_t master_royalty_time_ms() const;
  [[nodiscard]] std::uint64_t slave_life_time_ms() const { return slave_life_time_ms_; }
  [[nodiscard]] bool no_item() const { return no_item_; }
  [[nodiscard]] bool tameable() const { return tameable_; }
  [[nodiscard]] std::uint64_t aggro_target_id() const { return aggro_target_id_; }
  [[nodiscard]] std::uint64_t target_actor_id() const { return aggro_target_id_; }
  [[nodiscard]] std::uint64_t target_focus_time_ms() const;
  [[nodiscard]] std::int32_t target_x() const { return target_x_; }
  [[nodiscard]] std::int32_t target_y() const { return target_y_; }
  [[nodiscard]] bool has_target_xy() const { return target_x_ >= 0 && target_y_ >= 0; }
  [[nodiscard]] std::uint64_t walk_time_ms() const { return walk_time_ms_; }
  [[nodiscard]] std::uint64_t hit_time_ms() const { return hit_time_ms_; }
  [[nodiscard]] std::uint64_t search_enemy_time_ms() const;
  [[nodiscard]] std::uint64_t think_time_ms() const { return think_time_ms_; }
  [[nodiscard]] std::uint64_t walk_wait_cur_time_ms() const;
  [[nodiscard]] std::int32_t walk_cur_step() const { return walk_cur_step_; }
  [[nodiscard]] bool walk_wait_mode() const { return walk_wait_mode_; }
  [[nodiscard]] bool dup_mode() const { return dup_mode_; }
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const;
  [[nodiscard]] std::uint64_t legacy_search_time_ms() const { return search_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_rate_ms() const { return search_rate_ms_; }
  [[nodiscard]] const std::vector<std::uint64_t>& legacy_visible_actor_ids() const;
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  //@}

  /// @name 定时检查
  //@{
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_search_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_attack_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_due_by_walk_time(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_attack_due_by_hit_time(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_wait_elapsed(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_holy_seize_active(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_crazy_active(std::uint64_t now_ms) const;
  //@}

  /// @name 状态修改
  //@{
  void make_legacy_holy_seize(std::uint64_t duration_ms, std::uint64_t now_ms);
  void break_legacy_holy_seize();
  void make_legacy_crazy(std::uint64_t duration_ms, std::uint64_t now_ms);
  void break_legacy_crazy();
  [[nodiscard]] std::int32_t apply_damage(std::int32_t amount, std::uint64_t attacker_id);
  [[nodiscard]] std::int32_t apply_damage(std::int32_t amount, std::uint64_t attacker_id,
                                          std::uint64_t now_ms);
  void add_status_effect(TimedStatusEffect effect);
  [[nodiscard]] bool apply_legacy_poison(std::int32_t poison_kind,
                                         std::uint64_t duration_ticks,
                                         std::int32_t poison_level,
                                         std::uint64_t poison_tick_interval,
                                         std::uint64_t source_actor_id,
                                         std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_defence_up(std::uint64_t duration_ticks,
                                                std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                                      std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_dc_up(std::uint64_t duration_ticks,
                                           std::uint64_t current_tick,
                                           std::int32_t bonus);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_death(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_leave_map(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_logout(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult tick_status_effects(std::uint64_t current_tick);
  [[nodiscard]] std::uint64_t next_status_tick() const;
  [[nodiscard]] std::int32_t current_slow_percent(std::uint64_t current_tick) const;
  [[nodiscard]] bool ai_due(std::uint64_t current_tick) const { return current_tick >= next_ai_tick_; }
  //@}

  /// @name 计时器标记
  //@{
  void mark_legacy_run_time(std::uint64_t now_ms);
  void mark_legacy_search_time(std::uint64_t now_ms);
  void refresh_legacy_visible_actor_ids(const std::vector<std::uint64_t>& scanned_actor_ids);
  void mark_legacy_attack_time(std::uint64_t now_ms);
  void mark_legacy_walk_time(std::uint64_t now_ms);
  void mark_legacy_hit_time(std::uint64_t now_ms);
  void mark_search_enemy_time(std::uint64_t now_ms);
  void mark_think_time(std::uint64_t now_ms);
  void mark_legacy_ghost_time(std::uint64_t now_ms);
  void record_legacy_hitter(std::uint64_t attacker_id, std::uint64_t now_ms,
                            bool exp_hitter = true);
  void clear_last_hitter();
  void clear_exp_hitter();
  void clear_legacy_hitters();
  void expire_legacy_hitters(std::uint64_t now_ms);
  [[nodiscard]] StatusTickResult mark_legacy_death(std::uint64_t now_ms);
  void mark_legacy_ghost(std::uint64_t now_ms);
  [[nodiscard]] bool death_due_for_ghost(std::uint64_t now_ms,
                                         std::uint64_t corpse_ms) const;
  void mark_death_settled();
  //@}

  /// @name 行为设置
  //@{
  void set_chain_shot(std::int32_t value);
  void increment_chain_shot();
  void set_chain_shot_count(std::int32_t value);
  void set_hide_mode(bool value);
  void set_stick_mode(bool value);
  void set_dig_ranges(std::int32_t up_range, std::int32_t down_range);
  void set_appear_time_ms(std::uint64_t now_ms);
  void add_child_actor_id(std::uint64_t actor_id);
  void prune_child_actor_ids(const std::unordered_set<std::uint64_t>& live_child_ids);
  void set_summon(std::string monster_name, std::int32_t limit, std::uint64_t delay_ms);
  void set_master_actor_id(std::uint64_t actor_id);
  void configure_slave(std::uint64_t master_actor_id, std::int32_t slave_exp,
                       std::int32_t slave_make_level, std::int32_t slave_exp_level,
                       std::uint64_t master_royalty_time_ms,
                       std::uint64_t slave_life_time_ms, bool no_item);
  void set_master_royalty_time_ms(std::uint64_t value);
  void set_slave_life_time_ms(std::uint64_t value);
  void set_no_item(bool value);
  void set_hp_mp(std::int32_t hp, std::int32_t mp);
  void reduce_hp_to_loyalty_break_floor();
  [[nodiscard]] bool gain_slave_exp(std::int32_t slain_level);
  void schedule_next_ai_tick(std::uint64_t current_tick);
  void set_dir(std::uint8_t dir);
  void select_target(std::uint64_t actor_id, std::uint64_t now_ms);
  void lose_target();
  void set_target_xy(std::int32_t x, std::int32_t y);
  void clear_target_xy();
  void begin_walk_wait(std::uint64_t now_ms);
  void set_walk_wait_mode(bool value);
  void set_dup_mode(bool value);
  void reset_walk_cur_step();
  void increment_walk_cur_step();
  void initialize_legacy_ai_timers(std::uint64_t now_ms,
                                   std::uint64_t walk_offset_ms,
                                   std::uint64_t hit_offset_ms);
  void set_aggro_target_id(std::uint64_t actor_id);
  void clear_aggro_target();
  [[nodiscard]] bool inside_home_area() const;
  //@}

  /// @name 虚拟方法重写
  //@{
  void on_tick(MapContext& context) override;
  //@}

 private:
  /// @brief 根据宠物等级应用能力加成
  void apply_slave_level_abilities();

  std::int32_t level_{1};                             ///< 等级
  std::int32_t hp_{12};                               ///< 当前 HP
  std::int32_t max_hp_{12};                           ///< 最大 HP
  std::int32_t mp_{0};                                ///< 当前 MP
  std::int32_t max_mp_{0};                            ///< 最大 MP
  std::int32_t attack_power_{3};                      ///< 攻击强度
  std::int32_t dc_min_{0};                            ///< 最小攻击力
  std::int32_t dc_max_{3};                            ///< 最大攻击力
  std::int32_t defense_{0};                           ///< 物理防御
  std::int32_t magic_defense_{0};                     ///< 魔法防御
  std::int32_t mc_{0};                                ///< 魔法力
  std::int32_t sc_{0};                                ///< 道术力
  std::int32_t exp_reward_{12};                       ///< 经验奖励
  std::int32_t life_attrib_{0};                       ///< 生命属性（0=普通, 1=不死）
  std::int32_t race_server_{0};                       ///< 服务端种族
  std::int32_t race_image_{0};                        ///< 客户端显示种族
  std::int32_t appearance_{0};                        ///< 外观
  std::int32_t cool_eye_{0};                          ///< 特殊效果
  std::uint8_t dir_{4};                               ///< 朝向
  std::int32_t speed_point_{0};                       ///< 速度点数
  std::int32_t accuracy_point_{0};                    ///< 准确度点数
  std::int32_t walk_speed_ms_{20};                    ///< 行走速度（毫秒）
  std::int32_t walk_step_{1};                         ///< 行走步幅
  std::int32_t walk_wait_ms_{0};                      ///< 行走等待（毫秒）
  std::int32_t attack_speed_ms_{100};                 ///< 攻击速度（毫秒）
  MonsterAiProfile ai_profile_{MonsterAiProfile::basic}; ///< AI 行为模式
  std::int32_t home_x_{0};                            ///< 出生点 X
  std::int32_t home_y_{0};                            ///< 出生点 Y
  std::int32_t home_area_{0};                         ///< 活动范围
  bool legacy_spawn_group_{false};                    ///< 传统分组刷新标记
  std::vector<LegacyUserItem> drop_items_{};          ///< 掉落物品列表
  std::int32_t drop_gold_{0};                         ///< 掉落金币量
  std::uint64_t legacy_open_health_expire_tick_{0};   ///< 开天斩过期 tick
  std::uint64_t last_hitter_id_{0};                   ///< 上次攻击者 ID
  std::uint64_t last_hit_time_ms_{0};                 ///< 上次被击时间
  std::uint64_t exp_hitter_id_{0};                    ///< 经验归属者 ID
  std::uint64_t exp_hit_time_ms_{0};                  ///< 经验归属时间
  std::uint64_t death_time_ms_{0};                    ///< 死亡时间
  bool ghosted_{false};                               ///< 是否已鬼魂化
  bool death_settled_{false};                         ///< 死亡是否已结算
  std::uint64_t aggro_target_id_{0};                  ///< 当前仇恨目标 ID
  std::uint64_t next_ai_tick_{0};                     ///< 下次 AI 处理 tick
  std::int64_t run_time_ms_{0};                       ///< 运行时间
  std::uint64_t run_next_tick_ms_{250};               ///< 下次运行 tick
  std::uint64_t attack_time_ms_{0};                   ///< 攻击时间
  std::uint64_t search_time_ms_{0};                   ///< 搜索时间
  std::uint64_t search_rate_ms_{0};                   ///< 搜索频率
  std::vector<std::uint64_t> legacy_visible_actor_ids_{}; ///< 可见角色列表
  std::uint64_t walk_time_ms_{0};                     ///< 行走时间
  std::uint64_t hit_time_ms_{0};                      ///< 攻击时间
  std::uint64_t search_enemy_time_ms_{0};             ///< 搜索敌人时间
  std::uint64_t think_time_ms_{0};                    ///< AI 思考时间
  std::uint64_t walk_wait_cur_time_ms_{0};            ///< 行走等待起始时间
  std::uint64_t target_focus_time_ms_{0};             ///< 目标关注时间
  std::int32_t target_x_{-1};                         ///< 目标 X 坐标（-1=无目标）
  std::int32_t target_y_{-1};                         ///< 目标 Y 坐标（-1=无目标）
  std::int32_t walk_cur_step_{0};                     ///< 当前行走步数
  bool walk_wait_mode_{false};                        ///< 行走等待模式
  bool dup_mode_{false};                              ///< 是否分身
  std::uint64_t ghost_time_ms_{0};                    ///< 鬼魂时间
  std::int32_t chain_shot_{0};                        ///< 连锁射击计数
  std::int32_t chain_shot_count_{0};                  ///< 连锁射击总次数
  bool hide_mode_{false};                             ///< 隐藏模式
  bool stick_mode_{false};                            ///< 粘附模式
  std::int32_t dig_up_range_{0};                      ///< 挖掘上范围
  std::int32_t dig_down_range_{0};                    ///< 挖掘下范围
  std::uint64_t appear_time_ms_{0};                   ///< 出现时间
  std::vector<std::uint64_t> child_actor_ids_{};      ///< 子角色 ID 列表
  std::int32_t summon_limit_{0};                      ///< 召唤限制
  std::string summon_monster_name_{};                 ///< 召唤怪物名称
  std::uint64_t summon_delay_ms_{0};                  ///< 召唤延迟
  std::uint64_t master_actor_id_{0};                  ///< 主人 ID
  bool is_slave_{false};                              ///< 是否为宠物
  std::int32_t slave_exp_{0};                         ///< 宠物经验
  std::int32_t slave_make_level_{0};                  ///< 宠物制造等级
  std::int32_t slave_exp_level_{0};                   ///< 宠物经验等级
  std::uint64_t master_royalty_time_ms_{0};           ///< 主人忠诚时间
  std::uint64_t slave_life_time_ms_{0};               ///< 宠物存活时间
  bool no_item_{false};                               ///< 是否无掉落
  bool tameable_{true};                               ///< 是否可驯服
  std::uint64_t legacy_holy_seize_until_ms_{0};       ///< 圣言捕捉到期时间
  std::uint64_t legacy_crazy_until_ms_{0};            ///< 疯狂状态到期时间
  std::int32_t base_max_hp_{12};                      ///< 基础最大 HP（不含宠物加成）
  std::int32_t base_dc_max_{3};                       ///< 基础最大攻击力（不含宠物加成）
  std::int32_t base_magic_defense_{0};                ///< 基础魔法防御（不含宠物加成）
  std::vector<TimedStatusEffect> status_effects_{};   ///< 定时状态效果
  LegacyBuffContainer legacy_buffs_{};                ///< Buff 容器
};

/**
 * @class Npc
 * @brief 非玩家角色（NPC/商人）类
 * @details 管理 NPC 的交互行为，包括：
 *          - 商人交易（购买、出售、修理、仓库存储）
 *          - 任务对话（基于脚本的 NPC 对话框系统）
 *          - 武器升级服务
 *          - 公会和城堡管理功能
 *          - 商品管理（库存刷新、价格设置）
 */
class Npc : public GameObject {
 public:
  /**
   * @brief 构造 Npc 实例
   * @param id NPC ID
   * @param name NPC 名称
   * @param map_id 所在地图 ID
   * @param x X 坐标
   * @param y Y 坐标
   * @param service 服务类型字符串
   * @param merchant_items 商人物品列表
   * @param dialog_sections 对话框脚本段落
   * @param price_rate_percent 价格倍率（百分比，100=标准）
   * @param merchant_key 商人持久化键名
   * @param merchant_products 商品产品配置
   * @param merchant_prices 自定义价格表
   * @param deal_std_modes 支持的交易标准模式
   * @param weapon_upgrades 武器升级记录
   */
  Npc(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
      std::string service, std::vector<LegacyUserItem> merchant_items,
      std::vector<NpcDialogSectionConfig> dialog_sections,
      std::int32_t price_rate_percent = 100, std::string merchant_key = {},
      std::vector<MerchantProductRuntimeConfig> merchant_products = {},
      std::unordered_map<std::int32_t, std::int32_t> merchant_prices = {},
      std::vector<std::int32_t> deal_std_modes = {},
      std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades = {});

  /// @name 服务支持查询
  //@{
  [[nodiscard]] bool supports_buy() const;
  [[nodiscard]] bool supports_sell() const;
  [[nodiscard]] bool supports_repair() const;
  [[nodiscard]] bool supports_storage() const;
  [[nodiscard]] bool supports_guild() const;
  [[nodiscard]] bool supports_castle() const;
  [[nodiscard]] bool supports_weapon_upgrade() const;
  //@}

  /// @name 定时检查
  //@{
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_search_due(std::uint64_t now_ms) const;
  //@}

  /// @name 属性查询
  //@{
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const { return run_next_tick_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_time_ms() const { return search_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_rate_ms() const { return search_rate_ms_; }
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_refill_time_ms() const { return refill_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_verify_time_ms() const { return verify_time_ms_; }
  [[nodiscard]] const std::string& service() const { return service_; }
  [[nodiscard]] const std::string& merchant_key() const { return merchant_key_; }
  [[nodiscard]] std::int32_t price_rate_percent() const { return price_rate_percent_; }
  [[nodiscard]] const std::vector<LegacyUserItem>& merchant_items() const;
  [[nodiscard]] std::vector<LegacyUserItem>& merchant_items_mutable();
  [[nodiscard]] const std::vector<LegacyWeaponUpgradeRecord>& weapon_upgrades() const;
  [[nodiscard]] std::vector<LegacyWeaponUpgradeRecord>& weapon_upgrades_mutable();
  [[nodiscard]] const std::vector<MerchantProductRuntimeConfig>& merchant_products() const;
  [[nodiscard]] std::vector<MerchantProductRuntimeConfig>& merchant_products_mutable();
  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>& merchant_prices() const;
  [[nodiscard]] std::optional<std::int32_t> merchant_price(std::int32_t item_id) const;
  void set_merchant_price(std::int32_t item_id, std::int32_t price);
  [[nodiscard]] bool deals_std_mode(std::int32_t std_mode) const;
  void apply_merchant_state(const MerchantStateRecord& state);
  [[nodiscard]] MerchantStateRecord snapshot_merchant_state() const;
  [[nodiscard]] const std::vector<NpcDialogSectionConfig>& dialog_sections() const;
  //@}

  /// @name 计时器标记
  //@{
  void mark_legacy_run_time(std::uint64_t now_ms);
  void mark_legacy_search_time(std::uint64_t now_ms);
  void mark_legacy_ghost_time(std::uint64_t now_ms);
  void mark_legacy_refill_time(std::uint64_t now_ms);
  void mark_legacy_verify_time(std::uint64_t now_ms);
  //@}

  /// @name 虚拟方法重写
  //@{
  void on_tick(MapContext& context) override;
  //@}

 private:
  std::string service_{};                                          ///< 服务类型
  std::string merchant_key_{};                                     ///< 商人持久化键名
  std::vector<LegacyUserItem> merchant_items_{};                   ///< 商品列表
  std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades_{};       ///< 武器升级记录
  std::vector<MerchantProductRuntimeConfig> merchant_products_{};  ///< 商品产品配置
  std::unordered_map<std::int32_t, std::int32_t> merchant_prices_{}; ///< 自定义价格表
  std::vector<std::int32_t> deal_std_modes_{};                     ///< 支持的交易标准模式
  std::vector<NpcDialogSectionConfig> dialog_sections_{};          ///< 对话框脚本段落
  std::int32_t price_rate_percent_{100};                           ///< 价格倍率
  bool buy_enabled_{false};                                        ///< 购买功能是否启用
  bool weapon_upgrade_enabled_{false};                             ///< 武器升级功能是否启用
  std::int64_t run_time_ms_{0};                                    ///< 运行时间
  std::uint64_t run_next_tick_ms_{1000};                           ///< 下次运行 tick
  std::uint64_t search_time_ms_{0};                                ///< 搜索时间
  std::uint64_t search_rate_ms_{1000};                             ///< 搜索频率
  std::uint64_t ghost_time_ms_{0};                                 ///< 鬼魂时间
  std::uint64_t refill_time_ms_{0};                                ///< 补货时间
  std::uint64_t verify_time_ms_{0};                                ///< 验证时间
};

/**
 * @class EventObject
 * @brief 地图事件对象类
 * @details 表示地图上的一个临时事件对象，如矿石采集点、
 *          堆石障碍物、火墙等。这些对象通常有生命周期并由
 *          LegacyEventManager 管理，其 tick 处理负责检查
 *          过期和更新状态。
 */
class EventObject : public GameObject {
 public:
  /**
   * @brief 构造 EventObject 实例
   * @param id 事件对象 ID
   * @param name 事件对象名称
   * @param map_id 所在地图 ID
   * @param x X 坐标
   * @param y Y 坐标
   */
  EventObject(std::uint64_t id, std::string name, std::string map_id, std::int32_t x,
              std::int32_t y);

  /// @brief tick 处理（检查过期等）
  void on_tick(MapContext& context) override;
};

}  // namespace mir2
