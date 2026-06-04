/**
 * @file models.hpp
 * @brief 配置数据模型定义
 * @details 定义服务端运行时所需的所有配置数据结构，包括：
 *          - 运行时参数（RuntimeConfig）
 *          - 端口绑定（PortConfig, PortBinding）
 *          - 逻辑预算（LogicBudgetConfig）
 *          - 地图相关（MapConfig, MapZoneConfig, MapGateConfig, MapEntryQuestConfig）
 *          - 刷怪配置（SpawnConfig）
 *          - 怪物定义（MonsterDefConfig, MonsterDropConfig）
 *          - 物品定义（ItemConfig）
 *          - 魔法定义（MagicConfig, LegacyMagicDefinition）
 *          - NPC 配置（NpcConfig, MerchantProductConfig, NpcDialogSectionConfig）
 *          - 地图任务（MapQuestConfig, MapEntryRuleConfig）
 *          - 城堡与公会相关（CastleDialogContext, GuildState, GuildCastleSnapshot）
 *          - 顶层宿主配置（HostConfig）
 * @note 这些结构均为纯数据容器，不包含业务逻辑。
 *       所有字段均提供合理的默认值，确保在配置文件缺失字段时仍可正常运行。
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace mir2 {

/**
 * @brief 运行时配置
 * @details 包含服务端运行时的所有基本参数，涵盖日志、数据目录、
 *          网关设置、背压/断线阈值、城堡战争文本模板、公会操作
 *          文本模板、经济系统参数等。所有字符串字段均提供默认值，
 *          用于向客户端展示本地化信息。
 */
struct RuntimeConfig {
  std::filesystem::path log_dir{"logs"};                                      ///< @brief 日志输出目录
  std::filesystem::path data_dir{"data"};                                     ///< @brief 数据持久化目录
  std::filesystem::path asset_root{};                                         ///< @brief 资源根目录（客户端资源文件路径）
  std::filesystem::path legacy_admin_list{"Envir/AdminList.txt"};              ///< @brief 遗留管理员列表文件路径
  std::filesystem::path status_file{"runtime/status.json"};                   ///< @brief 运行时状态文件路径

  std::size_t default_queue_capacity{4096};                                   ///< @brief 默认消息队列容量
  std::size_t io_threads{2};                                                  ///< @brief I/O 线程数量
  bool enable_legacy_gateways{false};                                         ///< @brief 是否启用遗留网关（旧版协议）
  bool enable_client_v1_gateways{true};                                       ///< @brief 是否启用 v1 客户端网关
  bool legacy_approval_mode{false};                                            ///< @brief 是否使用遗留审批模式
  std::size_t backpressure_threshold{3072};                                   ///< @brief 背压阈值，超过此值开始限流
  std::size_t disconnect_threshold{3};                                        ///< @brief 断线阈值，超过此值断开连接
  std::uint32_t castle_context_refresh_ms{5000};                              ///< @brief 城堡上下文刷新间隔（毫秒）

  std::string login_notice_title{"Notice"};                                   ///< @brief 登录公告标题
  std::string login_notice_text{};                                            ///< @brief 登录公告正文
  std::string castle_name{"Sabuk"};                                           ///< @brief 城堡名称
  std::string default_castle_war_date{"Unknown"};                             ///< @brief 默认城堡战争日期文本
  std::string no_active_wars_text{"No active wars."};                         ///< @brief 无活跃战争时的显示文本
  std::string unclaimed_castle_owner{"Unclaimed"};                            ///< @brief 未认领城堡的领主显示文本
  std::string unclaimed_castle_lord{"Unclaimed"};                             ///< @brief 未认领城堡的城主显示文本
  std::string castle_owner_role_label{"Castle Owner"};                        ///< @brief 城堡所有者角色标签
  std::string castle_owner_guild_role_label{"Owner"};                         ///< @brief 城堡所有者公会角色标签
  std::string castle_challenger_role_label{"Challenger"};                     ///< @brief 城堡挑战者角色标签
  std::string castle_rival_role_label{"Rival"};                               ///< @brief 城堡敌对角色标签
  std::string castle_unknown_role_label{"Unknown"};                           ///< @brief 城堡未知角色标签
  std::string castle_war_entry_listed_label{"Listed"};                        ///< @brief 城堡战争已报名标签
  std::string castle_war_entry_unlisted_label{"Not Listed"};                  ///< @brief 城堡战争未报名标签
  std::string castle_war_status_active_label{"Active"};                       ///< @brief 城堡战争活跃状态标签
  std::string castle_war_status_available_label{"Available"};                 ///< @brief 城堡战争可参与状态标签
  std::string castle_role_change_owner_label{"Castle Owner"};                 ///< @brief 角色变更 - 城堡所有者
  std::string castle_role_change_challenger_label{"Castle Challenger"};       ///< @brief 角色变更 - 城堡挑战者
  std::string castle_claim_summary_template{                                  ///< @brief 城堡认领成功摘要模板
      "Castle claimed for guild <$GUILD>."};
  std::string castle_war_summary_template{                                    ///< @brief 城堡宣战摘要模板
      "Castle war declared against <$TARGETGUILD> for <$GOLD> gold."};
  std::string castle_claim_require_guild_template{                            ///< @brief 认领城堡需加入公会的提示模板
      "Join a guild before claiming the castle."};
  std::string castle_claim_missing_guild_template{                            ///< @brief 公会数据不可用的提示模板
      "Guild data is unavailable. Try again in a moment."};
  std::string castle_claim_only_lord_template{                                ///< @brief 仅会长可认领城堡的提示模板
      "Only the guild lord can claim the castle."};
  std::string castle_war_require_guild_template{                              ///< @brief 宣战需加入公会的提示模板
      "Join a guild before declaring war."};
  std::string castle_war_missing_guild_template{                              ///< @brief 宣战时公会数据不可用的提示模板
      "Guild data is unavailable. Try again in a moment."};
  std::string castle_war_only_lord_template{                                  ///< @brief 仅会长可宣战的提示模板
      "Only the guild lord can declare war."};
  std::string castle_war_usage_template{                                      ///< @brief 宣战命令用法模板
      "Usage: @castle war <guild_name>"};
  std::string castle_war_self_target_template{                                ///< @brief 不能对自己公会宣战的提示模板
      "Your guild cannot declare war on itself."};
  std::string castle_war_target_missing_template{                             ///< @brief 目标公会不存在的提示模板
      "Target guild not found."};
  std::string castle_war_already_registered_template{                         ///< @brief 重复宣战的提示模板
      "Castle war against <$TARGETGUILD> is already registered."};
  std::string castle_war_need_gold_template{                                  ///< @brief 资金不足的宣战提示模板
      "You need <$GOLD> gold to declare war."};
  std::string guild_create_summary_template{                                  ///< @brief 公会创建成功摘要模板
      "Guild <$GUILD> created."};
  std::string guild_apply_summary_template{                                   ///< @brief 公会申请已发送摘要模板
      "Application sent to guild <$GUILD>."};
  std::string guild_withdraw_summary_template{                                ///< @brief 撤回公会申请摘要模板
      "Withdrew application from guild <$GUILD>."};
  std::string guild_approve_summary_template{                                 ///< @brief 批准公会申请摘要模板
      "Approved guild application for <$TARGET>."};
  std::string guild_reject_summary_template{                                  ///< @brief 拒绝公会申请摘要模板
      "Rejected guild application for <$TARGET>."};
  std::string guild_kick_summary_template{                                    ///< @brief 踢出公会成员摘要模板
      "Kicked guild member <$TARGET>."};
  std::string guild_title_summary_template{                                   ///< @brief 设置公会头衔摘要模板
      "Set guild title for <$TARGET> to <$TITLE>."};
  std::string guild_transfer_summary_template{                                ///< @brief 转让公会会长摘要模板
      "Transferred guild leadership to <$TARGET>."};
  std::string guild_leave_summary_template{                                   ///< @brief 离开公会摘要模板
      "You left <$GUILD>."};
  std::string guild_leave_transfer_summary_template{                          ///< @brief 离开公会并转让会长摘要模板
      "You left <$GUILD>. New lord: <$NEWLORD>."};
  std::string guild_disband_summary_template{                                 ///< @brief 公会解散摘要模板
      "Guild <$GUILD> has been disbanded."};
  std::string guild_membership_cleared_summary_template{                      ///< @brief 公会成员清空摘要模板
      "Guild membership cleared."};
  std::string guild_apply_alert_template{                                     ///< @brief 公会申请通知模板（发送给会长）
      "<$TARGET> applied to join <$GUILD>."};
  std::string guild_withdraw_alert_template{                                  ///< @brief 撤回申请通知模板
      "<$TARGET> withdrew the application to <$GUILD>."};
  std::string guild_approved_notice_template{                                 ///< @brief 申请通过通知模板（发送给申请人）
      "Your application to <$GUILD> was approved."};
  std::string guild_rejected_notice_template{                                 ///< @brief 申请被拒通知模板
      "Your application to <$GUILD> was rejected."};
  std::string guild_removed_notice_template{                                  ///< @brief 被移除公会通知模板
      "You were removed from guild <$GUILD>."};
  std::string guild_new_lord_notice_template{                                 ///< @brief 新任会长通知模板
      "You are now the guild lord of <$GUILD>."};
  std::string guild_title_changed_notice_template{                            ///< @brief 头衔变更通知模板
      "Your guild title is now <$TITLE>."};
  std::string guild_create_leave_current_template{                            ///< @brief 创建公会前需退出当前公会的提示
      "Leave your current guild before creating a new one."};
  std::string guild_create_choose_name_template{                              ///< @brief 创建公会需先选择名称的提示
      "Choose a guild name first."};
  std::string guild_create_name_unavailable_template{                         ///< @brief 公会名已被占用的提示
      "That guild already exists."};
  std::string guild_create_need_gold_template{                                ///< @brief 创建公会资金不足的提示
      "You need <$GOLD> gold to found a guild."};
  std::string guild_apply_leave_current_template{                             ///< @brief 加入公会前需退出当前公会的提示
      "Leave your current guild before joining another."};
  std::string guild_apply_choose_guild_template{                              ///< @brief 加入公会需先选择公会的提示
      "Choose a guild first."};
  std::string guild_not_found_template{                                       ///< @brief 公会不存在的提示
      "Guild not found."};
  std::string guild_apply_already_pending_template{                           ///< @brief 申请已存在的提示
      "Your application to <$GUILD> is already pending."};
  std::int32_t guild_war_fee{30000};                                          ///< @brief 公会战争费用
  std::int32_t upgrade_weapon_fee{10000};                                     ///< @brief 武器升级费用
  std::int32_t guild_create_fee{10000};                                       ///< @brief 创建公会费用
  std::string black_stone_name{"BlackStone"};                                 ///< @brief 黑铁矿石名称
  std::int32_t legacy_user_full_count{500};                                   ///< @brief 遗留模式满员数量
  std::int32_t legacy_zen_fast_step{300};                                     ///< @brief 遗留金币快速步进阈值
  std::optional<std::uint32_t> legacy_random_seed{};                          ///< @brief 遗留随机数种子（可选，用于确定性重现）
};

/**
 * @brief 端口绑定
 * @details 描述一个网络服务监听的地址和端口组合。
 *          地址默认为 127.0.0.1（本地回环），端口为 0（由系统分配）。
 */
struct PortBinding {
  std::string address{"127.0.0.1"};   ///< @brief 绑定地址
  std::uint16_t port{0};              ///< @brief 绑定端口
};

/**
 * @brief 端口配置
 * @details 定义服务端所有网关的监听端口，包括传统网关和 v1 客户端网关。
 *          v1 客户端指使用第一代网络协议的客户端版本。
 */
struct PortConfig {
  PortBinding login_gateway{};             ///< @brief 登录网关（传统协议）
  PortBinding game_gateway{};              ///< @brief 游戏网关（传统协议）
  PortBinding client_v1_login_gateway{"127.0.0.1", 5600};  ///< @brief v1 客户端登录网关
  PortBinding client_v1_game_gateway{"127.0.0.1", 7100};   ///< @brief v1 客户端游戏网关
};

/**
 * @brief 逻辑预算配置
 * @details 定义服务端各子系统在每个 tick 内的最大执行时间预算。
 *           用于在单线程逻辑循环中公平分配 CPU 时间，防止某个子系统
 *           阻塞其他系统的更新。所有时间单位均为毫秒。
 */
struct LogicBudgetConfig {
  std::uint32_t tick_ms{10};                          ///< @brief 逻辑 tick 间隔（毫秒）
  std::uint32_t player_budget_ms{30};                 ///< @brief 每 tick 玩家更新预算
  std::uint32_t player_input_budget_per_tick{0};       ///< @brief 每 tick 玩家输入处理预算（0=不限制）
  std::uint32_t monster_budget_ms{30};                ///< @brief 每 tick 怪物更新预算
  std::uint32_t spawn_budget_ms{30};                  ///< @brief 每 tick 刷怪更新预算
  std::uint32_t npc_budget_ms{5};                     ///< @brief 每 tick NPC 更新预算
  std::uint32_t net_flush_budget_ms{30};              ///< @brief 每 tick 网络刷新预算
};

/**
 * @brief 地图区域配置
 * @details 定义地图上的矩形区域，用于表示安全区、红名区等。
 *          如果 width 或 height 未指定（为 0），则可以通过
 *          right/bottom 坐标自动计算宽高。
 */
struct MapZoneConfig {
  std::int32_t x{0};          ///< @brief 区域左上角 X 坐标（也可用 left 别名）
  std::int32_t y{0};          ///< @brief 区域左上角 Y 坐标（也可用 top 别名）
  std::int32_t width{0};      ///< @brief 区域宽度
  std::int32_t height{0};     ///< @brief 区域高度
};

/**
 * @brief 地图传送门配置
 * @details 定义地图上的传送门/入口点，玩家踩上后可传送到目标地图。
 *          可选择是否需要特定门处于打开状态才能传送。
 */
struct MapGateConfig {
  std::int32_t x{0};                          ///< @brief 传送门 X 坐标
  std::int32_t y{0};                          ///< @brief 传送门 Y 坐标
  std::string target_map_id{};                ///< @brief 目标地图 ID
  std::int32_t target_x{0};                   ///< @brief 目标 X 坐标
  std::int32_t target_y{0};                   ///< @brief 目标 Y 坐标
  bool require_doors_open{true};              ///< @brief 是否需要门打开才能传送
};

/**
 * @brief NPC 对话框片段配置
 * @details 表示 NPC 对话框中的一个功能片段，包含动作标识和
 *          对应的对话文本。动作标识通常以 @ 开头（例如 @main），
 *          用于在遗留脚本中跳转到对应的对话分支。
 */
struct NpcDialogSectionConfig {
  std::string action{};       ///< @brief 动作标识（如 @main、@buy）
  std::string text{};        ///< @brief 对话文本内容
};

/**
 * @brief 地图进入任务配置
 * @details 定义玩家进入地图时需要检查的任务条件。
 *          如果指定了任务脚本文件（qfile），将从该文件中加载
 *          对话片段以向玩家展示任务信息。
 */
struct MapEntryQuestConfig {
  std::string qfile{};                                ///< @brief 任务脚本文件名
  std::vector<NpcDialogSectionConfig> dialog_sections{};  ///< @brief 对话片段列表
};

/**
 * @brief 地图配置
 * @details 定义服务端中一张地图的所有属性，包括尺寸、安全区、
 *          红名区、传送门、PK 规则、等级限制、任务检查等。
 *          是游戏世界的基本空间单元。
 */
struct MapConfig {
  std::string id{};                       ///< @brief 地图唯一标识
  std::string title{};                    ///< @brief 地图显示名称
  std::filesystem::path source_map{};     ///< @brief 地图源文件路径
  std::int32_t width{0};                  ///< @brief 地图宽度（单元格数）
  std::int32_t height{0};                 ///< @brief 地图高度（单元格数）
  std::int32_t home_x{0};                 ///< @brief 默认复活 X 坐标
  std::int32_t home_y{0};                 ///< @brief 默认复活 Y 坐标
  bool allow_pk{true};                    ///< @brief 是否允许 PK
  std::vector<MapZoneConfig> safe_zones{};    ///< @brief 安全区域列表
  std::vector<MapZoneConfig> badman_zones{};  ///< @brief 红名区域列表
  bool law_full{false};                   ///< @brief 是否完全法治（禁止任何恶意行为，safe 别名）
  bool fight_zone{false};                 ///< @brief 是否为战斗区域（fight 别名）
  bool fight3_zone{false};                ///< @brief 是否为三阵营战斗区域（fight3 别名）
  bool daylight{false};                   ///< @brief 是否始终为白天（day 别名）
  bool darkness{false};                   ///< @brief 是否始终为黑暗（dark 别名）
  bool no_reconnect{false};               ///< @brief 断线后是否禁止重连到本图
  bool need_hole{false};                  ///< @brief 是否需要隐身戒指才能进入
  bool no_recall{false};                  ///< @brief 是否禁止记忆召唤
  bool no_random_move{false};             ///< @brief 是否禁止随机传送
  bool no_drug{false};                    ///< @brief 是否禁止使用药品
  bool no_position_move{false};           ///< @brief 是否禁止定位移动
  std::int32_t need_level{0};             ///< @brief 进入所需最低等级（level 别名）
  std::int32_t mine_map{0};              ///< @brief 矿区地图标识（mine 别名）
  std::string back_map{};                ///< @brief 返回地图 ID
  std::vector<MapGateConfig> gates{};    ///< @brief 传送门列表
  bool quiz_zone{false};                 ///< @brief 是否为答题区域（quiz 别名）
  std::int32_t need_set_number{-1};       ///< @brief 进入所需任务集编号（need_set 别名）
  std::int32_t need_set_value{-1};        ///< @brief 进入所需任务集值
  std::optional<MapEntryQuestConfig> check_quest{};  ///< @brief 进入地图的任务检查（可选）
};

/**
 * @brief 地图进入规则配置
 * @details 定义玩家进入地图时需要满足的条件。与 MapConfig 中的
 *          部分字段重叠，但独立用于声明式的进入规则检查系统。
 *          包含等级、隐身、任务条件等检查项。
 */
struct MapEntryRuleConfig {
  std::string map_id{};                                   ///< @brief 目标地图 ID
  std::filesystem::path source_map{};                     ///< @brief 地图源文件路径
  std::int32_t width{0};                                  ///< @brief 地图宽度
  std::int32_t height{0};                                 ///< @brief 地图高度
  std::int32_t need_level{0};                             ///< @brief 进入所需等级
  bool need_hole{false};                                  ///< @brief 是否需要隐身戒指
  std::int32_t need_set_number{-1};                       ///< @brief 所需任务集编号
  std::int32_t need_set_value{-1};                        ///< @brief 所需任务集值
  std::optional<MapEntryQuestConfig> check_quest{};       ///< @brief 任务检查配置（可选）
};

/**
 * @brief 刷怪配置
 * @details 定义在地图上生成怪物的规则，包括怪物类型、位置、
 *          数量、属性、重生时间和金币掉落等参数。
 *          legacy_group 标记表示该刷怪项需要按旧版逻辑分组处理。
 */
struct SpawnConfig {
  std::string map_id{};                       ///< @brief 刷怪所在地图 ID
  std::string actor_type{};                   ///< @brief 角色类型（通常为 monster）
  std::string name{};                         ///< @brief 怪物名称
  std::int32_t x{0};                          ///< @brief 刷怪中心 X 坐标
  std::int32_t y{0};                          ///< @brief 刷怪中心 Y 坐标
  std::uint32_t respawn_ms{0};                ///< @brief 重生间隔（毫秒）
  std::int32_t level{1};                      ///< @brief 怪物等级
  std::int32_t max_hp{12};                    ///< @brief 最大生命值
  std::int32_t attack_power{3};               ///< @brief 攻击力
  std::int32_t defense{0};                    ///< @brief 防御力
  std::int32_t magic_defense{0};              ///< @brief 魔法防御力
  std::int32_t exp_reward{12};                ///< @brief 经验奖励
  std::int32_t life_attrib{0};                ///< @brief 生命属性（0=正常，1=不死）
  bool tameable{true};                        ///< @brief 是否可被驯服
  std::int32_t area{0};                       ///< @brief 刷怪范围半径
  std::int32_t count{1};                      ///< @brief 一次生成的怪物数量
  std::uint32_t zen_time_ms{0};               ///< @brief 金币掉落持续时间（毫秒）
  std::int32_t small_zen_rate{0};             ///< @brief 小额金币掉落概率百分比
  bool legacy_group{false};                   ///< @brief 是否使用遗留分组逻辑
};

/**
 * @brief 怪物 AI 类型枚举
 * @details 定义怪物的人工智能行为模式，影响怪物的移动、
 *          攻击和反应策略。
 */
enum class MonsterAiProfile {
  passive_animal,     ///< @brief 被动型动物（不主动攻击，受击逃跑）
  basic,              ///< @brief 基础型（标准行为模式）
  aggressive,         ///< @brief 攻击型（主动攻击接近的玩家）
  slow,               ///< @brief 迟缓型（移动慢，攻击间隔长）
  ranged,             ///< @brief 远程型（使用远程攻击）
  stationary          ///< @brief 静止型（不移动，原地攻击）
};

/**
 * @brief 怪物定义配置
 * @details 完整定义一种怪物类型的所有属性，包括外观、战斗属性、
 *          移动速度和 AI 行为模式。是怪物模版数据，与具体刷怪点无关。
 */
struct MonsterDefConfig {
  std::string name{};                         ///< @brief 怪物名称
  std::int32_t race_server{0};                ///< @brief 服务端种族类型（race 别名）
  std::int32_t race_image{0};                 ///< @brief 种族外观图像索引（race_img 别名）
  std::int32_t appearance{0};                 ///< @brief 外观图像索引（img_index 别名）
  std::int32_t level{1};                      ///< @brief 等级
  bool undead{false};                         ///< @brief 是否是不死系怪物
  bool tameable{true};                        ///< @brief 是否可被驯服
  std::int32_t cool_eye{0};                   ///< @brief 反隐形等级
  std::int32_t exp{12};                       ///< @brief 击杀经验值
  std::int32_t hp{12};                        ///< @brief 生命值
  std::int32_t mp{0};                         ///< @brief 魔法值
  std::int32_t ac{0};                         ///< @brief 防御力
  std::int32_t mac{0};                        ///< @brief 魔法防御力
  std::int32_t dc{3};                         ///< @brief 攻击力下限
  std::int32_t dc_max{0};                     ///< @brief 攻击力上限（dcmax 别名）
  std::int32_t mc{0};                         ///< @brief 魔法力
  std::int32_t sc{0};                         ///< @brief 道术力
  std::int32_t agility{0};                    ///< @brief 敏捷度
  std::int32_t accurate{0};                   ///< @brief 准确度
  std::int32_t walk_speed_ms{200};            ///< @brief 行走速度（毫秒）
  std::int32_t walk_step{1};                  ///< @brief 每次行走步数
  std::int32_t walk_wait_ms{0};               ///< @brief 行走等待时间（毫秒）
  std::int32_t attack_speed_ms{200};          ///< @brief 攻击速度（毫秒）
  MonsterAiProfile ai_profile{MonsterAiProfile::basic};  ///< @brief AI 行为模式
};

/**
 * @brief 怪物掉落配置
 * @details 定义怪物死亡后可掉落的物品及概率。sel_point 和 max_point
 *          构成掉落概率系统，通常 sel_point/max_point 表示掉落权重。
 */
struct MonsterDropConfig {
  std::string monster_name{};     ///< @brief 怪物名称
  std::int32_t sel_point{0};      ///< @brief 选中点（分子/权重）
  std::int32_t max_point{0};      ///< @brief 最大点（分母/总权重）
  std::string item_name{};        ///< @brief 掉落物品名称
  std::int32_t count{1};          ///< @brief 掉落数量
};

/**
 * @brief 物品配置
 * @details 定义游戏中一种物品的所有属性，包括基础属性（重量、价格）、
 *          装备属性（AC/MAC/DC/MC/SC）、职业性别限制、特殊效果、
 *          绑定/解绑信息等。与传奇类游戏的物品系统兼容。
 */
struct ItemConfig {
  std::int32_t id{0};                         ///< @brief 物品唯一 ID
  std::string name{};                         ///< @brief 物品名称
  std::int32_t weight{0};                     ///< @brief 重量
  std::int32_t price{0};                      ///< @brief 价格
  std::int32_t std_mode{0};                   ///< @brief 标准模式（物品类型分类）
  std::int32_t shape{0};                      ///< @brief 形状
  std::int32_t looks{0};                      ///< @brief 外观索引（默认等于 id）
  std::int32_t dura_max{0};                   ///< @brief 最大耐久度
  std::int32_t equip_slot{-1};                ///< @brief 装备槽位（-1=不可装备）
  std::int32_t hp_add{0};                     ///< @brief HP 加成
  std::int32_t mp_add{0};                     ///< @brief MP 加成
  std::int32_t need{0};                       ///< @brief 需求类型
  std::int32_t need_level{0};                 ///< @brief 需求等级（required_level 别名）
  std::int32_t job{-1};                       ///< @brief 需求职业（-1=无限制，required_job 别名）
  std::int32_t sex{-1};                       ///< @brief 需求性别（-1=无限制，required_sex 别名）
  std::int32_t stock{0};                      ///< @brief 库存数量
  std::int32_t item_desc{0};                  ///< @brief 物品描述类型
  std::int32_t special_pwr{0};                ///< @brief 特殊威力
  std::uint16_t ac{0};                        ///< @brief 防御力
  std::uint16_t mac{0};                       ///< @brief 魔法防御力
  std::uint16_t dc{0};                        ///< @brief 攻击力
  std::uint16_t mc{0};                        ///< @brief 魔法力
  std::uint16_t sc{0};                        ///< @brief 道术力
  std::int32_t accurate{0};                   ///< @brief 准确度（accuracy 别名）
  std::int32_t agility{0};                    ///< @brief 敏捷度
  std::int32_t atk_spd{0};                    ///< @brief 攻击速度
  std::int32_t mg_avoid{0};                   ///< @brief 魔法躲避
  std::int32_t strong{0};                     ///< @brief 强度
  std::int32_t undead{0};                     ///< @brief 不死系加成
  std::int32_t exp_add{0};                    ///< @brief 经验加成
  std::int32_t eff_type1{0};                  ///< @brief 效果类型 1（特殊效果）
  std::int32_t eff_rate1{0};                  ///< @brief 效果触发概率 1（百分比）
  std::int32_t eff_value1{0};                 ///< @brief 效果数值 1
  std::int32_t eff_type2{0};                  ///< @brief 效果类型 2
  std::int32_t eff_rate2{0};                  ///< @brief 效果触发概率 2
  std::int32_t eff_value2{0};                 ///< @brief 效果数值 2
  std::string scroll_kind{};                  ///< @brief 卷轴类型
  std::string unbind_item{};                  ///< @brief 解绑所需物品
  std::int32_t unbind_count{0};               ///< @brief 解绑所需数量
  std::int32_t ani_count{0};                  ///< @brief 动画帧数
};

/**
 * @brief 遗留魔法定义
 * @details 兼容旧版传奇客户端的魔法定义字段。当 legacy_present 为 true 时，
 *          表示该魔法需要以旧版格式发送到客户端。包含效果类型、符文、
 *          训练等级限制、防御符文等旧版特有字段。
 */
struct LegacyMagicDefinition {
  bool legacy_present{false};                 ///< @brief 是否存在遗留定义
  std::int32_t effect_type{0};                ///< @brief 效果类型
  std::int32_t effect{0};                     ///< @brief 效果索引
  std::int32_t spell{0};                      ///< @brief 符文ID
  std::int32_t min_power{0};                  ///< @brief 最小威力
  std::int32_t max_power{0};                  ///< @brief 最大威力
  std::int32_t job{0};                        ///< @brief 职业限制
  std::array<std::int32_t, 4> need_level{0, 0, 0, 0};   ///< @brief 每级训练所需等级
  std::array<std::int32_t, 4> max_train{0, 0, 0, 0};    ///< @brief 每级最大熟练度
  std::int32_t max_train_level{0};            ///< @brief 最大可训练等级
  std::int32_t delay_time{0};                 ///< @brief 施法延迟时间
  std::int32_t def_spell{0};                  ///< @brief 防御符文
  std::int32_t def_min_power{0};              ///< @brief 防御最小威力
  std::int32_t def_max_power{0};              ///< @brief 防御最大威力
  std::string desc{};                         ///< @brief 描述文本
  bool is_sword_skill{false};                 ///< @brief 是否为剑术技能
};

/**
 * @brief 魔法配置
 * @details 定义服务端中一种魔法的完整属性，包括消耗、范围、
 *          治疗效果、持续伤害（DoT）、减速、护盾等效果。
 *          同时包含可选的遗留魔法定义以兼容旧版客户端。
 */
struct MagicConfig {
  std::int32_t id{0};                             ///< @brief 魔法唯一 ID
  std::string name{};                             ///< @brief 魔法名称
  std::int32_t mp_cost{0};                        ///< @brief 魔法消耗
  std::int32_t power{0};                          ///< @brief 魔法威力
  std::int32_t radius{0};                         ///< @brief 效果半径
  bool affect_players{false};                     ///< @brief 是否影响玩家
  bool affect_monsters{true};                     ///< @brief 是否影响怪物
  std::int32_t instant_heal{0};                   ///< @brief 瞬间治疗量
  std::int32_t heal_per_tick{0};                  ///< @brief 每 tick 持续治疗量
  bool dispel_negative{false};                    ///< @brief 是否可驱散负面状态
  std::int32_t dot_damage{0};                     ///< @brief 持续伤害量
  std::uint32_t effect_duration_ms{0};            ///< @brief 效果持续时间（毫秒）
  std::uint32_t effect_tick_ms{0};                ///< @brief 效果触发间隔（毫秒）
  std::int32_t slow_percent{0};                   ///< @brief 减速百分比
  std::int32_t shield_amount{0};                  ///< @brief 护盾吸收量
  LegacyMagicDefinition legacy{};                 ///< @brief 遗留魔法定义（兼容旧版客户端）
};

/**
 * @brief 商人商品配置
 * @details 定义 NPC 商人出售的商品及其库存和刷新周期。
 *          refresh_hours 控制商品的补货间隔。
 */
struct MerchantProductConfig {
  std::string item_name{};        ///< @brief 物品名称
  std::int32_t count{0};          ///< @brief 库存数量
  std::int32_t refresh_hours{0};  ///< @brief 刷新间隔（小时）
};

/**
 * @brief 城堡对话上下文
 * @details 存储城堡战争系统中所有与玩家对话相关的文本模板。
 *          这些模板通过格式化字符串（如 <$GUILD>、<$GOLD>）
 *          动态插入实际值后展示给玩家。
 * @note 所有字段均与 RuntimeConfig 中的对应字段一一映射，
 *       用于在城堡系统中组合对话显示。
 */
struct CastleDialogContext {
  std::string castle_name{};                          ///< @brief 城堡名称
  std::string owner_guild{};                          ///< @brief 拥有公会名称
  std::string lord{};                                 ///< @brief 城主名称
  std::string castle_war_date{};                      ///< @brief 城堡战争日期
  std::string list_of_war{};                          ///< @brief 战争列表
  std::string no_active_wars_text{};                  ///< @brief 无活跃战争文本
  std::string unclaimed_owner_label{};                ///< @brief 未认领领主标签
  std::string unclaimed_lord_label{};                 ///< @brief 未认领城主标签
  std::string owner_role_label{};                     ///< @brief 所有者角色标签
  std::string owner_guild_role_label{};               ///< @brief 所有者公会角色标签
  std::string challenger_role_label{};                ///< @brief 挑战者角色标签
  std::string rival_role_label{};                     ///< @brief 敌对角色标签
  std::string unknown_role_label{};                   ///< @brief 未知角色标签
  std::string war_entry_listed_label{};               ///< @brief 战争报名已登记标签
  std::string war_entry_unlisted_label{};             ///< @brief 战争报名未登记标签
  std::string war_status_active_label{};              ///< @brief 战争活跃状态标签
  std::string war_status_available_label{};           ///< @brief 战争可参与状态标签
  std::string role_change_owner_label{};              ///< @brief 角色变更所有者标签
  std::string role_change_challenger_label{};         ///< @brief 角色变更挑战者标签
  std::string claim_summary_template{};               ///< @brief 认领摘要模板
  std::string war_summary_template{};                 ///< @brief 宣战摘要模板
  std::string claim_require_guild_template{};         ///< @brief 认领需加入公会模板
  std::string claim_missing_guild_template{};         ///< @brief 认领时公会缺失模板
  std::string claim_only_lord_template{};             ///< @brief 仅限会长认领模板
  std::string war_require_guild_template{};           ///< @brief 宣战需加入公会模板
  std::string war_missing_guild_template{};           ///< @brief 宣战时公会缺失模板
  std::string war_only_lord_template{};               ///< @brief 仅限会长宣战模板
  std::string war_usage_template{};                   ///< @brief 宣战命令用法模板
  std::string war_self_target_template{};             ///< @brief 自宣战提示模板
  std::string war_target_missing_template{};          ///< @brief 目标缺失模板
  std::string war_already_registered_template{};      ///< @brief 已登记战争模板
  std::string war_need_gold_template{};               ///< @brief 需要金币模板
  std::string guild_create_summary_template{};        ///< @brief 公会创建摘要模板
  std::string guild_apply_summary_template{};         ///< @brief 公会申请摘要模板
  std::string guild_withdraw_summary_template{};      ///< @brief 撤回申请摘要模板
  std::string guild_approve_summary_template{};       ///< @brief 批准申请摘要模板
  std::string guild_reject_summary_template{};        ///< @brief 拒绝申请摘要模板
  std::string guild_kick_summary_template{};          ///< @brief 踢出成员摘要模板
  std::string guild_title_summary_template{};         ///< @brief 设置头衔摘要模板
  std::string guild_transfer_summary_template{};      ///< @brief 转让会长摘要模板
  std::string guild_leave_summary_template{};         ///< @brief 离开公会摘要模板
  std::string guild_leave_transfer_summary_template{}; ///< @brief 离开并转让摘要模板
  std::string guild_disband_summary_template{};       ///< @brief 解散公会摘要模板
  std::string guild_membership_cleared_summary_template{}; ///< @brief 成员清空摘要模板
  std::string guild_apply_alert_template{};           ///< @brief 申请提醒模板
  std::string guild_withdraw_alert_template{};        ///< @brief 撤回提醒模板
  std::string guild_approved_notice_template{};       ///< @brief 批准通知模板
  std::string guild_rejected_notice_template{};       ///< @brief 拒绝通知模板
  std::string guild_removed_notice_template{};        ///< @brief 移除通知模板
  std::string guild_new_lord_notice_template{};       ///< @brief 新任会长通知模板
  std::string guild_title_changed_notice_template{};  ///< @brief 头衔变更通知模板
  std::string guild_create_leave_current_template{};  ///< @brief 创建需退出当前公会模板
  std::string guild_create_choose_name_template{};    ///< @brief 创建需选择名称模板
  std::string guild_create_name_unavailable_template{}; ///< @brief 名称不可用模板
  std::string guild_create_need_gold_template{};      ///< @brief 创建需要金币模板
  std::string guild_apply_leave_current_template{};   ///< @brief 加入需退出当前公会模板
  std::string guild_apply_choose_guild_template{};    ///< @brief 加入需选择公会模板
  std::string guild_not_found_template{};             ///< @brief 公会不存在模板
  std::string guild_apply_already_pending_template{}; ///< @brief 申请已存在模板
  std::int32_t guild_war_fee{0};                      ///< @brief 公会战争费用
  std::int32_t upgrade_weapon_fee{0};                 ///< @brief 武器升级费用
  std::int32_t guild_create_fee{0};                   ///< @brief 公会创建费用
};

/**
 * @brief 公会状态
 * @details 表示单个公会的运行时状态，包括名称、会长、成员和申请人列表。
 *          用于城堡战争系统中展示公会信息和处理公会交互。
 */
struct GuildState {
  std::string guild_name{};               ///< @brief 公会名称
  std::string lord{};                     ///< @brief 会长名称
  std::vector<std::string> members{};     ///< @brief 成员名称列表
  std::vector<std::string> applicants{};  ///< @brief 申请人名称列表
};

/**
 * @brief 城堡公会快照
 * @details 包含城堡对话上下文和所有公会状态的完整快照，
 *          用于在城堡战争相关对话中向玩家展示最新信息。
 */
struct GuildCastleSnapshot {
  CastleDialogContext castle_dialog{};     ///< @brief 城堡对话上下文
  std::vector<GuildState> guilds{};       ///< @brief 公会状态列表
};

/**
 * @brief NPC 配置
 * @details 定义一个 NPC 的所有属性，包括位置、外观、脚本、服务类型、
 *          商人货物、对话片段和遗留模式交易配置。
 *          NPC 可以通过对话片段与玩家交互，商人 NPC 还可以进行买卖和修理。
 */
struct NpcConfig {
  std::string id{};                                       ///< @brief NPC 唯一标识
  std::string map_id{};                                   ///< @brief 所在地图 ID
  std::string name{};                                     ///< @brief NPC 显示名称
  std::int32_t x{0};                                      ///< @brief X 坐标
  std::int32_t y{0};                                      ///< @brief Y 坐标
  std::string script{};                                   ///< @brief 脚本文件名
  std::string service{};                                  ///< @brief 服务类型（如 sell_repair, storage, guild_castle）
  std::vector<std::int32_t> merchant_goods{};             ///< @brief 商人货物 ID 列表
  std::vector<NpcDialogSectionConfig> dialog_sections{};  ///< @brief 对话片段列表
  std::int32_t price_rate_percent{100};                    ///< @brief 价格倍率（百分比）
  std::vector<std::int32_t> legacy_deal_std_modes{};      ///< @brief 遗留交易标准模式列表
  std::vector<MerchantProductConfig> merchant_products{};  ///< @brief 商品列表（带刷新周期）
};

/**
 * @brief 地图任务配置
 * @details 定义地图上的任务触发器，当玩家在地图上击杀指定怪物时，
 *          会检查任务集状态并触发相应的对话脚本。
 *          enable_group 控制是否为队伍共享任务进度。
 */
struct MapQuestConfig {
  std::string map_id{};                                   ///< @brief 地图 ID
  std::int32_t set_number{0};                             ///< @brief 任务集合编号
  std::int32_t value{0};                                  ///< @brief 任务值（0 或 1）
  std::string monster_name{};                             ///< @brief 目标怪物名称（mon_name 别名）
  std::string item_name{};                                ///< @brief 任务物品名称
  std::string qfile{};                                    ///< @brief 任务脚本文件名
  bool enable_group{false};                               ///< @brief 是否启用队伍共享
  std::vector<NpcDialogSectionConfig> dialog_sections{};  ///< @brief 任务对话片段
};

/**
 * @brief 宿主配置（顶层配置容器）
 * @details 作为服务端配置的顶层容器，聚合所有子系统配置。
 *          包含运行时参数、端口绑定、逻辑预算以及所有地图、
 *          怪物、刷怪、物品、魔法、NPC、地图任务和启动任务
 *          的完整定义列表。是配置加载过程的最终产出物。
 */
struct HostConfig {
  RuntimeConfig runtime{};                        ///< @brief 运行时参数
  PortConfig ports{};                             ///< @brief 端口配置
  LogicBudgetConfig budgets{};                    ///< @brief 逻辑预算配置
  std::vector<MapConfig> maps{};                  ///< @brief 地图配置列表
  std::vector<SpawnConfig> spawns{};              ///< @brief 刷怪配置列表
  std::vector<MonsterDefConfig> monsters{};       ///< @brief 怪物定义列表
  std::vector<MonsterDropConfig> monster_drops{}; ///< @brief 怪物掉落列表
  std::vector<ItemConfig> items{};                ///< @brief 物品定义列表
  std::vector<MagicConfig> magics{};              ///< @brief 魔法定义列表
  std::vector<NpcConfig> npcs{};                  ///< @brief NPC 配置列表
  std::vector<MapQuestConfig> map_quests{};       ///< @brief 地图任务列表
  std::vector<NpcDialogSectionConfig> startup_quest_dialog_sections{}; ///< @brief 启动任务对话片段
};

}  // namespace mir2
