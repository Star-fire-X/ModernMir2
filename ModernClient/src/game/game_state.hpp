// ============================================================
// Mir2 现代客户端 — 游戏状态数据结构
// 职责：定义客户端所有游戏状态（玩家/世界/UI），
//       提供协议消息的 apply 方法将服务端数据同步到本地状态
//
// 架构说明：
// GameStateStore 是客户端的数据层核心，所有从服务端收到的
// 协议消息都通过 apply() 方法更新到此状态存储中。场景和 UI
// 则从中读取数据进行渲染和逻辑判断。
//
// 数据流向：
//   服务端 -> TCP -> ProtocolClient -> 协议事件 ->
//   ClientApp::handle_protocol_events() -> apply(message) ->
//   GameStateStore -> 场景/UI 读取 -> 渲染
//
// 与经典传奇客户端的差异：
// 原版 Delphi 客户端使用全局变量（如 Actor, UserState, Items
// 等记录类型）存储状态。本实现将这些分散的全局变量整合为
// 结构化的 GameStateStore，提供类型安全的访问方式。
// ============================================================
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "audio/audio_settings.hpp"
#include "shared/legacy/action_ids.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2::client {

namespace detail {

/// 获取单调时钟的毫秒时间戳
/// 用于帧间时间差计算、动画同步、冷却判断等
/// 使用 steady_clock 保证不受系统时间调整影响
inline std::uint64_t monotonic_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace detail

namespace legacy_sm {

constexpr std::uint16_t kTurn = 10;
constexpr std::uint16_t kDeath = 32;
constexpr std::uint16_t kNowDeath = 34;
constexpr std::uint16_t kStruck = 31;

}  // namespace legacy_sm

/// 客户端配置，从 client.ini 加载
/// 对应原传奇客户端的 Mir2.ini 配置文件
struct ClientConfig {
  /// 自动播放模式配置（用于测试/演示）
  /// 自动播放是传奇私服测试环境中常见的功能，
  /// 客户端可自动完成登录、创建账号、创建角色等流程
  struct AutoPlayConfig {
    bool enabled{false};
    bool create_account{true};       ///< 账号不存在时自动创建
    bool create_character{true};     ///< 无角色时自动创建
    std::string account_id{};
    std::string password{};
    std::string display_name{};
    std::string character_name{};
    std::uint8_t job{0};            ///< 职业（0=战士, 1=法师, 2=道士）
    std::uint8_t sex{0};            ///< 性别（0=男, 1=女）
    std::uint8_t hair{0};           ///< 发型编号
  };

  std::string login_host{"127.0.0.1"};   ///< 登录网关 IP 地址
  std::uint16_t login_port{5600};         ///< 登录网关端口（经典端口 5600）
  std::uint32_t client_build{1};          ///< 客户端构建号（版本握手用）
  std::uint32_t resource_revision{1};     ///< 资源修订号（WIL 文件版本）
  AutoPlayConfig auto_play{};
  AudioSettings audio{};                  ///< 音频开关与音量配置
  std::wstring asset_root{};              ///< 资源根目录（需含 Data/ 和 Map/）
  std::wstring window_title{L"Modern Mir2 Client"};
  int window_width{800};
  int window_height{600};
};

/// 角色（Actor）运行时状态
/// 包含坐标、动作、血量等所有与服务端同步的动态数据
/// 坐标以瓦片（tile）为单位，与经典客户端的 MapUnit 坐标一致
struct ActorState {
  std::uint64_t actor_id{0};
  std::string name{};
  int x{0};              ///< 当前 X 坐标（瓦片单位，对应游戏的逻辑坐标）
  int y{0};              ///< 当前 Y 坐标（瓦片单位）
  int from_x{0};         ///< 移动起始 X（用于帧间平滑插值）
  int from_y{0};         ///< 移动起始 Y
  std::uint8_t dir{0};   ///< 面向方向（0-7：0=上, 1=右上, 2=右, ..., 7=左上）
  std::int32_t feature{0};  ///< 外观特征编码（包含 race/dress/weapon/hair 编码在一个 32 位值中）
  std::int32_t status{0};
  client_v1::ActorType actor_type{client_v1::ActorType::player};  ///< 角色类型（玩家/怪物/NPC）
  std::uint64_t move_started_ms{0};     ///< 移动开始的时间戳（用于插值计算）
  std::uint64_t move_duration_ms{0};    ///< 移动持续时间（行走 180ms，跑步 140ms）
  bool running{false};                  ///< 是否在跑步（跑步比行走快，但增加饥饿度）
  client_v1::ActorActionKind current_action{client_v1::ActorActionKind::turn};
  std::uint16_t legacy_action_ident{0}; ///< 旧版动作标识（兼容 Delphi 客户端的动作编号）
  std::uint16_t magic_id{0};            ///< 当前施法的魔法 ID
  std::uint64_t action_target_actor_id{0};
  int action_target_x{-1};
  int action_target_y{-1};
  bool action_magic{false};             ///< 当前动作是否为魔法
  int action_magic_effect{0};
  int action_magic_effect_type{-1};
  bool action_magic_failed{false};
  std::uint64_t action_started_ms{0};
  std::uint64_t action_duration_ms{0};
  int legacy_old_x{0};
  int legacy_old_y{0};
  std::uint8_t legacy_old_dir{0};
  bool legacy_has_old_position{false};
  std::uint64_t legacy_event_sequence{0};
  std::vector<ActorState> legacy_pending_actions{};
  bool dead{false};
  bool skeleton{false};                ///< 死亡后是否已变为骨架
  int hp{-1};
  int max_hp{-1};
  int mp{-1};
  int max_mp{-1};
  int last_damage{0};                   ///< 最近一次受到的伤害值
  std::uint64_t last_hitter_id{0};      ///< 最近一次攻击者的 actor_id
  bool last_damage_magic{false};        ///< 最近一次伤害是否为魔法伤害
  std::string saying{};                 ///< 头顶说话内容（对应 TActor.Saying）
  std::uint32_t saying_fore_color{0xFFFFFFFFU};
  std::uint32_t saying_back_color{0x00000000U};
  std::uint64_t saying_started_ms{0};   ///< 头顶说话开始时间，4 秒后隐藏
  std::uint32_t name_color{0xFFFFFFFFU};
  bool pending_remove{false};
};

/// 魔法快捷键状态
/// 对应原传奇客户端的 Magic 数组，每个魔法绑定到 F1-F8 快捷键
struct MagicShortcutState {
  std::uint16_t magic_id{0};
  std::uint8_t key{0};     ///< 快捷键键位（1-8 对应 F1-F8，0 表示未绑定）
  std::uint8_t level{0};   ///< 技能等级（0-3，最高 3 级）
  int train{0};            ///< 熟练度（当前经验值）
  int delay_ms{0};         ///< 施法延迟（毫秒，冷却时间）
  std::string name{};
  int effect{0};            ///< 魔法图标索引基数（Delphi WMagIcon[effect*2]）
  int max_train{0};         ///< 当前等级熟练度上限
  int effect_type{0};
};

/// 模态对话框状态
struct ModalState {
  bool visible{false};
  std::wstring title{};
  std::wstring message{};
};

constexpr int kMaxBagItems = 46;        ///< 背包最大格子数（对应 Delphi MAXBAGITEM = 46）
constexpr int kEquipmentSlotCount = 13; ///< 装备栏槽位数（对应 Delphi UseItems[0..12]）

/// 正在拖动的物品来源类型
enum class MovingItemSource {
  none,
  bag,        ///< 从背包中拖出的物品
  equipment   ///< 从装备栏中拖出的物品
};

/// 正在拖动的物品状态
/// 对应原传奇客户端的 ItemMoving 全局变量
struct MovingItemState {
  bool active{false};
  MovingItemSource source{MovingItemSource::none};
  int source_slot{-1};
  client_v1::ItemState item{};
};

enum class PendingItemActionKind {
  none,
  use,
  equip,
  unequip,
  drop
};

struct PendingItemActionState {
  bool active{false};
  PendingItemActionKind kind{PendingItemActionKind::none};
  MovingItemSource source{MovingItemSource::none};
  int source_slot{-1};
  int target_slot{-1};
  client_v1::ItemState item{};
  std::uint64_t started_ms{0};
};

/// 聊天板行：对应 Delphi DScreen.ChatStrs + ChatBks
struct ChatLineState {
  std::string text{};
  std::uint32_t fore_color{0xFFFFFFFFU};
  std::uint32_t back_color{0x00000000U};
};

/// NPC 对话状态：对应 Delphi DMerchantDlg / CurMerchant / MDlgX / MDlgY
struct NpcDialogState {
  bool visible{false};
  std::uint64_t merchant_id{0};
  std::int32_t face{0};
  std::string npc_name{};
  std::string text{};
  int opened_x{-1};
  int opened_y{-1};
};

/// 商店状态：对应 Delphi DMenuDlg / DSellDlg 的 P1 最小买卖闭环
struct MerchantShopState {
  bool visible{false};
  std::uint64_t merchant_id{0};
  std::vector<client_v1::MerchantGoodsItem> goods{};
  int page{0};
  bool sell_selecting{false};
  std::int32_t pending_sell_make_index{0};
  std::string pending_sell_name{};
  std::int32_t pending_sell_price{0};
};

/// 修理状态：对应 DSellDlg(dmRepair) 的询价/确认闭环
struct RepairState {
  bool selecting{false};
  bool dialog_visible{false};
  std::uint64_t merchant_id{0};
  std::int32_t pending_make_index{0};
  std::string pending_name{};
  std::int32_t pending_price{0};
};

/// 仓库状态：Delphi 使用 DMenuDlg + DSellDlg(dmStorage)，这里保留列表和存入选择态
struct StorageState {
  bool visible{false};
  bool deposit_selecting{false};
  std::uint64_t merchant_id{0};
  std::vector<client_v1::ItemState> items{};
  int selected_index{-1};
};

/// 组队窗口状态
struct GroupUiState {
  bool visible{false};
  bool allow_group{false};
  std::vector<std::string> members{};
};

/// 交易窗口状态
struct TradeUiState {
  bool visible{false};
  std::string remote_name{};
  std::vector<client_v1::ItemSlotState> local_items{};
  std::vector<client_v1::ItemSlotState> remote_items{};
  std::int32_t local_gold{0};
  std::int32_t remote_gold{0};
  bool local_accept{false};
  bool remote_accept{false};
};

/// 行会窗口状态
struct GuildUiState {
  bool visible{false};
  std::string guild_name{};
  std::string rank_name{};
  std::string notice{};
  std::vector<client_v1::GuildMemberState> members{};
  std::vector<std::string> ranks{};
  bool can_admin{false};
};

/// 小地图窗口状态：由 MiniMapData 协议驱动
struct MiniMapViewState {
  bool visible{false};
  bool loaded{false};
  std::string map_id{};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::vector<std::uint8_t> pixels{};
  std::string error_message{};
};

/// 旧版行走动作枚举（兼容 Delphi 客户端）
enum class LegacyChrAction {
  none,
  walk,
  run
};

/// 登录状态枚举
/// 对应原传奇客户端的登录窗口各状态
enum class LoginState {
  lsLogin,       ///< 登录界面（账号密码输入）
  lsNewid,       ///< 注册新账号（填写资料）
  lsNewidRetry,  ///< 注册失败后重试（保留已填内容）
  lsChgpw,       ///< 修改密码
  lsCloseAll     ///< 关闭客户端
};

/// 登录界面状态
/// 包含账号名、密码、资料、状态提示等
struct LoginViewState {
  std::string account_id{};
  std::string password{};
  client_v1::AccountProfile account_profile{};  ///< 账号资料（注册时的详细信息）
  bool needs_account_update{false};              ///< 是否需要补充/更新资料
  LoginState login_state{LoginState::lsLogin};
  std::wstring status{};  ///< 状态文本（显示在界面底部的提示信息）
  bool request_pending{false};
};

/// 游戏大厅状态（选服/选角）
/// 对应原传奇客户端中选服和选角界面的数据
struct LobbyViewState {
  std::vector<client_v1::ServerEntry> servers{};       ///< 可用服务器列表
  std::vector<client_v1::CharacterSummary> characters{}; ///< 当前账号的角色列表
  int selected_server_index{0};
  std::string selected_server_name{};
  int selected_index{0};  ///< 当前选中的角色索引
  bool server_select_pending{false};
  bool character_list_pending{false};
  bool create_character_pending{false};
  bool delete_character_pending{false};
  bool enter_character_pending{false};
};

struct MapDoorRuntimeState {
  bool open{false};
  std::uint64_t updated_ms{0};
};

/// 世界视图状态：包含所有动态游戏数据
/// 这是客户端中最大、最核心的状态结构，对应原传奇客户端中的
/// 大量全局变量（Actor列表、物品列表、魔法列表、战斗状态等）
struct WorldViewState {
  std::string map_id{"0"};
  int width{800};   ///< 地图宽度（瓦片数）
  int height{600};  ///< 地图高度（瓦片数）
  bool map_transition_pending{false};
  std::uint64_t self_actor_id{0};                                 ///< 自己的 actor_id
  std::unordered_map<std::uint64_t, ActorState> actors{};         ///< 所有在线角色（key = actor_id）
  std::unordered_map<std::uint64_t, client_v1::GroundItemState> ground_items{};  ///< 地面物品
  std::unordered_map<std::uint64_t, MapDoorRuntimeState> map_doors{};  ///< 动态门状态（key = x/y）
  std::vector<std::uint64_t> actor_draw_order{};       ///< Delphi ActorList-equivalent draw order
  std::vector<std::uint64_t> ground_item_draw_order{}; ///< Delphi DropedItemList-equivalent draw order
  std::vector<std::string> sys_messages{};    ///< 系统消息队列（黄色文字，显示在聊天框）
  std::vector<ChatLineState> chat_lines{};     ///< 底部聊天板，最多 200 行
  int chat_board_top{0};                       ///< 聊天板顶部可见行
  std::string whisper_name{};                  ///< 最近私聊对象
  NpcDialogState npc_dialog{};                 ///< 当前 NPC/商人对话
  std::vector<MagicShortcutState> magics{};   ///< 已习得魔法列表
  client_v1::SelfAbility self_ability{};       ///< 主角 HUD 能力摘要
  client_v1::SelfAbilityDetail self_ability_detail{}; ///< 状态窗口完整能力摘要
  MerchantShopState merchant_shop{};           ///< 当前 NPC 商店列表/出售状态
  RepairState repair{};                        ///< 当前修理窗口状态
  StorageState storage{};                      ///< 当前仓库窗口状态
  GroupUiState group{};                        ///< 当前组队窗口状态
  TradeUiState trade{};                        ///< 当前交易窗口状态
  GuildUiState guild{};                        ///< 当前行会窗口状态
  MiniMapViewState minimap{};                  ///< 小地图窗口状态
  std::array<client_v1::ItemState, kMaxBagItems> bag_items{};             ///< 背包物品镜像（46 格）
  std::array<client_v1::ItemState, kEquipmentSlotCount> equipment{};      ///< 装备栏镜像（13 格）
  MovingItemState moving_item{};                                          ///< 拖拽中的物品（对应 Delphi ItemMoving）
  MovingItemState waiting_item{};                                         ///< 待服务端确认的物品操作
  std::uint64_t waiting_item_started_ms{0};                               ///< 待确认物品操作开始时间
  PendingItemActionState pending_item_action{};                           ///< 等待服务端确认的物品动作
  int hovered_bag_slot{-1};                ///< 鼠标悬停的背包格子索引
  int hovered_equipment_slot{-1};          ///< 鼠标悬停的装备槽位索引
  int selected_bag_slot{-1};               ///< 选中的背包格子索引
  bool action_locked{false};               ///< 动作锁定（等待服务端确认期间不可执行新动作）
  std::uint64_t action_lock_started_ms{0}; ///< 动作锁定开始时间
  std::uint64_t last_action_ack_ms{0};     ///< 最近一次动作确认时间
  bool last_action_ack_ok{true};           ///< 上次动作确认结果（false 表示被服务端拒绝）
  bool action_fail_lock{false};
  std::uint16_t fail_action_ident{0};
  std::uint8_t fail_dir{0};
  std::uint64_t fail_action_time_ms{0};
  std::uint16_t last_sent_action_ident{0};
  std::uint8_t last_sent_action_dir{0};
  std::uint64_t dizzy_delay_start_ms{0};
  std::uint64_t dizzy_delay_time_ms{0};
  int skip_tick{0};
  int move_slow_level{0};
  bool move_slow{false};
  bool attack_slow{false};
  std::uint64_t latest_struck_ms{0};       ///< 最近一次被击中的时间戳
  std::uint64_t latest_spell_ms{0};        ///< 最近一次施法时间戳
  std::uint64_t magic_pk_delay_ms{300};    ///< 魔法/PK 最小间隔（毫秒，防速攻）
  int legacy_target_x{-1};                 ///< 旧版行走目标 X（用于旧版点击移动）
  int legacy_target_y{-1};
  LegacyChrAction legacy_chr_action{LegacyChrAction::none};  ///< 旧版行走动作
  std::uint64_t focus_actor_id{0};            ///< 鼠标悬停的角色 ID（显示名字）
  std::uint64_t focus_ground_item_id{0};      ///< 鼠标悬停的地面物品 ID
  std::uint64_t target_actor_id{0};           ///< 当前锁定的目标
  std::uint64_t pending_pickup_item_id{0};    ///< 待拾取物品 ID（点击地面物品后设置）
  int action_key{-1};                         ///< 按下的动作键（0-7 对应 F1-F8 快捷键）
  int run_ready_count{0};                     ///< 连续跑步计数（旧版跑步机制，连续走 N 步后进入跑步状态）
  std::uint64_t last_attack_ms{0};            ///< 上次攻击时间
  std::uint64_t latest_hit_ms{0};             ///< 最近一次命中时间
  std::uint64_t last_move_ms{0};              ///< 上次移动时间
  std::uint64_t mouse_down_ms{0};             ///< 鼠标按下时间戳（用于长按检测）
  std::uint64_t last_pickup_ms{0};            ///< 上次拾取时间
  std::int32_t eating_item_make_index{0};     ///< 正在使用的物品 MakeIndex
  std::int32_t eating_item_slot{-1};          ///< 正在使用的物品槽位
  std::uint64_t eat_time_ms{0};               ///< 使用物品的时间戳（用于冷却判断）
  bool last_use_item_ok{true};                ///< 上次使用物品是否成功
};

/// 计算从 then 到 now 的经过时间（毫秒）
/// 保证了 now < then 时返回 0（不会溢出）
inline std::uint64_t elapsed_ms(const std::uint64_t now, const std::uint64_t then) {
  return now >= then ? now - then : 0;
}

constexpr std::uint64_t kLegacyMapDoorOpenExpireMs = 6000U;

/// 检查动作锁定是否仍然有效
/// 锁定超过 10 秒自动解除（安全措施，避免锁死）
inline bool action_lock_active(const WorldViewState& world, const std::uint64_t now) {
  return world.action_locked && elapsed_ms(now, world.action_lock_started_ms) <= 10000U;
}

inline bool server_accept_next_action(WorldViewState& world, const std::uint64_t now) {
  if (!world.action_locked) {
    return true;
  }
  if (elapsed_ms(now, world.action_lock_started_ms) > 10000U) {
    world.action_locked = false;
  }
  return false;
}

inline void update_legacy_weight_slow(WorldViewState& world) {
  const auto& ability = world.self_ability_detail;
  world.move_slow = false;
  world.attack_slow = false;
  world.move_slow_level = 0;
  if (ability.max_weight > 0 && ability.weight > ability.max_weight) {
    world.move_slow_level += ability.weight / ability.max_weight;
    world.move_slow = true;
  }
  if (ability.max_wear_weight > 0 && ability.wear_weight > ability.max_wear_weight) {
    world.move_slow_level += ability.wear_weight / ability.max_wear_weight;
    world.move_slow = true;
  }
  if (ability.max_hand_weight > 0 && ability.hand_weight > ability.max_hand_weight) {
    world.attack_slow = true;
  }
}

inline bool legacy_move_skip_due_to_slow(WorldViewState& world) {
  if (world.move_slow && world.skip_tick < world.move_slow_level) {
    ++world.skip_tick;
    return true;
  }
  world.skip_tick = 0;
  return false;
}

/// 检查角色是否正在播放动作动画
/// 动作动画播放期间不能执行新动作（与传奇客户端的行为一致）
inline bool actor_action_animating(const ActorState& actor, const std::uint64_t now) {
  return actor.action_started_ms != 0 && actor.action_duration_ms != 0 &&
         elapsed_ms(now, actor.action_started_ms) < actor.action_duration_ms;
}

/// 检查角色是否可以执行下一个动作
/// 死亡或动作锁定或动画播放中均不可执行新动作
inline bool can_next_action(const WorldViewState& world, const ActorState& self,
                            const bool animation_idle, const std::uint64_t now) {
  if (self.dead || action_lock_active(world, now)) {
    return false;
  }
  if (world.dizzy_delay_time_ms != 0 &&
      elapsed_ms(now, world.dizzy_delay_start_ms) <= world.dizzy_delay_time_ms) {
    return false;
  }
  return animation_idle;
}

inline bool can_next_action(const WorldViewState& world, const ActorState& self,
                            const std::uint64_t now) {
  if (self.dead || action_lock_active(world, now)) {
    return false;
  }
  if (world.dizzy_delay_time_ms != 0 &&
      elapsed_ms(now, world.dizzy_delay_start_ms) <= world.dizzy_delay_time_ms) {
    return false;
  }
  return !actor_action_animating(self, now);
}

/// 检查角色是否可以发起下一次攻击
inline bool can_next_hit(const WorldViewState& world, const ActorState& self,
                         const std::uint64_t now) {
  if (self.dead) {
    return false;
  }
  auto level_fast = std::min(370, static_cast<int>(world.self_ability_detail.level) * 14);
  level_fast = std::min(800, level_fast + static_cast<int>(world.self_ability_detail.speed) * 60);
  auto next_hit = 1400 - level_fast;
  if (world.attack_slow) {
    next_hit += 1500;
  }
  next_hit = std::max(0, next_hit);
  return world.latest_hit_ms == 0 ||
         elapsed_ms(now, world.latest_hit_ms) > static_cast<std::uint64_t>(next_hit);
}

inline bool is_unlock_action(WorldViewState& world, const std::uint16_t action_ident,
                             const std::uint8_t dir, const std::uint64_t now) {
  if (world.action_fail_lock && action_ident == world.fail_action_ident &&
      dir == world.fail_dir && elapsed_ms(now, world.fail_action_time_ms) < 1000U) {
    return false;
  }
  world.action_fail_lock = false;
  return true;
}

inline void legacy_action_failed(WorldViewState& world, ActorState& self,
                                 const std::uint64_t now) {
  world.legacy_target_x = -1;
  world.legacy_target_y = -1;
  world.legacy_chr_action = LegacyChrAction::none;
  world.action_fail_lock = true;
  world.fail_action_ident = world.last_sent_action_ident;
  world.fail_dir = world.last_sent_action_dir;
  world.fail_action_time_ms = now;
  self.x = self.legacy_has_old_position ? self.legacy_old_x : self.from_x;
  self.y = self.legacy_has_old_position ? self.legacy_old_y : self.from_y;
  if (self.legacy_has_old_position) {
    self.dir = self.legacy_old_dir;
  }
  self.from_x = self.x;
  self.from_y = self.y;
  self.running = false;
  self.move_started_ms = 0;
  self.move_duration_ms = 0;
  self.current_action = client_v1::ActorActionKind::turn;
  self.action_started_ms = 0;
  self.action_duration_ms = 0;
  self.legacy_pending_actions.clear();
  self.legacy_has_old_position = false;
}

inline bool item_empty(const client_v1::ItemState& item) { return item.empty(); }

inline bool same_item_identity(const client_v1::ItemState& candidate,
                               const client_v1::ItemState& expected) {
  if (item_empty(candidate) || item_empty(expected)) {
    return false;
  }
  if (expected.make_index != 0) {
    return candidate.make_index == expected.make_index;
  }
  return candidate.name == expected.name;
}

inline bool valid_bag_slot(const int slot) { return slot >= 0 && slot < kMaxBagItems; }

inline bool valid_equipment_slot(const int slot) {
  return slot >= 0 && slot < kEquipmentSlotCount;
}

/// 登录公告视图状态
struct LoginNoticeViewState {
  std::string title{};
  std::string text{};
};

inline int legacy_visual_effect_type(const std::uint16_t magic_id, const int effect_type) {
  switch (magic_id) {
    case 9:
      return 5;   // mtFireGun
    case 10:
      return 6;   // mtLightingThunder
    case 11:
      return 7;   // mtThunder
    case 13:
    case 19:
      return 8;   // mtExploBujauk
    case 14:
    case 15:
      return 9;   // mtBujaukGroundEffect
    case 22:
      return 13;  // mtGroundEffect
    case 33:
      return 2;   // mtExplosion with magic-specific frame params
    default:
      return effect_type;
  }
}

/// 游戏状态主存储：包含所有子状态和协议消息的 apply 方法
/// 这是客户端数据层的中心点，所有状态变更都通过此结构
struct GameStateStore {
  LoginViewState login{};
  LobbyViewState lobby{};
  WorldViewState world{};
  LoginNoticeViewState login_notice{};
  ModalState modal{};
  std::string display_name{};          ///< 登录后显示的游戏内名称
  std::string enter_world_token{};     ///< 进入世界的令牌（服务端下发的身份凭证，用于验证）
  std::string selected_character{};    ///< 当前选中的角色名
  std::string pending_character_host{};///< 待连接的角色网关地址（选服后获得）
  std::uint16_t pending_character_port{0};
  std::string pending_lobby_token{};   ///< 选服后获得的 Lobby 令牌
  std::string pending_game_host{};     ///< 待连接的游戏网关地址
  std::uint16_t pending_game_port{0};
  std::uint64_t pending_self_actor_id{0};  ///< 进入世界后自己的 actor_id
  int pending_spawn_x{0};                  ///< 角色出生 X 坐标
  int pending_spawn_y{0};                  ///< 角色出生 Y 坐标

  [[nodiscard]] const MagicShortcutState* magic_for_id(const std::uint16_t magic_id) const {
    const auto it = std::find_if(world.magics.begin(), world.magics.end(),
                                 [magic_id](const MagicShortcutState& magic) {
                                   return magic.magic_id == magic_id;
                                 });
    return it == world.magics.end() ? nullptr : &*it;
  }

  void apply_magic_metadata(ActorState& actor, const std::uint16_t magic_id) const {
    if (const auto* magic = magic_for_id(magic_id); magic != nullptr) {
      if (actor.action_magic_effect <= 0) {
        actor.action_magic_effect = magic->effect;
      }
      actor.action_magic_effect_type = legacy_visual_effect_type(magic_id, magic->effect_type);
    }
  }

  /// 连接阶段：标识当前所处的网络连接流程
  /// 传奇客户端的网络连接分为几个独立的阶段，
  /// 每个阶段连接到不同的网关服务器
  enum class ConnectionPhase {
    login,              ///< 登录阶段（连接到 LoginGate）
    select_character,   ///< 选角阶段（连接到 SelGate）
    reselect_character, ///< 重新选角阶段（返回 SelGate）
    play                ///< 游戏中阶段（连接到 RunGate）
  };
  ConnectionPhase connection_phase{ConnectionPhase::login};
  std::uint64_t legacy_actor_event_sequence{0};

  void record_legacy_actor_event(ActorState& actor) {
    auto event = actor;
    event.legacy_pending_actions.clear();
    event.pending_remove = false;
    event.legacy_event_sequence = ++legacy_actor_event_sequence;
    actor.legacy_pending_actions.push_back(event);
    if (actor.legacy_pending_actions.size() > 32U) {
      actor.legacy_pending_actions.erase(actor.legacy_pending_actions.begin(),
                                         actor.legacy_pending_actions.end() - 32);
    }
    actor.legacy_event_sequence = event.legacy_event_sequence;
  }

  /// 根据动作类型返回动画持续时间（毫秒）
  /// 这些时间值基于对经典传奇客户端的逆向分析，
  /// 与 Delphi 客户端的动作帧播放速度一致
  static std::uint64_t action_duration_ms(client_v1::ActorActionKind kind,
                                          std::uint16_t legacy_ident) {
    switch (kind) {
      case client_v1::ActorActionKind::walk:
        return 540;    // 行走动画总时长
      case client_v1::ActorActionKind::run:
      case client_v1::ActorActionKind::rush:
      case client_v1::ActorActionKind::rush_kung:
        return 720;    // 跑步动画总时长
      case client_v1::ActorActionKind::backstep:
      case client_v1::ActorActionKind::knockback:
        return 540;
      case client_v1::ActorActionKind::hit:
        if (legacy_ident == legacy::kSmHeavyHit) {
          return 540;
        }
        return legacy_ident == legacy::kSmBigHit ? 560 : 510;
      case client_v1::ActorActionKind::spell:
        return 360;    // 施法动画
      case client_v1::ActorActionKind::struck:
        return 210;    // 受击后仰动画
      case client_v1::ActorActionKind::turn:
      default:
        return 200;    // 转身/待机
    }
  }

  static int estimated_legacy_text_width(const std::string& text) {
    auto width = 0;
    for (unsigned char ch : text) {
      width += ch >= 128 ? 7 : 7;
    }
    return width;
  }

  /// 写入底部聊天板，按 Delphi BOXWIDTH=374 做保守换行，最多保留 200 行
  void push_chat_line(std::string text, const std::uint32_t fore_color,
                      const std::uint32_t back_color) {
    constexpr int kChatBoxWidth = 374;
    constexpr int kViewChatLine = 9;
    constexpr std::size_t kMaxChatLines = 200;
    while (!text.empty()) {
      auto width = 0;
      std::size_t split = 0;
      for (; split < text.size(); ++split) {
        const auto ch = static_cast<unsigned char>(text[split]);
        width += ch >= 128 ? 7 : 7;
        if (width > kChatBoxWidth) {
          break;
        }
      }
      if (split == 0) {
        split = std::min<std::size_t>(1, text.size());
      }
      auto line = split < text.size() ? text.substr(0, split) : text;
      world.chat_lines.push_back(ChatLineState{std::move(line), fore_color, back_color});
      if (world.chat_lines.size() > kMaxChatLines) {
        world.chat_lines.erase(world.chat_lines.begin());
        if (static_cast<int>(world.chat_lines.size()) - world.chat_board_top < kViewChatLine) {
          world.chat_board_top = std::max(0, world.chat_board_top - 1);
        }
      } else if (static_cast<int>(world.chat_lines.size()) - world.chat_board_top >
                 kViewChatLine) {
        ++world.chat_board_top;
      }
      if (split >= text.size()) {
        break;
      }
      text = " " + text.substr(split);
    }
    world.chat_board_top =
        std::clamp(world.chat_board_top, 0,
                   std::max(0, static_cast<int>(world.chat_lines.size()) - 1));
  }

  void erase_actor(const std::uint64_t actor_id) {
    if (actor_id == 0 || actor_id == world.self_actor_id) {
      return;
    }
    world.actors.erase(actor_id);
    world.actor_draw_order.erase(std::remove(world.actor_draw_order.begin(),
                                             world.actor_draw_order.end(),
                                             actor_id),
                                 world.actor_draw_order.end());
    if (world.focus_actor_id == actor_id) {
      world.focus_actor_id = 0;
    }
    if (world.target_actor_id == actor_id) {
      world.target_actor_id = 0;
    }
    if (world.npc_dialog.merchant_id == actor_id) {
      world.npc_dialog = NpcDialogState{};
      world.merchant_shop = MerchantShopState{};
      world.repair = RepairState{};
      world.storage = StorageState{};
    }
    for (auto& entry : world.actors) {
      auto& actor = entry.second;
      if (actor.action_target_actor_id == actor_id) {
        actor.action_target_actor_id = 0;
      }
    }
  }

  void prune_pending_actor_removals(const std::uint64_t now_ms) {
    std::vector<std::uint64_t> removals;
    for (const auto& [actor_id, actor] : world.actors) {
      if (actor.pending_remove && !actor_action_animating(actor, now_ms)) {
        removals.push_back(actor_id);
      }
    }
    for (const auto actor_id : removals) {
      erase_actor(actor_id);
    }
  }

  void apply_actor_feature_changed(const std::uint64_t actor_id, const std::int32_t feature) {
    auto& actor = world.actors[actor_id];
    actor.actor_id = actor_id;
    actor.feature = feature;
  }

  void apply_actor_status_changed(const std::uint64_t actor_id, const std::int32_t status) {
    auto& actor = world.actors[actor_id];
    actor.actor_id = actor_id;
    actor.status = status;
  }

  void apply_actor_name_color_changed(const std::uint64_t actor_id,
                                      const std::uint32_t name_color) {
    auto& actor = world.actors[actor_id];
    actor.actor_id = actor_id;
    actor.name_color = name_color;
  }

  void close_npc_dialog() {
    world.npc_dialog = NpcDialogState{};
    world.merchant_shop = MerchantShopState{};
  }

  void clear_legacy_waiting_item() {
    world.waiting_item = MovingItemState{};
    world.waiting_item_started_ms = 0;
  }

  void clear_pending_item_action() {
    world.pending_item_action = PendingItemActionState{};
    clear_legacy_waiting_item();
  }

  void begin_pending_item_action(const PendingItemActionKind kind, const MovingItemSource source,
                                 const int source_slot, const int target_slot,
                                 const client_v1::ItemState& item,
                                 const std::uint64_t started_ms) {
    if (item_empty(item) || kind == PendingItemActionKind::none) {
      clear_pending_item_action();
      return;
    }
    world.pending_item_action =
        PendingItemActionState{true, kind, source, source_slot, target_slot, item, started_ms};
    world.waiting_item = MovingItemState{true, source, source_slot, item};
    world.waiting_item_started_ms = started_ms;
  }

  [[nodiscard]] bool pending_item_action_expired(
      const std::uint64_t now, const std::uint64_t timeout_ms = 3000U) const {
    return world.pending_item_action.active && world.pending_item_action.started_ms != 0 &&
           elapsed_ms(now, world.pending_item_action.started_ms) >= timeout_ms;
  }

  void remove_item_instance(const client_v1::ItemState& item) {
    if (item_empty(item)) {
      return;
    }
    for (auto& bag_item : world.bag_items) {
      if (same_item_identity(bag_item, item)) {
        bag_item = client_v1::ItemState{};
      }
    }
    for (auto& equipped : world.equipment) {
      if (same_item_identity(equipped, item)) {
        equipped = client_v1::ItemState{};
      }
    }
  }

  void restore_pending_item_action() {
    if (!world.pending_item_action.active) {
      return;
    }
    const auto pending = world.pending_item_action;
    clear_pending_item_action();
    remove_item_instance(pending.item);

    if (pending.source == MovingItemSource::bag && valid_bag_slot(pending.source_slot)) {
      auto& slot = world.bag_items[static_cast<std::size_t>(pending.source_slot)];
      if (item_empty(slot)) {
        slot = pending.item;
        return;
      }
    }
    if (pending.source == MovingItemSource::equipment &&
        valid_equipment_slot(pending.source_slot)) {
      auto& slot = world.equipment[static_cast<std::size_t>(pending.source_slot)];
      if (item_empty(slot)) {
        slot = pending.item;
        return;
      }
    }
    for (auto& slot : world.bag_items) {
      if (item_empty(slot)) {
        slot = pending.item;
        return;
      }
    }
  }

  void maybe_clear_pending_after_bag_slot(const int slot) {
    if (!world.pending_item_action.active || !valid_bag_slot(slot)) {
      return;
    }
    const auto& pending = world.pending_item_action;
    const auto& bag_item = world.bag_items[static_cast<std::size_t>(slot)];
    switch (pending.kind) {
      case PendingItemActionKind::equip:
      case PendingItemActionKind::drop:
        if (pending.source == MovingItemSource::bag && slot == pending.source_slot &&
            !same_item_identity(bag_item, pending.item)) {
          clear_pending_item_action();
        }
        return;
      case PendingItemActionKind::unequip:
        if (same_item_identity(bag_item, pending.item)) {
          clear_pending_item_action();
        }
        return;
      case PendingItemActionKind::use:
      case PendingItemActionKind::none:
        return;
    }
  }

  void maybe_clear_pending_after_bag_sync() {
    if (!world.pending_item_action.active) {
      return;
    }
    const auto pending = world.pending_item_action;
    switch (pending.kind) {
      case PendingItemActionKind::equip:
      case PendingItemActionKind::drop:
        if (pending.source == MovingItemSource::bag && valid_bag_slot(pending.source_slot) &&
            !same_item_identity(world.bag_items[static_cast<std::size_t>(pending.source_slot)],
                                pending.item)) {
          clear_pending_item_action();
        }
        return;
      case PendingItemActionKind::unequip:
        for (const auto& item : world.bag_items) {
          if (same_item_identity(item, pending.item)) {
            clear_pending_item_action();
            return;
          }
        }
        return;
      case PendingItemActionKind::use:
      case PendingItemActionKind::none:
        return;
    }
  }

  void maybe_clear_pending_after_equipment_sync() {
    if (!world.pending_item_action.active) {
      return;
    }
    const auto pending = world.pending_item_action;
    switch (pending.kind) {
      case PendingItemActionKind::equip:
        if (valid_equipment_slot(pending.target_slot) &&
            same_item_identity(world.equipment[static_cast<std::size_t>(pending.target_slot)],
                               pending.item)) {
          clear_pending_item_action();
        }
        return;
      case PendingItemActionKind::unequip:
        if (pending.source == MovingItemSource::equipment &&
            valid_equipment_slot(pending.source_slot) &&
            !same_item_identity(world.equipment[static_cast<std::size_t>(pending.source_slot)],
                                pending.item)) {
          clear_pending_item_action();
        }
        return;
      case PendingItemActionKind::drop:
      case PendingItemActionKind::use:
      case PendingItemActionKind::none:
        return;
    }
  }

  void bind_magic_key(const std::uint16_t magic_id, const std::uint8_t key) {
    const auto clamped_key = key <= 8 ? key : 0;
    if (clamped_key != 0) {
      for (auto& magic : world.magics) {
        if (magic.magic_id != magic_id && magic.key == clamped_key) {
          magic.key = 0;
        }
      }
    }
    for (auto& magic : world.magics) {
      if (magic.magic_id == magic_id) {
        magic.key = clamped_key;
        return;
      }
    }
  }

  void clear_world_ui_state() {
    world.npc_dialog = NpcDialogState{};
    world.merchant_shop = MerchantShopState{};
    world.repair = RepairState{};
    world.storage = StorageState{};
    world.group = GroupUiState{};
    world.trade = TradeUiState{};
    world.guild = GuildUiState{};
    world.minimap = MiniMapViewState{};
    world.moving_item = MovingItemState{};
    clear_pending_item_action();
    world.hovered_bag_slot = -1;
    world.hovered_equipment_slot = -1;
    world.selected_bag_slot = -1;
    world.focus_actor_id = 0;
    world.focus_ground_item_id = 0;
    world.target_actor_id = 0;
    world.pending_pickup_item_id = 0;
    world.action_locked = false;
    world.action_lock_started_ms = 0;
    world.last_action_ack_ms = 0;
    world.last_action_ack_ok = true;
    world.action_fail_lock = false;
    world.fail_action_ident = 0;
    world.fail_dir = 0;
    world.fail_action_time_ms = 0;
    world.last_sent_action_ident = 0;
    world.last_sent_action_dir = 0;
    world.dizzy_delay_start_ms = 0;
    world.dizzy_delay_time_ms = 0;
    world.skip_tick = 0;
    world.move_slow_level = 0;
    world.move_slow = false;
    world.attack_slow = false;
    world.legacy_target_x = -1;
    world.legacy_target_y = -1;
    world.legacy_chr_action = LegacyChrAction::none;
    world.action_key = -1;
    world.run_ready_count = 0;
    world.mouse_down_ms = 0;
    world.last_pickup_ms = 0;
    world.eating_item_make_index = 0;
    world.eating_item_slot = -1;
    world.eat_time_ms = 0;
    world.last_use_item_ok = true;
  }

  void clear_play_scene_state() {
    world = WorldViewState{};
  }

  void clear_map_objects_for_transition() {
    world.self_actor_id = 0;
    world.actors.clear();
    world.ground_items.clear();
    world.map_doors.clear();
    world.actor_draw_order.clear();
    world.ground_item_draw_order.clear();
    world.npc_dialog = NpcDialogState{};
    world.merchant_shop = MerchantShopState{};
    world.repair = RepairState{};
    world.storage = StorageState{};
    world.trade = TradeUiState{};
    world.minimap = MiniMapViewState{};
    world.moving_item = MovingItemState{};
    clear_pending_item_action();
    world.focus_actor_id = 0;
    world.focus_ground_item_id = 0;
    world.target_actor_id = 0;
    world.pending_pickup_item_id = 0;
    world.action_locked = false;
    world.action_lock_started_ms = 0;
    world.action_fail_lock = false;
    world.fail_action_ident = 0;
    world.fail_dir = 0;
    world.fail_action_time_ms = 0;
    world.last_sent_action_ident = 0;
    world.last_sent_action_dir = 0;
    world.legacy_target_x = -1;
    world.legacy_target_y = -1;
    world.legacy_chr_action = LegacyChrAction::none;
    world.action_key = -1;
    world.mouse_down_ms = 0;
    world.map_transition_pending = true;
  }

  void clear_waiting_item() {
    clear_pending_item_action();
  }

  // ---- 协议消息 apply 方法 ----
  // 这些方法将服务端下发的协议消息中的数据同步到本地状态
  // 每个方法对应一种 client_v1::Message 变体类型

  /// 应用角色列表消息
  void apply(const client_v1::CharacterList& message) {
    lobby.characters = message.characters;
    // 修正选中索引范围
    if (lobby.selected_index >= static_cast<int>(lobby.characters.size())) {
      lobby.selected_index = lobby.characters.empty() ? 0 : static_cast<int>(lobby.characters.size() - 1);
    }
    // 优先选中服务端指定名称的角色
    if (!message.selected_name.empty()) {
      for (std::size_t index = 0; index < lobby.characters.size(); ++index) {
        if (lobby.characters[index].name == message.selected_name) {
          lobby.selected_index = static_cast<int>(index);
          break;
        }
      }
    }
  }

  /// 应用服务器列表消息
  void apply(const client_v1::ServerList& message) {
    lobby.servers = message.servers;
    if (lobby.selected_server_index >= static_cast<int>(lobby.servers.size())) {
      lobby.selected_server_index = lobby.servers.empty() ? 0 : static_cast<int>(lobby.servers.size() - 1);
    }
    if (lobby.servers.empty()) {
      lobby.selected_server_name.clear();
      return;
    }

    // 尝试保持之前选中的服务器名
    auto found = false;
    for (std::size_t index = 0; index < lobby.servers.size(); ++index) {
      if (lobby.servers[index].name == lobby.selected_server_name) {
        lobby.selected_server_index = static_cast<int>(index);
        found = true;
        break;
      }
    }
    if (!found) {
      lobby.selected_server_index = 0;
      lobby.selected_server_name = lobby.servers.front().name;
    }
  }

  /// 应用选服结果消息
  void apply(const client_v1::SelectServerResult& message) {
    if (!message.success) {
      return;
    }
    lobby.selected_server_name = message.name;
    pending_character_host = message.address;  // 记录角色网关地址
    pending_character_port = message.port;
    pending_lobby_token = message.lobby_token;
    for (std::size_t index = 0; index < lobby.servers.size(); ++index) {
      if (lobby.servers[index].name == message.name) {
        lobby.selected_server_index = static_cast<int>(index);
        break;
      }
    }
    connection_phase = ConnectionPhase::select_character;  // 进入选角阶段
  }

  /// 应用世界快照消息：重置整个世界状态
  /// 这是进入游戏时最重要的消息，包含地图信息和所有可见角色
  void apply(const client_v1::WorldSnapshot& message) {
    const auto preserve_runtime = world.map_transition_pending;
    const auto bag_items = world.bag_items;
    const auto equipment = world.equipment;
    const auto magics = world.magics;
    const auto self_ability = world.self_ability;
    const auto self_ability_detail = world.self_ability_detail;
    const auto sys_messages = world.sys_messages;
    const auto chat_lines = world.chat_lines;
    const auto chat_board_top = world.chat_board_top;
    const auto whisper_name = world.whisper_name;
    const auto group = world.group;
    const auto guild = world.guild;
    clear_play_scene_state();
    world.map_id = message.map_id;
    world.width = message.width;
    world.height = message.height;
    world.self_actor_id = message.self_actor_id;
    world.actors.clear();
    world.ground_items.clear();
    world.map_doors.clear();
    world.actor_draw_order.clear();
    world.ground_item_draw_order.clear();
    world.chat_lines.clear();
    world.chat_board_top = 0;
    world.whisper_name.clear();
    world.npc_dialog = NpcDialogState{};
    world.self_ability = client_v1::SelfAbility{};
    world.self_ability_detail = client_v1::SelfAbilityDetail{};
    world.merchant_shop = MerchantShopState{};
    world.repair = RepairState{};
    world.storage = StorageState{};
    world.group = GroupUiState{};
    world.trade = TradeUiState{};
    world.guild = GuildUiState{};
    world.minimap = MiniMapViewState{};
    world.bag_items.fill(client_v1::ItemState{});
    world.equipment.fill(client_v1::ItemState{});
    world.moving_item = MovingItemState{};
    clear_pending_item_action();
    world.hovered_bag_slot = -1;
    world.hovered_equipment_slot = -1;
    world.selected_bag_slot = -1;
    world.focus_actor_id = 0;
    world.focus_ground_item_id = 0;
    world.target_actor_id = 0;
    world.pending_pickup_item_id = 0;
    if (preserve_runtime) {
      world.bag_items = bag_items;
      world.equipment = equipment;
      world.magics = magics;
      world.self_ability = self_ability;
      world.self_ability_detail = self_ability_detail;
      world.sys_messages = sys_messages;
      world.chat_lines = chat_lines;
      world.chat_board_top = chat_board_top;
      world.whisper_name = whisper_name;
      world.group = group;
      world.guild = guild;
    }
    world.map_transition_pending = false;
    for (const auto& actor : message.actors) {
      world.actors[actor.actor_id] = ActorState{actor.actor_id, actor.name, actor.x, actor.y,
                                                actor.x, actor.y, actor.dir, actor.feature,
                                                actor.status, actor.actor_type};
      world.actor_draw_order.push_back(actor.actor_id);
    }
  }

  void apply(const client_v1::WorldClearObjects& /*message*/) {
    clear_map_objects_for_transition();
  }

  void apply(const client_v1::MapChange& message) {
    clear_map_objects_for_transition();
    if (!message.map_id.empty()) {
      world.map_id = message.map_id;
    }
  }

  void apply(const client_v1::MapDoorState& message) {
    const auto key = map_door_key(message.x, message.y);
    world.map_doors[key] = MapDoorRuntimeState{message.open, detail::monotonic_ms()};
  }

  /// 应用角色增量更新消息（坐标/方向变化）
  /// 服务端通知某个角色的位置或朝向发生了变化
  void apply(const client_v1::ActorStateDelta& message) {
    auto& actor = world.actors[message.actor_id];
    actor.actor_id = message.actor_id;
    actor.x = message.x;
    actor.y = message.y;
    actor.dir = message.dir;
  }

  static std::uint64_t map_door_key(const std::int32_t x, const std::int32_t y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
           static_cast<std::uint32_t>(y);
  }

  static std::int32_t map_door_key_x(const std::uint64_t key) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(key >> 32U));
  }

  static std::int32_t map_door_key_y(const std::uint64_t key) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(key));
  }

  [[nodiscard]] bool map_door_open(std::int32_t x, std::int32_t y) const {
    const auto it = world.map_doors.find(map_door_key(x, y));
    return it != world.map_doors.end() && it->second.open;
  }

  void expire_map_door_states(const std::uint64_t now_ms) {
    for (auto it = world.map_doors.begin(); it != world.map_doors.end();) {
      if (it->second.open &&
          elapsed_ms(now_ms, it->second.updated_ms) >= kLegacyMapDoorOpenExpireMs) {
        it = world.map_doors.erase(it);
      } else {
        ++it;
      }
    }
  }

  /// 应用角色新增/更新消息
  /// 服务端通知有新的角色进入视野或更新已有角色
  void apply(const client_v1::ActorUpsert& message) {
    const auto inserted = world.actors.find(message.actor.actor_id) == world.actors.end();
    auto& actor = world.actors[message.actor.actor_id];
    const auto previous_x = actor.actor_id == 0 ? message.actor.x : actor.x;
    const auto previous_y = actor.actor_id == 0 ? message.actor.y : actor.y;
    actor.actor_id = message.actor.actor_id;
    if (!message.actor.name.empty()) {
      actor.name = message.actor.name;
    }
    actor.x = message.actor.x;
    actor.y = message.actor.y;
    actor.from_x = previous_x;
    actor.from_y = previous_y;
    actor.dir = message.actor.dir;
    actor.feature = message.actor.feature;
    actor.status = message.actor.status;
    actor.actor_type = message.actor.actor_type;
    if (inserted) {
      world.actor_draw_order.push_back(message.actor.actor_id);
    }
  }

  /// 应用角色删除消息
  /// 服务端通知角色离开九宫格视野或被清理时，移除本地对象并清掉悬挂目标。
  void apply(const client_v1::ActorRemove& message) {
    if (message.actor_id == 0 || message.actor_id == world.self_actor_id) {
      return;
    }
    const auto now_ms = detail::monotonic_ms();
    auto it = world.actors.find(message.actor_id);
    if (it == world.actors.end()) {
      return;
    }
    auto& actor = it->second;
    if (actor.dead || actor_action_animating(actor, now_ms)) {
      actor.pending_remove = true;
      return;
    }
    erase_actor(message.actor_id);
  }

  /// 应用角色动作消息
  /// 服务端通知某个角色正在执行某种动作（攻击/施法/受击等）
  void apply(const client_v1::ActorAction& message) {
    auto& actor = world.actors[message.actor_id];
    const auto legacy_ident =
        message.kind == client_v1::ActorActionKind::hit
            ? legacy::normalize_attack_ident_to_sm(message.legacy_ident)
            : message.legacy_ident;
    actor.actor_id = message.actor_id;
    actor.current_action = message.kind;
    actor.legacy_action_ident = legacy_ident;
    actor.magic_id = message.magic_id;
    actor.action_target_actor_id = message.target_actor_id;
    actor.action_target_x = message.x;
    actor.action_target_y = message.y;
    actor.action_magic = message.magic;
    actor.action_magic_effect = message.magic_effect;
    actor.action_magic_effect_type = -1;
    actor.action_magic_failed = false;
    if (message.kind == client_v1::ActorActionKind::spell && message.magic_id != 0) {
      apply_magic_metadata(actor, message.magic_id);
    }
    actor.action_started_ms = detail::monotonic_ms();
    actor.action_duration_ms = action_duration_ms(message.kind, legacy_ident);
    const auto forced_move = message.kind == client_v1::ActorActionKind::rush ||
                             message.kind == client_v1::ActorActionKind::rush_kung ||
                             message.kind == client_v1::ActorActionKind::backstep ||
                             message.kind == client_v1::ActorActionKind::knockback;
    if (forced_move) {
      if (actor.x != message.x || actor.y != message.y ||
          message.kind == client_v1::ActorActionKind::rush_kung) {
        actor.from_x = actor.x;
        actor.from_y = actor.y;
      }
      actor.running = message.kind == client_v1::ActorActionKind::rush ||
                      message.kind == client_v1::ActorActionKind::rush_kung;
      actor.move_started_ms = actor.action_started_ms;
      actor.move_duration_ms = 0;
      if (message.kind == client_v1::ActorActionKind::rush_kung) {
        actor.action_target_x = message.x;
        actor.action_target_y = message.y;
      } else if (message.x != 0 || message.y != 0) {
        actor.x = message.x;
        actor.y = message.y;
      }
    } else if (message.kind != client_v1::ActorActionKind::spell && (message.x != 0 || message.y != 0)) {
      if (message.kind == client_v1::ActorActionKind::walk ||
          message.kind == client_v1::ActorActionKind::run) {
        if (actor.x != message.x || actor.y != message.y || actor.move_started_ms == 0) {
          actor.from_x = actor.x;
          actor.from_y = actor.y;
        }
        actor.running = message.kind == client_v1::ActorActionKind::run;
        actor.move_started_ms = actor.action_started_ms;
      }
      actor.x = message.x;
      actor.y = message.y;
    }
    if (message.dir < 8) {
      actor.dir = message.dir;
    }
    record_legacy_actor_event(actor);
    // 受击动作时记录伤害信息
    if (message.kind == client_v1::ActorActionKind::struck) {
      actor.last_damage = message.value;
      actor.last_hitter_id = message.target_actor_id;
      actor.last_damage_magic = message.magic;
      if (message.actor_id == world.self_actor_id) {
        world.latest_struck_ms = actor.action_started_ms;
      }
    }
  }

  void apply(const client_v1::ActorMagicFire& message) {
    auto& actor = world.actors[message.actor_id];
    actor.actor_id = message.actor_id;
    actor.action_target_actor_id = message.target_actor_id;
    actor.action_target_x = message.x;
    actor.action_target_y = message.y;
    actor.action_magic = true;
    actor.action_magic_effect = message.effect;
    actor.action_magic_effect_type =
        legacy_visual_effect_type(actor.magic_id, message.effect_type);
    actor.action_magic_failed = false;
    record_legacy_actor_event(actor);
  }

  void apply(const client_v1::ActorMagicFireFail& message) {
    auto& actor = world.actors[message.actor_id];
    actor.actor_id = message.actor_id;
    actor.action_magic = false;
    actor.action_magic_effect = 0;
    actor.action_magic_effect_type = -1;
    actor.action_magic_failed = true;
    record_legacy_actor_event(actor);
  }

  /// 应用角色属性（血量/蓝量）更新消息
  /// 血量/蓝量发生变化时由服务端主动推送
  void apply(const client_v1::ActorVitals& message) {
    auto& actor = world.actors[message.actor_id];
    actor.actor_id = message.actor_id;
    if (message.hp >= 0) {
      actor.hp = message.hp;
    }
    if (message.max_hp >= 0) {
      actor.max_hp = message.max_hp;
    }
    if (message.mp >= 0) {
      actor.mp = message.mp;
    }
    if (message.max_mp >= 0) {
      actor.max_mp = message.max_mp;
    }
    actor.last_damage = message.damage;
    actor.last_hitter_id = message.source_actor_id;
    actor.last_damage_magic = message.magic;
    if (message.actor_id == world.self_actor_id) {
      if (message.hp >= 0) {
        world.self_ability_detail.hp = static_cast<std::uint16_t>(std::clamp(message.hp, 0, 65535));
      }
      if (message.max_hp >= 0) {
        world.self_ability_detail.max_hp =
            static_cast<std::uint16_t>(std::clamp(message.max_hp, 0, 65535));
      }
      if (message.mp >= 0) {
        world.self_ability_detail.mp = static_cast<std::uint16_t>(std::clamp(message.mp, 0, 65535));
      }
      if (message.max_mp >= 0) {
        world.self_ability_detail.max_mp =
            static_cast<std::uint16_t>(std::clamp(message.max_mp, 0, 65535));
      }
    }
  }

  /// 应用角色死亡消息
  void apply(const client_v1::ActorDeath& message) {
    auto& actor = world.actors[message.actor_id];
    actor.actor_id = message.actor_id;
    actor.x = message.x;
    actor.y = message.y;
    actor.dir = message.dir;
    actor.dead = true;
    actor.skeleton = false;
    actor.hp = 0;
    actor.current_action = client_v1::ActorActionKind::turn;
    actor.legacy_action_ident =
        message.legacy_ident != 0 ? message.legacy_ident : legacy_sm::kDeath;
    actor.action_started_ms = detail::monotonic_ms();
    actor.action_duration_ms = action_duration_ms(client_v1::ActorActionKind::turn, 0);
    record_legacy_actor_event(actor);
  }

  /// 应用魔法列表消息
  void apply(const client_v1::MagicList& message) {
    world.magics.clear();
    for (const auto& entry : message.magics) {
      world.magics.push_back(MagicShortcutState{entry.magic_id, entry.key, entry.level,
                                                entry.train, entry.delay_ms, entry.name,
                                                entry.effect, entry.max_train,
                                                entry.effect_type});
    }
  }

  /// 应用主角能力摘要，底部 HUD 读取该状态绘制等级、经验、负重、金币、饥饿
  void apply(const client_v1::SelfAbility& message) {
    world.self_ability = message;
    world.self_ability_detail.level = message.level;
    world.self_ability_detail.job = message.job;
    world.self_ability_detail.exp = message.exp;
    world.self_ability_detail.max_exp = message.max_exp;
    world.self_ability_detail.weight = message.weight;
    world.self_ability_detail.max_weight = message.max_weight;
  }

  /// 应用主角完整能力摘要，状态窗口四页读取该状态
  void apply(const client_v1::SelfAbilityDetail& message) {
    world.self_ability_detail = message;
    world.self_ability.level = message.level;
    world.self_ability.job = message.job;
    world.self_ability.exp = message.exp;
    world.self_ability.max_exp = message.max_exp;
    world.self_ability.weight = message.weight;
    world.self_ability.max_weight = message.max_weight;
  }

  /// 应用聊天板行
  void apply(const client_v1::ChatLine& message) {
    push_chat_line(message.text, message.fore_color, message.back_color);
  }

  /// 应用角色头顶说话，同时写入聊天板
  void apply(const client_v1::ActorSay& message) {
    push_chat_line(message.text, message.fore_color, message.back_color);
    auto it = world.actors.find(message.actor_id);
    if (it == world.actors.end()) {
      return;
    }
    it->second.saying = message.text;
    it->second.saying_fore_color = message.fore_color;
    it->second.saying_back_color = message.back_color;
    it->second.saying_started_ms = detail::monotonic_ms();
  }

  /// 应用 NPC 对话，解析 Delphi 格式的 "npcname/dialog text"
  void apply(const client_v1::NpcDialog& message) {
    world.npc_dialog = NpcDialogState{};
    world.npc_dialog.visible = true;
    world.npc_dialog.merchant_id = message.merchant_id;
    world.npc_dialog.face = message.face;
    const auto slash = message.text.find('/');
    if (slash == std::string::npos) {
      world.npc_dialog.text = message.text;
    } else {
      world.npc_dialog.npc_name = message.text.substr(0, slash);
      world.npc_dialog.text = message.text.substr(slash + 1);
    }
    std::replace(world.npc_dialog.text.begin(), world.npc_dialog.text.end(), '\\', '\n');
    if (auto self = world.actors.find(world.self_actor_id); self != world.actors.end()) {
      world.npc_dialog.opened_x = self->second.x;
      world.npc_dialog.opened_y = self->second.y;
    }
  }

  /// 应用 NPC 对话关闭
  void apply(const client_v1::NpcDialogClose& message) {
    if (message.merchant_id == 0 || message.merchant_id == world.npc_dialog.merchant_id) {
      close_npc_dialog();
    }
  }

  /// 应用商店商品列表
  void apply(const client_v1::MerchantGoodsList& message) {
    world.merchant_shop.visible = true;
    world.merchant_shop.sell_selecting = false;
    world.merchant_shop.merchant_id = message.merchant_id;
    world.merchant_shop.goods = message.items;
    world.merchant_shop.page = std::clamp(
        world.merchant_shop.page, 0,
        std::max(0, (static_cast<int>(world.merchant_shop.goods.size()) + 4) / 5 - 1));
  }

  /// 应用商人价格结果
  void apply(const client_v1::MerchantPriceResult& message) {
    if (!message.sell) {
      return;
    }
    world.merchant_shop.merchant_id = message.merchant_id;
    if (message.item_index == 0 && message.ok) {
      world.merchant_shop.visible = false;
      world.merchant_shop.sell_selecting = true;
      world.merchant_shop.pending_sell_make_index = 0;
      world.merchant_shop.pending_sell_name.clear();
      world.merchant_shop.pending_sell_price = 0;
      return;
    }
    world.merchant_shop.sell_selecting = false;
    world.merchant_shop.pending_sell_make_index = message.item_index;
    world.merchant_shop.pending_sell_price = message.ok ? message.price : 0;
  }

  /// 应用修理询价结果
  void apply(const client_v1::MerchantRepairPriceResult& message) {
    world.repair.merchant_id = message.merchant_id;
    if (message.item_make_index == 0 && message.ok) {
      world.repair.selecting = true;
      world.repair.dialog_visible = false;
      world.repair.pending_make_index = 0;
      world.repair.pending_name.clear();
      world.repair.pending_price = 0;
      return;
    }
    world.repair.selecting = false;
    world.repair.dialog_visible = message.ok;
    world.repair.pending_make_index = message.item_make_index;
    world.repair.pending_price = message.ok ? message.price : 0;
    if (!message.ok) {
      world.repair.pending_name.clear();
      push_sys_message("Repair price unavailable.");
    }
  }

  /// 应用仓库列表
  void apply(const client_v1::StorageList& message) {
    world.storage.visible = true;
    world.storage.deposit_selecting = false;
    world.storage.merchant_id = message.merchant_id;
    world.storage.items = message.items;
    if (world.storage.items.empty()) {
      world.storage.selected_index = -1;
    } else {
      world.storage.selected_index =
          std::clamp(world.storage.selected_index, 0, static_cast<int>(world.storage.items.size() - 1));
    }
  }

  /// 应用组队状态
  void apply(const client_v1::GroupState& message) {
    world.group.visible = message.visible;
    world.group.allow_group = message.allow_group;
    world.group.members = message.members;
  }

  /// 应用交易状态
  void apply(const client_v1::TradeState& message) {
    world.trade.visible = message.visible;
    world.trade.remote_name = message.remote_name;
    world.trade.local_items = message.local_items;
    world.trade.remote_items = message.remote_items;
    world.trade.local_gold = message.local_gold;
    world.trade.remote_gold = message.remote_gold;
    world.trade.local_accept = message.local_accept;
    world.trade.remote_accept = message.remote_accept;
  }

  /// 应用行会状态
  void apply(const client_v1::GuildState& message) {
    world.guild.visible = message.visible;
    world.guild.guild_name = message.guild_name;
    world.guild.rank_name = message.rank_name;
    world.guild.notice = message.notice;
    world.guild.members = message.members;
    world.guild.ranks = message.ranks;
    world.guild.can_admin = message.can_admin;
  }

  /// 应用小地图数据
  void apply(const client_v1::MiniMapData& message) {
    world.minimap.visible = true;
    world.minimap.loaded = message.success;
    world.minimap.map_id = message.map_id;
    world.minimap.width = message.width;
    world.minimap.height = message.height;
    world.minimap.pixels = message.pixels;
    world.minimap.error_message = message.error_message;
    if (!message.success && !message.error_message.empty()) {
      push_sys_message(message.error_message);
    }
  }

  /// 系统消息保留顶部短提示，同时进入聊天板
  void apply(const client_v1::SysMessage& message) {
    push_sys_message(message.text);
  }

  /// 应用动作确认消息
  /// 服务端对客户端发起的动作（移动/攻击等）的响应。
  /// ok=true 表示服务端接受了动作；ok=false 表示拒绝，
  /// 客户端需要回滚到动作之前的状态。
  void apply(const client_v1::ActionAck& message) {
    world.action_locked = false;
    world.last_action_ack_ms = detail::monotonic_ms();
    world.last_action_ack_ok = message.ok;
    // 动作被服务端拒绝时，客户端需要回滚
    if (!message.ok) {
      if (auto it = world.actors.find(world.self_actor_id); it != world.actors.end()) {
        legacy_action_failed(world, it->second, world.last_action_ack_ms);
      }
    }
  }

  /// 应用地面物品添加消息
  void apply(const client_v1::GroundItemAdd& message) {
    const auto inserted = world.ground_items.find(message.item.object_id) == world.ground_items.end();
    world.ground_items[message.item.object_id] = message.item;
    if (inserted) {
      world.ground_item_draw_order.push_back(message.item.object_id);
    }
  }

  /// 应用完整背包快照：覆盖本地 46 格背包镜像
  void apply(const client_v1::BagSnapshot& message) {
    world.bag_items.fill(client_v1::ItemState{});
    for (const auto& entry : message.items) {
      if (valid_bag_slot(entry.slot)) {
        world.bag_items[static_cast<std::size_t>(entry.slot)] = entry.item;
      }
    }
    maybe_clear_pending_after_bag_sync();
  }

  /// 应用背包新增物品消息
  void apply(const client_v1::InventoryAdd& message) {
    if (valid_bag_slot(message.entry.slot)) {
      world.bag_items[static_cast<std::size_t>(message.entry.slot)] = message.entry.item;
    }
    maybe_clear_pending_after_bag_slot(message.entry.slot);
  }

  /// 应用背包物品更新消息
  void apply(const client_v1::InventoryUpdate& message) {
    if (valid_bag_slot(message.entry.slot)) {
      world.bag_items[static_cast<std::size_t>(message.entry.slot)] = message.entry.item;
    }
    maybe_clear_pending_after_bag_slot(message.entry.slot);
  }

  /// 应用背包物品移除消息
  void apply(const client_v1::InventoryRemove& message) {
    if (valid_bag_slot(message.slot)) {
      world.bag_items[static_cast<std::size_t>(message.slot)] = client_v1::ItemState{};
    }
    maybe_clear_pending_after_bag_slot(message.slot);
  }

  /// 应用背包范围清理消息（闭区间 [first_slot, last_slot]）
  void apply(const client_v1::InventoryClearRange& message) {
    auto first = std::min(message.first_slot, message.last_slot);
    auto last = std::max(message.first_slot, message.last_slot);
    if (last < 0 || first >= kMaxBagItems) {
      return;
    }
    first = std::clamp(first, 0, kMaxBagItems - 1);
    last = std::clamp(last, 0, kMaxBagItems - 1);
    for (auto slot = first; slot <= last; ++slot) {
      world.bag_items[static_cast<std::size_t>(slot)] = client_v1::ItemState{};
      maybe_clear_pending_after_bag_slot(slot);
    }
  }

  /// 应用完整装备快照：覆盖 Delphi UseItems[0..12] 装备栏
  void apply(const client_v1::EquipmentSnapshot& message) {
    world.equipment.fill(client_v1::ItemState{});
    for (const auto& entry : message.items) {
      if (valid_equipment_slot(entry.slot)) {
        world.equipment[static_cast<std::size_t>(entry.slot)] = entry.item;
      }
    }
    maybe_clear_pending_after_equipment_sync();
  }

  /// 应用耐久变化：旧端 SM_DURACHANGE 只表达槽位，client_v1 归一化为 make_index
  void apply(const client_v1::DurabilityChange& message) {
    if (message.item_make_index == 0) {
      return;
    }
    const auto dura = static_cast<std::uint16_t>(std::clamp(message.dura, 0, 65535));
    const auto dura_max = static_cast<std::uint16_t>(std::clamp(message.dura_max, 0, 65535));
    auto update = [&](client_v1::ItemState& item) {
      if (!item_empty(item) && item.make_index == message.item_make_index) {
        item.dura = dura;
        item.dura_max = dura_max;
      }
    };
    for (auto& item : world.bag_items) {
      update(item);
    }
    for (auto& item : world.equipment) {
      update(item);
    }
  }

  /// 应用地面物品移除消息
  /// object_id 为 0 时按坐标移除（兼容旧版协议）
  void apply(const client_v1::GroundItemRemove& message) {
    if (message.object_id != 0) {
      world.ground_items.erase(message.object_id);
      world.ground_item_draw_order.erase(std::remove(world.ground_item_draw_order.begin(),
                                                     world.ground_item_draw_order.end(),
                                                     message.object_id),
                                         world.ground_item_draw_order.end());
    } else {
      // object_id 为 0 时按坐标移除所有匹配的物品
      for (auto it = world.ground_items.begin(); it != world.ground_items.end();) {
        if (it->second.x == message.x && it->second.y == message.y) {
          const auto removed_id = it->first;
          it = world.ground_items.erase(it);
          world.ground_item_draw_order.erase(
              std::remove(world.ground_item_draw_order.begin(),
                          world.ground_item_draw_order.end(),
                          removed_id),
              world.ground_item_draw_order.end());
        } else {
          ++it;
        }
      }
    }
    // 清理已移除物品的焦点/拾取状态
    if (world.focus_ground_item_id == message.object_id) {
      world.focus_ground_item_id = 0;
    }
    if (world.pending_pickup_item_id == message.object_id) {
      world.pending_pickup_item_id = 0;
    }
  }

  /// 应用使用物品结果消息
  void apply(const client_v1::UseItemResult& message) {
    world.last_use_item_ok = message.ok;
    world.eating_item_make_index = 0;
    world.eating_item_slot = -1;
    world.eat_time_ms = 0;
    if (world.pending_item_action.active &&
        world.pending_item_action.kind == PendingItemActionKind::use) {
      if (message.ok) {
        clear_pending_item_action();
      } else {
        restore_pending_item_action();
      }
      return;
    }
    if (message.ok) {
      clear_legacy_waiting_item();
    }
  }

  /// 推送系统消息（聊天框黄色文字），最多保留 8 条
  void push_sys_message(std::string text) {
    push_chat_line(text, 0xFFFFFF00U, 0x00000000U);
    world.sys_messages.push_back(std::move(text));
    if (world.sys_messages.size() > 8) {
      world.sys_messages.erase(world.sys_messages.begin());
    }
  }

  void show_modal(std::wstring title, std::wstring message) {
    modal.visible = true;
    modal.title = std::move(title);
    modal.message = std::move(message);
  }

  void hide_modal() { modal = ModalState{}; }
};

}  // namespace mir2::client
