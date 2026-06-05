/**
 * @file map_actor.hpp
 * @brief MapActor 类声明 - 地图演员的核心管理类
 * @details 该文件声明了 MapActor 类，它是单个地图实例的核心管理对象，负责：
 *          - 玩家生命周期管理（生成、初始化、运行、关闭、断开）
 *          - 怪物生命周期管理（生成、AI 行为、死亡、重生）
 *          - 商人/NPC 处理（脚本执行、对话、交易、武器升级）
 *          - 地图事件管理（传送门、门开关、火墙、圣言等事件）
 *          - 物品管理（地面物品、掉落、拾取、过期清理）
 *          - 可见性管理（玩家视野同步、物品/事件可见性）
 *          - 传送转移（传送门、物品触发的跨地图移动、随机卷轴）
 *          - 交易系统（玩家间交易、出价、确认、提交）
 *          - 宠物奴隶系统（召唤、驯服、跟随、生命周期）
 *          - GM 命令处理（等级调整、装备生成、魔法修改等）
 *
 *          MapActor 通过 ActorMail 消息队列与外部系统通信，
 *          每帧通过 tick() 处理待决消息并运行维护逻辑。
 *          继承自 game_object.hpp 中定义的 GameObject 类型体系。
 *
 * @see game_object.hpp
 * @see map_actor_movement.hpp
 * @see map_actor_player.hpp
 * @see map_actor_visibility.hpp
 */

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "shared/legacy/map_document.hpp"
#include "core/wheel_timer.hpp"
#include "world/game_object.hpp"
#include "world/legacy_map_environment.hpp"
#include "world/legacy_name_list_repository.hpp"
#include "world/legacy_random.hpp"
#include "world/make_index_allocator.hpp"

namespace mir2 {

struct LegacyMagicTrainResult;

/**
 * @class MapActor
 * @brief 地图演员核心管理类，管理单个地图实例上的所有游戏对象
 * @details MapActor 是每个地图的核心管理器，包含该地图上所有游戏对象
 *          （玩家、怪物、NPC、事件对象）的完整生命周期管理逻辑。
 *          通过 ActorMail 消息队列接收外部请求，每帧驱动游戏逻辑运行。
 *
 *          核心职责包括：
 *          - 邮件处理：通过 enqueue_mail/drain_pending_mail 处理异步消息
 *          - 帧驱动：tick() 方法每帧调用，驱动所有对象更新
 *          - 玩家管理：生成、初始化、每帧运行、关闭断开
 *          - 怪物管理：AI 行为、战斗、死亡、重生调度
 *          - NPC 脚本：对话脚本执行、条件评估、商店操作
 *          - 可见性：每个玩家的可见对象集维护和增量同步
 *          - 传送系统：传送门、物品传送、随机卷轴
 *          - 交易系统：玩家间物品/金币交换
 *          - 宠物系统：召唤/驯服奴隶、跟随主人、战斗
 *
 * @note MapActor 不直接处理网络 I/O，所有外部输入通过 ActorMail 入队
 * @warning 跨地图操作（如传送）涉及从当前 MapActor 移除对象并发送到另一个 MapActor
 *
 * @see logic_runtime.hpp LogicRuntime 管理所有 MapActor 实例
 */
class MapActor {
 public:
  // ─── 内嵌类型定义 ───────────────────────────────────────────────

  /**
   * @struct GroundItem
   * @brief 地面物品数据结构
   * @details 表示地图上掉落的物品实例，包含物品属性、所有权、过期时间等信息。
   *          支持金币和普通物品两种类型，具有所有权保护和自动过期机制。
   */
  struct GroundItem {
    std::uint64_t id{0};                ///< 地面物品唯一标识符
    bool is_gold{false};                ///< 是否为金币堆
    std::int32_t gold_amount{0};        ///< 金币数量（is_gold=true 时有效）
    LegacyUserItem item{};              ///< 物品数据（非金币时有效）
    std::string name{};                 ///< 物品显示名称
    std::int32_t count{1};              ///< 物品堆叠数量
    std::int32_t looks{0};              ///< 外观索引
    std::int32_t ani_count{0};          ///< 动画变体计数
    std::int32_t x{0};                  ///< 地图 X 坐标
    std::int32_t y{0};                  ///< 地图 Y 坐标
    std::uint64_t owner_actor_id{0};    ///< 拥有者角色 ID（所有权保护用）
    std::uint64_t drop_time_ms{0};      ///< 掉落时间毫秒
    std::uint64_t ownership_expire_ms{0}; ///< 所有权保护过期时间
    std::uint64_t expire_time_ms{0};    ///< 物品消失过期时间
    std::uint64_t dropper_actor_id{0};  ///< 掉落者角色 ID
    std::string dropper_name{};         ///< 掉落者名称
    bool death_drop{false};             ///< 是否为死亡掉落（PK 保护相关）
  };

  struct StoneMineSnapshot {
    std::int32_t mine_count{0};
    std::int32_t mine_fill_count{0};
    std::uint64_t refill_time_ms{0};
    std::int32_t type{0};
  };

  /**
   * @struct LegacyGmCommandResult
   * @brief GM 命令执行结果
   * @details 封装 GM 命令执行后的完整结果，包括分发输出、
   *          是否处理、是否成功、原因描述和消息列表。
   */
  struct LegacyGmCommandResult {
    RuntimeDispatch dispatch{};         ///< 运行时调度输出
    bool handled{false};                ///< 命令是否被识别处理
    bool success{false};                ///< 命令执行是否成功
    std::string reason{};               ///< 失败原因描述
    std::vector<std::string> messages{}; ///< 返回给玩家的消息列表
  };

  /**
   * @struct LegacyScriptMapHooks
   * @brief 脚本引擎地图钩子函数集合
   * @details 提供给 NPC 脚本引擎的回调函数，用于脚本中查询地图状态。
   *          支持获取怪物/玩家数量以及清除怪物。
   *          由 LogicRuntime 注入，使脚本能跨地图查询。
   */
  struct LegacyScriptMapHooks {
    /// 获取指定地图的怪物数量
    std::function<std::int32_t(std::string_view map_id)> monster_count{};
    /// 获取指定地图的玩家数量
    std::function<std::int32_t(std::string_view map_id)> player_count{};
    /// 清除指定地图的所有怪物
    std::function<std::int32_t(std::string_view map_id, RuntimeDispatch& dispatch,
                               std::uint64_t current_tick, std::uint64_t now_ms)>
        clear_monsters{};
  };

  // ─── 构造函数 ───────────────────────────────────────────────────

  /**
   * @brief 构造函数
   * @param config 地图配置
   * @param budgets 逻辑预算配置（每帧各类对象处理数量上限）
   * @param item_configs 物品配置表（ID -> ItemConfig）
   * @param magic_configs 魔法配置表（ID -> MagicConfig）
   * @param map_quests 地图任务配置列表
   * @param castle_dialog_context 城堡对话框上下文
   * @param monster_defs 怪物定义配置表（名称 -> MonsterDefConfig）
   * @param map_entry_rules 地图入口规则配置表（地图ID -> MapEntryRuleConfig）
   * @param make_index_allocator 制造索引分配器（可选，用于跨地图唯一性）
   * @param black_stone_name 黑铁矿石名称（武器升级用）
   * @param legacy_approval_mode 传统审批模式标志
   * @param script_global_params 脚本全局参数数组（10个整型参数）
   * @param script_name_lists 脚本名称列表仓库
   * @param startup_quest_dialog_sections 启动任务对话段
   */
  MapActor(MapConfig config, LogicBudgetConfig budgets,
           std::unordered_map<std::int32_t, ItemConfig> item_configs,
           std::unordered_map<std::int32_t, MagicConfig> magic_configs,
           std::vector<MapQuestConfig> map_quests = {},
           CastleDialogContext castle_dialog_context = {},
           std::unordered_map<std::string, MonsterDefConfig> monster_defs = {},
           std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules = {},
           MakeIndexAllocator* make_index_allocator = nullptr,
           std::string black_stone_name = "BlackStone",
           bool legacy_approval_mode = false,
           std::shared_ptr<std::array<std::int32_t, 10>> script_global_params = nullptr,
           std::shared_ptr<LegacyNameListRepository> script_name_lists = nullptr,
           std::vector<NpcDialogSectionConfig> startup_quest_dialog_sections = {});

  // ─── 公共接口 ───────────────────────────────────────────────────

  /**
   * @brief 将邮件加入待处理队列
   * @param mail 待处理的演员邮件
   * @details 外部系统通过该方法向此地图发送异步请求。
   *          邮件将在下一帧的 drain_pending_mail 中被处理。
   */
  void enqueue_mail(ActorMail mail);

  /**
   * @brief 设置传统随机数生成器
   * @param legacy_random 随机数生成器指针
   */
  void set_legacy_random(LegacyRandom* legacy_random);

  /**
   * @brief 设置脚本地图钩子
   * @param hooks 钩子函数集合
   */
  void set_legacy_script_map_hooks(LegacyScriptMapHooks hooks);

  /**
   * @brief 应用商人状态记录
   * @param state 商人状态记录
   * @return 是否成功应用
   * @details 用于恢复商人状态（如商店物品库存变化）。
   */
  bool apply_merchant_state(const MerchantStateRecord& state);

  /**
   * @brief 设置城堡对话框上下文
   * @param castle_dialog_context 城堡对话框上下文
   */
  void set_castle_dialog_context(CastleDialogContext castle_dialog_context);

  /**
   * @brief 设置行会城堡快照
   * @param guild_castle_snapshot 行会城堡快照
   */
  void set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot);

  /**
   * @brief 排空待处理邮件队列
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 合并的运行时调度输出
   * @details 处理 mailbox_ 中所有待处理的 ActorMail，按邮件类型分发到
   *          对应的处理函数。这是每帧帧处理流水线的第一阶段。
   */
  [[nodiscard]] RuntimeDispatch drain_pending_mail(std::uint64_t current_tick,
                                                   std::uint64_t now_ms);

  /**
   * @brief 运行维护帧（门过期、物品过期、怪物重生检查等）
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 合并的运行时调度输出
   * @details 完成地图级别的周期维护工作：
   *          - 关闭超时的门
   *          - 移除过期的地面物品
   *          - 检查怪物重生调度
   *          - 清理过期数据缓存
   */
  [[nodiscard]] RuntimeDispatch run_maintenance_tick(std::uint64_t current_tick,
                                                     std::uint64_t now_ms);

  /**
   * @brief 主帧 tick 函数（使用系统时间）
   * @param current_tick 当前逻辑 tick
   * @return 合并的运行时调度输出
   * @details 自动获取当前系统时间调用双参数版本。主入口函数。
   */
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t current_tick);

  /**
   * @brief 主帧 tick 函数（指定时间）
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 合并的运行时调度输出
   * @details 完整版本的帧处理入口，依次执行：
   *          1. 排空待处理邮件队列
   *          2. 运行维护帧
   *          3. 处理所有就绪对象的更新
   *          4. 关闭过期门
   */
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t current_tick, std::uint64_t now_ms);

  /**
   * @brief 关闭所有超时的门
   * @param now_ms 当前系统时间（毫秒）
   * @return 运行时调度输出
   * @details 检查所有已开门的状态，关闭超过持续时间的门并广播给附近玩家。
   */
  [[nodiscard]] RuntimeDispatch close_expired_doors(std::uint64_t now_ms);

  /**
   * @brief 生成玩家角色
   * @param mail 包含玩家角色数据的生成邮件
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param fast_initialize 是否快速初始化（跳过登录序列包发送？）
   * @param run_startup_quest 是否运行启动任务脚本
   * @return 运行时调度输出
   * @details 创建 Player 对象、插入 objects_ 映射、设置初始位置、
   *          发送登录序列包（如果非快速初始化）。
   *          支持跨地图传送和首次登录两种场景。
   */
  [[nodiscard]] RuntimeDispatch legacy_spawn_player(const ActorMail& mail,
                                                    std::uint64_t current_tick,
                                                    std::uint64_t now_ms,
                                                    bool fast_initialize,
                                                    bool run_startup_quest = false);

  /**
   * @brief 处理单个玩家的帧更新
   * @param actor_id 玩家角色 ID
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param persistence_overloaded 持久化层是否过载
   * @param player_input_budget_per_tick 每帧玩家输入处理预算（0表示无限制）
   * @return 运行时调度输出
   * @details 处理玩家从 notice_pending -> initialize_pending -> running -> ghost 的
   *          完整状态机转换。在 running 状态下执行周期操作和玩家输入消费。
   */
  [[nodiscard]] RuntimeDispatch legacy_process_player(std::uint64_t actor_id,
                                                      std::uint64_t current_tick,
                                                      std::uint64_t now_ms,
                                                      bool persistence_overloaded,
                                                      std::size_t player_input_budget_per_tick = 0);

  /**
   * @brief 处理单个怪物的帧更新
   * @param actor_id 怪物角色 ID
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param cursor 怪物处理游标（用于轮询调度）
   * @param sub_cursor 子游标（用于复杂 AI 的细分调度）
   * @return 运行时调度输出
   * @details 驱动怪物的状态机：空闲、搜索、追踪、攻击、返回出生点等。
   *          根据怪物种族调用不同的 AI 行为。
   */
  [[nodiscard]] RuntimeDispatch legacy_process_monster(std::uint64_t actor_id,
                                                       std::uint64_t current_tick,
                                                       std::uint64_t now_ms,
                                                       std::size_t cursor,
                                                       std::size_t sub_cursor);

  /**
   * @brief 检查怪物是否存活
   * @param actor_id 怪物角色 ID
   * @return true 如果怪物对象存在且未死亡
   */
  [[nodiscard]] bool legacy_monster_alive(std::uint64_t actor_id) const;

  /**
   * @brief 检查怪物是否计入重生计数
   * @param actor_id 怪物角色 ID
   * @return true 如果怪物存在且应计入重生配额
   * @details 用于判断该怪物是否应该被计入重生上限计数。
   *          排除已经准备重生或即将消失的怪物。
   */
  [[nodiscard]] bool legacy_monster_counts_for_spawn(std::uint64_t actor_id) const;

  /**
   * @brief 获取当前存活的怪物数量
   * @return 存活怪物总数
   */
  [[nodiscard]] std::int32_t legacy_live_monster_count() const;

  /**
   * @brief 获取当前存活的玩家数量
   * @return 存活玩家总数
   */
  [[nodiscard]] std::int32_t legacy_live_player_count() const;

  /**
   * @brief 清除地图上的所有怪物
   * @param dispatch 运行时调度输出
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 被清除的怪物数量
   * @details 遍历所有怪物对象，发送死亡广播并移除。
   *          用于脚本命令和 GM 操作。
   */
  [[nodiscard]] std::int32_t legacy_clear_monsters(RuntimeDispatch& dispatch,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms);

  /**
   * @brief 检查指定位置是否可以生成怪物
   * @param x 目标 X 坐标
   * @param y 目标 Y 坐标
   * @return true 如果该位置可行走且未被其他怪物占用
   */
  [[nodiscard]] bool legacy_can_spawn_monster(std::int32_t x, std::int32_t y) const;

  /**
   * @brief 处理单个商人的帧更新
   * @param actor_id 商人角色 ID
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param cursor 商人处理游标
   * @return 运行时调度输出
   * @details 处理商人的周期状态更新，如商品补货、价格刷新等。
   */
  [[nodiscard]] RuntimeDispatch legacy_process_merchant(std::uint64_t actor_id,
                                                        std::uint64_t current_tick,
                                                        std::uint64_t now_ms,
                                                        std::size_t cursor);

  /**
   * @brief 处理单个 NPC 的帧更新
   * @param actor_id NPC 角色 ID
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param cursor NPC 处理游标
   * @return 运行时调度输出
   */
  [[nodiscard]] RuntimeDispatch legacy_process_npc(std::uint64_t actor_id,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms,
                                                   std::size_t cursor);

  /**
   * @brief 添加事件对象到地图
   * @param event_id 事件对象 ID
   * @param x 目标 X 坐标
   * @param y 目标 Y 坐标
   * @param now_ms 当前系统时间（毫秒）
   * @param blocks_walk 是否阻挡行走
   * @param dispatch 运行时调度输出（可选，用于广播可见性更新）
   * @param type 事件类型
   * @return 是否成功添加
   * @details 在地图指定位置创建事件对象（如火墙、法术痕迹、圣言区域等），
   *          可选择阻挡行走路径并广播可见性更新。
   */
  [[nodiscard]] bool legacy_add_event_object(std::uint64_t event_id, std::int32_t x,
                                             std::int32_t y, std::uint64_t now_ms,
                                             bool blocks_walk = false,
                                             RuntimeDispatch* dispatch = nullptr,
                                             LegacyEventType type = LegacyEventType::pile_stones,
                                             std::int32_t event_param = 0,
                                             std::int32_t event_damage = 0);

  /**
   * @brief 添加事件对象到地图（简化版本）
   * @param event_id 事件对象 ID
   * @param x 目标 X 坐标
   * @param y 目标 Y 坐标
   * @param now_ms 当前系统时间（毫秒）
   * @param dispatch 运行时调度输出（可选，用于广播可见性更新）
   * @return 是否成功添加
   * @details 简化版本，默认不阻挡行走，事件类型为 pile_stones。
   */
  [[nodiscard]] bool legacy_add_event_object(std::uint64_t event_id, std::int32_t x,
                                             std::int32_t y, std::uint64_t now_ms,
                                             RuntimeDispatch* dispatch,
                                             std::int32_t event_param = 0);

  /**
   * @brief 移除地图上的事件对象
   * @param event_id 事件对象 ID
   * @param x 事件 X 坐标
   * @param y 事件 Y 坐标
   * @param dispatch 运行时调度输出（可选，用于广播可见性更新）
   * @details 从地图和可见性系统中移除指定事件对象。
   */
  void legacy_remove_event_object(std::uint64_t event_id, std::int32_t x, std::int32_t y,
                                  RuntimeDispatch* dispatch = nullptr);

  /**
   * @brief 应用火墙燃烧事件
   * @param event 火墙事件记录
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 运行时调度输出
   * @details 对火墙范围内的玩家和怪物施加火焰伤害。
   *          处理伤害计算和广播。
   */
  [[nodiscard]] RuntimeDispatch legacy_apply_fire_burn_event(const LegacyEventRecord& event,
                                                             std::uint64_t current_tick,
                                                             std::uint64_t now_ms);

  /**
   * @brief 获取圣言范围内活跃的演员 ID 列表
   * @param actor_ids 候选演员 ID 列表
   * @param now_ms 当前系统时间（毫秒）
   * @return 活跃的（未死亡的）演员 ID 子集
   * @details 过滤出当前仍然存活的演员用于圣言技能效果处理。
   */
  [[nodiscard]] std::vector<std::uint64_t> legacy_active_holy_seize_actor_ids(
      const std::vector<std::uint64_t>& actor_ids, std::uint64_t now_ms) const;

  /**
   * @brief 获取随机空间移动的目标位置
   * @param random 随机数生成器引用
   * @return 目标坐标对，如果找不到则返回 std::nullopt
   * @details 在地图移动范围内随机选择一个可行走的位置。
   *          用于空间移动魔法和随机传送卷轴。
   */
  [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>>
  legacy_random_space_move_target(LegacyRandom& random) const;

  /**
   * @brief 执行玩家空间移动
   * @param actor_id 玩家角色 ID
   * @param target_map_id 目标地图 ID
   * @param target_x 目标 X 坐标
   * @param target_y 目标 Y 坐标
   * @param show2 是否使用 show2 包（视觉效果类型）
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return 运行时调度输出
   * @details 将玩家移动到另一个位置或地图，发送位置改变和可见性更新。
   *          用于空间移动魔法效果。
   */
  [[nodiscard]] RuntimeDispatch legacy_space_move_player(
      std::uint64_t actor_id, const std::string& target_map_id, std::int32_t target_x,
      std::int32_t target_y, bool show2, std::uint64_t current_tick, std::uint64_t now_ms);

  /**
   * @brief 应用 GM 命令
   * @param actor_id 执行 GM 命令的玩家角色 ID
   * @param command_name 命令名称（如 Level、Make、DeleteItem 等）
   * @param args 命令参数列表
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @return GM 命令执行结果
   * @details 解析并执行 GM 命令，支持所有传奇传统 GM 命令。
   *          验证玩家权限、命令参数，执行对应操作并返回结果。
   */
  [[nodiscard]] LegacyGmCommandResult legacy_apply_gm_command(
      std::uint64_t actor_id, const std::string& command_name,
      const std::vector<std::string>& args, std::uint64_t current_tick,
      std::uint64_t now_ms);

  /**
   * @brief 入队玩家的传统命令
   * @param mail 包含玩家输入的消息邮件
   * @param now_ms 当前系统时间（毫秒）
   * @return 是否成功入队
   * @details 将客户端发送的操作消息转换为 LegacyQueuedCommand 并放入玩家邮箱。
   *          这些命令将在玩家运行帧中被消费处理。
   */
  bool enqueue_legacy_player_command(const ActorMail& mail, std::uint64_t now_ms);

  /**
   * @brief 标记玩家为幽灵状态（即将断开连接）
   * @param actor_id 玩家角色 ID
   * @param now_ms 当前系统时间（毫秒）
   * @return 是否成功标记
   */
  bool mark_legacy_player_ghost(std::uint64_t actor_id, std::uint64_t now_ms);

  /**
   * @brief 断开玩家连接
   * @param actor_id 玩家角色 ID
   * @param now_ms 当前系统时间（毫秒）
   * @return 运行时调度输出
   * @details 执行玩家断开清理流程：取消交易、保存角色、释放奴隶、断开连接。
   */
  [[nodiscard]] RuntimeDispatch legacy_disconnect_player(std::uint64_t actor_id,
                                                         std::uint64_t now_ms);

  /**
   * @brief 获取玩家状态
   * @param actor_id 玩家角色 ID
   * @return 玩家状态枚举值，如果玩家不存在则返回 std::nullopt
   */
  [[nodiscard]] std::optional<LegacyPlayerState> legacy_player_state(
      std::uint64_t actor_id) const;

  /**
   * @brief 获取玩家收件箱中的待处理命令数
   * @param actor_id 玩家角色 ID
   * @return 待处理命令数量
   */
  [[nodiscard]] std::size_t legacy_player_inbox_size(std::uint64_t actor_id) const;

  /**
   * @brief 获取玩家收件箱中的会话序列号列表
   * @param actor_id 玩家角色 ID
   * @return 会话序列号 vector
   */
  [[nodiscard]] std::vector<std::uint64_t> legacy_player_inbox_session_sequences(
      std::uint64_t actor_id) const;

  /**
   * @brief 获取玩家已经运行的时间（毫秒）
   * @param actor_id 玩家角色 ID
   * @return 运行时间（毫秒），如果玩家不存在返回 0
   */
  [[nodiscard]] std::int64_t legacy_player_run_time_ms(std::uint64_t actor_id) const;

  /**
   * @brief 快照玩家角色数据
   * @param actor_id 玩家角色 ID
   * @return 角色记录快照，如果玩家不存在返回 std::nullopt
   * @details 获取玩家当前内存数据的快照，不涉及持久化存储。
   */
  [[nodiscard]] std::optional<CharacterRecord> snapshot_player(std::uint64_t actor_id) const;

  /**
   * @brief 获取玩家持久化快照（含奴隶快照）
   * @param actor_id 玩家角色 ID
   * @param now_ms 当前系统时间（毫秒）
   * @return 角色记录快照，如果玩家不存在返回 std::nullopt
   * @details 获取包含完整状态的持久化快照，包括宠物奴隶数据。
   */
  [[nodiscard]] std::optional<CharacterRecord> persistent_snapshot_player(
      std::uint64_t actor_id, std::uint64_t now_ms);

  /**
   * @brief 获取怪物快照
   * @param actor_id 怪物角色 ID
   * @return 怪物快照，如果怪物不存在返回 std::nullopt
   */
  [[nodiscard]] std::optional<MonsterSnapshot> legacy_monster_snapshot(
      std::uint64_t actor_id) const;

  /**
   * @brief 设置玩家奴隶为放松状态
   * @param actor_id 玩家角色 ID
   * @param value true 为放松模式（不攻击），false 为战斗模式
   * @return 是否成功设置
   */
  [[nodiscard]] bool legacy_set_player_slave_relax(std::uint64_t actor_id, bool value);

  /**
   * @brief 检查玩家是否正在追踪指定事件
   * @param actor_id 玩家角色 ID
   * @param event_id 事件对象 ID
   * @return true 如果该事件在玩家的可见事件集中
   */
  [[nodiscard]] bool legacy_player_tracks_event(std::uint64_t actor_id,
                                                std::uint64_t event_id) const;

  /// @brief 获取地图 ID
  [[nodiscard]] const std::string& id() const { return config_.id; }

  /// @brief 获取当前地图上的对象总数
  [[nodiscard]] std::size_t object_count() const { return objects_.size(); }
  [[nodiscard]] std::size_t legacy_stone_mine_count() const { return stone_mines_.size(); }
  [[nodiscard]] std::optional<StoneMineSnapshot> legacy_stone_mine_snapshot(
      std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool legacy_try_mine(std::int32_t x, std::int32_t y);
  void refill_legacy_stone_mines(std::uint64_t now_ms);

 private:
  // ─── 内嵌私有类型 ────────────────────────────────────────────

  /**
   * @struct MonsterSpawnTemplate
   * @brief 怪物重生模板
   * @details 存储怪物的重生配置，包括生成所需的邮件数据。
   *          当怪物死亡后，使用此模板调度怪物重生。
   */
  struct MonsterSpawnTemplate {
    ActorMail mail{};  ///< 怪物生成邮件模板
  };

  using CellKey = std::pair<std::int32_t, std::int32_t>;

  struct EventObjectState {
    std::int32_t x{0};
    std::int32_t y{0};
    LegacyEventType type{LegacyEventType::pile_stones};
    std::int32_t event_param{0};
    bool blocks_walk{false};
  };

  struct StoneMineState {
    std::int32_t mine_count{0};
    std::int32_t mine_fill_count{0};
    std::uint64_t refill_time_ms{0};
    std::int32_t type{0};
  };

  /**
   * @struct PlayerVisibility
   * @brief 玩家可见性状态跟踪
   * @details 维护每个玩家视野范围内的所有可见对象集合，
   *          包括演员（其他玩家/怪物）、物品和事件对象。
   *          用于增量同步和离开视野时的清理。
   */
  struct PlayerVisibility {
    std::unordered_set<std::uint64_t> actors{};    ///< 可见演员 ID 集合
    std::unordered_set<std::uint64_t> items{};      ///< 可见地面物品 ID 集合
    std::unordered_set<std::uint64_t> events{};     ///< 可见事件对象 ID 集合
    std::vector<std::uint64_t> actor_order{};       ///< 演员显示顺序列表
    std::vector<std::uint64_t> item_order{};        ///< 物品显示顺序列表
    std::vector<std::uint64_t> event_order{};       ///< 事件显示顺序列表
    std::unordered_map<std::uint64_t, std::uint64_t> delayed_item_hides_until_ms{};  ///< 延迟隐藏物品的过期时间
  };

  /**
   * @enum ItemVisibilityRemovalMode
   * @brief 物品可见性移除模式
   * @details 控制地面上物品被拾取或消失时，从玩家视野中移除的方式。
   */
  enum class ItemVisibilityRemovalMode {
    immediate_all,              ///< 立即从所有玩家视野移除
    delayed_all,                ///< 延迟从所有玩家视野移除（用于拾取动画）
    immediate_single_session    ///< 仅从单个会话立即移除
  };

  /**
   * @struct LegacyRefTargetCache
   * @brief 玩家引用目标缓存
   * @details 缓存玩家周围的观察者 ID 列表，用于减少每帧重复计算。
   *          在一定时间间隔内复用缓存结果。
   */
  struct LegacyRefTargetCache {
    std::vector<std::uint64_t> watcher_ids{};  ///< 观察者角色 ID 列表
    std::uint64_t collected_at_ms{0};           ///< 缓存收集时间戳
  };

  /**
   * @struct TradeOffer
   * @brief 交易出价数据
   * @details 存储交易一方提供的物品和金币，以及接受状态。
   */
  struct TradeOffer {
    std::vector<LegacyUserItem> items{};  ///< 提供的物品列表
    std::int32_t gold{0};                 ///< 提供的金币数量
    bool accepted{false};                 ///< 是否已确认接受
    std::uint64_t last_change_time_ms{0}; ///< 最后修改时间
  };

  /**
   * @struct TradeSession
   * @brief 交易会话数据
   * @details 管理两个玩家之间的交易会话，包含双方出价和状态。
   */
  struct TradeSession {
    std::uint64_t id{0};              ///< 交易会话唯一 ID
    std::uint64_t first_actor_id{0};  ///< 交易发起方角色 ID
    std::uint64_t second_actor_id{0}; ///< 交易接收方角色 ID
    TradeOffer first{};               ///< 发起方出价
    TradeOffer second{};              ///< 接收方出价
  };

  // ─── 私有方法 ─────────────────────────────────────────────────

  /**
   * @brief 邮件处理主分发器
   * @param mail 待处理的演员邮件
   * @param dispatch 运行时调度输出
   * @param current_tick 当前逻辑 tick
   * @param now_ms 当前系统时间（毫秒）
   * @param from_legacy_operate 是否来自玩家运行帧处理
   * @details 根据 ActorMailKind 分派到对应的处理逻辑。
   *          支持 spawn_player/spawn_monster/spawn_npc、移动、攻击、
   *          交易、对话、技能、物品、GM 命令等所有邮件类型。
   */
  void handle_mail(const ActorMail& mail, RuntimeDispatch& dispatch, std::uint64_t current_tick,
                   std::uint64_t now_ms, bool from_legacy_operate = false);

  // 以下方法在对应的实现细节头文件中实现：
  //   - map_actor_player.hpp:     玩家生命周期
  //   - map_actor_visibility.hpp: 可见性同步
  //   - map_actor_movement.hpp:   传送、门、物品移动

  /// @name 玩家生命周期管理
  /// @{

  void dispatch_legacy_run_notice(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t now_ms);
  void dispatch_legacy_initialize(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t now_ms);
  void dispatch_legacy_close(Player& player, RuntimeDispatch& dispatch);
  void legacy_operate_player_running(std::uint64_t actor_id, Player& player,
                                     RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms,
                                     bool persistence_overloaded,
                                     std::size_t player_input_budget_per_tick);
  void trace_player_operate(RuntimeDispatch& dispatch, const Player& player,
                            std::string action, std::uint64_t current_tick,
                            std::uint64_t now_ms, bool success = true,
                            std::int32_t value = 0,
                            std::string label = {}) const;
  void handle_player_health_spell_tick(Player& player, RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick);
  void handle_player_status_effects(Player& player, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick);

  /// @}

  /// @name 怪物 AI 与生命周期管理
  /// @{

  [[nodiscard]] bool handle_monster_status_effects(Monster& monster, RuntimeDispatch& dispatch,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms);
  void handle_monster_ai(Monster& monster, RuntimeDispatch& dispatch, std::uint64_t current_tick,
                         std::uint64_t now_ms);
  [[nodiscard]] std::size_t legacy_dup_count(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool legacy_try_monster_walk(Monster& monster, std::uint8_t dir,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_think(Monster& monster, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  void legacy_refresh_monster_visible_actors(Monster& monster);
  [[nodiscard]] bool legacy_monster_valid_target(const Monster& monster,
                                                 const GameObject& target,
                                                 std::uint64_t current_tick) const;
  [[nodiscard]] bool legacy_monster_search_candidate(const Monster& monster,
                                                     const GameObject& target,
                                                     std::uint64_t current_tick) const;
  void legacy_active_search(Monster& monster, RuntimeDispatch& dispatch,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_normal_attack(Monster& monster,
                                                  RuntimeDispatch& dispatch,
                                                  std::uint64_t current_tick,
                                                  std::uint64_t now_ms);
  [[nodiscard]] bool legacy_attack_target(Monster& monster, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  [[nodiscard]] bool legacy_goto_target_xy(Monster& monster, RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick,
                                           std::uint64_t now_ms);
  void legacy_wondering(Monster& monster, RuntimeDispatch& dispatch,
                        std::uint64_t current_tick, std::uint64_t now_ms);
  void legacy_monster_temp_attack(Monster& monster, Player& target,
                                  RuntimeDispatch& dispatch,
                                  std::uint64_t current_tick, std::uint64_t now_ms);
  void legacy_monster_attack_monster(Monster& monster, Monster& target,
                                     RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_special_run(Monster& monster,
                                                RuntimeDispatch& dispatch,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_special_attack_target(Monster& monster,
                                                          RuntimeDispatch& dispatch,
                                                          std::uint64_t current_tick,
                                                          std::uint64_t now_ms);
  void legacy_monster_summon_child(Monster& monster, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] Player* legacy_nearest_player_target(const Monster& monster,
                                                     std::uint64_t current_tick,
                                                     std::int32_t max_range,
                                                     bool guard_rules);

  /// @}

  /// @name 移动与传送管理
  /// @{

  void refresh_moving_object_state(const GameObject& object, std::uint64_t now_ms);

  /// @}

  /// @name 战斗与经验管理
  /// @{

  void award_monster_kill(Player& attacker, const Monster& monster, RuntimeDispatch& dispatch);

  /// @}

  /// @name 怪物重生调度
  /// @{

  void schedule_monster_respawn(std::uint64_t monster_id, std::uint64_t current_tick);

  /// @}

  /// @name 魔法技能与训练
  /// @{

  void schedule_legacy_magic_lvexp(Player& player, const LegacyMagicTrainResult& training,
                                   RuntimeDispatch& dispatch, const ActorMail& source_mail,
                                   std::uint64_t current_tick, std::uint64_t now_ms);

  /// @}

  /// @name 宠物奴隶系统
  /// @{

  [[nodiscard]] std::optional<ActorMail> build_slave_spawn_mail(
      const std::string& monster_name, Player& master, std::int32_t x, std::int32_t y,
      std::int32_t make_level, std::int32_t exp_level, std::int32_t slave_exp,
      std::uint64_t royalty_time_ms, std::uint64_t life_time_ms,
      std::uint64_t now_ms, std::optional<CharacterSlaveRecord> restored = std::nullopt);
  [[nodiscard]] bool summon_player_slave(Player& master, const std::string& monster_name,
                                         std::int32_t make_level, std::int32_t max_slaves,
                                         std::uint64_t royalty_seconds,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms,
                                         const ActorMail& source_mail);
  [[nodiscard]] bool tame_player_slave(Player& master, Monster& target,
                                       std::int32_t make_level, std::int32_t max_slaves,
                                       RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick,
                                       std::uint64_t now_ms,
                                       const ActorMail& source_mail);
  [[nodiscard]] std::array<CharacterSlaveRecord, kMaxLegacySlaves> snapshot_owned_slaves(
      Player& player, std::uint64_t now_ms);
  [[nodiscard]] CharacterRecord snapshot_player_with_slaves(Player& player,
                                                            std::uint64_t now_ms);
  void queue_save_player_character(RuntimeDispatch& dispatch, Player& player,
                                   std::uint64_t now_ms);
  void restore_saved_slaves(Player& player, RuntimeDispatch& dispatch,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  void detach_owned_slaves(Player& player, RuntimeDispatch& dispatch,
                           std::uint64_t now_ms, bool erase_objects);
  void recall_owned_slaves_to_master(Player& player, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms);
  void notify_owned_slaves_target(Player& player, std::uint64_t target_actor_id,
                                  std::uint64_t now_ms);
  void remove_slave_from_master(Monster& slave);
  [[nodiscard]] bool handle_slave_lifecycle(Monster& monster, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick,
                                            std::uint64_t now_ms);
  [[nodiscard]] bool handle_slave_follow(Monster& monster, RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms);

  /// @}

  /// @name 怪物死亡处理
  /// @{

  void finalize_monster_death(std::uint64_t monster_id, std::uint64_t killer_actor_id,
                               RuntimeDispatch& dispatch, std::uint64_t current_tick);
  void finalize_monster_ghost(std::uint64_t monster_id, RuntimeDispatch& dispatch,
                              std::uint64_t current_tick, std::uint64_t now_ms);

  /// @}

  /// @name 玩家通知与状态广播
  /// @{

  void notify_player_and_watchers(RuntimeDispatch& dispatch, const Player& player,
                                  const std::string& self_message,
                                  const std::string& watcher_message) const;
  void dispatch_player_status_tick_result(Player& player, const StatusTickResult& result,
                                          RuntimeDispatch& dispatch,
                                          bool include_health) const;
  void broadcast_legacy_char_status_changed(RuntimeDispatch& dispatch,
                                            const Player& player) const;

  /// @}

  /// @name 追踪与审计
  /// @{

  void add_legacy_trace(RuntimeDispatch& dispatch,
                        std::string stage,
                        std::string action,
                        const ActorMail& mail,
                        std::uint64_t current_tick,
                        std::uint64_t now_ms,
                        bool success = true,
                        std::int32_t value = 0,
                        std::int32_t damage = 0,
                        std::string label = {}) const;

  /// @}

  /// @name 对象调度
  /// @{

  void schedule_actor(std::uint64_t current_tick, const GameObject& object);

  /// @}

  /// @name 可见性同步
  /// @{

  void sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force,
                              std::uint64_t now_ms);
  void sync_all_player_visibility(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  void sync_visibility_after_actor_move(const GameObject& actor, std::int32_t old_x,
                                        std::int32_t old_y, std::int32_t new_x,
                                        std::int32_t new_y, RuntimeDispatch& dispatch,
                                        std::uint64_t now_ms);
  void sync_visibility_after_item_change(std::int32_t item_x, std::int32_t item_y,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t now_ms,
                                         std::optional<std::uint64_t> refresh_item_id = std::nullopt);
  void sync_visibility_after_event_change(std::int32_t event_x, std::int32_t event_y,
                                          RuntimeDispatch& dispatch, std::uint64_t now_ms);
  void force_refresh_after_same_map_transfer(Player& player, std::int32_t old_x,
                                             std::int32_t old_y, RuntimeDispatch& dispatch,
                                             std::uint64_t now_ms,
                                             std::uint16_t space_move_hide_ident = 0,
                                             std::uint16_t space_move_show_ident = 0);
  void remove_actor_from_visibility(std::uint64_t actor_id, RuntimeDispatch& dispatch);
  void remove_item_from_visibility(
      std::uint64_t item_id, RuntimeDispatch& dispatch,
      std::uint64_t now_ms,
      ItemVisibilityRemovalMode mode = ItemVisibilityRemovalMode::immediate_all,
      std::uint64_t immediate_session_id = 0);
  [[nodiscard]] std::vector<std::uint64_t> ordered_player_ids() const;
  [[nodiscard]] std::vector<std::uint64_t> ordered_visible_actor_ids(
      const Player& player) const;
  [[nodiscard]] std::vector<std::uint64_t> ordered_visible_item_ids(
      const Player& player) const;
  [[nodiscard]] std::vector<std::uint64_t> ordered_visible_event_ids(
      const Player& player) const;

  /// @}

  /// @name 地面物品管理
  /// @{

  void refresh_ground_item_ownership(GroundItem& item, std::uint64_t now_ms);
  void remove_expired_ground_items(RuntimeDispatch& dispatch, std::uint64_t now_ms);

  /// @}

  /// @name 传送门与入口管理
  /// @{

  bool try_gate_transfer(Player& player, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool target_entry_allowed(Player& player, const LegacyMapGateState& gate,
                                          RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  [[nodiscard]] bool target_map_can_enter(const MapEntryRuleConfig& rule,
                                          const LegacyMapGateState& gate) const;
  [[nodiscard]] bool has_event_at(std::int32_t x, std::int32_t y,
                                  LegacyEventType type) const;
  bool try_item_map_move(Player& player, std::string target_map_id, std::int32_t target_x,
                         std::int32_t target_y, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms,
                         bool send_space_move_packets = false);
  [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>>
  random_item_scroll_target(RuntimeDispatch& dispatch, const Player& player,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  void broadcast_open_doors(const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
                            RuntimeDispatch& dispatch);
  void broadcast_close_doors(const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
                             RuntimeDispatch& dispatch);
  void set_castle_door_wall_state(std::int32_t x, std::int32_t y, bool open);
  void clear_castle_door_wall_state(std::int32_t x, std::int32_t y);
  void initialize_legacy_stone_mines();

  /// @}

  /// @name 预算与资源管理
  /// @{

  [[nodiscard]] std::uint64_t budget_for(GameObjectKind kind) const;

  /// @}

  /// @name 玩家查找
  /// @{

  [[nodiscard]] Player* find_player(std::uint64_t actor_id);
  [[nodiscard]] const Player* find_player(std::uint64_t actor_id) const;
  [[nodiscard]] Player* find_player_by_name(std::string_view character_name);

  /// @}

  /// @name 交易系统
  /// @{

  [[nodiscard]] TradeSession* trade_session_for(std::uint64_t actor_id);
  [[nodiscard]] TradeOffer* trade_offer_for(TradeSession& session, std::uint64_t actor_id);
  [[nodiscard]] TradeOffer* trade_peer_offer_for(TradeSession& session,
                                                 std::uint64_t actor_id);
  void cancel_trade_for(std::uint64_t actor_id, RuntimeDispatch& dispatch, bool notify);
  bool can_receive_trade_items(const Player& receiver,
                               const std::vector<LegacyUserItem>& items) const;
  bool commit_trade(TradeSession& session, RuntimeDispatch& dispatch);

  /// @}

  /// @name 地图环境与移动辅助
  /// @{

  [[nodiscard]] std::int32_t movement_width() const;
  [[nodiscard]] std::int32_t movement_height() const;
  [[nodiscard]] bool can_walk_tile(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] LegacyMovingObjectState moving_state_for(const GameObject& object) const;
  [[nodiscard]] std::vector<const GroundItem*> ordered_ground_items() const;

  /// @}

  /// @name 制造索引分配
  /// @{

  [[nodiscard]] std::int32_t allocate_make_index();

  /// @}

  /// @name 装备耐久度与武器升级
  /// @{

  bool apply_equipped_item_durability_loss(Player& player, std::size_t slot,
                                           std::int32_t loss,
                                           RuntimeDispatch& dispatch);
  [[nodiscard]] std::int32_t roll_legacy_weapon_durability_loss(
      const Player& attacker, const GameObject& target, RuntimeDispatch& dispatch,
      std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_durability_loss(Player& attacker, std::int32_t loss,
                                           RuntimeDispatch& dispatch);
  bool apply_legacy_struck_equipment_durability(Player& target, std::uint64_t hitter_id,
                                                RuntimeDispatch& dispatch,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms,
                                                std::string stage);
  [[nodiscard]] std::int32_t roll_legacy_player_attack_power(
      const Player& attacker, const GameObject& target, std::uint16_t ident,
      RuntimeDispatch& dispatch, std::string stage, std::string command,
      std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool handle_legacy_rush_rush(Player& attacker, LegacyUseMagicInfo& user_magic,
                                             const MagicConfig& magic, const ActorMail& mail,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms);
  bool apply_legacy_physical_equipment_specials(Player& attacker, GameObject& target,
                                                std::int32_t hit_damage,
                                                std::int32_t suck_damage,
                                                RuntimeDispatch& dispatch,
                                                std::string stage,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms);
  bool handle_weapon_upgrade_start(Player& player, Npc& npc, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick, std::uint64_t now_ms);
  bool handle_weapon_upgrade_get_back(Player& player, Npc& npc, RuntimeDispatch& dispatch,
                                      std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_pending_weapon_upgrade_result(Player& attacker, RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_good_luck(Player& player, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_unlock(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t current_tick, std::uint64_t now_ms,
                                  std::string stage);

  /// @}

  /// @name 玩家死亡与复活
  /// @{

  void apply_bad_kill_penalty(Player& killer, const Player& victim, RuntimeDispatch& dispatch,
                              std::uint64_t current_tick, std::uint64_t now_ms,
                              std::string stage);
  bool settle_player_death(Player& player, RuntimeDispatch& dispatch,
                           std::uint64_t current_tick, std::uint64_t now_ms);
  bool try_legacy_revival(Player& player, RuntimeDispatch& dispatch,
                          std::uint64_t current_tick, std::uint64_t now_ms);

  /// @}

  /// @name 随机数生成
  /// @{

  [[nodiscard]] std::int32_t legacy_random_value(RuntimeDispatch& dispatch,
                                                 std::string stage,
                                                 std::string action,
                                                 std::int32_t range,
                                                 std::uint64_t actor_id,
                                                 std::uint64_t target_actor_id,
                                                 std::string command,
                                                 std::uint64_t now_ms,
                                                 std::uint64_t current_tick);

  /// @}

  /// @name NPC 脚本引擎
  /// @{

  /**
   * @struct LegacyScriptExecutionContext
   * @brief 脚本执行上下文
   * @details 在 NPC 脚本执行过程中跟踪各种副作用的临时状态，
   *          包括最后一次拿取的物品、检查的物品等。
   */
  struct LegacyScriptExecutionContext {
    std::optional<std::string> last_taken_item_name{};   ///< 最后一次拿取的物品名称
    std::optional<LegacyUserItem> last_checked_item{};   ///< 最后检查的物品实例
    std::optional<std::string> last_checked_item_name{}; ///< 最后检查的物品名称
    bool end_quest{false};                                ///< 是否结束任务
  };

  bool legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms);
  bool legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms,
                                 LegacyScriptExecutionContext& script_context,
                                 std::int32_t depth);

  /// @}

  /// @name 任务系统
  /// @{

  bool trigger_map_quest(Player& player, std::string monster_name, std::string item_name,
                         bool group_call, std::string source, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms);
  bool trigger_startup_quest(Player& player, RuntimeDispatch& dispatch,
                             std::uint64_t current_tick, std::uint64_t now_ms);

  /// @}

  /// @name 引用目标缓存
  /// @{

  [[nodiscard]] std::vector<std::uint64_t> legacy_ref_target_player_ids(
      const GameObject& origin, std::uint64_t now_ms);

  /// @}

  // ─── 私有成员变量 ───────────────────────────────────────────────

  MapConfig config_{};                                                         ///< 地图配置
  LogicBudgetConfig budgets_{};                                                ///< 逻辑预算配置
  std::unordered_map<std::int32_t, ItemConfig> item_configs_{};               ///< 物品配置表
  std::unordered_map<std::int32_t, MagicConfig> magic_configs_{};            ///< 魔法配置表
  std::unordered_map<std::string, MonsterDefConfig> monster_defs_{};         ///< 怪物定义配置表
  std::vector<MapQuestConfig> map_quests_{};                                 ///< 地图任务配置列表
  std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules_{};   ///< 地图入口规则配置表
  std::string black_stone_name_{"BlackStone"};                                ///< 黑铁矿石名称
  bool legacy_approval_mode_{false};                                           ///< 传统审批模式标志
  std::shared_ptr<const legacy::MapDocument> movement_map_{};                 ///< 移动地图文档
  LegacyMapEnvironment environment_{};                                        ///< 地图环境（碰撞/门/传送门）
  CastleDialogContext castle_dialog_context_{};                               ///< 城堡对话框上下文
  GuildCastleSnapshot guild_castle_snapshot_{};                               ///< 行会城堡快照
  std::vector<NpcDialogSectionConfig> startup_quest_dialog_sections_{};       ///< 启动任务对话段
  std::deque<ActorMail> mailbox_{};                                           ///< 入站邮件队列
  WheelTimer<std::uint64_t> object_wheel_{1024};                              ///< 对象调度时间轮
  WheelTimer<ActorMail> delayed_mail_wheel_{1024};                           ///< 延迟邮件时间轮
  LegacyRandom* legacy_random_{nullptr};                                      ///< 随机数生成器指针
  LegacyScriptMapHooks legacy_script_map_hooks_{};                           ///< 脚本地图钩子
  MakeIndexAllocator fallback_make_index_allocator_{};                       ///< 备用制造索引分配器
  MakeIndexAllocator* make_index_allocator_{nullptr};                         ///< 制造索引分配器指针
  std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>> objects_{}; ///< 所有游戏对象（ID -> 对象）
  std::unordered_map<std::uint64_t, MonsterSpawnTemplate> monster_spawn_templates_{}; ///< 怪物重生模板表
  std::unordered_map<std::uint64_t, GroundItem> ground_items_{};             ///< 地面物品表
  std::unordered_map<std::uint64_t, TradeSession> trade_sessions_{};         ///< 交易会话表
  std::unordered_map<std::uint64_t, std::uint64_t> trade_session_by_actor_{};///< 演员到交易会话的映射
  std::unordered_map<std::uint64_t, EventObjectState> event_objects_{};     ///< 事件对象状态表
  std::unordered_map<std::uint64_t, PlayerVisibility> visibility_{};         ///< 玩家可见性状态表
  std::unordered_map<std::uint64_t, LegacyRefTargetCache> legacy_ref_target_cache_{}; ///< 引用目标缓存
  std::shared_ptr<std::array<std::int32_t, 10>> script_global_params_{};    ///< 脚本全局参数
  std::shared_ptr<LegacyNameListRepository> script_name_lists_{};           ///< 脚本名称列表仓库
  std::map<CellKey, StoneMineState> stone_mines_{};
  std::map<CellKey, std::int32_t> castle_door_wall_block_counts_{};
  bool stone_mines_initialized_{false};
  std::uint64_t next_ground_item_id_{1};                                     ///< 下一个地面物品 ID
  std::uint64_t next_trade_session_id_{1};                                   ///< 下一个交易会话 ID
  std::uint64_t next_script_monster_id_{0x6000000000000000ULL};              ///< 下一个脚本怪物 ID
};

}  // namespace mir2
