// ============================================================
// Mir2 新版客户端协议定义（client_v1）
// 职责：定义客户端与服务端之间的二进制协议，包括消息枚举、
//       所有消息结构的序列化/反序列化、帧封装和 TCP 流解析。
// 协议格式：小端编码的二进制流，4 字节长度前缀 + 2 字节消息 ID
//           + 2 字节标志 + 4 字节序号 + 可选帧元数据 + 可变长度载荷
// ============================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mir2::client_v1 {

constexpr std::uint32_t kProtocolVersion = 6;

constexpr std::uint16_t kFrameFlagLegacyBundle = 0x0001U;

enum class LegacyBundleMode : std::uint8_t {
  immediate = 0,
  actor_queue = 1
};

struct LegacyBundleMeta {
  std::uint64_t bundle_id{0};
  std::uint16_t bundle_index{0};
  std::uint16_t bundle_count{0};
  std::uint16_t legacy_ident{0};
  LegacyBundleMode bundle_mode{LegacyBundleMode::immediate};
};

/// 消息 ID 枚举：每个消息类型对应唯一的 16 位 ID
/// 分组规则：1-99 会话，100-199 登录/账号，200-299 角色选择，
///           300-399 世界/角色同步，400-499 行动/背包，
///           500-599 聊天/通知，600-699 连接维护
enum class MessageId : std::uint16_t {
  client_hello = 1,
  login_request = 100,
  login_result = 101,
  create_account_request = 102,
  create_account_result = 103,
  change_password_request = 104,
  change_password_result = 105,
  update_account_request = 106,
  update_account_result = 107,
  need_update_account = 108,
  server_list = 110,
  select_server_request = 111,
  select_server_result = 112,
  character_list_request = 200,
  character_list = 201,
  create_character_request = 202,
  create_character_result = 203,
  delete_character_request = 204,
  delete_character_result = 205,
  select_character_request = 206,
  select_character_result = 207,
  enter_world_request = 300,
  enter_world_result = 301,
  world_snapshot = 302,
  actor_state_delta = 303,
  login_notice = 304,
  login_notice_ok = 305,
  actor_upsert = 306,
  actor_action = 307,
  actor_vitals = 308,
  actor_death = 309,
  magic_list = 310,
  self_ability = 311,
  self_ability_detail = 312,
  mini_map_request = 313,
  mini_map_data = 314,
  actor_magic_fire = 315,
  actor_remove = 316,
  actor_magic_fire_fail = 317,
  world_clear_objects = 318,
  map_change = 319,
  map_door_state = 320,
  map_entered = 321,
  actor_identity_update = 322,
  map_description = 323,
  move_intent = 400,
  action_intent = 401,
  spell_intent = 402,
  action_ack = 403,
  pickup_intent = 404,
  use_item_intent = 405,
  ground_item_add = 406,
  ground_item_remove = 407,
  use_item_result = 408,
  bag_snapshot = 409,
  inventory_add = 410,
  inventory_update = 411,
  inventory_remove = 412,
  inventory_clear_range = 413,
  equipment_snapshot = 414,
  equip_item_request = 415,
  unequip_item_request = 416,
  drop_item_request = 417,
  revive_request = 418,
  drop_gold_request = 419,
  durability_change = 420,
  magic_key_change_request = 430,
  chat_send = 500,
  sys_message = 501,
  notice = 502,
  chat_line = 503,
  actor_say = 504,
  npc_click_request = 520,
  npc_dialog = 521,
  npc_dialog_select_request = 522,
  npc_dialog_close = 523,
  merchant_goods_list = 524,
  merchant_buy_request = 525,
  merchant_sell_request = 526,
  merchant_sell_price_request = 527,
  merchant_price_result = 528,
  merchant_repair_price_request = 529,
  merchant_repair_request = 530,
  merchant_repair_price_result = 531,
  storage_list = 532,
  storage_deposit_request = 533,
  storage_withdraw_request = 534,
  group_mode_request = 535,
  group_create_request = 536,
  group_add_member_request = 537,
  group_remove_member_request = 538,
  group_state = 539,
  trade_try_request = 540,
  trade_cancel_request = 541,
  trade_add_item_request = 542,
  trade_remove_item_request = 543,
  trade_set_gold_request = 544,
  trade_accept_request = 545,
  trade_state = 546,
  guild_open_request = 547,
  guild_home_request = 548,
  guild_member_list_request = 549,
  guild_add_member_request = 550,
  guild_remove_member_request = 551,
  guild_update_notice_request = 552,
  guild_update_grade_request = 553,
  guild_state = 554,
  ping = 600,
  pong = 601,
  disconnect_reason = 602
};

/// 角色类型：区分玩家、怪物和 NPC
enum class ActorType : std::uint8_t {
  player = 1,
  monster = 2,
  npc = 3
};

/// 移动模式：行走或跑步
enum class MoveMode : std::uint8_t {
  walk = 0,
  run = 1
};

/// 世界动作类型：客户端发送给服务端的动作意图
enum class WorldActionKind : std::uint8_t {
  turn = 0,
  walk = 1,
  run = 2,
  attack = 3
};

/// 角色动作类型：服务端同步给客户端的角色动作
enum class ActorActionKind : std::uint8_t {
  turn = 0,
  walk = 1,
  run = 2,
  hit = 3,
  spell = 4,
  struck = 5,
  rush = 6,
  rush_kung = 7,
  backstep = 8,
  knockback = 9
};

// ====================================================================
// 协议消息结构体
// 每个结构体对应一个 MessageId，包含该消息的所有字段
// ====================================================================

/// 客户端问候：建立连接后的第一个消息，包含版本协商信息
struct ClientHello {
  std::uint32_t protocol_version{kProtocolVersion};
  std::uint32_t client_build{0};
  std::uint32_t resource_revision{0};
  std::uint32_t capabilities{0};
};

/// 登录请求：账号和密码
struct LoginRequest {
  std::string account_id{};
  std::string password{};
};

/// 登录结果：包含登录是否成功以及服务器列表或错误信息
struct LoginResult {
  bool success{false};
  std::int32_t code{0};
  std::string account_id{};
  std::string display_name{};
  std::string error_message{};
};

/// 创建账号请求：包含完整的个人资料
struct CreateAccountRequest {
  std::string account_id{};
  std::string password{};
  struct AccountProfile {
    std::string display_name{};
    std::string user_name{};
    std::string ss_no{};
    std::string birthday{};
    std::string quiz{};
    std::string answer{};
    std::string quiz2{};
    std::string answer2{};
    std::string phone{};
    std::string mobile_phone{};
    std::string email{};
    std::string memo1{};
    std::string memo2{};
  } profile{};
};

using AccountProfile = CreateAccountRequest::AccountProfile;

/// 创建账号结果
struct CreateAccountResult {
  bool success{false};
  std::int32_t code{0};
  std::string error_message{};
};

/// 修改密码请求
struct ChangePasswordRequest {
  std::string account_id{};
  std::string password{};
  std::string new_password{};
};

/// 修改密码结果
struct ChangePasswordResult {
  bool success{false};
  std::int32_t code{0};
  std::string error_message{};
};

/// 更新账号请求（完善个人资料）
struct UpdateAccountRequest {
  std::string account_id{};
  std::string password{};
  AccountProfile profile{};
};

/// 更新账号结果
struct UpdateAccountResult {
  bool success{false};
  std::int32_t code{0};
  std::string error_message{};
};

/// 服务端通知客户端需要补充个人资料
struct NeedUpdateAccount {
  std::string account_id{};
  AccountProfile profile{};
  std::string message{};
};

/// 服务器列表条目
struct ServerEntry {
  std::string name{};
  std::string address{};
  std::uint16_t port{0};
};

/// 服务器列表：登录成功后由服务端返回
struct ServerList {
  std::vector<ServerEntry> servers{};
};

/// 选择服务器请求
struct SelectServerRequest {
  std::string name{};
};

/// 选择服务器结果：包含网关地址和 lobby token
struct SelectServerResult {
  bool success{false};
  std::string name{};
  std::string address{};
  std::uint16_t port{0};
  std::string lobby_token{};
  std::string error_message{};
};

/// 角色概要信息
struct CharacterSummary {
  std::string name{};
  std::uint16_t level{1};
  std::uint8_t job{0};
  std::uint8_t sex{0};
  std::uint8_t hair{0};
  std::string map_id{};
};

/// 角色列表请求（携带 lobby token）
struct CharacterListRequest {
  std::string lobby_token{};
};

/// 角色列表：服务端返回该账号下的所有角色
struct CharacterList {
  std::vector<CharacterSummary> characters{};
  std::string selected_name{};
};

/// 创建角色请求
struct CreateCharacterRequest {
  std::string name{};
  std::uint8_t job{0};
  std::uint8_t sex{0};
  std::uint8_t hair{0};
};

/// 创建角色结果
struct CreateCharacterResult {
  bool success{false};
  std::int32_t code{0};
  std::string error_message{};
  CharacterSummary character{};
};

/// 删除角色请求
struct DeleteCharacterRequest {
  std::string name{};
};

/// 删除角色结果
struct DeleteCharacterResult {
  bool success{false};
  std::int32_t code{0};
  std::string error_message{};
  std::string deleted_name{};
};

/// 选择角色请求（进入游戏前的最后一步）
struct SelectCharacterRequest {
  std::string name{};
};

/// 选择角色结果：包含网关地址和 enter_world_token
struct SelectCharacterResult {
  bool success{false};
  std::string character_name{};
  std::string enter_world_token{};
  std::string address{};
  std::uint16_t port{0};
  std::string error_message{};
};

/// 进入世界请求：携带 token 和版本信息
struct EnterWorldRequest {
  std::string token{};
  std::uint32_t client_build{0};
  std::uint32_t resource_revision{0};
};

/// 进入世界结果：包含角色初始位置和地图信息
struct EnterWorldResult {
  bool success{false};
  std::uint64_t self_actor_id{0};
  std::string character_name{};
  std::string map_id{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::string error_message{};
};

/// 世界中的角色/怪物/NPC 概要信息
struct WorldActor {
  std::uint64_t actor_id{0};
  std::string name{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::int32_t feature{0};
  std::int32_t status{0};
  ActorType actor_type{ActorType::player};
  std::uint16_t level{1};
  std::uint8_t light{0};
};

/// 世界快照：进入世界后服务端发送的完整场景状态
struct WorldSnapshot {
  std::string map_id{};
  std::int32_t width{800};
  std::int32_t height{600};
  std::uint64_t self_actor_id{0};
  std::vector<WorldActor> actors{};
};

/// 清空当前地图对象：对应 Delphi SM_CLEAROBJECTS，切图时先清 actor/item 列表。
struct WorldClearObjects {};

/// 地图切换通知：对应 Delphi SM_CHANGEMAP，完整 WorldSnapshot 会随后到达。
struct MapChange {
  std::string map_id{};
};

/// 地图门开关状态：对应 Delphi SM_OPENDOOR_OK / SM_CLOSEDOOR。
struct MapDoorState {
  std::int32_t x{0};
  std::int32_t y{0};
  bool open{false};
};

struct MapEntered {
  std::string map_id{};
  std::uint64_t self_actor_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
};

constexpr std::uint8_t kActorIdentityName = 1U;
constexpr std::uint8_t kActorIdentityNameColor = 2U;
constexpr std::uint8_t kActorIdentityFeature = 4U;
constexpr std::uint8_t kActorIdentityStatus = 8U;
constexpr std::uint8_t kActorIdentityLight = 16U;

struct ActorIdentityUpdate {
  std::uint64_t actor_id{0};
  std::uint8_t mask{0};
  std::string name{};
  std::uint32_t name_color{0xFFFFFFFFU};
  std::int32_t feature{0};
  std::int32_t status{0};
  std::uint8_t light{0};
};

struct MapDescription {
  std::string title{};
};

/// 主角能力摘要：驱动底部 HUD 的等级、经验、负重、金币和饥饿图标
struct SelfAbility {
  std::uint16_t level{1};
  std::uint8_t job{0};
  std::uint32_t exp{0};
  std::uint32_t max_exp{0};
  std::uint16_t weight{0};
  std::uint16_t max_weight{0};
  std::int32_t gold{0};
  std::uint8_t hunger_state{0};
};

/// 主角完整能力详情：状态窗口分页读取
struct SelfAbilityDetail {
  std::uint16_t level{1};
  std::uint8_t job{0};
  std::uint8_t sex{0};
  std::uint8_t hair{0};
  std::uint16_t hp{0};
  std::uint16_t max_hp{0};
  std::uint16_t mp{0};
  std::uint16_t max_mp{0};
  std::uint16_t ac{0};
  std::uint16_t mac{0};
  std::uint16_t dc{0};
  std::uint16_t mc{0};
  std::uint16_t sc{0};
  std::uint32_t exp{0};
  std::uint32_t max_exp{0};
  std::uint16_t weight{0};
  std::uint16_t max_weight{0};
  std::uint16_t wear_weight{0};
  std::uint16_t max_wear_weight{0};
  std::uint16_t hand_weight{0};
  std::uint16_t max_hand_weight{0};
  std::int32_t hit{0};
  std::int32_t speed{0};
  std::int32_t anti_magic{0};
  std::int32_t anti_poison{0};
  std::int32_t poison_recover{0};
  std::int32_t health_recover{0};
  std::int32_t spell_recover{0};
  std::string guild_name{};
  std::string guild_rank_name{};
  std::uint32_t name_color{0xFFFFFFFFU};
};

/// 小地图请求：请求当前或指定地图的缩略图
struct MiniMapRequest {
  std::string map_id{};
};

/// 小地图数据：固定由服务端下发缩略像素，像素值 0=阻挡/暗，1=可走
struct MiniMapData {
  bool success{false};
  std::string map_id{};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::vector<std::uint8_t> pixels{};
  std::string error_message{};
};

/// 角色状态增量：服务端同步角色位置变化
struct ActorStateDelta {
  std::uint64_t actor_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
};

/// 新增/更新角色：服务端通知有新角色进入或角色信息更新
struct ActorUpsert {
  WorldActor actor{};
};

/// 删除角色：服务端通知角色离开九宫格视野或被清理
struct ActorRemove {
  std::uint64_t actor_id{0};
  std::uint16_t legacy_ident{0};
};

/// 角色动作：服务端同步角色的动作事件（攻击、施法等）
struct ActorAction {
  std::uint64_t actor_id{0};
  ActorActionKind kind{ActorActionKind::turn};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint64_t target_actor_id{0};
  std::int32_t value{0};
  std::uint16_t legacy_ident{0};
  std::uint16_t magic_id{0};
  bool magic{false};
  std::uint16_t magic_effect{0};
};

struct ActorMagicFire {
  std::uint64_t actor_id{0};
  std::uint64_t target_actor_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t effect_type{0};
  std::uint8_t effect{0};
  std::uint16_t legacy_ident{0};
};

struct ActorMagicFireFail {
  std::uint64_t actor_id{0};
  std::uint16_t legacy_ident{0};
};

/// 角色生命/魔法值：服务端同步角色的 HP/MP 变化
struct ActorVitals {
  std::uint64_t actor_id{0};
  std::int32_t hp{-1};
  std::int32_t max_hp{-1};
  std::int32_t mp{-1};
  std::int32_t max_mp{-1};
  std::int32_t damage{0};
  std::uint64_t source_actor_id{0};
  bool magic{false};
  std::uint16_t legacy_ident{0};
  std::uint16_t actor_level{0};
  std::int32_t health_gauge_visible{-1};  ///< -1=不变, 0=关闭, 1=打开
};

/// 角色死亡事件
struct ActorDeath {
  std::uint64_t actor_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint16_t legacy_ident{0};
};

/// 魔法条目：角色已学习的魔法信息
struct MagicEntry {
  std::uint16_t magic_id{0};
  std::uint8_t key{0};
  std::uint8_t level{0};
  std::int32_t train{0};
  std::int32_t delay_ms{0};
  std::string name{};
  std::int32_t effect{0};
  std::int32_t max_train{0};
  std::int32_t effect_type{0};
  std::int32_t spell{0};
  std::int32_t def_spell{0};
  std::int32_t max_train_level{0};
};

/// 魔法列表：服务端同步角色的所有魔法
struct MagicList {
  std::vector<MagicEntry> magics{};
};

/// 登录公告：进入游戏前服务端显示的公告
struct LoginNotice {
  std::string title{};
  std::string text{};
};

/// 客户端确认已阅读登录公告
struct LoginNoticeOk {};

/// 移动意图：客户端请求移动
struct MoveIntent {
  std::int32_t x{0};
  std::int32_t y{0};
  MoveMode mode{MoveMode::walk};
};

/// 动作意图：客户端请求执行动作（转身、行走、跑步、攻击）
struct ActionIntent {
  WorldActionKind kind{WorldActionKind::walk};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint64_t target_actor_id{0};
  std::uint16_t legacy_ident{0};
};

/// 施法意图：客户端请求释放魔法
struct SpellIntent {
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint64_t target_actor_id{0};
  std::uint16_t magic_id{0};
};

/// 动作确认：服务端对客户端动作请求的回应
struct ActionAck {
  bool ok{false};
  std::uint32_t server_time_ms{0};
};

/// 拾取意图：客户端请求拾取地上的物品
struct PickupIntent {
  std::int32_t x{0};
  std::int32_t y{0};
};

/// 使用物品意图：客户端请求使用消耗品
struct UseItemIntent {
  std::int32_t item_make_index{0};
  std::int32_t item_slot{-1};
  std::string name{};
};

/// 物品状态：单个物品的完整信息
struct ItemState {
  std::string name{};
  std::int32_t make_index{0};
  std::int32_t looks{0};
  std::uint8_t std_mode{0};
  std::uint16_t dura{0};
  std::uint16_t dura_max{0};

  [[nodiscard]] bool empty() const { return name.empty(); }
};

/// 背包槽位状态：槽位编号 + 物品信息
struct ItemSlotState {
  std::int32_t slot{-1};
  ItemState item{};
};

/// 背包快照：服务端同步完整的背包内容
struct BagSnapshot {
  std::vector<ItemSlotState> items{};
};

/// 背包新增物品
struct InventoryAdd {
  ItemSlotState entry{};
};

/// 背包更新物品
struct InventoryUpdate {
  ItemSlotState entry{};
};

/// 背包移除物品
struct InventoryRemove {
  std::int32_t slot{-1};
};

/// 背包清空范围：移除指定槽位区间内的所有物品
struct InventoryClearRange {
  std::int32_t first_slot{0};
  std::int32_t last_slot{0};
};

/// 装备快照：服务端同步所有已装备的物品
struct EquipmentSnapshot {
  std::vector<ItemSlotState> items{};
};

/// 装备物品请求
struct EquipItemRequest {
  std::int32_t equipment_slot{-1};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 卸下装备请求
struct UnequipItemRequest {
  std::int32_t equipment_slot{-1};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 丢弃物品请求
struct DropItemRequest {
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 复活请求：死亡后回安全/出生点复活
struct ReviveRequest {};

/// 丢弃金币请求：amount 对应旧端 CM_DROPGOLD 的 Recog
struct DropGoldRequest {
  std::int32_t amount{0};
};

/// 装备/背包耐久变化：按 make_index 更新本地镜像
struct DurabilityChange {
  std::int32_t item_make_index{0};
  std::int32_t dura{0};
  std::int32_t dura_max{0};
};

/// 魔法快捷键变更：key 为 0 或 '1'..'8'
struct MagicKeyChangeRequest {
  std::uint16_t magic_id{0};
  std::uint8_t key{0};
};

/// 地上物品状态
struct GroundItemState {
  std::uint64_t object_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t looks{0};
  std::string name{};
};

/// 地上新增物品
struct GroundItemAdd {
  GroundItemState item{};
};

/// 地上移除物品
struct GroundItemRemove {
  std::uint64_t object_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
};

/// 使用物品结果
struct UseItemResult {
  bool ok{false};
};

/// 聊天发送：客户端发送聊天消息
struct ChatSend {
  std::string text{};
};

/// 聊天板行：服务端广播或系统写入底部聊天板
struct ChatLine {
  std::string text{};
  std::uint32_t fore_color{0xFFFFFFFFU};
  std::uint32_t back_color{0x00000000U};
};

/// 角色头顶说话：同时写入聊天板并在角色头顶显示短时间文字
struct ActorSay {
  std::uint64_t actor_id{0};
  std::string text{};
  std::uint32_t fore_color{0xFFFFFFFFU};
  std::uint32_t back_color{0x00000000U};
};

/// 点击 NPC：客户端请求打开 NPC/商人对话
struct NpcClickRequest {
  std::uint64_t actor_id{0};
};

/// NPC 对话：对应 Delphi SM_MERCHANTSAY
struct NpcDialog {
  std::uint64_t merchant_id{0};
  std::int32_t face{0};
  std::string text{};
};

/// NPC 对话链接选择：对应 Delphi CM_MERCHANTDLGSELECT
struct NpcDialogSelectRequest {
  std::uint64_t merchant_id{0};
  std::string selection{};
};

/// NPC 对话关闭：对应 Delphi SM_MERCHANTDLGCLOSE
struct NpcDialogClose {
  std::uint64_t merchant_id{0};
};

/// 商店商品条目
struct MerchantGoodsItem {
  std::int32_t server_index{0};
  std::string name{};
  std::int32_t looks{0};
  std::uint8_t std_mode{0};
  std::int32_t price{0};
};

/// 商店商品列表
struct MerchantGoodsList {
  std::uint64_t merchant_id{0};
  std::vector<MerchantGoodsItem> items{};
};

/// 购买商店物品
struct MerchantBuyRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_server_index{0};
  std::string name{};
};

/// 出售背包物品
struct MerchantSellRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 查询出售价格
struct MerchantSellPriceRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 商人价格结果：sell=true 表示出售询价
struct MerchantPriceResult {
  std::uint64_t merchant_id{0};
  std::int32_t item_index{0};
  std::int32_t price{0};
  bool sell{false};
  bool ok{false};
};

/// 查询修理价格
struct MerchantRepairPriceRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 确认修理背包物品
struct MerchantRepairRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 修理价格结果
struct MerchantRepairPriceResult {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::int32_t price{0};
  bool ok{false};
};

/// 仓库物品列表
struct StorageList {
  std::uint64_t merchant_id{0};
  std::vector<ItemState> items{};
};

/// 存入仓库
struct StorageDepositRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 从仓库取回
struct StorageWithdrawRequest {
  std::uint64_t merchant_id{0};
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 允许/禁止组队
struct GroupModeRequest {
  bool allow{false};
};

/// 创建队伍
struct GroupCreateRequest {
  std::string target_name{};
};

/// 添加队员
struct GroupAddMemberRequest {
  std::string target_name{};
};

/// 移除队员
struct GroupRemoveMemberRequest {
  std::string target_name{};
};

/// 组队窗口状态
struct GroupState {
  bool visible{false};
  bool allow_group{false};
  std::vector<std::string> members{};
};

/// 尝试交易
struct TradeTryRequest {
  std::string target_name{};
};

/// 取消交易
struct TradeCancelRequest {};

/// 放入交易物品
struct TradeAddItemRequest {
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 移出交易物品
struct TradeRemoveItemRequest {
  std::int32_t item_make_index{0};
  std::string name{};
};

/// 设置交易金币
struct TradeSetGoldRequest {
  std::int32_t gold{0};
};

/// 确认交易
struct TradeAcceptRequest {};

/// 交易窗口状态
struct TradeState {
  bool visible{false};
  std::string remote_name{};
  std::vector<ItemSlotState> local_items{};
  std::vector<ItemSlotState> remote_items{};
  std::int32_t local_gold{0};
  std::int32_t remote_gold{0};
  bool local_accept{false};
  bool remote_accept{false};
};

/// 打开行会窗口
struct GuildOpenRequest {};

/// 请求行会首页
struct GuildHomeRequest {};

/// 请求行会成员列表
struct GuildMemberListRequest {};

/// 添加行会成员
struct GuildAddMemberRequest {
  std::string name{};
};

/// 删除行会成员
struct GuildRemoveMemberRequest {
  std::string name{};
};

/// 更新行会公告
struct GuildUpdateNoticeRequest {
  std::string text{};
};

/// 更新行会职位文本
struct GuildUpdateGradeRequest {
  std::string text{};
};

/// 行会成员行
struct GuildMemberState {
  std::string name{};
  std::string rank{};
  bool online{false};
};

/// 行会窗口状态
struct GuildState {
  bool visible{false};
  std::string guild_name{};
  std::string rank_name{};
  std::string notice{};
  std::vector<GuildMemberState> members{};
  std::vector<std::string> ranks{};
  bool can_admin{false};
};

/// 系统消息：服务端发送给客户端的系统提示
struct SysMessage {
  std::string text{};
  std::uint8_t level{0};
};

/// 公告：服务端发送的全局公告
struct Notice {
  std::string title{};
  std::string text{};
};

/// Ping：客户端心跳
struct Ping {
  std::uint64_t client_time_ms{0};
};

/// Pong：服务端心跳回复
struct Pong {
  std::uint64_t client_time_ms{0};
  std::uint64_t server_time_ms{0};
};

/// 断开连接原因
struct DisconnectReason {
  std::uint16_t code{0};
  std::string text{};
};

/// 消息变体：所有消息类型的联合，用于 decode_any 的多态分发
using Message = std::variant<ClientHello,
                             LoginRequest,
                             LoginResult,
                             CreateAccountRequest,
                             CreateAccountResult,
                             ChangePasswordRequest,
                             ChangePasswordResult,
                             UpdateAccountRequest,
                             UpdateAccountResult,
                             NeedUpdateAccount,
                             ServerList,
                             SelectServerRequest,
                             SelectServerResult,
                             CharacterListRequest,
                             CharacterList,
                             CreateCharacterRequest,
                             CreateCharacterResult,
                             DeleteCharacterRequest,
                             DeleteCharacterResult,
                             SelectCharacterRequest,
                             SelectCharacterResult,
                             EnterWorldRequest,
                             EnterWorldResult,
                             WorldSnapshot,
                             WorldClearObjects,
                             MapChange,
                             MapDoorState,
                             MapEntered,
                             ActorIdentityUpdate,
                             MapDescription,
                             ActorStateDelta,
                             LoginNotice,
                             LoginNoticeOk,
                             ActorUpsert,
                             ActorRemove,
                             ActorAction,
                             ActorMagicFire,
                             ActorMagicFireFail,
                             ActorVitals,
                             ActorDeath,
                             MagicList,
                             SelfAbility,
                             SelfAbilityDetail,
                             MiniMapRequest,
                             MiniMapData,
                             MoveIntent,
                             ActionIntent,
                             SpellIntent,
                             ActionAck,
                             PickupIntent,
                             UseItemIntent,
                             BagSnapshot,
                             InventoryAdd,
                             InventoryUpdate,
                             InventoryRemove,
                             InventoryClearRange,
                             EquipmentSnapshot,
                             EquipItemRequest,
                             UnequipItemRequest,
                             DropItemRequest,
                             ReviveRequest,
                             DropGoldRequest,
                             DurabilityChange,
                             MagicKeyChangeRequest,
                             GroundItemAdd,
                             GroundItemRemove,
                             UseItemResult,
                             ChatSend,
                             ChatLine,
                             ActorSay,
                             NpcClickRequest,
                             NpcDialog,
                             NpcDialogSelectRequest,
                             NpcDialogClose,
                             MerchantGoodsList,
                             MerchantBuyRequest,
                             MerchantSellRequest,
                             MerchantSellPriceRequest,
                             MerchantPriceResult,
                             MerchantRepairPriceRequest,
                             MerchantRepairRequest,
                             MerchantRepairPriceResult,
                             StorageList,
                             StorageDepositRequest,
                             StorageWithdrawRequest,
                             GroupModeRequest,
                             GroupCreateRequest,
                             GroupAddMemberRequest,
                             GroupRemoveMemberRequest,
                             GroupState,
                             TradeTryRequest,
                             TradeCancelRequest,
                             TradeAddItemRequest,
                             TradeRemoveItemRequest,
                             TradeSetGoldRequest,
                             TradeAcceptRequest,
                             TradeState,
                             GuildOpenRequest,
                             GuildHomeRequest,
                             GuildMemberListRequest,
                             GuildAddMemberRequest,
                             GuildRemoveMemberRequest,
                             GuildUpdateNoticeRequest,
                             GuildUpdateGradeRequest,
                             GuildState,
                             SysMessage,
                             Notice,
                             Ping,
                             Pong,
                             DisconnectReason>;

/// 帧结构：网络传输的基本单元
struct Frame {
  MessageId message_id{MessageId::client_hello};
  std::uint16_t flags{0};
  std::uint32_t sequence{0};
  std::vector<std::uint8_t> payload{};
  std::optional<LegacyBundleMeta> legacy_bundle{};
};

// ====================================================================
// 字节写入器（ByteWriter）
// 职责：将协议字段按小端序编码为字节流
// 支持的字段类型：u8, bool, u16, u32, u64, i32, string, vector
// ====================================================================
class ByteWriter {
 public:
  void write_u8(std::uint8_t value) { buffer_.push_back(value); }

  void write_bool(bool value) { write_u8(value ? 1U : 0U); }

  void write_u16(std::uint16_t value) { write_scalar(value); }

  void write_u32(std::uint32_t value) { write_scalar(value); }

  void write_u64(std::uint64_t value) { write_scalar(value); }

  void write_i32(std::int32_t value) { write_scalar(value); }

  /// 写入字符串：2 字节长度前缀 + byte payload。legacy-sensitive 字段不隐含 UTF-8 语义。
  void write_string(std::string_view value) {
    const auto size = static_cast<std::uint16_t>(std::min<std::size_t>(value.size(), 0xFFFFU));
    write_u16(size);
    buffer_.insert(buffer_.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(size));
  }

  /// 写入变长数组：2 字节数量前缀 + 逐个元素回调
  template <typename Callback>
  void write_vector(std::size_t size, Callback&& callback) {
    const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(size, 0xFFFFU));
    write_u16(count);
    for (std::uint16_t index = 0; index < count; ++index) {
      callback(index);
    }
  }

  [[nodiscard]] std::vector<std::uint8_t> take_buffer() && { return std::move(buffer_); }

 private:
  /// 通用标量写入：小端逐字节写入
  template <typename T>
  void write_scalar(T value) {
    static_assert(std::is_integral_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      buffer_.push_back(static_cast<std::uint8_t>((static_cast<std::make_unsigned_t<T>>(value) >>
                                                   (index * 8U)) &
                                                  0xFFU));
    }
  }

  std::vector<std::uint8_t> buffer_{};
};

// ====================================================================
// 字节读取器（ByteReader）
// 职责：从字节流中按小端序解码协议字段
// 提供边界检查，解码失败时返回 false
// ====================================================================
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool read_u8(std::uint8_t& value) { return read_scalar(value); }

  [[nodiscard]] bool read_bool(bool& value) {
    std::uint8_t raw = 0;
    if (!read_u8(raw)) {
      return false;
    }
    value = raw != 0;
    return true;
  }

  [[nodiscard]] bool read_u16(std::uint16_t& value) { return read_scalar(value); }

  [[nodiscard]] bool read_u32(std::uint32_t& value) { return read_scalar(value); }

  [[nodiscard]] bool read_u64(std::uint64_t& value) { return read_scalar(value); }

  [[nodiscard]] bool read_i32(std::int32_t& value) { return read_scalar(value); }

  /// 读取字符串：2 字节长度前缀 + byte payload。显示层自行决定是否按 UTF-8 展示。
  [[nodiscard]] bool read_string(std::string& value) {
    std::uint16_t size = 0;
    if (!read_u16(size) || offset_ + size > bytes_.size()) {
      return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
    offset_ += size;
    return true;
  }

  /// 读取变长数组：2 字节数量前缀 + 逐个元素回调
  template <typename Callback>
  [[nodiscard]] bool read_vector(Callback&& callback) {
    std::uint16_t count = 0;
    if (!read_u16(count)) {
      return false;
    }
    for (std::uint16_t index = 0; index < count; ++index) {
      if (!callback(index)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool finished() const { return offset_ == bytes_.size(); }

 private:
  /// 通用标量读取：小端逐字节组合
  template <typename T>
  [[nodiscard]] bool read_scalar(T& value) {
    static_assert(std::is_integral_v<T>);
    if (offset_ + sizeof(T) > bytes_.size()) {
      return false;
    }
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned raw = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      raw |= static_cast<Unsigned>(bytes_[offset_ + index]) << (index * 8U);
    }
    value = static_cast<T>(raw);
    offset_ += sizeof(T);
    return true;
  }

  std::span<const std::uint8_t> bytes_{};
  std::size_t offset_{0};
};

// ====================================================================
// MessageTraits：消息类型到 MessageId 的编译期映射
// 使用宏 MIR2_CLIENT_V1_TRAIT 为每个消息类型生成特化
// ====================================================================
template <typename T>
struct MessageTraits;

#define MIR2_CLIENT_V1_TRAIT(type_name, enum_name)                 \
  template <>                                                      \
  struct MessageTraits<type_name> {                                \
    static constexpr MessageId kMessageId = MessageId::enum_name;  \
  }

MIR2_CLIENT_V1_TRAIT(ClientHello, client_hello);
MIR2_CLIENT_V1_TRAIT(LoginRequest, login_request);
MIR2_CLIENT_V1_TRAIT(LoginResult, login_result);
MIR2_CLIENT_V1_TRAIT(CreateAccountRequest, create_account_request);
MIR2_CLIENT_V1_TRAIT(CreateAccountResult, create_account_result);
MIR2_CLIENT_V1_TRAIT(ChangePasswordRequest, change_password_request);
MIR2_CLIENT_V1_TRAIT(ChangePasswordResult, change_password_result);
MIR2_CLIENT_V1_TRAIT(UpdateAccountRequest, update_account_request);
MIR2_CLIENT_V1_TRAIT(UpdateAccountResult, update_account_result);
MIR2_CLIENT_V1_TRAIT(NeedUpdateAccount, need_update_account);
MIR2_CLIENT_V1_TRAIT(ServerList, server_list);
MIR2_CLIENT_V1_TRAIT(SelectServerRequest, select_server_request);
MIR2_CLIENT_V1_TRAIT(SelectServerResult, select_server_result);
MIR2_CLIENT_V1_TRAIT(CharacterListRequest, character_list_request);
MIR2_CLIENT_V1_TRAIT(CharacterList, character_list);
MIR2_CLIENT_V1_TRAIT(CreateCharacterRequest, create_character_request);
MIR2_CLIENT_V1_TRAIT(CreateCharacterResult, create_character_result);
MIR2_CLIENT_V1_TRAIT(DeleteCharacterRequest, delete_character_request);
MIR2_CLIENT_V1_TRAIT(DeleteCharacterResult, delete_character_result);
MIR2_CLIENT_V1_TRAIT(SelectCharacterRequest, select_character_request);
MIR2_CLIENT_V1_TRAIT(SelectCharacterResult, select_character_result);
MIR2_CLIENT_V1_TRAIT(EnterWorldRequest, enter_world_request);
MIR2_CLIENT_V1_TRAIT(EnterWorldResult, enter_world_result);
MIR2_CLIENT_V1_TRAIT(WorldSnapshot, world_snapshot);
MIR2_CLIENT_V1_TRAIT(WorldClearObjects, world_clear_objects);
MIR2_CLIENT_V1_TRAIT(MapChange, map_change);
MIR2_CLIENT_V1_TRAIT(MapDoorState, map_door_state);
MIR2_CLIENT_V1_TRAIT(MapEntered, map_entered);
MIR2_CLIENT_V1_TRAIT(ActorIdentityUpdate, actor_identity_update);
MIR2_CLIENT_V1_TRAIT(MapDescription, map_description);
MIR2_CLIENT_V1_TRAIT(ActorStateDelta, actor_state_delta);
MIR2_CLIENT_V1_TRAIT(LoginNotice, login_notice);
MIR2_CLIENT_V1_TRAIT(LoginNoticeOk, login_notice_ok);
MIR2_CLIENT_V1_TRAIT(ActorUpsert, actor_upsert);
MIR2_CLIENT_V1_TRAIT(ActorRemove, actor_remove);
MIR2_CLIENT_V1_TRAIT(ActorAction, actor_action);
MIR2_CLIENT_V1_TRAIT(ActorMagicFire, actor_magic_fire);
MIR2_CLIENT_V1_TRAIT(ActorMagicFireFail, actor_magic_fire_fail);
MIR2_CLIENT_V1_TRAIT(ActorVitals, actor_vitals);
MIR2_CLIENT_V1_TRAIT(ActorDeath, actor_death);
MIR2_CLIENT_V1_TRAIT(MagicList, magic_list);
MIR2_CLIENT_V1_TRAIT(SelfAbility, self_ability);
MIR2_CLIENT_V1_TRAIT(SelfAbilityDetail, self_ability_detail);
MIR2_CLIENT_V1_TRAIT(MiniMapRequest, mini_map_request);
MIR2_CLIENT_V1_TRAIT(MiniMapData, mini_map_data);
MIR2_CLIENT_V1_TRAIT(MoveIntent, move_intent);
MIR2_CLIENT_V1_TRAIT(ActionIntent, action_intent);
MIR2_CLIENT_V1_TRAIT(SpellIntent, spell_intent);
MIR2_CLIENT_V1_TRAIT(ActionAck, action_ack);
MIR2_CLIENT_V1_TRAIT(PickupIntent, pickup_intent);
MIR2_CLIENT_V1_TRAIT(UseItemIntent, use_item_intent);
MIR2_CLIENT_V1_TRAIT(BagSnapshot, bag_snapshot);
MIR2_CLIENT_V1_TRAIT(InventoryAdd, inventory_add);
MIR2_CLIENT_V1_TRAIT(InventoryUpdate, inventory_update);
MIR2_CLIENT_V1_TRAIT(InventoryRemove, inventory_remove);
MIR2_CLIENT_V1_TRAIT(InventoryClearRange, inventory_clear_range);
MIR2_CLIENT_V1_TRAIT(EquipmentSnapshot, equipment_snapshot);
MIR2_CLIENT_V1_TRAIT(EquipItemRequest, equip_item_request);
MIR2_CLIENT_V1_TRAIT(UnequipItemRequest, unequip_item_request);
MIR2_CLIENT_V1_TRAIT(DropItemRequest, drop_item_request);
MIR2_CLIENT_V1_TRAIT(ReviveRequest, revive_request);
MIR2_CLIENT_V1_TRAIT(DropGoldRequest, drop_gold_request);
MIR2_CLIENT_V1_TRAIT(DurabilityChange, durability_change);
MIR2_CLIENT_V1_TRAIT(MagicKeyChangeRequest, magic_key_change_request);
MIR2_CLIENT_V1_TRAIT(GroundItemAdd, ground_item_add);
MIR2_CLIENT_V1_TRAIT(GroundItemRemove, ground_item_remove);
MIR2_CLIENT_V1_TRAIT(UseItemResult, use_item_result);
MIR2_CLIENT_V1_TRAIT(ChatSend, chat_send);
MIR2_CLIENT_V1_TRAIT(ChatLine, chat_line);
MIR2_CLIENT_V1_TRAIT(ActorSay, actor_say);
MIR2_CLIENT_V1_TRAIT(NpcClickRequest, npc_click_request);
MIR2_CLIENT_V1_TRAIT(NpcDialog, npc_dialog);
MIR2_CLIENT_V1_TRAIT(NpcDialogSelectRequest, npc_dialog_select_request);
MIR2_CLIENT_V1_TRAIT(NpcDialogClose, npc_dialog_close);
MIR2_CLIENT_V1_TRAIT(MerchantGoodsList, merchant_goods_list);
MIR2_CLIENT_V1_TRAIT(MerchantBuyRequest, merchant_buy_request);
MIR2_CLIENT_V1_TRAIT(MerchantSellRequest, merchant_sell_request);
MIR2_CLIENT_V1_TRAIT(MerchantSellPriceRequest, merchant_sell_price_request);
MIR2_CLIENT_V1_TRAIT(MerchantPriceResult, merchant_price_result);
MIR2_CLIENT_V1_TRAIT(MerchantRepairPriceRequest, merchant_repair_price_request);
MIR2_CLIENT_V1_TRAIT(MerchantRepairRequest, merchant_repair_request);
MIR2_CLIENT_V1_TRAIT(MerchantRepairPriceResult, merchant_repair_price_result);
MIR2_CLIENT_V1_TRAIT(StorageList, storage_list);
MIR2_CLIENT_V1_TRAIT(StorageDepositRequest, storage_deposit_request);
MIR2_CLIENT_V1_TRAIT(StorageWithdrawRequest, storage_withdraw_request);
MIR2_CLIENT_V1_TRAIT(GroupModeRequest, group_mode_request);
MIR2_CLIENT_V1_TRAIT(GroupCreateRequest, group_create_request);
MIR2_CLIENT_V1_TRAIT(GroupAddMemberRequest, group_add_member_request);
MIR2_CLIENT_V1_TRAIT(GroupRemoveMemberRequest, group_remove_member_request);
MIR2_CLIENT_V1_TRAIT(GroupState, group_state);
MIR2_CLIENT_V1_TRAIT(TradeTryRequest, trade_try_request);
MIR2_CLIENT_V1_TRAIT(TradeCancelRequest, trade_cancel_request);
MIR2_CLIENT_V1_TRAIT(TradeAddItemRequest, trade_add_item_request);
MIR2_CLIENT_V1_TRAIT(TradeRemoveItemRequest, trade_remove_item_request);
MIR2_CLIENT_V1_TRAIT(TradeSetGoldRequest, trade_set_gold_request);
MIR2_CLIENT_V1_TRAIT(TradeAcceptRequest, trade_accept_request);
MIR2_CLIENT_V1_TRAIT(TradeState, trade_state);
MIR2_CLIENT_V1_TRAIT(GuildOpenRequest, guild_open_request);
MIR2_CLIENT_V1_TRAIT(GuildHomeRequest, guild_home_request);
MIR2_CLIENT_V1_TRAIT(GuildMemberListRequest, guild_member_list_request);
MIR2_CLIENT_V1_TRAIT(GuildAddMemberRequest, guild_add_member_request);
MIR2_CLIENT_V1_TRAIT(GuildRemoveMemberRequest, guild_remove_member_request);
MIR2_CLIENT_V1_TRAIT(GuildUpdateNoticeRequest, guild_update_notice_request);
MIR2_CLIENT_V1_TRAIT(GuildUpdateGradeRequest, guild_update_grade_request);
MIR2_CLIENT_V1_TRAIT(GuildState, guild_state);
MIR2_CLIENT_V1_TRAIT(SysMessage, sys_message);
MIR2_CLIENT_V1_TRAIT(Notice, notice);
MIR2_CLIENT_V1_TRAIT(Ping, ping);
MIR2_CLIENT_V1_TRAIT(Pong, pong);
MIR2_CLIENT_V1_TRAIT(DisconnectReason, disconnect_reason);

#undef MIR2_CLIENT_V1_TRAIT

// ====================================================================
// encode/decode 函数对
// 每个消息类型都有对应的 encode（写入 ByteWriter）和
// decode（从 ByteReader 读取）函数
// ====================================================================

inline void encode(ByteWriter& writer, const ServerEntry& value) {
  writer.write_string(value.name);
  writer.write_string(value.address);
  writer.write_u16(value.port);
}

inline bool decode(ByteReader& reader, ServerEntry& value) {
  return reader.read_string(value.name) && reader.read_string(value.address) &&
         reader.read_u16(value.port);
}

inline void encode(ByteWriter& writer, const CharacterSummary& value) {
  writer.write_string(value.name);
  writer.write_u16(value.level);
  writer.write_u8(value.job);
  writer.write_u8(value.sex);
  writer.write_u8(value.hair);
  writer.write_string(value.map_id);
}

inline bool decode(ByteReader& reader, CharacterSummary& value) {
  return reader.read_string(value.name) && reader.read_u16(value.level) &&
         reader.read_u8(value.job) && reader.read_u8(value.sex) && reader.read_u8(value.hair) &&
         reader.read_string(value.map_id);
}

inline void encode(ByteWriter& writer, const WorldActor& value) {
  writer.write_u64(value.actor_id);
  writer.write_string(value.name);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
  writer.write_i32(value.feature);
  writer.write_i32(value.status);
  writer.write_u8(static_cast<std::uint8_t>(value.actor_type));
  writer.write_u16(value.level);
  writer.write_u8(value.light);
}

inline bool decode(ByteReader& reader, WorldActor& value) {
  std::uint8_t actor_type = 0;
  std::uint16_t level = 1;
  if (!(reader.read_u64(value.actor_id) && reader.read_string(value.name) &&
        reader.read_i32(value.x) && reader.read_i32(value.y) && reader.read_u8(value.dir) &&
        reader.read_i32(value.feature) && reader.read_i32(value.status) &&
        reader.read_u8(actor_type) && reader.read_u16(level) && reader.read_u8(value.light))) {
    return false;
  }
  value.actor_type = static_cast<ActorType>(actor_type);
  value.level = level;
  return true;
}

inline void encode(ByteWriter& writer, const MagicEntry& value) {
  writer.write_u16(value.magic_id);
  writer.write_u8(value.key);
  writer.write_u8(value.level);
  writer.write_i32(value.train);
  writer.write_i32(value.delay_ms);
  writer.write_string(value.name);
  writer.write_i32(value.effect);
  writer.write_i32(value.max_train);
  writer.write_i32(value.effect_type);
  writer.write_i32(value.spell);
  writer.write_i32(value.def_spell);
  writer.write_i32(value.max_train_level);
}

inline bool decode(ByteReader& reader, MagicEntry& value) {
  return reader.read_u16(value.magic_id) && reader.read_u8(value.key) &&
         reader.read_u8(value.level) && reader.read_i32(value.train) &&
         reader.read_i32(value.delay_ms) && reader.read_string(value.name) &&
         reader.read_i32(value.effect) && reader.read_i32(value.max_train) &&
         reader.read_i32(value.effect_type) && reader.read_i32(value.spell) &&
         reader.read_i32(value.def_spell) && reader.read_i32(value.max_train_level);
}

inline void encode(ByteWriter& writer, const ClientHello& value) {
  writer.write_u32(value.protocol_version);
  writer.write_u32(value.client_build);
  writer.write_u32(value.resource_revision);
  writer.write_u32(value.capabilities);
}

inline bool decode(ByteReader& reader, ClientHello& value) {
  return reader.read_u32(value.protocol_version) && reader.read_u32(value.client_build) &&
         reader.read_u32(value.resource_revision) && reader.read_u32(value.capabilities);
}

inline void encode(ByteWriter& writer, const LoginRequest& value) {
  writer.write_string(value.account_id);
  writer.write_string(value.password);
}

inline bool decode(ByteReader& reader, LoginRequest& value) {
  return reader.read_string(value.account_id) && reader.read_string(value.password);
}

inline void encode(ByteWriter& writer, const LoginResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.account_id);
  writer.write_string(value.display_name);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, LoginResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.account_id) && reader.read_string(value.display_name) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const AccountProfile& value) {
  writer.write_string(value.display_name);
  writer.write_string(value.user_name);
  writer.write_string(value.ss_no);
  writer.write_string(value.birthday);
  writer.write_string(value.quiz);
  writer.write_string(value.answer);
  writer.write_string(value.quiz2);
  writer.write_string(value.answer2);
  writer.write_string(value.phone);
  writer.write_string(value.mobile_phone);
  writer.write_string(value.email);
  writer.write_string(value.memo1);
  writer.write_string(value.memo2);
}

inline bool decode(ByteReader& reader, AccountProfile& value) {
  return reader.read_string(value.display_name) && reader.read_string(value.user_name) &&
         reader.read_string(value.ss_no) && reader.read_string(value.birthday) &&
         reader.read_string(value.quiz) && reader.read_string(value.answer) &&
         reader.read_string(value.quiz2) && reader.read_string(value.answer2) &&
         reader.read_string(value.phone) && reader.read_string(value.mobile_phone) &&
         reader.read_string(value.email) && reader.read_string(value.memo1) &&
         reader.read_string(value.memo2);
}

inline void encode(ByteWriter& writer, const CreateAccountRequest& value) {
  writer.write_string(value.account_id);
  writer.write_string(value.password);
  encode(writer, value.profile);
}

inline bool decode(ByteReader& reader, CreateAccountRequest& value) {
  return reader.read_string(value.account_id) && reader.read_string(value.password) &&
         decode(reader, value.profile);
}

inline void encode(ByteWriter& writer, const CreateAccountResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, CreateAccountResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const ChangePasswordRequest& value) {
  writer.write_string(value.account_id);
  writer.write_string(value.password);
  writer.write_string(value.new_password);
}

inline bool decode(ByteReader& reader, ChangePasswordRequest& value) {
  return reader.read_string(value.account_id) && reader.read_string(value.password) &&
         reader.read_string(value.new_password);
}

inline void encode(ByteWriter& writer, const ChangePasswordResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, ChangePasswordResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const UpdateAccountRequest& value) {
  writer.write_string(value.account_id);
  writer.write_string(value.password);
  encode(writer, value.profile);
}

inline bool decode(ByteReader& reader, UpdateAccountRequest& value) {
  return reader.read_string(value.account_id) && reader.read_string(value.password) &&
         decode(reader, value.profile);
}

inline void encode(ByteWriter& writer, const UpdateAccountResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, UpdateAccountResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const NeedUpdateAccount& value) {
  writer.write_string(value.account_id);
  encode(writer, value.profile);
  writer.write_string(value.message);
}

inline bool decode(ByteReader& reader, NeedUpdateAccount& value) {
  return reader.read_string(value.account_id) && decode(reader, value.profile) &&
         reader.read_string(value.message);
}

inline void encode(ByteWriter& writer, const ServerList& value) {
  writer.write_vector(value.servers.size(), [&](std::uint16_t index) { encode(writer, value.servers[index]); });
}

inline bool decode(ByteReader& reader, ServerList& value) {
  value.servers.clear();
  return reader.read_vector([&](std::uint16_t) {
    ServerEntry entry;
    if (!decode(reader, entry)) {
      return false;
    }
    value.servers.push_back(std::move(entry));
    return true;
  });
}

inline void encode(ByteWriter& writer, const SelectServerRequest& value) {
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, SelectServerRequest& value) {
  return reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const SelectServerResult& value) {
  writer.write_bool(value.success);
  writer.write_string(value.name);
  writer.write_string(value.address);
  writer.write_u16(value.port);
  writer.write_string(value.lobby_token);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, SelectServerResult& value) {
  return reader.read_bool(value.success) && reader.read_string(value.name) &&
         reader.read_string(value.address) && reader.read_u16(value.port) &&
         reader.read_string(value.lobby_token) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const CharacterListRequest& value) {
  writer.write_string(value.lobby_token);
}

inline bool decode(ByteReader& reader, CharacterListRequest& value) {
  return reader.read_string(value.lobby_token);
}

inline void encode(ByteWriter& writer, const CharacterList& value) {
  writer.write_vector(value.characters.size(),
                      [&](std::uint16_t index) { encode(writer, value.characters[index]); });
  writer.write_string(value.selected_name);
}

inline bool decode(ByteReader& reader, CharacterList& value) {
  value.characters.clear();
  if (!reader.read_vector([&](std::uint16_t) {
        CharacterSummary summary;
        if (!decode(reader, summary)) {
          return false;
        }
        value.characters.push_back(std::move(summary));
        return true;
      })) {
    return false;
  }
  return reader.read_string(value.selected_name);
}

inline void encode(ByteWriter& writer, const CreateCharacterRequest& value) {
  writer.write_string(value.name);
  writer.write_u8(value.job);
  writer.write_u8(value.sex);
  writer.write_u8(value.hair);
}

inline bool decode(ByteReader& reader, CreateCharacterRequest& value) {
  return reader.read_string(value.name) && reader.read_u8(value.job) &&
         reader.read_u8(value.sex) && reader.read_u8(value.hair);
}

inline void encode(ByteWriter& writer, const CreateCharacterResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.error_message);
  encode(writer, value.character);
}

inline bool decode(ByteReader& reader, CreateCharacterResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.error_message) && decode(reader, value.character);
}

inline void encode(ByteWriter& writer, const DeleteCharacterRequest& value) {
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, DeleteCharacterRequest& value) {
  return reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const DeleteCharacterResult& value) {
  writer.write_bool(value.success);
  writer.write_i32(value.code);
  writer.write_string(value.error_message);
  writer.write_string(value.deleted_name);
}

inline bool decode(ByteReader& reader, DeleteCharacterResult& value) {
  return reader.read_bool(value.success) && reader.read_i32(value.code) &&
         reader.read_string(value.error_message) && reader.read_string(value.deleted_name);
}

inline void encode(ByteWriter& writer, const SelectCharacterRequest& value) {
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, SelectCharacterRequest& value) {
  return reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const SelectCharacterResult& value) {
  writer.write_bool(value.success);
  writer.write_string(value.character_name);
  writer.write_string(value.enter_world_token);
  writer.write_string(value.address);
  writer.write_u16(value.port);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, SelectCharacterResult& value) {
  return reader.read_bool(value.success) && reader.read_string(value.character_name) &&
         reader.read_string(value.enter_world_token) && reader.read_string(value.address) &&
         reader.read_u16(value.port) && reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const EnterWorldRequest& value) {
  writer.write_string(value.token);
  writer.write_u32(value.client_build);
  writer.write_u32(value.resource_revision);
}

inline bool decode(ByteReader& reader, EnterWorldRequest& value) {
  return reader.read_string(value.token) && reader.read_u32(value.client_build) &&
         reader.read_u32(value.resource_revision);
}

inline void encode(ByteWriter& writer, const EnterWorldResult& value) {
  writer.write_bool(value.success);
  writer.write_u64(value.self_actor_id);
  writer.write_string(value.character_name);
  writer.write_string(value.map_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, EnterWorldResult& value) {
  return reader.read_bool(value.success) && reader.read_u64(value.self_actor_id) &&
         reader.read_string(value.character_name) && reader.read_string(value.map_id) &&
         reader.read_i32(value.x) && reader.read_i32(value.y) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const WorldSnapshot& value) {
  writer.write_string(value.map_id);
  writer.write_i32(value.width);
  writer.write_i32(value.height);
  writer.write_u64(value.self_actor_id);
  writer.write_vector(value.actors.size(), [&](std::uint16_t index) { encode(writer, value.actors[index]); });
}

inline bool decode(ByteReader& reader, WorldSnapshot& value) {
  value.actors.clear();
  if (!(reader.read_string(value.map_id) && reader.read_i32(value.width) &&
        reader.read_i32(value.height) && reader.read_u64(value.self_actor_id))) {
    return false;
  }
  return reader.read_vector([&](std::uint16_t) {
    WorldActor actor;
    if (!decode(reader, actor)) {
      return false;
    }
    value.actors.push_back(std::move(actor));
    return true;
  });
}

inline void encode(ByteWriter& /*writer*/, const WorldClearObjects& /*value*/) {}

inline bool decode(ByteReader& /*reader*/, WorldClearObjects& /*value*/) {
  return true;
}

inline void encode(ByteWriter& writer, const MapChange& value) {
  writer.write_string(value.map_id);
}

inline bool decode(ByteReader& reader, MapChange& value) {
  return reader.read_string(value.map_id);
}

inline void encode(ByteWriter& writer, const MapDoorState& value) {
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.open ? 1U : 0U);
}

inline bool decode(ByteReader& reader, MapDoorState& value) {
  std::uint8_t open = 0;
  if (!reader.read_i32(value.x) || !reader.read_i32(value.y) || !reader.read_u8(open)) {
    return false;
  }
  value.open = open != 0U;
  return true;
}

inline void encode(ByteWriter& writer, const MapEntered& value) {
  writer.write_string(value.map_id);
  writer.write_u64(value.self_actor_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
}

inline bool decode(ByteReader& reader, MapEntered& value) {
  return reader.read_string(value.map_id) && reader.read_u64(value.self_actor_id) &&
         reader.read_i32(value.x) && reader.read_i32(value.y) && reader.read_u8(value.dir);
}

inline void encode(ByteWriter& writer, const ActorIdentityUpdate& value) {
  writer.write_u64(value.actor_id);
  writer.write_u8(value.mask);
  writer.write_string(value.name);
  writer.write_u32(value.name_color);
  writer.write_i32(value.feature);
  writer.write_i32(value.status);
  writer.write_u8(value.light);
}

inline bool decode(ByteReader& reader, ActorIdentityUpdate& value) {
  return reader.read_u64(value.actor_id) && reader.read_u8(value.mask) &&
         reader.read_string(value.name) && reader.read_u32(value.name_color) &&
         reader.read_i32(value.feature) && reader.read_i32(value.status) &&
         reader.read_u8(value.light);
}

inline void encode(ByteWriter& writer, const MapDescription& value) {
  writer.write_string(value.title);
}

inline bool decode(ByteReader& reader, MapDescription& value) {
  return reader.read_string(value.title);
}

inline void encode(ByteWriter& writer, const SelfAbility& value) {
  writer.write_u16(value.level);
  writer.write_u8(value.job);
  writer.write_u32(value.exp);
  writer.write_u32(value.max_exp);
  writer.write_u16(value.weight);
  writer.write_u16(value.max_weight);
  writer.write_i32(value.gold);
  writer.write_u8(value.hunger_state);
}

inline bool decode(ByteReader& reader, SelfAbility& value) {
  return reader.read_u16(value.level) && reader.read_u8(value.job) &&
         reader.read_u32(value.exp) && reader.read_u32(value.max_exp) &&
         reader.read_u16(value.weight) && reader.read_u16(value.max_weight) &&
         reader.read_i32(value.gold) && reader.read_u8(value.hunger_state);
}

inline void encode(ByteWriter& writer, const SelfAbilityDetail& value) {
  writer.write_u16(value.level);
  writer.write_u8(value.job);
  writer.write_u8(value.sex);
  writer.write_u8(value.hair);
  writer.write_u16(value.hp);
  writer.write_u16(value.max_hp);
  writer.write_u16(value.mp);
  writer.write_u16(value.max_mp);
  writer.write_u16(value.ac);
  writer.write_u16(value.mac);
  writer.write_u16(value.dc);
  writer.write_u16(value.mc);
  writer.write_u16(value.sc);
  writer.write_u32(value.exp);
  writer.write_u32(value.max_exp);
  writer.write_u16(value.weight);
  writer.write_u16(value.max_weight);
  writer.write_u16(value.wear_weight);
  writer.write_u16(value.max_wear_weight);
  writer.write_u16(value.hand_weight);
  writer.write_u16(value.max_hand_weight);
  writer.write_i32(value.hit);
  writer.write_i32(value.speed);
  writer.write_i32(value.anti_magic);
  writer.write_i32(value.anti_poison);
  writer.write_i32(value.poison_recover);
  writer.write_i32(value.health_recover);
  writer.write_i32(value.spell_recover);
  writer.write_string(value.guild_name);
  writer.write_string(value.guild_rank_name);
  writer.write_u32(value.name_color);
}

inline bool decode(ByteReader& reader, SelfAbilityDetail& value) {
  return reader.read_u16(value.level) && reader.read_u8(value.job) &&
         reader.read_u8(value.sex) && reader.read_u8(value.hair) &&
         reader.read_u16(value.hp) && reader.read_u16(value.max_hp) &&
         reader.read_u16(value.mp) && reader.read_u16(value.max_mp) &&
         reader.read_u16(value.ac) && reader.read_u16(value.mac) &&
         reader.read_u16(value.dc) && reader.read_u16(value.mc) &&
         reader.read_u16(value.sc) && reader.read_u32(value.exp) &&
         reader.read_u32(value.max_exp) && reader.read_u16(value.weight) &&
         reader.read_u16(value.max_weight) && reader.read_u16(value.wear_weight) &&
         reader.read_u16(value.max_wear_weight) && reader.read_u16(value.hand_weight) &&
         reader.read_u16(value.max_hand_weight) && reader.read_i32(value.hit) &&
         reader.read_i32(value.speed) && reader.read_i32(value.anti_magic) &&
         reader.read_i32(value.anti_poison) && reader.read_i32(value.poison_recover) &&
         reader.read_i32(value.health_recover) && reader.read_i32(value.spell_recover) &&
         reader.read_string(value.guild_name) && reader.read_string(value.guild_rank_name) &&
         reader.read_u32(value.name_color);
}

inline void encode(ByteWriter& writer, const MiniMapRequest& value) {
  writer.write_string(value.map_id);
}

inline bool decode(ByteReader& reader, MiniMapRequest& value) {
  return reader.read_string(value.map_id);
}

inline void encode(ByteWriter& writer, const MiniMapData& value) {
  writer.write_bool(value.success);
  writer.write_string(value.map_id);
  writer.write_u16(value.width);
  writer.write_u16(value.height);
  writer.write_vector(value.pixels.size(), [&](const std::uint16_t index) {
    writer.write_u8(value.pixels[index]);
  });
  writer.write_string(value.error_message);
}

inline bool decode(ByteReader& reader, MiniMapData& value) {
  value.pixels.clear();
  return reader.read_bool(value.success) && reader.read_string(value.map_id) &&
         reader.read_u16(value.width) && reader.read_u16(value.height) &&
         reader.read_vector([&](std::uint16_t) {
           std::uint8_t pixel = 0;
           if (!reader.read_u8(pixel)) {
             return false;
           }
           value.pixels.push_back(pixel);
           return true;
         }) &&
         reader.read_string(value.error_message);
}

inline void encode(ByteWriter& writer, const ActorStateDelta& value) {
  writer.write_u64(value.actor_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
}

inline bool decode(ByteReader& reader, ActorStateDelta& value) {
  return reader.read_u64(value.actor_id) && reader.read_i32(value.x) &&
         reader.read_i32(value.y) && reader.read_u8(value.dir);
}

inline void encode(ByteWriter& writer, const LoginNotice& value) {
  writer.write_string(value.title);
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, LoginNotice& value) {
  return reader.read_string(value.title) && reader.read_string(value.text);
}

inline void encode(ByteWriter& /*writer*/, const LoginNoticeOk& /*value*/) {}

inline bool decode(ByteReader& /*reader*/, LoginNoticeOk& /*value*/) { return true; }

inline void encode(ByteWriter& writer, const ActorUpsert& value) { encode(writer, value.actor); }

inline bool decode(ByteReader& reader, ActorUpsert& value) { return decode(reader, value.actor); }

inline void encode(ByteWriter& writer, const ActorRemove& value) {
  writer.write_u64(value.actor_id);
  writer.write_u16(value.legacy_ident);
}

inline bool decode(ByteReader& reader, ActorRemove& value) {
  return reader.read_u64(value.actor_id) && reader.read_u16(value.legacy_ident);
}

inline void encode(ByteWriter& writer, const ActorAction& value) {
  writer.write_u64(value.actor_id);
  writer.write_u8(static_cast<std::uint8_t>(value.kind));
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
  writer.write_u64(value.target_actor_id);
  writer.write_i32(value.value);
  writer.write_u16(value.legacy_ident);
  writer.write_u16(value.magic_id);
  writer.write_bool(value.magic);
  writer.write_u16(value.magic_effect);
}

inline bool decode(ByteReader& reader, ActorAction& value) {
  std::uint8_t kind = 0;
  if (!(reader.read_u64(value.actor_id) && reader.read_u8(kind) && reader.read_i32(value.x) &&
        reader.read_i32(value.y) && reader.read_u8(value.dir) &&
        reader.read_u64(value.target_actor_id) && reader.read_i32(value.value) &&
        reader.read_u16(value.legacy_ident) && reader.read_u16(value.magic_id) &&
        reader.read_bool(value.magic) && reader.read_u16(value.magic_effect))) {
    return false;
  }
  value.kind = static_cast<ActorActionKind>(kind);
  return true;
}

inline void encode(ByteWriter& writer, const ActorMagicFire& value) {
  writer.write_u64(value.actor_id);
  writer.write_u64(value.target_actor_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.effect_type);
  writer.write_u8(value.effect);
  writer.write_u16(value.legacy_ident);
}

inline bool decode(ByteReader& reader, ActorMagicFire& value) {
  return reader.read_u64(value.actor_id) && reader.read_u64(value.target_actor_id) &&
         reader.read_i32(value.x) && reader.read_i32(value.y) &&
         reader.read_u8(value.effect_type) && reader.read_u8(value.effect) &&
         reader.read_u16(value.legacy_ident);
}

inline void encode(ByteWriter& writer, const ActorMagicFireFail& value) {
  writer.write_u64(value.actor_id);
  writer.write_u16(value.legacy_ident);
}

inline bool decode(ByteReader& reader, ActorMagicFireFail& value) {
  return reader.read_u64(value.actor_id) && reader.read_u16(value.legacy_ident);
}

inline void encode(ByteWriter& writer, const ActorVitals& value) {
  writer.write_u64(value.actor_id);
  writer.write_i32(value.hp);
  writer.write_i32(value.max_hp);
  writer.write_i32(value.mp);
  writer.write_i32(value.max_mp);
  writer.write_i32(value.damage);
  writer.write_u64(value.source_actor_id);
  writer.write_bool(value.magic);
  writer.write_u16(value.legacy_ident);
  writer.write_u16(value.actor_level);
  writer.write_i32(value.health_gauge_visible);
}

inline bool decode(ByteReader& reader, ActorVitals& value) {
  return reader.read_u64(value.actor_id) && reader.read_i32(value.hp) &&
         reader.read_i32(value.max_hp) && reader.read_i32(value.mp) &&
         reader.read_i32(value.max_mp) && reader.read_i32(value.damage) &&
         reader.read_u64(value.source_actor_id) && reader.read_bool(value.magic) &&
         reader.read_u16(value.legacy_ident) && reader.read_u16(value.actor_level) &&
         reader.read_i32(value.health_gauge_visible);
}

inline void encode(ByteWriter& writer, const ActorDeath& value) {
  writer.write_u64(value.actor_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
  writer.write_u16(value.legacy_ident);
}

inline bool decode(ByteReader& reader, ActorDeath& value) {
  return reader.read_u64(value.actor_id) && reader.read_i32(value.x) &&
         reader.read_i32(value.y) && reader.read_u8(value.dir) &&
         reader.read_u16(value.legacy_ident);
}

inline void encode(ByteWriter& writer, const MagicList& value) {
  writer.write_vector(value.magics.size(), [&](std::uint16_t index) { encode(writer, value.magics[index]); });
}

inline bool decode(ByteReader& reader, MagicList& value) {
  value.magics.clear();
  return reader.read_vector([&](std::uint16_t) {
    MagicEntry entry;
    if (!decode(reader, entry)) {
      return false;
    }
    value.magics.push_back(std::move(entry));
    return true;
  });
}

inline void encode(ByteWriter& writer, const MoveIntent& value) {
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(static_cast<std::uint8_t>(value.mode));
}

inline bool decode(ByteReader& reader, MoveIntent& value) {
  std::uint8_t mode = 0;
  if (!(reader.read_i32(value.x) && reader.read_i32(value.y) && reader.read_u8(mode))) {
    return false;
  }
  value.mode = static_cast<MoveMode>(mode);
  return true;
}

inline void encode(ByteWriter& writer, const ActionIntent& value) {
  writer.write_u8(static_cast<std::uint8_t>(value.kind));
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
  writer.write_u64(value.target_actor_id);
  writer.write_u16(value.legacy_ident);
}

inline bool decode(ByteReader& reader, ActionIntent& value) {
  std::uint8_t kind = 0;
  if (!(reader.read_u8(kind) && reader.read_i32(value.x) && reader.read_i32(value.y) &&
        reader.read_u8(value.dir) && reader.read_u64(value.target_actor_id) &&
        reader.read_u16(value.legacy_ident))) {
    return false;
  }
  value.kind = static_cast<WorldActionKind>(kind);
  return true;
}

inline void encode(ByteWriter& writer, const SpellIntent& value) {
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_u8(value.dir);
  writer.write_u64(value.target_actor_id);
  writer.write_u16(value.magic_id);
}

inline bool decode(ByteReader& reader, SpellIntent& value) {
  return reader.read_i32(value.x) && reader.read_i32(value.y) && reader.read_u8(value.dir) &&
         reader.read_u64(value.target_actor_id) && reader.read_u16(value.magic_id);
}

inline void encode(ByteWriter& writer, const ActionAck& value) {
  writer.write_bool(value.ok);
  writer.write_u32(value.server_time_ms);
}

inline bool decode(ByteReader& reader, ActionAck& value) {
  return reader.read_bool(value.ok) && reader.read_u32(value.server_time_ms);
}

inline void encode(ByteWriter& writer, const PickupIntent& value) {
  writer.write_i32(value.x);
  writer.write_i32(value.y);
}

inline bool decode(ByteReader& reader, PickupIntent& value) {
  return reader.read_i32(value.x) && reader.read_i32(value.y);
}

inline void encode(ByteWriter& writer, const UseItemIntent& value) {
  writer.write_i32(value.item_make_index);
  writer.write_i32(value.item_slot);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, UseItemIntent& value) {
  return reader.read_i32(value.item_make_index) && reader.read_i32(value.item_slot) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const ItemState& value) {
  writer.write_string(value.name);
  writer.write_i32(value.make_index);
  writer.write_i32(value.looks);
  writer.write_u8(value.std_mode);
  writer.write_u16(value.dura);
  writer.write_u16(value.dura_max);
}

inline bool decode(ByteReader& reader, ItemState& value) {
  return reader.read_string(value.name) && reader.read_i32(value.make_index) &&
         reader.read_i32(value.looks) && reader.read_u8(value.std_mode) &&
         reader.read_u16(value.dura) && reader.read_u16(value.dura_max);
}

inline void encode(ByteWriter& writer, const ItemSlotState& value) {
  writer.write_i32(value.slot);
  encode(writer, value.item);
}

inline bool decode(ByteReader& reader, ItemSlotState& value) {
  return reader.read_i32(value.slot) && decode(reader, value.item);
}

inline void encode(ByteWriter& writer, const BagSnapshot& value) {
  writer.write_vector(value.items.size(), [&](const std::uint16_t index) {
    encode(writer, value.items[index]);
  });
}

inline bool decode(ByteReader& reader, BagSnapshot& value) {
  value.items.clear();
  return reader.read_vector([&](std::uint16_t) {
    ItemSlotState entry;
    if (!decode(reader, entry)) {
      return false;
    }
    value.items.push_back(std::move(entry));
    return true;
  });
}

inline void encode(ByteWriter& writer, const InventoryAdd& value) { encode(writer, value.entry); }

inline bool decode(ByteReader& reader, InventoryAdd& value) { return decode(reader, value.entry); }

inline void encode(ByteWriter& writer, const InventoryUpdate& value) {
  encode(writer, value.entry);
}

inline bool decode(ByteReader& reader, InventoryUpdate& value) {
  return decode(reader, value.entry);
}

inline void encode(ByteWriter& writer, const InventoryRemove& value) {
  writer.write_i32(value.slot);
}

inline bool decode(ByteReader& reader, InventoryRemove& value) {
  return reader.read_i32(value.slot);
}

inline void encode(ByteWriter& writer, const InventoryClearRange& value) {
  writer.write_i32(value.first_slot);
  writer.write_i32(value.last_slot);
}

inline bool decode(ByteReader& reader, InventoryClearRange& value) {
  return reader.read_i32(value.first_slot) && reader.read_i32(value.last_slot);
}

inline void encode(ByteWriter& writer, const EquipmentSnapshot& value) {
  writer.write_vector(value.items.size(), [&](const std::uint16_t index) {
    encode(writer, value.items[index]);
  });
}

inline bool decode(ByteReader& reader, EquipmentSnapshot& value) {
  value.items.clear();
  return reader.read_vector([&](std::uint16_t) {
    ItemSlotState entry;
    if (!decode(reader, entry)) {
      return false;
    }
    value.items.push_back(std::move(entry));
    return true;
  });
}

inline void encode(ByteWriter& writer, const EquipItemRequest& value) {
  writer.write_i32(value.equipment_slot);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, EquipItemRequest& value) {
  return reader.read_i32(value.equipment_slot) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const UnequipItemRequest& value) {
  writer.write_i32(value.equipment_slot);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, UnequipItemRequest& value) {
  return reader.read_i32(value.equipment_slot) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const DropItemRequest& value) {
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, DropItemRequest& value) {
  return reader.read_i32(value.item_make_index) && reader.read_string(value.name);
}

inline void encode(ByteWriter& /*writer*/, const ReviveRequest& /*value*/) {}

inline bool decode(ByteReader& /*reader*/, ReviveRequest& /*value*/) { return true; }

inline void encode(ByteWriter& writer, const DropGoldRequest& value) {
  writer.write_i32(value.amount);
}

inline bool decode(ByteReader& reader, DropGoldRequest& value) {
  return reader.read_i32(value.amount);
}

inline void encode(ByteWriter& writer, const DurabilityChange& value) {
  writer.write_i32(value.item_make_index);
  writer.write_i32(value.dura);
  writer.write_i32(value.dura_max);
}

inline bool decode(ByteReader& reader, DurabilityChange& value) {
  return reader.read_i32(value.item_make_index) && reader.read_i32(value.dura) &&
         reader.read_i32(value.dura_max);
}

inline void encode(ByteWriter& writer, const MagicKeyChangeRequest& value) {
  writer.write_u16(value.magic_id);
  writer.write_u8(value.key);
}

inline bool decode(ByteReader& reader, MagicKeyChangeRequest& value) {
  return reader.read_u16(value.magic_id) && reader.read_u8(value.key);
}

inline void encode(ByteWriter& writer, const GroundItemState& value) {
  writer.write_u64(value.object_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
  writer.write_i32(value.looks);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, GroundItemState& value) {
  return reader.read_u64(value.object_id) && reader.read_i32(value.x) && reader.read_i32(value.y) &&
         reader.read_i32(value.looks) && reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const GroundItemAdd& value) { encode(writer, value.item); }

inline bool decode(ByteReader& reader, GroundItemAdd& value) { return decode(reader, value.item); }

inline void encode(ByteWriter& writer, const GroundItemRemove& value) {
  writer.write_u64(value.object_id);
  writer.write_i32(value.x);
  writer.write_i32(value.y);
}

inline bool decode(ByteReader& reader, GroundItemRemove& value) {
  return reader.read_u64(value.object_id) && reader.read_i32(value.x) && reader.read_i32(value.y);
}

inline void encode(ByteWriter& writer, const UseItemResult& value) { writer.write_bool(value.ok); }

inline bool decode(ByteReader& reader, UseItemResult& value) { return reader.read_bool(value.ok); }

inline void encode(ByteWriter& writer, const ChatSend& value) { writer.write_string(value.text); }

inline bool decode(ByteReader& reader, ChatSend& value) { return reader.read_string(value.text); }

inline void encode(ByteWriter& writer, const ChatLine& value) {
  writer.write_string(value.text);
  writer.write_u32(value.fore_color);
  writer.write_u32(value.back_color);
}

inline bool decode(ByteReader& reader, ChatLine& value) {
  return reader.read_string(value.text) && reader.read_u32(value.fore_color) &&
         reader.read_u32(value.back_color);
}

inline void encode(ByteWriter& writer, const ActorSay& value) {
  writer.write_u64(value.actor_id);
  writer.write_string(value.text);
  writer.write_u32(value.fore_color);
  writer.write_u32(value.back_color);
}

inline bool decode(ByteReader& reader, ActorSay& value) {
  return reader.read_u64(value.actor_id) && reader.read_string(value.text) &&
         reader.read_u32(value.fore_color) && reader.read_u32(value.back_color);
}

inline void encode(ByteWriter& writer, const NpcClickRequest& value) {
  writer.write_u64(value.actor_id);
}

inline bool decode(ByteReader& reader, NpcClickRequest& value) {
  return reader.read_u64(value.actor_id);
}

inline void encode(ByteWriter& writer, const NpcDialog& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.face);
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, NpcDialog& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.face) &&
         reader.read_string(value.text);
}

inline void encode(ByteWriter& writer, const NpcDialogSelectRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_string(value.selection);
}

inline bool decode(ByteReader& reader, NpcDialogSelectRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_string(value.selection);
}

inline void encode(ByteWriter& writer, const NpcDialogClose& value) {
  writer.write_u64(value.merchant_id);
}

inline bool decode(ByteReader& reader, NpcDialogClose& value) {
  return reader.read_u64(value.merchant_id);
}

inline void encode(ByteWriter& writer, const MerchantGoodsItem& value) {
  writer.write_i32(value.server_index);
  writer.write_string(value.name);
  writer.write_i32(value.looks);
  writer.write_u8(value.std_mode);
  writer.write_i32(value.price);
}

inline bool decode(ByteReader& reader, MerchantGoodsItem& value) {
  return reader.read_i32(value.server_index) && reader.read_string(value.name) &&
         reader.read_i32(value.looks) && reader.read_u8(value.std_mode) &&
         reader.read_i32(value.price);
}

inline void encode(ByteWriter& writer, const MerchantGoodsList& value) {
  writer.write_u64(value.merchant_id);
  writer.write_vector(value.items.size(), [&](const std::uint16_t index) {
    encode(writer, value.items[index]);
  });
}

inline bool decode(ByteReader& reader, MerchantGoodsList& value) {
  value.items.clear();
  return reader.read_u64(value.merchant_id) &&
         reader.read_vector([&](std::uint16_t) {
           MerchantGoodsItem item;
           if (!decode(reader, item)) {
             return false;
           }
           value.items.push_back(std::move(item));
           return true;
         });
}

inline void encode(ByteWriter& writer, const MerchantBuyRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_server_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, MerchantBuyRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_server_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const MerchantSellRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, MerchantSellRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const MerchantSellPriceRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, MerchantSellPriceRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const MerchantPriceResult& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_index);
  writer.write_i32(value.price);
  writer.write_bool(value.sell);
  writer.write_bool(value.ok);
}

inline bool decode(ByteReader& reader, MerchantPriceResult& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_index) &&
         reader.read_i32(value.price) && reader.read_bool(value.sell) &&
         reader.read_bool(value.ok);
}

inline void encode(ByteWriter& writer, const MerchantRepairPriceRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, MerchantRepairPriceRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const MerchantRepairRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, MerchantRepairRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const MerchantRepairPriceResult& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_i32(value.price);
  writer.write_bool(value.ok);
}

inline bool decode(ByteReader& reader, MerchantRepairPriceResult& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_i32(value.price) && reader.read_bool(value.ok);
}

inline void encode(ByteWriter& writer, const StorageList& value) {
  writer.write_u64(value.merchant_id);
  writer.write_vector(value.items.size(), [&](const std::uint16_t index) {
    encode(writer, value.items[index]);
  });
}

inline bool decode(ByteReader& reader, StorageList& value) {
  value.items.clear();
  return reader.read_u64(value.merchant_id) &&
         reader.read_vector([&](std::uint16_t) {
           ItemState item;
           if (!decode(reader, item)) {
             return false;
           }
           value.items.push_back(std::move(item));
           return true;
         });
}

inline void encode(ByteWriter& writer, const StorageDepositRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, StorageDepositRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const StorageWithdrawRequest& value) {
  writer.write_u64(value.merchant_id);
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, StorageWithdrawRequest& value) {
  return reader.read_u64(value.merchant_id) && reader.read_i32(value.item_make_index) &&
         reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const GroupModeRequest& value) {
  writer.write_bool(value.allow);
}

inline bool decode(ByteReader& reader, GroupModeRequest& value) {
  return reader.read_bool(value.allow);
}

inline void encode(ByteWriter& writer, const GroupCreateRequest& value) {
  writer.write_string(value.target_name);
}

inline bool decode(ByteReader& reader, GroupCreateRequest& value) {
  return reader.read_string(value.target_name);
}

inline void encode(ByteWriter& writer, const GroupAddMemberRequest& value) {
  writer.write_string(value.target_name);
}

inline bool decode(ByteReader& reader, GroupAddMemberRequest& value) {
  return reader.read_string(value.target_name);
}

inline void encode(ByteWriter& writer, const GroupRemoveMemberRequest& value) {
  writer.write_string(value.target_name);
}

inline bool decode(ByteReader& reader, GroupRemoveMemberRequest& value) {
  return reader.read_string(value.target_name);
}

inline void encode(ByteWriter& writer, const GroupState& value) {
  writer.write_bool(value.visible);
  writer.write_bool(value.allow_group);
  writer.write_vector(value.members.size(), [&](const std::uint16_t index) {
    writer.write_string(value.members[index]);
  });
}

inline bool decode(ByteReader& reader, GroupState& value) {
  value.members.clear();
  return reader.read_bool(value.visible) && reader.read_bool(value.allow_group) &&
         reader.read_vector([&](std::uint16_t) {
           std::string member;
           if (!reader.read_string(member)) {
             return false;
           }
           value.members.push_back(std::move(member));
           return true;
         });
}

inline void encode(ByteWriter& writer, const TradeTryRequest& value) {
  writer.write_string(value.target_name);
}

inline bool decode(ByteReader& reader, TradeTryRequest& value) {
  return reader.read_string(value.target_name);
}

inline void encode(ByteWriter& writer, const TradeCancelRequest&) { (void)writer; }

inline bool decode(ByteReader& reader, TradeCancelRequest&) {
  (void)reader;
  return true;
}

inline void encode(ByteWriter& writer, const TradeAddItemRequest& value) {
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, TradeAddItemRequest& value) {
  return reader.read_i32(value.item_make_index) && reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const TradeRemoveItemRequest& value) {
  writer.write_i32(value.item_make_index);
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, TradeRemoveItemRequest& value) {
  return reader.read_i32(value.item_make_index) && reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const TradeSetGoldRequest& value) {
  writer.write_i32(value.gold);
}

inline bool decode(ByteReader& reader, TradeSetGoldRequest& value) {
  return reader.read_i32(value.gold);
}

inline void encode(ByteWriter& writer, const TradeAcceptRequest&) { (void)writer; }

inline bool decode(ByteReader& reader, TradeAcceptRequest&) {
  (void)reader;
  return true;
}

inline void encode(ByteWriter& writer, const TradeState& value) {
  writer.write_bool(value.visible);
  writer.write_string(value.remote_name);
  writer.write_vector(value.local_items.size(), [&](const std::uint16_t index) {
    encode(writer, value.local_items[index]);
  });
  writer.write_vector(value.remote_items.size(), [&](const std::uint16_t index) {
    encode(writer, value.remote_items[index]);
  });
  writer.write_i32(value.local_gold);
  writer.write_i32(value.remote_gold);
  writer.write_bool(value.local_accept);
  writer.write_bool(value.remote_accept);
}

inline bool decode(ByteReader& reader, TradeState& value) {
  value.local_items.clear();
  value.remote_items.clear();
  return reader.read_bool(value.visible) && reader.read_string(value.remote_name) &&
         reader.read_vector([&](std::uint16_t) {
           ItemSlotState item;
           if (!decode(reader, item)) {
             return false;
           }
           value.local_items.push_back(std::move(item));
           return true;
         }) &&
         reader.read_vector([&](std::uint16_t) {
           ItemSlotState item;
           if (!decode(reader, item)) {
             return false;
           }
           value.remote_items.push_back(std::move(item));
           return true;
         }) &&
         reader.read_i32(value.local_gold) && reader.read_i32(value.remote_gold) &&
         reader.read_bool(value.local_accept) && reader.read_bool(value.remote_accept);
}

inline void encode(ByteWriter& writer, const GuildOpenRequest&) { (void)writer; }

inline bool decode(ByteReader& reader, GuildOpenRequest&) {
  (void)reader;
  return true;
}

inline void encode(ByteWriter& writer, const GuildHomeRequest&) { (void)writer; }

inline bool decode(ByteReader& reader, GuildHomeRequest&) {
  (void)reader;
  return true;
}

inline void encode(ByteWriter& writer, const GuildMemberListRequest&) { (void)writer; }

inline bool decode(ByteReader& reader, GuildMemberListRequest&) {
  (void)reader;
  return true;
}

inline void encode(ByteWriter& writer, const GuildAddMemberRequest& value) {
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, GuildAddMemberRequest& value) {
  return reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const GuildRemoveMemberRequest& value) {
  writer.write_string(value.name);
}

inline bool decode(ByteReader& reader, GuildRemoveMemberRequest& value) {
  return reader.read_string(value.name);
}

inline void encode(ByteWriter& writer, const GuildUpdateNoticeRequest& value) {
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, GuildUpdateNoticeRequest& value) {
  return reader.read_string(value.text);
}

inline void encode(ByteWriter& writer, const GuildUpdateGradeRequest& value) {
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, GuildUpdateGradeRequest& value) {
  return reader.read_string(value.text);
}

inline void encode(ByteWriter& writer, const GuildMemberState& value) {
  writer.write_string(value.name);
  writer.write_string(value.rank);
  writer.write_bool(value.online);
}

inline bool decode(ByteReader& reader, GuildMemberState& value) {
  return reader.read_string(value.name) && reader.read_string(value.rank) &&
         reader.read_bool(value.online);
}

inline void encode(ByteWriter& writer, const GuildState& value) {
  writer.write_bool(value.visible);
  writer.write_string(value.guild_name);
  writer.write_string(value.rank_name);
  writer.write_string(value.notice);
  writer.write_vector(value.members.size(), [&](const std::uint16_t index) {
    encode(writer, value.members[index]);
  });
  writer.write_vector(value.ranks.size(), [&](const std::uint16_t index) {
    writer.write_string(value.ranks[index]);
  });
  writer.write_bool(value.can_admin);
}

inline bool decode(ByteReader& reader, GuildState& value) {
  value.members.clear();
  value.ranks.clear();
  return reader.read_bool(value.visible) && reader.read_string(value.guild_name) &&
         reader.read_string(value.rank_name) && reader.read_string(value.notice) &&
         reader.read_vector([&](std::uint16_t) {
           GuildMemberState member;
           if (!decode(reader, member)) {
             return false;
           }
           value.members.push_back(std::move(member));
           return true;
         }) &&
         reader.read_vector([&](std::uint16_t) {
           std::string rank;
           if (!reader.read_string(rank)) {
             return false;
           }
           value.ranks.push_back(std::move(rank));
           return true;
         }) &&
         reader.read_bool(value.can_admin);
}

inline void encode(ByteWriter& writer, const SysMessage& value) {
  writer.write_string(value.text);
  writer.write_u8(value.level);
}

inline bool decode(ByteReader& reader, SysMessage& value) {
  return reader.read_string(value.text) && reader.read_u8(value.level);
}

inline void encode(ByteWriter& writer, const Notice& value) {
  writer.write_string(value.title);
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, Notice& value) {
  return reader.read_string(value.title) && reader.read_string(value.text);
}

inline void encode(ByteWriter& writer, const Ping& value) { writer.write_u64(value.client_time_ms); }

inline bool decode(ByteReader& reader, Ping& value) { return reader.read_u64(value.client_time_ms); }

inline void encode(ByteWriter& writer, const Pong& value) {
  writer.write_u64(value.client_time_ms);
  writer.write_u64(value.server_time_ms);
}

inline bool decode(ByteReader& reader, Pong& value) {
  return reader.read_u64(value.client_time_ms) && reader.read_u64(value.server_time_ms);
}

inline void encode(ByteWriter& writer, const DisconnectReason& value) {
  writer.write_u16(value.code);
  writer.write_string(value.text);
}

inline bool decode(ByteReader& reader, DisconnectReason& value) {
  return reader.read_u16(value.code) && reader.read_string(value.text);
}

// ====================================================================
// 载荷编码/解码辅助函数
// ====================================================================

/// 将消息编码为字节向量（载荷部分，不含帧头）
template <typename T>
std::vector<std::uint8_t> encode_payload(const T& value) {
  ByteWriter writer;
  encode(writer, value);
  return std::move(writer).take_buffer();
}

/// 从字节向量解码消息（载荷部分，不含帧头）
template <typename T>
std::optional<T> decode_payload(std::span<const std::uint8_t> bytes) {
  ByteReader reader(bytes);
  T value{};
  if (!decode(reader, value) || !reader.finished()) {
    return std::nullopt;
  }
  return value;
}

// ====================================================================
// 帧编码/解码
// 帧格式：[4 字节长度][2 字节 MessageId][2 字节 flags]
//          [4 字节 sequence][可选 legacy bundle header][可变长度 payload]
// 长度字段 = sizeof(u16) + sizeof(u16) + sizeof(u32) + optional_header + payload_size
// ====================================================================

/// 将 Frame 结构编码为线缆字节流（小端序）
inline std::vector<std::uint8_t> encode_frame(const Frame& frame) {
  std::vector<std::uint8_t> bytes;
  const auto payload_size = static_cast<std::uint32_t>(frame.payload.size());
  constexpr auto legacy_bundle_header_size =
      sizeof(std::uint64_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) +
      sizeof(std::uint16_t) + sizeof(std::uint8_t);
  const auto bundle_header_size =
      frame.legacy_bundle.has_value() ? legacy_bundle_header_size : 0U;
  const auto length = static_cast<std::uint32_t>(sizeof(std::uint16_t) + sizeof(std::uint16_t) +
                                                 sizeof(std::uint32_t) + bundle_header_size +
                                                 payload_size);
  bytes.reserve(sizeof(std::uint32_t) + length);
  auto append_u16 = [&](std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  };
  auto append_u32 = [&](std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  };
  auto append_u64 = [&](std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
      bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
  };
  append_u32(length);
  append_u16(static_cast<std::uint16_t>(frame.message_id));
  const auto wire_flags =
      frame.legacy_bundle.has_value()
          ? static_cast<std::uint16_t>(frame.flags | kFrameFlagLegacyBundle)
          : static_cast<std::uint16_t>(frame.flags & ~kFrameFlagLegacyBundle);
  append_u16(wire_flags);
  append_u32(frame.sequence);
  if (frame.legacy_bundle.has_value()) {
    append_u64(frame.legacy_bundle->bundle_id);
    append_u16(frame.legacy_bundle->bundle_index);
    append_u16(frame.legacy_bundle->bundle_count);
    append_u16(frame.legacy_bundle->legacy_ident);
    bytes.push_back(static_cast<std::uint8_t>(frame.legacy_bundle->bundle_mode));
  }
  bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
  return bytes;
}

/// 从 TCP 缓冲区中提取完整的帧
/// 处理粘包问题：一次调用可能提取 0、1 或多个帧
/// 未完成的帧数据保留在缓冲区中等待更多数据到达
inline std::vector<Frame> drain_frames(std::vector<std::uint8_t>& buffer) {
  std::vector<Frame> frames;
  std::size_t offset = 0;
  auto read_u16 = [](const std::uint8_t* bytes) -> std::uint16_t {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
  };
  auto read_u32 = [](const std::uint8_t* bytes) -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  };
  auto read_u64 = [](const std::uint8_t* bytes) -> std::uint64_t {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
      value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
  };

  while (buffer.size() - offset >= sizeof(std::uint32_t)) {
    const auto* base = buffer.data() + offset;
    const auto length = read_u32(base);
    constexpr auto fixed_header_size =
        sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint32_t);
    constexpr auto legacy_bundle_header_size =
        sizeof(std::uint64_t) + sizeof(std::uint16_t) + sizeof(std::uint16_t) +
        sizeof(std::uint16_t) + sizeof(std::uint8_t);
    if (length < fixed_header_size) {
      buffer.clear();
      return {};
    }
    if (buffer.size() - offset < sizeof(std::uint32_t) + length) {
      break;
    }

    Frame frame;
    frame.message_id = static_cast<MessageId>(read_u16(base + 4));
    frame.flags = read_u16(base + 6);
    frame.sequence = read_u32(base + 8);
    auto payload_offset =
        offset + sizeof(std::uint32_t) + fixed_header_size;
    if ((frame.flags & kFrameFlagLegacyBundle) != 0U) {
      if (length < fixed_header_size + legacy_bundle_header_size) {
        buffer.clear();
        return {};
      }
      const auto* bundle = buffer.data() + payload_offset;
      frame.legacy_bundle = LegacyBundleMeta{
          read_u64(bundle),
          read_u16(bundle + 8),
          read_u16(bundle + 10),
          read_u16(bundle + 12),
          static_cast<LegacyBundleMode>(bundle[14])};
      payload_offset += legacy_bundle_header_size;
    }
    const auto payload_size =
        static_cast<std::size_t>(length) - (payload_offset - offset - sizeof(std::uint32_t));
    frame.payload.assign(buffer.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                         buffer.begin() +
                             static_cast<std::ptrdiff_t>(payload_offset + payload_size));
    frames.push_back(std::move(frame));
    offset += sizeof(std::uint32_t) + length;
  }

  if (offset > 0) {
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  return frames;
}

/// 将消息类型编码为 Frame（编译期类型安全）
template <typename T>
Frame make_frame(const T& message, std::uint32_t sequence, std::uint16_t flags = 0,
                 std::optional<LegacyBundleMeta> legacy_bundle = std::nullopt) {
  return Frame{MessageTraits<T>::kMessageId, flags, sequence, encode_payload(message),
               std::move(legacy_bundle)};
}

/// 从 Frame 解码为指定消息类型（编译期类型安全）
template <typename T>
std::optional<T> decode_message(const Frame& frame) {
  if (frame.message_id != MessageTraits<T>::kMessageId) {
    return std::nullopt;
  }
  return decode_payload<T>(frame.payload);
}

/// 从 Frame 解码为 Message 变体（运行期多态分发）
/// 根据 frame.message_id 切换到对应的 decode_message<T>
/// 覆盖所有 client_v1 消息类型
inline std::optional<Message> decode_any(const Frame& frame) {
  switch (frame.message_id) {
    case MessageId::client_hello:
      if (auto value = decode_message<ClientHello>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::login_request:
      if (auto value = decode_message<LoginRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::login_result:
      if (auto value = decode_message<LoginResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::create_account_request:
      if (auto value = decode_message<CreateAccountRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::create_account_result:
      if (auto value = decode_message<CreateAccountResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::change_password_request:
      if (auto value = decode_message<ChangePasswordRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::change_password_result:
      if (auto value = decode_message<ChangePasswordResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::update_account_request:
      if (auto value = decode_message<UpdateAccountRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::update_account_result:
      if (auto value = decode_message<UpdateAccountResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::need_update_account:
      if (auto value = decode_message<NeedUpdateAccount>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::server_list:
      if (auto value = decode_message<ServerList>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::select_server_request:
      if (auto value = decode_message<SelectServerRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::select_server_result:
      if (auto value = decode_message<SelectServerResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::character_list_request:
      if (auto value = decode_message<CharacterListRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::character_list:
      if (auto value = decode_message<CharacterList>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::create_character_request:
      if (auto value = decode_message<CreateCharacterRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::create_character_result:
      if (auto value = decode_message<CreateCharacterResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::delete_character_request:
      if (auto value = decode_message<DeleteCharacterRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::delete_character_result:
      if (auto value = decode_message<DeleteCharacterResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::select_character_request:
      if (auto value = decode_message<SelectCharacterRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::select_character_result:
      if (auto value = decode_message<SelectCharacterResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::enter_world_request:
      if (auto value = decode_message<EnterWorldRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::enter_world_result:
      if (auto value = decode_message<EnterWorldResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::world_snapshot:
      if (auto value = decode_message<WorldSnapshot>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::world_clear_objects:
      if (auto value = decode_message<WorldClearObjects>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::map_change:
      if (auto value = decode_message<MapChange>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::map_door_state:
      if (auto value = decode_message<MapDoorState>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::map_entered:
      if (auto value = decode_message<MapEntered>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_identity_update:
      if (auto value = decode_message<ActorIdentityUpdate>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::map_description:
      if (auto value = decode_message<MapDescription>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_state_delta:
      if (auto value = decode_message<ActorStateDelta>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::login_notice:
      if (auto value = decode_message<LoginNotice>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::login_notice_ok:
      if (auto value = decode_message<LoginNoticeOk>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_upsert:
      if (auto value = decode_message<ActorUpsert>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_remove:
      if (auto value = decode_message<ActorRemove>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_action:
      if (auto value = decode_message<ActorAction>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_magic_fire:
      if (auto value = decode_message<ActorMagicFire>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_magic_fire_fail:
      if (auto value = decode_message<ActorMagicFireFail>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_vitals:
      if (auto value = decode_message<ActorVitals>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_death:
      if (auto value = decode_message<ActorDeath>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::magic_list:
      if (auto value = decode_message<MagicList>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::self_ability:
      if (auto value = decode_message<SelfAbility>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::self_ability_detail:
      if (auto value = decode_message<SelfAbilityDetail>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::mini_map_request:
      if (auto value = decode_message<MiniMapRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::mini_map_data:
      if (auto value = decode_message<MiniMapData>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::move_intent:
      if (auto value = decode_message<MoveIntent>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::action_intent:
      if (auto value = decode_message<ActionIntent>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::spell_intent:
      if (auto value = decode_message<SpellIntent>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::action_ack:
      if (auto value = decode_message<ActionAck>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::pickup_intent:
      if (auto value = decode_message<PickupIntent>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::use_item_intent:
      if (auto value = decode_message<UseItemIntent>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::bag_snapshot:
      if (auto value = decode_message<BagSnapshot>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::inventory_add:
      if (auto value = decode_message<InventoryAdd>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::inventory_update:
      if (auto value = decode_message<InventoryUpdate>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::inventory_remove:
      if (auto value = decode_message<InventoryRemove>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::inventory_clear_range:
      if (auto value = decode_message<InventoryClearRange>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::equipment_snapshot:
      if (auto value = decode_message<EquipmentSnapshot>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::equip_item_request:
      if (auto value = decode_message<EquipItemRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::unequip_item_request:
      if (auto value = decode_message<UnequipItemRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::drop_item_request:
      if (auto value = decode_message<DropItemRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::revive_request:
      if (auto value = decode_message<ReviveRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::drop_gold_request:
      if (auto value = decode_message<DropGoldRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::durability_change:
      if (auto value = decode_message<DurabilityChange>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::magic_key_change_request:
      if (auto value = decode_message<MagicKeyChangeRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::ground_item_add:
      if (auto value = decode_message<GroundItemAdd>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::ground_item_remove:
      if (auto value = decode_message<GroundItemRemove>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::use_item_result:
      if (auto value = decode_message<UseItemResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::chat_send:
      if (auto value = decode_message<ChatSend>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::chat_line:
      if (auto value = decode_message<ChatLine>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::actor_say:
      if (auto value = decode_message<ActorSay>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::npc_click_request:
      if (auto value = decode_message<NpcClickRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::npc_dialog:
      if (auto value = decode_message<NpcDialog>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::npc_dialog_select_request:
      if (auto value = decode_message<NpcDialogSelectRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::npc_dialog_close:
      if (auto value = decode_message<NpcDialogClose>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_goods_list:
      if (auto value = decode_message<MerchantGoodsList>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_buy_request:
      if (auto value = decode_message<MerchantBuyRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_sell_request:
      if (auto value = decode_message<MerchantSellRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_sell_price_request:
      if (auto value = decode_message<MerchantSellPriceRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_price_result:
      if (auto value = decode_message<MerchantPriceResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_repair_price_request:
      if (auto value = decode_message<MerchantRepairPriceRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_repair_request:
      if (auto value = decode_message<MerchantRepairRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::merchant_repair_price_result:
      if (auto value = decode_message<MerchantRepairPriceResult>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::storage_list:
      if (auto value = decode_message<StorageList>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::storage_deposit_request:
      if (auto value = decode_message<StorageDepositRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::storage_withdraw_request:
      if (auto value = decode_message<StorageWithdrawRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::group_mode_request:
      if (auto value = decode_message<GroupModeRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::group_create_request:
      if (auto value = decode_message<GroupCreateRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::group_add_member_request:
      if (auto value = decode_message<GroupAddMemberRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::group_remove_member_request:
      if (auto value = decode_message<GroupRemoveMemberRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::group_state:
      if (auto value = decode_message<GroupState>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_try_request:
      if (auto value = decode_message<TradeTryRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_cancel_request:
      if (auto value = decode_message<TradeCancelRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_add_item_request:
      if (auto value = decode_message<TradeAddItemRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_remove_item_request:
      if (auto value = decode_message<TradeRemoveItemRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_set_gold_request:
      if (auto value = decode_message<TradeSetGoldRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_accept_request:
      if (auto value = decode_message<TradeAcceptRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::trade_state:
      if (auto value = decode_message<TradeState>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_open_request:
      if (auto value = decode_message<GuildOpenRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_home_request:
      if (auto value = decode_message<GuildHomeRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_member_list_request:
      if (auto value = decode_message<GuildMemberListRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_add_member_request:
      if (auto value = decode_message<GuildAddMemberRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_remove_member_request:
      if (auto value = decode_message<GuildRemoveMemberRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_update_notice_request:
      if (auto value = decode_message<GuildUpdateNoticeRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_update_grade_request:
      if (auto value = decode_message<GuildUpdateGradeRequest>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::guild_state:
      if (auto value = decode_message<GuildState>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::sys_message:
      if (auto value = decode_message<SysMessage>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::notice:
      if (auto value = decode_message<Notice>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::ping:
      if (auto value = decode_message<Ping>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::pong:
      if (auto value = decode_message<Pong>(frame); value.has_value()) return Message{*value};
      break;
    case MessageId::disconnect_reason:
      if (auto value = decode_message<DisconnectReason>(frame); value.has_value()) return Message{*value};
      break;
    default:
      break;
  }
  return std::nullopt;
}

/// 将 Message 变体编码为 Frame（运行期多态分发）
/// 使用 std::visit 自动匹配实际类型
inline Frame encode_any(const Message& message, std::uint32_t sequence, std::uint16_t flags = 0,
                        std::optional<LegacyBundleMeta> legacy_bundle = std::nullopt) {
  return std::visit(
      [&](const auto& value) { return make_frame(value, sequence, flags, legacy_bundle); }, message);
}

}  // namespace mir2::client_v1
