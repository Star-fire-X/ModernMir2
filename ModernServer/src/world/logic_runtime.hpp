/**
 * @file logic_runtime.hpp
 * @brief 逻辑运行时头文件，定义了 LogicRuntime 类及相关的运行时数据结构
 * @details 该文件是游戏服务器核心逻辑运行时的定义部分，包含：
 *          - LegacyReadyUser: 待初始化的就绪用户数据结构
 *          - LegacyRuntimeContext: 运行时上下文（负载控制、处理预算）
 *          - LegacyShutUpEntry: 禁言条目数据结构
 *          - LogicRuntime 类：管理所有地图、玩家、怪物、NPC、事件的生命周期
 *
 *          LogicRuntime 是服务器主循环的核心协调者，负责驱动帧处理
 *          流水线的 user_engine_execute_run 阶段。
 */

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "core/wheel_timer.hpp"
#include "world/game_object.hpp"
#include "world/legacy_chat_parser.hpp"
#include "world/legacy_event_manager.hpp"
#include "world/legacy_gm_commands.hpp"
#include "world/legacy_random.hpp"
#include "world/make_index_allocator.hpp"
#include "world/map_actor.hpp"

namespace mir2 {

/**
 * @struct LegacyReadyUser
 * @brief 待初始化的就绪用户数据结构
 * @details 当玩家从登录服务器验证通过后，会创建此结构并将其加入就绪队列。
 *          后续由 process_ready_users 处理，创建 MapActor 中的 Player 对象。
 *          fast_initialize 标记指示是否跳过出生物品发放等初始化步骤（如同地图转移后）。
 */
struct LegacyReadyUser {
  std::uint64_t session_id{0};        ///< 网关会话 ID
  std::string gateway{"game_gateway"}; ///< 网关标识（默认 game_gateway）
  std::string account_id{};           ///< 账号 ID
  std::string character_name{};       ///< 角色名称
  std::string map_id{};               ///< 玩家所在地图 ID
  std::int32_t x{0};                  ///< 出生 X 坐标
  std::int32_t y{0};                  ///< 出生 Y 坐标
  CharacterRecord character{};        ///< 完整角色数据记录
  bool fast_initialize{false};        ///< 是否快速初始化（跳过出生物品发放）
};

/**
 * @struct LegacyRuntimeContext
 * @brief 运行时上下文，用于每帧处理的配置参数
 * @details 包含当前帧的负载控制参数，如持久化层是否过载、
 *          每帧玩家处理上限和输入预算。这些参数影响帧处理的
 *          行为模式，用于防止服务器过载。
 */
struct LegacyRuntimeContext {
  bool persistence_overloaded{false}; ///< 持久化层是否过载
  std::size_t player_process_limit{0}; ///< 每帧最多处理的玩家数量
  std::size_t player_input_budget_per_tick{0}; ///< 每帧每个玩家的输入处理预算
};

/**
 * @struct LegacyShutUpEntry
 * @brief 禁言条目数据结构
 * @details 记录被禁言的角色名称和禁言到期时间。
 *          禁言到期后自动解除，角色可恢复发言。
 */
struct LegacyShutUpEntry {
  std::string character_name{};      ///< 被禁言的角色名称
  std::uint64_t expire_ms{0};        ///< 禁言到期时间（毫秒时间戳）
};

/**
 * @class LogicRuntime
 * @brief 游戏逻辑运行时，管理整个服务器的核心游戏循环
 * @details LogicRuntime 是游戏服务器的中央协调器，负责：
 *          - 管理所有地图（MapActor）的生命周期
 *          - 处理就绪用户的初始化（process_ready_users）
 *          - 驱动玩家、怪物、商人、NPC 的每帧处理
 *          - 管理队伍系统（LegacyGroup）
 *          - 管理技能冷却和定时回调（时间召回、批次移动等）
 *          - 处理跨地图邮件路由
 *          - 管理事件系统（LegacyEventManager）
 *          - 随机数生成和追踪
 *
 *          对应 Delphi 服务端的 UserEngine 角色。
 */
class LogicRuntime {
 public:
  /**
   * @brief 构造 LogicRuntime 实例
   * @param config 主机配置（包含地图、物品、魔法、怪物配置等）
   */
  explicit LogicRuntime(HostConfig config);

  /**
   * @brief 初始化运行时，创建所有地图对象和初始怪物/NPC
   * @details 初始化流程：
   *          1. 设置随机种子
   *          2. 加载配置数据到内部映射表
   *          3. 创建所有 MapActor 实例
   *          4. 生成初始怪物和 NPC
   *          5. 设置跨地图钩子函数
   */
  void initialize();

  /// @brief 设置随机数种子
  void set_legacy_random_seed(std::uint32_t seed);

  /// @brief 获取当前随机数状态
  [[nodiscard]] std::uint32_t legacy_random_state() const;

  /**
   * @brief 设置商人状态数据
   * @param merchant_states 商人状态记录列表（商品、价格等）
   */
  void set_merchant_states(std::vector<MerchantStateRecord> merchant_states);

  /**
   * @brief 应用商人状态到所有地图中的对应 NPC
   * @param merchant_states 商人状态记录列表
   */
  void apply_merchant_states(std::vector<MerchantStateRecord> merchant_states);

  /**
   * @brief 设置城堡对话框上下文
   * @param castle_dialog_context 城堡对话框上下文
   */
  void set_castle_dialog_context(CastleDialogContext castle_dialog_context);

  /**
   * @brief 设置公会城堡快照数据
   * @param guild_castle_snapshot 公会城堡快照
   */
  void set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot);

  /// @brief 路由逻辑命令（玩家输入）到目标地图
  [[nodiscard]] RuntimeDispatch route_logic_command(const LogicCommand& command);

  /// @brief 路由角色邮件到目标地图
  [[nodiscard]] RuntimeDispatch route_actor_mail(const ActorMail& mail);

  /**
   * @brief 将就绪用户加入等待队列
   * @param ready_user 就绪用户信息
   * @return 空的 RuntimeDispatch
   */
  [[nodiscard]] RuntimeDispatch enqueue_ready_user(LegacyReadyUser ready_user);

  /**
   * @brief 标记会话已断开连接
   * @param session_id 会话 ID
   * @param reason 断开原因
   * @return RuntimeDispatch 包含断开连接的处理结果
   */
  [[nodiscard]] RuntimeDispatch mark_session_disconnected(std::uint64_t session_id,
                                                          std::string reason);

  /// @brief 执行一帧的 tick 处理
  [[nodiscard]] RuntimeDispatch tick();

  /// @brief 执行一帧的 tick 处理（指定时间）
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t now_ms);

  /**
   * @brief 执行一帧的 tick 处理（指定时间和上下文）
   * @param now_ms 当前系统时间
   * @param context 运行上下文（负载控制参数）
   */
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t now_ms, LegacyRuntimeContext context);

  /**
   * @brief 运行传统事件管理器
   * @param now_ms 当前系统时间
   * @return 事件处理产生的 RuntimeDispatch
   */
  [[nodiscard]] RuntimeDispatch run_legacy_event_manager(std::uint64_t now_ms);

  /**
   * @brief 将事件记录加入事件管理器
   * @param record 事件记录
   * @return 分配的事件 ID
   */
  [[nodiscard]] std::uint64_t enqueue_legacy_event(LegacyEventRecord record);

  /**
   * @brief 查找特定位置和类型的事件
   * @param map_id 地图 ID
   * @param x X 坐标
   * @param y Y 坐标
   * @param type 事件类型
   * @return 事件记录（如果找到）
   */
  [[nodiscard]] std::optional<LegacyEventRecord> find_legacy_event(
      const std::string& map_id, std::int32_t x, std::int32_t y,
      LegacyEventType type) const;

  /// @brief 根据角色名称查找所在地图和角色 ID
  [[nodiscard]] std::optional<std::pair<std::string, std::uint64_t>> locate_character_actor(
      std::string_view character_name) const;

  /// @brief 获取在线角色的快照
  [[nodiscard]] std::optional<CharacterRecord> snapshot_character_actor(
      std::string_view character_name) const;

  /// @brief 获取所有在线角色的快照列表
  [[nodiscard]] std::vector<CharacterRecord> snapshot_online_characters();

  /// @brief 获取指定怪物的快照
  [[nodiscard]] std::optional<MonsterSnapshot> legacy_monster_snapshot(
      std::string_view map_id, std::uint64_t actor_id) const;

  /// @brief 添加禁言记录
  void add_legacy_shut_up(std::string_view character_name, std::uint64_t duration_ms,
                          std::uint64_t now_ms);

  /// @brief 解除禁言
  bool release_legacy_shut_up(std::string_view character_name);

  /// @brief 获取所有禁言条目
  [[nodiscard]] std::vector<LegacyShutUpEntry> legacy_shut_up_entries() const;

  /** @name 查询方法 */
  //@{
  [[nodiscard]] std::size_t map_count() const { return maps_.size(); }
  [[nodiscard]] std::size_t online_session_count() const { return session_index_.size(); }
  [[nodiscard]] std::uint64_t current_tick() const { return current_tick_; }
  [[nodiscard]] const std::vector<std::string>& map_order() const { return map_order_; }
  [[nodiscard]] std::size_t legacy_ready_count() const { return ready_users_.size(); }
  [[nodiscard]] std::size_t legacy_run_user_count() const { return run_user_order_.size(); }
  [[nodiscard]] std::size_t legacy_close_record_count() const { return close_records_.size(); }
  [[nodiscard]] std::size_t legacy_monster_group_count() const { return monster_groups_.size(); }
  [[nodiscard]] std::size_t legacy_merchant_count() const { return merchant_order_.size(); }
  [[nodiscard]] std::size_t legacy_npc_count() const { return npc_order_.size(); }
  [[nodiscard]] std::size_t legacy_active_event_count() const {
    return legacy_event_manager_.active_count();
  }
  [[nodiscard]] std::size_t legacy_closed_event_count() const {
    return legacy_event_manager_.closed_count();
  }
  [[nodiscard]] std::size_t legacy_mon_cur() const { return mon_cur_; }
  [[nodiscard]] std::size_t legacy_mon_sub_cur() const { return mon_sub_cur_; }
  [[nodiscard]] std::size_t legacy_gen_cur() const { return gen_cur_; }
  [[nodiscard]] std::size_t legacy_mer_cur() const { return mer_cur_; }
  [[nodiscard]] std::size_t legacy_npc_cur() const { return npc_cur_; }
  [[nodiscard]] std::optional<LegacyPlayerState> legacy_session_state(
      std::uint64_t session_id) const;
  [[nodiscard]] std::size_t legacy_session_inbox_size(std::uint64_t session_id) const;
  [[nodiscard]] std::vector<std::uint64_t> legacy_session_inbox_sequences(
      std::uint64_t session_id) const;
  [[nodiscard]] std::int64_t legacy_session_run_time_ms(std::uint64_t session_id) const;
  //@}

 private:
  /**
   * @struct ActorLocator
   * @brief 玩家角色定位器，存储会话与角色之间的映射关系
   * @details 维护每个在线玩家的状态，包括所在位置、聊天设置、
   *          管理权限、队伍关联等信息。通过 session_index_ 映射
   *          （session_id -> ActorLocator）进行快速查找。
   */
  struct ActorLocator {
    std::string map_id{};                           ///< 所在地图 ID
    std::uint64_t actor_id{0};                      ///< 角色 ID
    std::string account_id{};                       ///< 账号 ID
    std::string character_name{};                   ///< 角色名称
    std::string latest_say_text{};                  ///< 最近发言文本
    std::uint64_t bomb_say_time_ms{0};              ///< 刷屏检测起始时间
    std::int32_t bomb_say_count{0};                 ///< 刷屏计数
    std::uint64_t auto_shut_up_until_ms{0};         ///< 自动禁言到期时间
    bool has_latest_cry_time{false};                ///< 是否有呐喊时间记录
    std::uint64_t latest_cry_time_ms{0};            ///< 最近呐喊时间
    bool hear_whisper{true};                        ///< 是否接收耳语
    bool hear_cry{true};                            ///< 是否接收呐喊
    bool hear_guild_msg{true};                      ///< 是否接收公会消息
    std::vector<std::string> whisper_block_list{};  ///< 耳语黑名单
    std::uint64_t legacy_group_id{0};               ///< 队伍 ID（0 表示无队伍）
    LegacyUserDegree user_degree{LegacyUserDegree::normal}; ///< 用户权限等级
    bool legacy_sysop_mode{false};                  ///< 系统操作员模式
    bool legacy_supervisor_mode{false};             ///< 监管员模式
    bool legacy_superman_mode{false};               ///< 超人模式（无敌）
    bool legacy_sys_mission{false};                 ///< 系统任务模式
    std::string legacy_sys_mission_map{};           ///< 系统任务地图
    std::int32_t legacy_sys_mission_x{0};           ///< 系统任务 X 坐标
    std::int32_t legacy_sys_mission_y{0};           ///< 系统任务 Y 坐标
  };

  /**
   * @struct CloseRecord
   * @brief 玩家断开连接记录
   * @details 在玩家断开连接后保留一段时间，用于防止短时间内
   *          重复登录同一角色以及记录断开原因。
   */
  struct CloseRecord {
    std::uint64_t session_id{0};     ///< 原会话 ID
    std::string account_id{};        ///< 账号 ID
    std::string character_name{};    ///< 角色名称
    std::uint64_t closed_ms{0};      ///< 断开时间（毫秒）
    std::string reason{};            ///< 断开原因
  };

  /**
   * @struct ActorRef
   * @brief 角色引用，用于地图-角色关联
   * @details 在 merchant_order_ 和 npc_order_ 中使用，
   *          记录 NPC/商人的位置和 ID，用于轮询调度。
   */
  struct ActorRef {
    std::string map_id{};         ///< 所在地图 ID
    std::uint64_t actor_id{0};    ///< 角色 ID
    std::string name{};           ///< 角色名称
  };

  /**
   * @struct MonsterGroup
   * @brief 怪物刷新组配置
   * @details 对应 Delphi 服务端的怪物生成设置，包含刷新范围、
   *          数量、重生时间等参数。支持 legacy_group 传统分组模式。
   */
  struct MonsterGroup {
    std::string name{};                              ///< 怪物名称
    std::string map_id{};                            ///< 所在地图
    SpawnConfig spawn{};                             ///< 刷新配置
    std::int32_t x{0};                               ///< 刷新中心 X
    std::int32_t y{0};                               ///< 刷新中心 Y
    std::int32_t area{0};                            ///< 刷新范围半径
    std::int32_t count{1};                           ///< 初始刷新数量
    std::int32_t small_zen_rate{0};                  ///< 小怪刷新率（百分比）
    bool legacy_group{false};                        ///< 是否使用传统分组模式
    std::uint32_t respawn_ms{0};                     ///< 重生时间（毫秒）
    std::uint32_t zen_time_ms{0};                    ///< 刷新间隔时间
    std::uint64_t start_time_ms{0};                  ///< 开始刷新时间
    std::vector<ActorRef> monsters{};                ///< 已生成的怪物列表
  };

  /**
   * @struct LegacyGroupState
   * @brief 队伍状态信息
   */
  struct LegacyGroupState {
    std::vector<std::uint64_t> members{}; ///< 队伍成员 session_id 列表
  };

  /**
   * @struct LegacyTimeRecallState
   * @brief 时间召回状态
   * @details 记录已安排的时间召回请求，到期时将玩家传送回
   *          指定位置。用于技能和物品触发的定时召回。
   */
  struct LegacyTimeRecallState {
    std::uint64_t generation{0};  ///< 召回代次（用于版本控制）
    std::uint64_t session_id{0};  ///< 玩家会话 ID
    std::uint64_t actor_id{0};    ///< 角色 ID
    std::string map_id{};         ///< 召回目标地图
    std::int32_t x{0};            ///< 召回目标 X
    std::int32_t y{0};            ///< 召回目标 Y
  };

  /**
   * @struct LegacyTimeRecallDue
   * @brief 到期待处理的时间召回事件
   */
  struct LegacyTimeRecallDue {
    std::uint64_t session_id{0};   ///< 玩家会话 ID
    std::uint64_t generation{0};   ///< 召回代次
  };

  /** @name 内部辅助方法 */
  //@{
  /// @brief 解析地图 ID（支持别名/简写）
  [[nodiscard]] std::string resolve_map_id(const std::string& requested_map) const;
  /// @brief 检查是否有在线或正在关闭的角色
  [[nodiscard]] bool has_live_or_closing_character(std::string_view character_name) const;
  /// @brief 解析用户权限等级
  [[nodiscard]] LegacyUserDegree resolve_legacy_user_degree(
      std::string_view account_id, std::string_view character_name) const;
  /// @brief 根据角色名称查找演员会话 ID
  [[nodiscard]] std::optional<std::uint64_t> find_actor_session_by_name(
      std::string_view character_name) const;
  /// @brief 同步队伍成员的状态
  void sync_legacy_group_member(ActorLocator& locator, std::uint64_t group_id,
                                RuntimeDispatch& dispatch);
  /// @brief 创建新队伍
  void create_legacy_group(std::uint64_t owner_session_id, std::string_view target_name,
                           RuntimeDispatch& dispatch);
  /// @brief 添加队伍成员
  void add_legacy_group_member(std::uint64_t owner_session_id, std::string_view target_name,
                               RuntimeDispatch& dispatch);
  /// @brief 按名称移除队伍成员
  void remove_legacy_group_member_by_name(std::uint64_t owner_session_id,
                                          std::string_view target_name,
                                          RuntimeDispatch& dispatch);
  /// @brief 按会话 ID 移除队伍成员
  void remove_legacy_group_member(std::uint64_t session_id, RuntimeDispatch& dispatch);
  /// @brief 从 LogicCommand 构建 ActorMail
  [[nodiscard]] ActorMail make_player_mail(const LogicCommand& command,
                                           const ActorLocator& locator) const;
  /// @brief 处理传统聊天命令
  [[nodiscard]] bool handle_legacy_chat_command(const LogicCommand& command,
                                                ActorLocator& locator,
                                                const LegacyChatParseResult& parsed,
                                                std::uint64_t now_ms,
                                                RuntimeDispatch& dispatch);
  /// @brief 路由传统聊天命令
  [[nodiscard]] bool route_legacy_chat_command(const LogicCommand& command,
                                               ActorLocator& locator,
                                               std::uint64_t now_ms,
                                               RuntimeDispatch& dispatch);
  /// @brief 判断 NPC 是否为商人类型
  [[nodiscard]] bool is_merchant_npc_config(const NpcConfig& npc,
                                            const ActorMail& mail) const;
  /// @brief 添加阶段追踪记录
  void add_stage_trace(RuntimeDispatch& dispatch, std::string stage, std::string action,
                       std::uint64_t now_ms, std::size_t cursor = 0,
                       std::size_t sub_cursor = 0) const;
  /// @brief 处理就绪用户队列
  void process_ready_users(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  /// @brief 处理玩家（human）的每帧逻辑
  void process_user_humans(std::uint64_t now_ms, const LegacyRuntimeContext& context,
                           RuntimeDispatch& dispatch);
  /// @brief 处理怪物的每帧逻辑
  void process_monsters(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  /// @brief 处理怪物刷新组的生成逻辑
  void process_monster_spawn_group(std::size_t group_index, std::uint64_t now_ms,
                                    RuntimeDispatch& dispatch);
  /// @brief 获取怪物组的存活数量
  [[nodiscard]] std::int32_t legacy_monster_live_count_for_spawn(const MonsterGroup& group) const;
  /// @brief 计算传统刷新时间
  [[nodiscard]] std::uint64_t legacy_zen_time_ms(std::uint32_t zen_time_ms) const;
  /// @brief 构建怪物刷新邮件
  [[nodiscard]] ActorMail make_monster_spawn_mail(const MonsterGroup& group,
                                                   std::uint64_t actor_id,
                                                   std::int32_t x,
                                                   std::int32_t y,
                                                   std::uint64_t now_ms,
                                                   RuntimeDispatch* dispatch);
  /// @brief 生成 GM 命令创建的怪物
  [[nodiscard]] std::int32_t spawn_legacy_gm_monsters(
      RuntimeDispatch& dispatch, std::string map_id, std::int32_t x, std::int32_t y,
      std::string monster_name, std::int32_t count, std::uint64_t now_ms,
      std::uint64_t master_actor_id = 0, std::uint8_t slave_exp_level = 0,
      std::optional<std::pair<std::int32_t, std::int32_t>> target_xy = std::nullopt);
  /// @brief 为怪物刷新组随机生成掉落物品
  void roll_legacy_monster_items_for_spawn(const MonsterGroup& group, ActorMail& mail,
                                           std::uint64_t now_ms, RuntimeDispatch* dispatch);
  /// @brief 清理怪物组中已死亡的怪物
  void prune_monster_group(MonsterGroup& group);
  /// @brief 处理商人的每帧逻辑
  void process_merchants(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  /// @brief 处理普通 NPC 的每帧逻辑
  void process_npcs(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  /// @brief 处理用户引擎定时器（如定时保存）
  void process_user_engine_timers(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  /// @brief 重定位断线玩家的位置
  [[nodiscard]] RuntimeDispatch relocate_no_reconnect_player(std::uint64_t session_id,
                                                             std::uint64_t now_ms);
  /// @brief 处理事件创建请求
  void process_legacy_event_creates(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 处理随机空间移动请求
  void process_legacy_random_space_moves(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 处理批次移动请求
  void process_legacy_batch_move_requests(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 执行待处理的批次移动
  void process_legacy_batch_moves(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 执行单条批次移动请求
  void execute_legacy_batch_move_request(const LegacyBatchMoveRequest& request,
                                         RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 处理时间召回请求
  void process_legacy_time_recall_requests(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 处理到期的时间召回事件
  void process_legacy_time_recalls(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 处理跨地图邮件路由
  void process_cross_map_mails(RuntimeDispatch& dispatch);
  /// @brief 刷新圣言帷幕组状态
  void refresh_legacy_holy_curtain_groups(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  /// @brief 清理过期的关闭记录
  void cleanup_close_records(std::uint64_t now_ms);
  /// @brief 合并 RuntimeDispatch（追加模式）
  void append_dispatch(RuntimeDispatch& target, RuntimeDispatch source);
  //@}

  /** @name 内部状态 */
  //@{
  HostConfig config_{};
  std::unordered_map<std::int32_t, ItemConfig> item_configs_{};
  std::unordered_map<std::int32_t, MagicConfig> magic_configs_{};
  std::unordered_map<std::string, MonsterDefConfig> monster_defs_{};
  std::unordered_map<std::string, std::vector<MonsterDropConfig>> monster_drops_{};
  std::unordered_map<std::string, ItemConfig> item_configs_by_name_{};
  CastleDialogContext castle_dialog_context_{};
  GuildCastleSnapshot guild_castle_snapshot_{};
  std::unordered_map<std::string, MerchantStateRecord> merchant_states_{};
  std::unordered_map<std::string, LegacyShutUpEntry> legacy_shut_up_list_{};
  std::unordered_map<std::string, LegacyUserDegree> legacy_admin_degrees_{};
  std::uint64_t next_legacy_group_id_{1};
  std::unordered_map<std::uint64_t, LegacyGroupState> legacy_groups_{};
  std::unordered_map<std::uint64_t, LegacyTimeRecallState> legacy_time_recalls_{};
  WheelTimer<LegacyTimeRecallDue> legacy_time_recall_wheel_{1024};
  WheelTimer<LegacyBatchMoveRequest> legacy_batch_move_wheel_{1024};
  std::uint64_t next_legacy_time_recall_generation_{1};
  std::unordered_map<std::string, std::unique_ptr<MapActor>> maps_{};
  std::vector<std::string> map_order_{};
  std::unordered_map<std::uint64_t, ActorLocator> session_index_{};
  std::deque<LegacyReadyUser> ready_users_{};
  std::vector<std::uint64_t> run_user_order_{};
  std::unordered_map<std::string, CloseRecord> close_records_{};
  std::vector<MonsterGroup> monster_groups_{};
  std::vector<ActorRef> merchant_order_{};
  std::vector<ActorRef> npc_order_{};
  std::size_t hum_cur_{0};
  std::size_t mon_cur_{0};
  std::size_t mon_sub_cur_{0};
  std::size_t gen_cur_{0};
  std::size_t mer_cur_{0};
  std::size_t npc_cur_{0};
  std::uint64_t last_ready_process_ms_{0};
  std::uint64_t one_zen_time_ms_{0};
  bool one_zen_time_initialized_{false};
  std::uint64_t mission_time_ms_{0};
  std::uint64_t open_door_check_ms_{0};
  std::uint64_t timer10min_ms_{0};
  std::uint64_t timer10sec_ms_{0};
  bool user_engine_timers_initialized_{false};
  std::uint64_t last_now_ms_{0};
  std::uint64_t current_tick_{0};
  LegacyRandom legacy_random_{1};
  LegacyEventManager legacy_event_manager_{};
  MakeIndexAllocator make_index_allocator_{};
  std::shared_ptr<std::array<std::int32_t, 10>> script_global_params_{
      std::make_shared<std::array<std::int32_t, 10>>()};
  std::shared_ptr<LegacyNameListRepository> script_name_lists_{};
  std::uint64_t next_actor_id_{1};
  std::string default_map_id_{};
  //@}
};

}  // namespace mir2
