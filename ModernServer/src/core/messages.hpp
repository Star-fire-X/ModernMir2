/**
 * @file messages.hpp
 * @brief 消息类型定义——模块间通信的数据结构
 *
 * @details 该文件定义了 mir2 游戏服务器中所有用于模块间通信的消息类型。
 *          消息通过 LocalBus 在模块之间传递，覆盖了网络事件、逻辑命令、
 *          角色（Actor）通信、持久化请求/响应和审计事件等范畴。
 *
 * 消息分类：
 * 1. 网络层消息 —— SessionEvent（会话事件：连接、断开、数据包收发）
 * 2. 逻辑层消息 —— LogicCommand（玩家操作：移动、攻击、交易、组队等）
 * 3. Actor层消息 —— ActorMail（实体间通信：生成、移动、攻击、状态同步）
 * 4. 持久化消息 —— PersistRequest / PersistResult（数据库操作请求与响应）
 * 5. 审计消息 —— AuditEvent（操作审计日志）
 *
 * 所有消息类型通过 BusMessage 变体类型统一，使用 std::variant 实现类型安全的多态。
 *
 * @see LocalBus 消息传输总线
 * @see BusMessage 统一消息类型
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "config/models.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

// ======================== 常量定义 ========================

/**
 * @brief 旧版数据包魔数（Magic Number）
 *
 * @details 用于验证旧版客户端数据包头部合法性的固定标记值。
 *          值为 0xaa55aa55，这是一个典型的魔数模式，
 *          用于区分有效数据包和内存垃圾数据。
 */
constexpr std::int32_t kLegacyPacketCode = static_cast<std::int32_t>(0xaa55aa55);

/**
 * @brief 最大任务标记数量
 */
constexpr std::size_t kMaxLegacyQuestMarks = 256;

/**
 * @brief 最大脚本参数数量
 */
constexpr std::size_t kMaxLegacyScriptParams = 10;

/**
 * @brief 最大随从/奴隶数量
 */
constexpr std::size_t kMaxLegacySlaves = 5;

// ======================== 网络层结构 ========================

/**
 * @brief 旧版数据包头部
 *
 * @details 与旧版客户端兼容的通信协议数据包头部。
 *          包含魔数验证、连接标识、数据包类型索引和数据长度等字段。
 *          这些字段对应传统 Delphi 客户端的 TSendInfo / TDefaultMessage 结构。
 */
struct LegacyPacketHeader {
  std::int32_t code{kLegacyPacketCode};     ///< 魔数验证码，固定为 0xaa55aa55
  std::int32_t socket_number{0};            ///< 套接字编号
  std::uint16_t user_gate_index{0};         ///< 用户网关索引
  std::uint16_t ident{0};                   ///< 数据包协议标识
  std::uint16_t user_list_index{0};         ///< 用户列表索引
  std::uint16_t temp{0};                    ///< 临时字段（协议扩展用）
  std::int32_t length{0};                   ///< 数据包体长度（不含头部）
};

/**
 * @brief 旧版数据包（头部 + 体）
 */
struct LegacyPacket {
  LegacyPacketHeader header{};              ///< 数据包头部
  std::vector<std::uint8_t> body{};         ///< 数据包体（原始字节）
};

// ======================== 角色数据相关结构 ========================

/**
 * @brief 角色随从记录
 *
 * @details 存储玩家角色所拥有的随从/奴隶的基本状态信息。
 *          包括经验值、等级、生命值/魔法值等战斗相关属性。
 */
struct CharacterSlaveRecord {
  std::string name{};                       ///< 随从名称
  std::int32_t slave_exp{0};                ///< 随从当前经验值
  std::uint8_t slave_exp_level{0};          ///< 随从经验等级
  std::uint8_t slave_make_level{0};         ///< 随从制作等级
  std::int32_t remain_royalty_sec{0};       ///< 剩余忠诚时间（秒）
  std::int32_t hp{0};                       ///< 当前生命值
  std::int32_t mp{0};                       ///< 当前魔法值
};

/**
 * @brief 角色完整记录
 *
 * @details 存储玩家角色在游戏世界中的所有状态数据。
 *          这是游戏中最核心的数据结构之一，涵盖了角色的：
 *          - 身份信息（账号、名称、公会）
 *          - 位置信息（地图坐标、方向）
 *          - 外观信息（职业、性别、发型）
 *          - 属性信息（基础属性、装备、背包、仓库）
 *          - 状态信息（攻击模式、PK 值、死亡时间）
 *          - 任务信息（任务标记、脚本参数）
 *          - 随从信息（奴隶列表）
 *
 * @note 此结构用于持久化存储和跨模块传输，需要保持与旧版客户端的字段兼容性。
 */
struct CharacterRecord {
  std::string account_id{};                 ///< 账号 ID
  std::string character_name{};             ///< 角色名称
  std::string guild_name{};                 ///< 所属公会名称
  std::string guild_title{};                ///< 公会头衔
  std::string map_id{"0"};                  ///< 当前所在地图 ID
  std::int32_t x{330};                      ///< 当前 X 坐标（默认 330）
  std::int32_t y{270};                      ///< 当前 Y 坐标（默认 270）
  std::uint8_t dir{0};                      ///< 面向方向
  std::uint8_t light{0};                    ///< 光照等级
  std::uint8_t job{0};                      ///< 职业
  std::uint8_t sex{0};                      ///< 性别
  std::uint8_t hair{0};                     ///< 发型索引
  std::int32_t gold{0};                     ///< 金币数量
  std::int32_t feature{0};                  ///< 外观特征掩码
  std::int32_t status{0};                   ///< 状态标志
  LegacyAbility ability{};                  ///< 基础属性（攻击、防御、魔法等）
  std::array<LegacyUserItem, kMaxEquipSlots> equipped_items{};     ///< 已装备物品数组
  std::array<LegacyUserItem, kMaxBagItems> bag_items{};            ///< 背包物品数组
  std::array<LegacyUserItem, kMaxSaveItems> storage_items{};       ///< 仓库物品数组
  std::array<LegacyUseMagicInfo, kMaxUserMagic> magics{};          ///< 已学习魔法列表
  std::uint8_t attack_mode{1};              ///< 攻击模式
  std::int32_t pk_point{0};                 ///< PK 值
  std::uint64_t death_time_ms{0};           ///< 死亡时间戳（毫秒）
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_marks{};       ///< 任务完成标记
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_open_units{};  ///< 已开放任务单元
  std::array<std::uint8_t, kMaxLegacyQuestMarks> quest_units{};       ///< 任务单元进度
  std::array<std::int32_t, kMaxLegacyScriptParams> script_params{};   ///< 脚本参数数组
  std::uint32_t daily_quest{0};             ///< 每日任务状态
  std::array<CharacterSlaveRecord, kMaxLegacySlaves> slaves{};    ///< 随从列表
  double body_luck{0.0};                    ///< 角色幸运值
  bool birth_items_granted{true};           ///< 初始物品是否已发放
  std::uint64_t save_version{0};            ///< 存档版本号（用于兼容性检查）
};

// ======================== 装备升级相关 ========================

/**
 * @brief 装备升级记录
 *
 * @details 记录玩家提交的装备升级请求状态。
 *          包括升级所需的材料和等待时间等信息。
 */
struct LegacyWeaponUpgradeRecord {
  std::string character_name{};             ///< 提交升级的角色名称
  LegacyUserItem item{};                    ///< 待升级的物品
  std::uint8_t updc{0};                     ///< 升级所需矿物数量
  std::uint8_t upsc{0};                     ///< 升级所需特殊材料数量
  std::uint8_t upmc{0};                     ///< 升级所需魔法材料数量
  std::uint8_t durapoint{0};                ///< 耐久度点数
  std::uint64_t ready_time_ms{0};           ///< 升级完成时间（毫秒时间戳）
};

// ======================== 商人/商店相关 ========================

/**
 * @brief 商人商品运行时配置
 *
 * @details 定义商品的供应参数：目标库存数量、补货间隔和最后补货时间。
 *          用于控制 NPC 商店的商品供应动态。
 */
struct MerchantProductRuntimeConfig {
  std::int32_t item_id{0};                  ///< 物品 ID
  std::string item_name{};                  ///< 物品名称
  std::int32_t target_count{0};             ///< 目标库存数量
  std::uint64_t refresh_ms{0};              ///< 补货刷新间隔（毫秒）
  std::uint64_t last_refill_ms{0};          ///< 上次补货时间戳（毫秒）
};

/**
 * @brief 商人状态记录
 *
 * @details 存储 NPC 商人的运行时状态，包括商品库存、价格覆盖和装备升级队列。
 *          用于在服务器重启后恢复商人的交易状态。
 */
struct MerchantStateRecord {
  std::string merchant_key{};               ///< 商人唯一标识键
  std::string npc_id{};                     ///< NPC ID
  std::string map_id{};                     ///< 所在地图 ID
  std::vector<LegacyUserItem> goods{};      ///< 商品列表
  std::unordered_map<std::int32_t, std::int32_t> prices{};  ///< 价格映射表（物品ID -> 价格覆盖）
  std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades{};  ///< 待处理的装备升级队列
};

// ======================== 账号相关 ========================

/**
 * @brief 账号记录
 *
 * @details 存储用户账号的完整注册信息，包括登录凭据、个人信息和安全问题。
 *           包含账号状态（封禁标志、登录失败次数）。
 *
 * @note 密码字段存储的是哈希值而非明文。个人信息字段可选，可能为空。
 */
struct AccountRecord {
  std::string account_id{};                 ///< 账号 ID（唯一标识）
  std::string password{};                   ///< 密码哈希值
  std::string display_name{};               ///< 显示名称
  std::string user_name{};                  ///< 用户真实姓名
  std::string ss_no{};                      ///< 证件号码
  std::string phone{};                      ///< 电话号码
  std::string quiz{};                       ///< 安全问题1
  std::string answer{};                     ///< 安全问题1 答案
  std::string email{};                      ///< 电子邮箱
  std::string quiz2{};                      ///< 安全问题2
  std::string answer2{};                    ///< 安全问题2 答案
  std::string birthday{};                   ///< 生日
  std::string mobile_phone{};               ///< 手机号码
  std::string memo1{};                      ///< 备注字段1
  std::string memo2{};                      ///< 备注字段2
  std::int32_t server_index{0};             ///< 首选服务器索引
  std::int32_t passwd_fail{0};              ///< 密码尝试失败次数
  std::int64_t passwd_fail_time_ms{0};      ///< 最后失败时间戳（毫秒，用于冷却）
  bool banned{false};                       ///< 是否已被封禁
};

// ======================== 会话事件 ========================

/**
 * @brief 会话事件类型枚举
 *
 * @details 定义网关层与逻辑层之间传递的会话事件类型。
 *          覆盖了 TCP 连接的生命周期：建立连接、断开连接、数据收发。
 */
enum class SessionEventKind {
  connected,                 ///< 客户端连接建立
  disconnected,              ///< 客户端连接断开
  packet_received,           ///< 接收到客户端数据包
  send_packet,               ///< 向客户端发送数据包
  send_packet_and_close,     ///< 发送数据包后关闭连接
  force_disconnect           ///< 强制断开客户端连接
};

/**
 * @brief 会话事件
 *
 * @details 表示一个与客户端会话相关的网络事件。
 *          由网关服务产生，发送给逻辑处理模块（如 WorldService）消费。
 *          包含事件类型、网关标识、会话 ID、对端地址和数据包等信息。
 */
struct SessionEvent {
  SessionEventKind kind{SessionEventKind::connected};  ///< 事件类型
  std::string gateway{};                               ///< 来源网关名称
  std::uint64_t session_id{0};                         ///< 会话唯一 ID
  std::string peer_address{};                          ///< 客户端对端地址
  LegacyPacket packet{};                               ///< 关联的数据包
  std::string reason{};                                ///< 断开原因（用于 disconnected 事件）
  std::int32_t delay_ms{0};                            ///< 延迟 ms（用于延迟发送）
  std::uint64_t session_seq{0};                        ///< 会话序列号
};

// ======================== 逻辑命令 ========================

/**
 * @brief 逻辑命令类型枚举
 *
 * @details 定义了玩家可以在游戏中执行的所有操作命令。
 *           从基础的移动、攻击到复杂的交易、组队和公会管理。
 *           raw_packet 作为兜底类型，处理未映射的具体协议命令。
 */
enum class LogicCommandKind {
  authenticate,              ///< 身份认证请求
  revoke_authentication,     ///< 撤销身份认证
  enter_world,               ///< 进入游戏世界
  turn,                      ///< 转向
  walk,                      ///< 行走
  run,                       ///< 奔跑
  attack,                    ///< 攻击
  spell,                     ///< 施法
  say,                       ///< 发言/聊天
  click_npc,                 ///< 点击 NPC
  merchant_select,           ///< 选择商人服务
  query_username,            ///< 查询用户名
  query_bag_items,           ///< 查询背包物品
  query_storage_items,       ///< 查询仓库物品
  query_detail_goods,        ///< 查询商品详情
  query_sell_price,          ///< 查询出售价格
  query_repair_cost,         ///< 查询修理费用
  drop_item,                 ///< 丢弃物品
  pickup_item,               ///< 拾取物品
  open_door,                 ///< 开门
  take_on_item,              ///< 穿上装备
  take_off_item,             ///< 卸下装备
  eat_item,                  ///< 使用物品（食物/药水）
  drop_gold,                 ///< 丢弃金币
  revive,                    ///< 复活
  buy_item,                  ///< 购买物品
  sell_item,                 ///< 出售物品
  repair_item,               ///< 修理物品
  storage_item,              ///< 存入仓库
  take_back_storage_item,    ///< 从仓库取出
  trade_try,                 ///< 尝试交易
  trade_cancel,              ///< 取消交易
  trade_add_item,            ///< 交易中添加物品
  trade_remove_item,         ///< 交易中移除物品
  trade_set_gold,            ///< 交易中设置金币
  trade_accept,              ///< 接受交易
  group_create,              ///< 创建组队
  group_add_member,          ///< 添加组队成员
  group_remove_member,       ///< 移除组队成员
  logout,                    ///< 登出
  raw_packet                 ///< 原始数据包（未分类的协议命令）
};

/**
 * @brief 逻辑命令结构体
 *
 * @details 封装从会话事件解析出的玩家操作命令及其参数。
 *           该结构体是逻辑处理模块（WorldService）的主要输入。
 *           包含了命令类型、来源信息、玩家状态以及命令特有的参数。
 *
 * @note 不同命令使用不同的字段组合，未使用的字段保持默认值。
 */
struct LogicCommand {
  LogicCommandKind kind{LogicCommandKind::raw_packet};               ///< 命令类型
  std::string gateway{"game_gateway"};                               ///< 来源网关
  std::uint64_t session_id{0};                                       ///< 会话 ID
  std::uint64_t session_seq{0};                                      ///< 会话序列号
  std::string account_id{};                                          ///< 账号 ID
  std::string character_name{};                                      ///< 角色名称
  std::string map_id{};                                              ///< 当前地图 ID
  std::int32_t x{0};                                                 ///< X 坐标
  std::int32_t y{0};                                                 ///< Y 坐标
  std::uint8_t dir{0};                                               ///< 面向方向
  std::uint64_t target_actor_id{0};                                  ///< 目标 Actor ID
  std::int32_t item_make_index{0};                                   ///< 物品制作索引
  std::int32_t item_slot{-1};                                        ///< 物品槽位（-1 表示未指定）
  std::int32_t amount{0};                                            ///< 数量
  CharacterRecord character{};                                       ///< 角色完整数据
  LegacyDefaultMessage game_message{};                               ///< 原始游戏消息
  std::string text{};                                                ///< 文本内容（聊天/搜索）
  LegacyPacket packet{};                                             ///< 原始数据包
  std::int32_t certification{0};                                     ///< 认证码
  std::int32_t client_version{0};                                    ///< 客户端版本号
  std::int32_t client_checksum{0};                                   ///< 客户端校验和
  bool start_new{false};                                             ///< 是否全新开始
  std::uint64_t timestamp_ms{0};                                     ///< 命令时间戳（毫秒）
};

// ======================== Actor 通信 ========================

/**
 * @brief Actor 邮件类型枚举
 *
 * @details 定义了游戏世界中各实体（Actor）之间传递的邮件类型。
 *           包括生成/销毁通知、移动/攻击状态同步、NPC 交互、
 *           交易/组队操作和延迟效果等。
 *
 *           通过这些邮件，地图服务可以协调多个 Actor 之间的交互行为。
 */
enum class ActorMailKind {
  spawn_player,              ///< 生成玩家实体
  spawn_monster,             ///< 生成怪物实体
  spawn_npc,                 ///< 生成 NPC 实体
  system_notice,             ///< 系统公告
  guild_membership_sync,     ///< 公会成员同步
  group_membership_sync,     ///< 组队成员同步
  turn,                      ///< 转向通知
  move,                      ///< 移动通知
  run,                       ///< 奔跑通知
  attack,                    ///< 攻击通知
  spell,                     ///< 施法通知
  click_npc,                 ///< 点击 NPC
  merchant_select,            ///< 选择商人服务
  query_username,            ///< 查询用户名
  query_bag_items,           ///< 查询背包
  query_storage_items,       ///< 查询仓库
  query_detail_goods,        ///< 查询商品详情
  query_sell_price,          ///< 查询出售价格
  query_repair_cost,         ///< 查询修理费用
  drop_item,                 ///< 丢弃物品
  pickup_item,               ///< 拾取物品
  open_door,                 ///< 开门
  take_on_item,              ///< 穿上装备
  take_off_item,             ///< 卸下装备
  eat_item,                  ///< 使用物品
  drop_gold,                 ///< 丢弃金币
  revive,                    ///< 复活
  buy_item,                  ///< 购买物品
  sell_item,                 ///< 出售物品
  repair_item,               ///< 修理物品
  storage_item,              ///< 存入仓库
  take_back_storage_item,    ///< 取出仓库
  trade_try,                 ///< 尝试交易
  trade_cancel,              ///< 取消交易
  trade_add_item,            ///< 交易添加物品
  trade_remove_item,         ///< 交易移除物品
  trade_set_gold,            ///< 交易设置金币
  trade_accept,              ///< 接受交易
  despawn,                   ///< 销毁实体
  transfer,                  ///< 地图传送
  persistence_loaded,        ///< 持久化数据加载完成
  legacy_delayed_effect,     ///< 延迟效果触发（兼容旧版）
  legacy_magic_lvexp,        ///< 魔法等级经验更新（兼容旧版）
  say,                       ///< 发言
  legacy_chat_delivery       ///< 聊天消息投递（兼容旧版）
};

/**
 * @brief 延迟效果类型枚举
 *
 * @details 定义了游戏中可能发生的各种延迟效果类型。
 *           这些效果在特定时间延迟后触发，如魔法延时生效、持续伤害等。
 */
enum class LegacyDelayedEffectKind {
  none,                      ///< 无效果
  delay_magic,               ///< 延迟魔法释放
  mag_healing,               ///< 魔法治疗效果
  mag_struck,                ///< 魔法打击效果
  monster_struck,            ///< 怪物攻击效果
  open_health,               ///< 生命恢复效果
  make_poison,               ///< 中毒效果
  transparent                ///< 透明/隐形效果
};

/**
 * @brief 聊天消息投递类型枚举
 *
 * @details 定义游戏中不同聊天渠道的消息类型。每种类型决定了消息的
 *          广播范围和显示方式。
 */
enum class LegacyChatDeliveryKind {
  none,                      ///< 无类型
  normal,                    ///< 普通聊天（附近广播）
  whisper,                   ///< 私聊（点对点）
  guild,                     ///< 公会聊天
  group,                     ///< 组队聊天
  shout,                     ///< 大喊（大范围广播）
  shout_direct,              ///< 定向大喊
  system                     ///< 系统消息
};

/**
 * @brief 生成原因枚举
 *
 * @details 定义了实体（Actor）出现在地图中的原因，影响生成动画和状态初始化逻辑。
 */
enum class LegacySpawnReason {
  login,                     ///< 登录进入游戏
  map_transfer,              ///< 地图传送
  space_move,                ///< 空间移动
  space_move_show2           ///< 空间移动展示（变体）
};

/**
 * @brief Buff 状态传输结构
 *
 * @details 用于在 Actor 之间传递 Buff（增益/减益效果）的状态信息。
 *           包括效果类型、过期时间、触发间隔、等级和来源等。
 *           用于跨地图传送时保持 Buff 状态的一致性。
 */
struct LegacyBuffTransferState {
  std::int32_t kind{0};                      ///< Buff 类型 ID
  std::uint64_t expire_tick{0};              ///< 过期时间点
  std::uint64_t next_tick{0};                ///< 下次触发时间点
  std::uint64_t tick_interval{1};            ///< 触发间隔
  std::int32_t level{0};                     ///< Buff 等级
  std::uint64_t source_actor_id{0};          ///< Buff 来源 Actor ID
};

/**
 * @brief Actor 邮件——实体间通信的核心结构
 *
 * @details 这是游戏中最重要的消息类型之一，用于 Actor 之间以及地图服务之间的通信。
 *          包含了极其丰富的字段，覆盖了以下场景：
 *
 * - 实体管理：生成、销毁、传送
 * - 状态同步：位置、方向、移动、攻击
 * - 属性展示：等级、HP/MP、攻击力、防御力
 * - NPC 交互：对话、交易、修理
 * - 怪物相关：AI 参数、掉落物、奴隶控制
 * - 延迟效果：魔法延时、持续伤害
 * - 聊天消息：多渠道消息传递
 *
 * @note 此结构体因历史兼容性原因包含了大量字段。新开发应尽量使用
 *       特定领域的消息结构而非在此结构上继续扩展。
 */
struct ActorMail {
  ActorMailKind kind{ActorMailKind::say};     ///< 邮件类型

  // ---- 空间定位 ----
  std::string map_id{};                       ///< 目标地图 ID
  std::uint64_t actor_id{0};                  ///< 目标 Actor ID
  std::uint64_t session_id{0};                ///< 关联的会话 ID
  std::uint64_t session_seq{0};               ///< 会话序列号
  std::uint64_t target_actor_id{0};           ///< 目标 Actor ID（交互目标）

  // ---- 物品相关 ----
  std::int32_t item_make_index{0};            ///< 物品制作索引
  std::int32_t item_slot{-1};                 ///< 物品槽位
  std::int32_t amount{0};                     ///< 数量

  // ---- NPC/商店 ----
  std::string name{};                         ///< 实体名称
  std::string npc_service{};                  ///< NPC 提供的服务类型
  std::string merchant_key{};                 ///< 商人唯一键
  std::vector<LegacyUserItem> merchant_items{};                    ///< 商人商品列表
  std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades{};        ///< 武器升级队列
  std::vector<MerchantProductRuntimeConfig> merchant_products{};   ///< 商品运行时配置
  std::unordered_map<std::int32_t, std::int32_t> merchant_prices{}; ///< 价格覆盖映射
  std::vector<std::int32_t> legacy_deal_std_modes{};               ///< 标准交易模式（旧版兼容）
  std::vector<NpcDialogSectionConfig> npc_dialog_sections{};       ///< NPC 对话章节配置
  std::int32_t npc_price_rate_percent{100};                        ///< NPC 价格倍率（百分比）

  // ---- 位置信息 ----
  std::int32_t x{0};                          ///< X 坐标
  std::int32_t y{0};                          ///< Y 坐标

  // ---- 战斗属性 ----
  std::int32_t level{1};                      ///< 等级
  std::int32_t current_hp{0};                 ///< 当前生命值
  std::int32_t current_mp{0};                 ///< 当前魔法值
  std::int32_t max_hp{0};                     ///< 最大生命值
  std::int32_t attack_power{0};               ///< 攻击力
  std::int32_t dc_min{0};                     ///< 最小物理攻击
  std::int32_t dc_max{0};                     ///< 最大物理攻击
  std::int32_t defense{0};                    ///< 防御力
  std::int32_t magic_defense{0};              ///< 魔法防御
  std::int32_t mc{0};                         ///< 魔法力
  std::int32_t sc{0};                         ///< 道术力
  std::int32_t exp_reward{0};                 ///< 经验奖励值
  std::int32_t life_attrib{0};                ///< 生命属性

  // ---- 外观/种族 ----
  std::int32_t max_mp{0};                     ///< 最大魔法值
  std::int32_t race_server{0};                ///< 服务器端种族 ID
  std::int32_t race_image{0};                 ///< 外观图像 ID
  std::int32_t appearance{0};                 ///< 外观索引
  std::int32_t cool_eye{0};                   ///< 冷却眼神效果

  // ---- 移动/攻击速度 ----
  std::int32_t speed{0};                      ///< 移动速度
  std::int32_t accuracy{0};                   ///< 准确度
  std::int32_t walk_speed_ms{20};             ///< 行走速度（毫秒/步）
  std::int32_t walk_step{1};                  ///< 行走步长
  std::int32_t walk_wait_ms{0};               ///< 行走等待时间
  std::int32_t attack_speed_ms{100};          ///< 攻击速度（毫秒/次）

  // ---- 怪物 AI ----
  std::int32_t home_x{0};                     ///< 刷新点 X 坐标
  std::int32_t home_y{0};                     ///< 刷新点 Y 坐标
  std::int32_t home_area{0};                  ///< 刷新点活动范围
  MonsterAiProfile monster_ai_profile{MonsterAiProfile::basic};     ///< 怪物 AI 配置
  std::uint64_t monster_search_rate_ms{0};    ///< 怪物搜索频率（毫秒）
  bool legacy_spawn_group{false};             ///< 是否属于刷新组
  std::int32_t monster_drop_gold{0};          ///< 怪物掉落金币量
  std::vector<LegacyUserItem> monster_drop_items{};                  ///< 怪物掉落物品列表

  // ---- 随从/奴隶 ----
  std::uint64_t master_actor_id{0};           ///< 主人 Actor ID
  bool monster_is_slave{false};               ///< 是否是奴隶怪物
  std::int32_t slave_exp{0};                  ///< 奴隶经验
  std::int32_t slave_make_level{0};           ///< 奴隶制作等级
  std::int32_t slave_exp_level{0};            ///< 奴隶经验等级
  std::uint64_t master_royalty_time_ms{0};    ///< 主人忠诚到期时间
  std::uint64_t slave_life_time_ms{0};        ///< 奴隶存活时长

  // ---- 怪物标记 ----
  bool monster_no_item{false};                ///< 是否不掉落物品
  bool monster_tameable{true};                ///< 是否可被驯服
  bool monster_has_target_xy{false};          ///< 是否有目标坐标
  std::int32_t monster_target_x{0};           ///< 目标 X 坐标
  std::int32_t monster_target_y{0};           ///< 目标 Y 坐标

  // ---- 生成/显示 ----
  std::uint8_t legacy_name_color{255};        ///< 名称颜色（旧版兼容）
  LegacySpawnReason legacy_spawn_reason{LegacySpawnReason::login};    ///< 生成原因
  std::uint64_t legacy_group_id{0};           ///< 组 ID（用于刷新组同步）
  std::uint32_t respawn_ms{0};                ///< 重生时间（毫秒）
  std::uint8_t dir{0};                        ///< 方向
  std::uint8_t retry_count{0};                ///< 重试次数

  // ---- 延迟效果 ----
  LegacyDelayedEffectKind delayed_effect_kind{LegacyDelayedEffectKind::none};  ///< 延迟效果类型
  std::int32_t magic_id{0};                   ///< 魔法 ID
  std::int32_t power{0};                      ///< 效果强度
  std::int32_t undead_power{0};               ///< 对亡灵系强度
  std::int32_t range{0};                      ///< 效果范围
  std::int32_t magic_level{0};                ///< 魔法等级
  std::int32_t magic_train{0};                ///< 魔法熟练度
  std::uint32_t magic_lvexp_generation{0};    ///< 魔法等级经验代数

  // ---- 毒效果 ----
  std::int32_t poison_kind{0};                ///< 毒类型
  std::int32_t poison_level{0};               ///< 毒等级
  std::uint64_t duration_ticks{0};            ///< 持续时间（tick数）

  // ---- Buff 状态 ----
  std::vector<LegacyBuffTransferState> legacy_buffs{};               ///< Buff 列表

  // ---- 聊天 ----
  LegacyChatDeliveryKind legacy_chat_kind{LegacyChatDeliveryKind::none};  ///< 聊天类型

  // ---- 数据载荷 ----
  CharacterRecord character{};                ///< 角色完整记录
  LegacyDefaultMessage game_message{};        ///< 原始游戏消息
  std::string payload{};                      ///< 附加数据载荷
};

// ======================== 离线公会操作 ========================

/**
 * @brief 离线公会操作类型枚举
 */
enum class OfflineGuildCharacterOpKind {
  unknown,                   ///< 未知/非法操作
  approve,                   ///< 批准加入公会
  kick,                      ///< 踢出公会
  transfer,                  ///< 转让公会会长
  title                      ///< 设置公会头衔
};

/**
 * @brief 离线公会操作
 *
 * @details 用于处理玩家离线时的公会管理操作。
 *           当有公会管理权限的玩家对其他成员执行操作时，
 *           如果目标玩家离线，操作请求将被序列化存储，
 *           在目标玩家下次登录时处理。
 */
struct OfflineGuildCharacterOp {
  OfflineGuildCharacterOpKind kind{OfflineGuildCharacterOpKind::unknown};  ///< 操作类型
  std::string map_id{};                    ///< 发起者所在地图 ID
  std::uint64_t initiator_actor_id{0};     ///< 发起者 Actor ID
  std::string guild_name{};                ///< 公会名称
  std::string target_name{};               ///< 目标角色名称
  std::string title_name{};                ///< 头衔名称（用于 title 操作）
  std::string target_map_id{};             ///< 目标地图 ID
};

// ======================== 离线公会操作序列化 ========================

/**
 * @brief 获取离线公会操作类型的字符串表示
 * @param kind 操作类型枚举值
 * @return 对应的字符串标识
 */
inline std::string offline_guild_character_op_name(OfflineGuildCharacterOpKind kind) {
  switch (kind) {
    case OfflineGuildCharacterOpKind::approve:
      return "approve";
    case OfflineGuildCharacterOpKind::kick:
      return "kick";
    case OfflineGuildCharacterOpKind::transfer:
      return "transfer";
    case OfflineGuildCharacterOpKind::title:
      return "title";
    default:
      return "unknown";
  }
}

/**
 * @brief 将字符串解析为离线公会操作类型
 * @param text 字符串标识
 * @return 对应的操作类型枚举值，无法识别时返回 unknown
 */
inline OfflineGuildCharacterOpKind parse_offline_guild_character_op_kind(std::string_view text) {
  if (text == "approve") {
    return OfflineGuildCharacterOpKind::approve;
  }
  if (text == "kick") {
    return OfflineGuildCharacterOpKind::kick;
  }
  if (text == "transfer") {
    return OfflineGuildCharacterOpKind::transfer;
  }
  if (text == "title") {
    return OfflineGuildCharacterOpKind::title;
  }
  return OfflineGuildCharacterOpKind::unknown;
}

/**
 * @brief 将离线公会操作序列化为字符串
 * @param operation 离线公会操作
 * @return 序列化后的字符串（每行一个字段，以换行符分隔）
 *
 * @details 序列化格式：
 *          第1行：魔法字符串 "guild_offline"
 *          第2行：map_id
 *          第3行：initiator_actor_id
 *          第4行：操作类型名称
 *          第5行：guild_name
 *          第6行：target_name
 *          第7行（可选）：title_name
 *          第8行（可选）：target_map_id
 *
 * @see decode_offline_guild_character_op() 对应的反序列化函数
 */
inline std::string encode_offline_guild_character_op(const OfflineGuildCharacterOp& operation) {
  std::string encoded = "guild_offline\n";
  encoded += operation.map_id;
  encoded += '\n';
  encoded += std::to_string(operation.initiator_actor_id);
  encoded += '\n';
  encoded += offline_guild_character_op_name(operation.kind);
  encoded += '\n';
  encoded += operation.guild_name;
  encoded += '\n';
  encoded += operation.target_name;
  encoded += '\n';
  encoded += operation.title_name;
  encoded += '\n';
  encoded += operation.target_map_id;
  return encoded;
}

/**
 * @brief 将序列化字符串反序列化为离线公会操作
 * @param encoded 序列化的字符串（由 encode_offline_guild_character_op 生成）
 * @return 如果解析成功，返回包含操作的 std::optional；否则返回 std::nullopt
 *
 * @details 解析验证：
 * 1. 检查魔法字符串 "guild_offline" 是否正确
 * 2. 检查字段数量（至少6个）
 * 3. 验证 initiator_actor_id 是否能解析为数字
 * 4. 验证操作类型是否合法
 * 5. 验证关键字段（map_id, initiator_actor_id, guild_name, target_name）非空
 */
inline std::optional<OfflineGuildCharacterOp> decode_offline_guild_character_op(
    std::string_view encoded) {
  // 按换行符分割字段
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= encoded.size()) {
    const auto end = encoded.find('\n', start);
    if (end == std::string_view::npos) {
      fields.push_back(encoded.substr(start));
      break;
    }
    fields.push_back(encoded.substr(start, end - start));
    start = end + 1;
  }

  // 验证魔法字符串和最少字段数
  if (fields.size() < 6 || fields[0] != "guild_offline") {
    return std::nullopt;
  }

  // 解析发起者 Actor ID
  std::uint64_t initiator_actor_id = 0;
  try {
    initiator_actor_id = static_cast<std::uint64_t>(std::stoull(std::string(fields[2])));
  } catch (...) {
    return std::nullopt;
  }

  OfflineGuildCharacterOp operation;
  operation.map_id = std::string(fields[1]);
  operation.initiator_actor_id = initiator_actor_id;
  operation.kind = parse_offline_guild_character_op_kind(fields[3]);
  operation.guild_name = std::string(fields[4]);
  operation.target_name = std::string(fields[5]);
  if (fields.size() >= 7) {
    operation.title_name = std::string(fields[6]);
  }
  if (fields.size() >= 8) {
    operation.target_map_id = std::string(fields[7]);
  }

  // 验证必要字段是否有效
  if (operation.kind == OfflineGuildCharacterOpKind::unknown || operation.map_id.empty() ||
      operation.initiator_actor_id == 0 || operation.guild_name.empty() ||
      operation.target_name.empty()) {
    return std::nullopt;
  }
  return operation;
}

// ======================== 持久化请求与响应 ========================

/**
 * @brief 持久化请求类型枚举
 *
 * @details 定义了所有可以对持久化层（数据库）执行的操作类型。
 *           覆盖了数据模式管理、账号操作、角色操作、
 *           商人状态管理和审计记录等维度。
 */
enum class PersistRequestKind {
  ensure_schema,              ///< 确保数据库模式已创建
  load_account,               ///< 加载账号数据
  authenticate_account,       ///< 验证账号身份
  load_castle_dialog_context, ///< 加载城堡对话框上下文
  load_guild_castle_snapshot, ///< 加载公会城堡快照
  save_guild_payload,         ///< 保存公会数据载荷
  save_guild_state,           ///< 保存公会状态
  delete_guild,               ///< 删除公会
  save_castle_state,          ///< 保存城堡状态
  create_account,             ///< 创建新账号
  update_account,             ///< 更新账号信息
  change_password,            ///< 修改密码
  load_character,             ///< 加载角色数据
  load_character_by_name,     ///< 按名称加载角色
  list_characters,            ///< 列出角色的所有角色
  create_character,           ///< 创建新角色
  delete_character,           ///< 删除角色
  save_character,             ///< 保存角色数据
  load_merchant_states,       ///< 加载商人状态
  save_merchant_state,        ///< 保存商人状态
  record_audit,               ///< 记录审计日志
  seed_runtime                ///< 初始化运行时种子数据
};

/**
 * @brief 持久化请求
 *
 * @details 发送给 PersistenceService 的数据操作请求。
 *          包含请求类型、回复地址和各种操作所需的参数。
 *          不同的请求类型使用不同的字段组合。
 *
 * @note reply_to 字段用于指定响应消息的目标端点名称，
 *       使 PersistenceService 可以将处理结果回传给请求者。
 */
struct PersistRequest {
  PersistRequestKind kind{PersistRequestKind::ensure_schema};  ///< 请求类型
  std::string reply_to{};                     ///< 响应目标端点名称
  std::string account_id{};                   ///< 账号 ID
  std::string guild_name{};                   ///< 公会名称
  std::string castle_name{};                  ///< 城堡名称
  std::string password{};                     ///< 密码
  std::string new_password{};                 ///< 新密码
  std::string character_name{};               ///< 角色名称
  std::string payload_json{};                 ///< JSON 数据载荷
  AccountRecord account{};                    ///< 账号记录
  CharacterRecord character{};                ///< 角色记录
  MerchantStateRecord merchant_state{};       ///< 商人状态记录
  GuildState guild_state{};                   ///< 公会状态
  GuildCastleSnapshot guild_castle_snapshot{}; ///< 城堡快照
  std::string text{};                         ///< 文本内容
  std::string request_id{};                   ///< 请求唯一标识（用于去重/追踪）
  std::int64_t timestamp_ms{0};               ///< 请求时间戳（毫秒）
};

/**
 * @brief 持久化结果类型枚举
 */
enum class PersistResultKind {
  schema_ready,                ///< 数据库模式已就绪
  account_loaded,              ///< 账号数据已加载
  account_authenticated,       ///< 账号已通过认证
  castle_dialog_context_loaded, ///< 城堡对话框上下文已加载
  guild_castle_snapshot_loaded, ///< 公会城堡快照已加载
  account_created,             ///< 账号已创建
  account_updated,             ///< 账号已更新
  password_changed,            ///< 密码已修改
  character_loaded,            ///< 角色数据已加载
  characters_listed,           ///< 角色列表已返回
  character_created,           ///< 角色已创建
  character_deleted,           ///< 角色已删除
  character_saved,             ///< 角色已保存
  merchant_states_loaded,      ///< 商人状态已加载
  merchant_state_saved,        ///< 商人状态已保存
  audit_recorded,              ///< 审计记录已写入
  seeded,                      ///< 种子数据已初始化
  error                        ///< 操作发生错误
};

/**
 * @brief 持久化结果
 *
 * @details PersistenceService 处理请求后返回的结果。
 *          包含操作类型、回复目标地址、返回数据和错误信息。
 *          请求者通过 reply_to 字段匹配请求与响应。
 */
struct PersistResult {
  PersistResultKind kind{PersistResultKind::schema_ready};  ///< 结果类型
  std::string reply_to{};                    ///< 响应目标端点名称
  std::string account_id{};                  ///< 账号 ID
  AccountRecord account{};                   ///< 加载的账号记录
  CastleDialogContext castle_dialog_context{};  ///< 城堡对话框上下文
  GuildCastleSnapshot guild_castle_snapshot{};  ///< 公会城堡快照
  std::string character_name{};              ///< 角色名称
  CharacterRecord character{};               ///< 加载的角色记录
  std::vector<MerchantStateRecord> merchant_states{};  ///< 商人状态列表
  std::string error{};                       ///< 错误描述（仅 kind==error 时有效）
  std::vector<CharacterRecord> characters{};  ///< 角色列表（用于 list_characters）
  std::string request_id{};                  ///< 对应的请求 ID
  std::int32_t result_code{0};               ///< 结果代码（特定于各操作类型的返回码）
};

// ======================== 审计事件 ========================

/**
 * @brief 审计事件
 *
 * @details 记录系统中发生的需要审计的重要操作。
 *           审计事件通过消息总线发送给专门的审计处理器（如 PersistenceService），
 *           由后台线程异步写入持久化存储。
 */
struct AuditEvent {
  std::string category{};      ///< 审计分类（如 "login", "trade", "guild" 等）
  std::string message{};       ///< 审计消息内容
  std::string session_key{};   ///< 关联的会话键（用于溯源）
};

/**
 * @brief 跨服广播范围
 *
 * @details 目前仅承载 Delphi `@!` GM 跨服公告语义。
 */
enum class InterserverBroadcastScope {
  sysop_global_interserver  ///< GM 跨服全局公告
};

/**
 * @brief 跨服广播消息
 *
 * @details 既可作为 RuntimeDispatch 的输出，也可通过本地总线传入
 *          WorldService 做本服 fanout。
 *          `local_only=true` 表示消息来自远端服，只做本地 fanout，
 *          不再回传到其他 peer，避免环路。
 */
struct InterserverBroadcast {
  std::string message_id{};                      ///< 广播唯一 ID（用于去重/追踪）
  InterserverBroadcastScope scope{
      InterserverBroadcastScope::sysop_global_interserver};  ///< 广播范围
  std::string text{};                            ///< 广播正文
  std::string source_server_tag{};               ///< 源服务器标识
  bool local_only{false};                        ///< 是否仅在本机分发
};

// ======================== 统一消息类型 ========================

/**
 * @brief 总线消息类型——所有模块间消息的统一变体类型
 *
 * @details 使用 std::variant 实现类型安全的消息联合体。
 *          消息总线上传递的所有消息都必须是此类型。
 *          通过 std::visit 模式匹配可以安全地处理每种消息类型。
 *
 * 包含的消息类型：
 * - SessionEvent：网络会话事件
 * - LogicCommand：玩家逻辑命令
 * - ActorMail：Actor 间消息
 * - PersistRequest：持久化请求
 * - PersistResult：持久化响应
 * - AuditEvent：审计事件
 *
 * @note 新增消息类型时，需要在此变体类型中添加对应类型。
 *       同时需要更新所有使用 std::visit 处理消息的模块。
 */
using BusMessage =
    std::variant<SessionEvent, LogicCommand, ActorMail, PersistRequest, PersistResult, AuditEvent,
                 InterserverBroadcast>;

}  // namespace mir2
