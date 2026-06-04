/**
 * @file client_v1_game_gateway_service.hpp
 * @brief Client v1 游戏网关服务头文件
 *
 * @details 定义 ClientV1GameGatewayService 类，作为 Client v1 协议的游戏网关，
 *          负责处理已认证玩家在游戏世界中的所有消息交互。该类继承自
 *          ClientV1GatewayServiceBase，实现从新协议到遗留协议命令的翻译，
 *          以及将遗留服务器帧转换为 Client v1 消息的双向协议转换。
 *
 *          主要职责包括：
 *          - 处理玩家进入游戏世界的完整流程(令牌验证、角色加载)
 *          - 将 Client v1 消息(移动、攻击、魔法、物品等)转换为遗留协议命令
 *          - 将遗留服务器帧转换为 Client v1 消息推送给客户端
 *          - 管理会话状态、背包/装备、交易、组队、公会等运行时数据
 *          - 通过消息总线与 WorldService 通信
 *
 * @note 该类是系统中协议转换的核心模块，维护着大量的会话状态，
 *       包括背包物品、装备栏、魔法列表、交易状态、组队状态和公会状态。
 *       所有会话访问通过 mutex_ 互斥锁保护以确保线程安全。
 */

#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "protocol/canonical_login_state.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_gateway_service_base.hpp"
#include "storage/repository.hpp"

namespace mir2 {

struct CanonicalLegacyCommand;

/**
 * @class ClientV1GameGatewayService
 * @brief Client v1 游戏网关服务类
 *
 * @details 作为 Client v1 协议的游戏网关，管理玩家进入游戏世界后的所有交互。
 *          通过消息总线与 WorldService 通信，实现双向协议转换：
 *
 *          入站方向(客户端 -> 服务器)：
 *          将 Client v1 消息类型(如 MoveIntent、ActionIntent、SpellIntent 等)
 *          转换为 CanonicalLegacyCommand，再转为 LogicCommand 发送给 WorldService。
 *
 *          出站方向(服务器 -> 客户端)：
 *          监听来自 WorldService 的 SessionEvent，将 LegacyPacket 帧通过
 *          translate_legacy_packet_messages() 转换为 Client v1 消息。
 *          该转换函数包含约 80+ 种 kSm* 消息类型的 switch 分支，覆盖了
 *          移动、战斗、魔法、物品、NPC、交易、组队、公会等所有游戏交互。
 *
 *          会话生命周期管理：
 *          - 连接建立时创建 SessionState 并通知 WorldService
 *          - 连接断开时清理组队/交易/公会状态并通知 WorldService
 *          - 维护会话序列号用于消息排序
 *
 * @note 协议转换的重点是将遗留协议的帧标识符(kSm* 常量)映射到
 *       client_v1::Message 变体类型，同时维护服务器端的运行时状态缓存
 *       (背包、装备、魔法、交易等)，避免频繁查询数据库。
 */
class ClientV1GameGatewayService : public ClientV1GatewayServiceBase {
 public:
  /**
   * @brief 构造函数
   * @param admissions Client v1 准入注册表共享指针，用于验证进入游戏令牌
   */
  explicit ClientV1GameGatewayService(std::shared_ptr<ClientV1AdmissionRegistry> admissions);

  /**
   * @brief 启动服务
   * @param context 宿主上下文，包含配置、消息总线等
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止服务
   */
  void stop() override;

  /**
   * @brief 等待工作线程结束
   */
  void join() override;

#ifdef MIR2_ENABLE_TEST_HOOKS
  /// @name 测试钩子(仅在 MIR2_ENABLE_TEST_HOOKS 编译时启用)
  /// @{
  void seed_session_for_test(std::uint64_t session_id);
  void translate_legacy_packet_for_test(std::uint64_t session_id, const LegacyPacket& packet,
                                        std::vector<client_v1::Message>& messages);
  void translate_legacy_packet_frames_for_test(std::uint64_t session_id,
                                               const LegacyPacket& packet,
                                               std::vector<client_v1::Frame>& frames);
  [[nodiscard]] std::optional<CharacterRecord> session_character_for_test(
      std::uint64_t session_id) const;
  /// @}
#endif

 protected:
  /**
   * @brief 获取端口绑定配置
   * @param context 宿主上下文
   * @return 端口绑定信息(IP 和端口)
   */
  PortBinding binding(const HostContext& context) const override;

  /**
   * @brief 处理客户端消息
   * @param session_id 会话 ID
   * @param peer_address 客户端地址
   * @param sequence 消息序列号
   * @param message Client v1 消息(变体类型)
   *
   * @details 通过 std::visit 分发到具体的 handle_* 方法，
   *          覆盖 ClientHello、EnterWorldRequest、MoveIntent、ActionIntent、
   *          SpellIntent、物品操作、NPC 交互、交易、组队、公会等约 30+ 种消息类型。
   */
  void handle_message(std::uint64_t session_id, const std::string& peer_address,
                      std::uint32_t sequence,
                      const client_v1::Message& message) override;

  /**
   * @brief 处理客户端连接建立
   * @param session_id 会话 ID
   * @param peer_address 客户端地址
   *
   * @details 创建初始 SessionState 并通知 WorldService 有新连接。
   */
  void handle_connected(std::uint64_t session_id, const std::string& peer_address) override;

  /**
   * @brief 处理客户端连接断开
   * @param session_id 会话 ID
   * @param peer_address 客户端地址
   * @param reason 断开原因
   *
   * @details 清理组队/交易/公会状态，广播状态更新，通知 WorldService。
   */
  void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason) override;

 private:
  /**
   * @struct SessionState
   * @brief 会话状态结构体
   *
   * @details 存储每个会话的完整运行时状态，包括登录流程阶段、
   *          角色数据、背包/装备物品、魔法列表、交易/组队/公会状态等。
   *          是协议转换过程中最重要的数据结构。
   */
  struct SessionState {
    bool greeted{false};                          ///< 是否已完成 ClientHello 握手
    bool entered_world{false};                    ///< 是否已进入游戏世界
    bool pending_login_notice{false};             ///< 是否有待显示的登录公告
    bool world_result_sent{false};                ///< 是否已发送 EnterWorldResult
    bool map_change_pending{false};               ///< 是否有待处理的地图切换
    std::string account_id{};                     ///< 账号 ID
    std::string character_name{};                 ///< 角色名
    std::uint64_t actor_id{0};                    ///< 玩家在游戏世界中的 actor ID
    CharacterRecord character{};                  ///< 角色完整记录(能力、位置、装备等)
    std::optional<client_v1::ActionIntent> pending_action{}; ///< 待确认的动作意图
    std::array<client_v1::ItemState, kMaxBagItems> bag_items{};           ///< 背包物品数组
    std::array<client_v1::ItemState, kMaxEquipSlots> equipment_items{};   ///< 装备栏物品数组
    std::vector<client_v1::MagicEntry> magics{};  ///< 已学习的魔法列表
    std::uint64_t current_merchant_id{0};         ///< 当前交互的 NPC/商人 ID
    std::int32_t pending_sell_item_make_index{0}; ///< 待出售物品的 make_index
    std::string pending_sell_item_name{};         ///< 待出售物品的名称
    std::int32_t pending_repair_item_make_index{0}; ///< 待修理物品的 make_index
    std::string pending_repair_item_name{};       ///< 待修理物品的名称
    bool allow_group{false};                      ///< 是否允许组队邀请
    bool group_visible{false};                    ///< 组队面板是否可见
    std::uint64_t group_id{0};                    ///< 所在组队 ID(0 表示未组队)
    bool trade_visible{false};                    ///< 交易面板是否可见
    std::string pending_trade_remote_name{};      ///< 待确认交易的目标角色名
    std::uint64_t pending_trade_peer_session_id{0}; ///< 待确认交易的对端会话 ID
    std::uint64_t trade_peer_session_id{0};       ///< 当前交易的对端会话 ID
    std::string trade_remote_name{};              ///< 交易对方角色名
    std::vector<client_v1::ItemSlotState> trade_local_items{}; ///< 本地交易物品列表
    std::int32_t trade_local_gold{0};             ///< 本地交易金币数
    bool trade_local_accept{false};               ///< 本地是否已确认交易
    bool guild_visible{false};                    ///< 公会面板是否可见
    std::uint64_t next_session_seq{0};            ///< 下一条会话序列号(用于消息排序)
    std::uint64_t next_legacy_bundle_id{1};       ///< 下一个遗留协议包束 ID
    CanonicalLoginStage stage{CanonicalLoginStage::connected}; ///< 当前登录阶段

    /**
     * @brief 检查角色是否已进入游戏并可进行游戏操作
     * @return true 如果角色处于 gameplay 阶段
     */
    [[nodiscard]] bool in_game() const {
      return can_accept(stage, CanonicalLoginRequest::gameplay);
    }
  };

  /// @name Client v1 消息处理函数
  /// @{
  void handle_client_hello(std::uint64_t session_id, const client_v1::ClientHello& hello);
  void handle_enter_world_request(std::uint64_t session_id, std::uint32_t sequence,
                                  const client_v1::EnterWorldRequest& request);
  void handle_login_notice_ok(std::uint64_t session_id);
  void handle_move_intent(std::uint64_t session_id, const client_v1::MoveIntent& intent);
  void handle_action_intent(std::uint64_t session_id, const client_v1::ActionIntent& intent);
  void handle_spell_intent(std::uint64_t session_id, const client_v1::SpellIntent& intent);
  void handle_pickup_intent(std::uint64_t session_id, const client_v1::PickupIntent& intent);
  void handle_use_item_intent(std::uint64_t session_id, const client_v1::UseItemIntent& intent);
  void handle_equip_item_request(std::uint64_t session_id,
                                  const client_v1::EquipItemRequest& request);
  void handle_unequip_item_request(std::uint64_t session_id,
                                    const client_v1::UnequipItemRequest& request);
  void handle_drop_item_request(std::uint64_t session_id,
                                 const client_v1::DropItemRequest& request);
  void handle_drop_gold_request(std::uint64_t session_id,
                                 const client_v1::DropGoldRequest& request);
  void handle_revive_request(std::uint64_t session_id, const client_v1::ReviveRequest& request);
  void handle_magic_key_change_request(
      std::uint64_t session_id, const client_v1::MagicKeyChangeRequest& request);
  void handle_npc_click_request(std::uint64_t session_id,
                                 const client_v1::NpcClickRequest& request);
  void handle_npc_dialog_select_request(
      std::uint64_t session_id, const client_v1::NpcDialogSelectRequest& request);
  void handle_merchant_buy_request(std::uint64_t session_id,
                                    const client_v1::MerchantBuyRequest& request);
  void handle_merchant_sell_request(std::uint64_t session_id,
                                     const client_v1::MerchantSellRequest& request);
  void handle_merchant_sell_price_request(
      std::uint64_t session_id, const client_v1::MerchantSellPriceRequest& request);
  void handle_merchant_repair_price_request(
      std::uint64_t session_id, const client_v1::MerchantRepairPriceRequest& request);
  void handle_merchant_repair_request(std::uint64_t session_id,
                                       const client_v1::MerchantRepairRequest& request);
  void handle_storage_deposit_request(std::uint64_t session_id,
                                       const client_v1::StorageDepositRequest& request);
  void handle_storage_withdraw_request(std::uint64_t session_id,
                                        const client_v1::StorageWithdrawRequest& request);
  void handle_group_mode_request(std::uint64_t session_id,
                                  const client_v1::GroupModeRequest& request);
  void handle_group_create_request(std::uint64_t session_id,
                                    const client_v1::GroupCreateRequest& request);
  void handle_group_add_member_request(std::uint64_t session_id,
                                        const client_v1::GroupAddMemberRequest& request);
  void handle_group_remove_member_request(
      std::uint64_t session_id, const client_v1::GroupRemoveMemberRequest& request);
  void handle_trade_try_request(std::uint64_t session_id,
                                 const client_v1::TradeTryRequest& request);
  void handle_trade_cancel_request(std::uint64_t session_id,
                                    const client_v1::TradeCancelRequest& request);
  void handle_trade_add_item_request(std::uint64_t session_id,
                                      const client_v1::TradeAddItemRequest& request);
  void handle_trade_remove_item_request(std::uint64_t session_id,
                                         const client_v1::TradeRemoveItemRequest& request);
  void handle_trade_set_gold_request(std::uint64_t session_id,
                                      const client_v1::TradeSetGoldRequest& request);
  void handle_trade_accept_request(std::uint64_t session_id,
                                    const client_v1::TradeAcceptRequest& request);
  void handle_guild_open_request(std::uint64_t session_id,
                                  const client_v1::GuildOpenRequest& request);
  void handle_guild_home_request(std::uint64_t session_id,
                                  const client_v1::GuildHomeRequest& request);
  void handle_guild_member_list_request(
      std::uint64_t session_id, const client_v1::GuildMemberListRequest& request);
  void handle_guild_add_member_request(std::uint64_t session_id,
                                        const client_v1::GuildAddMemberRequest& request);
  void handle_guild_remove_member_request(
      std::uint64_t session_id, const client_v1::GuildRemoveMemberRequest& request);
  void handle_guild_update_notice_request(
      std::uint64_t session_id, const client_v1::GuildUpdateNoticeRequest& request);
  void handle_guild_update_grade_request(
      std::uint64_t session_id, const client_v1::GuildUpdateGradeRequest& request);
  void handle_minimap_request(std::uint64_t session_id,
                               const client_v1::MiniMapRequest& request);
  void handle_chat_send(std::uint64_t session_id, const client_v1::ChatSend& chat);
  void handle_ping(std::uint64_t session_id, const client_v1::Ping& ping);
  /// @}

  /// @name 内部辅助函数
  /// @{

  /**
   * @brief 发送进入游戏世界的事件
   * @param session_id 会话 ID
   * @param state 当前会话状态
   *
   * @details 构造 LogicCommand::enter_world 命令并发送给 WorldService，
   *          通知服务器玩家已准备好进入游戏。
   */
  void post_enter_world(std::uint64_t session_id, const SessionState& state);

  /**
   * @brief 发布规范化遗留命令
   * @param command 规范化遗留命令
   * @param assign_session_sequence 是否自动分配会话序列号(默认 true)
   *
   * @details 将 CanonicalLegacyCommand 转换为 LogicCommand 后发送给 WorldService。
   */
  void post_canonical_command(CanonicalLegacyCommand command,
                               bool assign_session_sequence = true);

  /**
   * @brief 发布逻辑命令到 WorldService
   * @param command 逻辑命令
   * @param assign_session_sequence 是否自动分配会话序列号(默认 true)
   */
  void post_logic_command(LogicCommand command, bool assign_session_sequence = true);

  /**
   * @brief 消息总线循环线程
   * @details 持续从消息总线获取 SessionEvent 并处理，
   *          负责将 LegacyPacket 转换为 client_v1::Frame 发送给客户端。
   */
  void bus_loop();

  /**
   * @brief 处理来自 WorldService 的会话事件
   * @param event 会话事件
   *
   * @details 主要处理 send_packet 和 send_packet_and_close 事件，
   *          将遗留协议包转换为 Client v1 帧后发送给客户端。
   */
  void handle_session_event(const SessionEvent& event);

  /**
   * @brief 翻译遗留协议包为 Client v1 帧
   * @param session_id 会话 ID
   * @param packet 遗留协议包
   * @param frames 输出参数，转换后的 Client v1 帧列表
   *
   * @details 先调用 translate_legacy_packet_messages() 生成消息列表，
   *          然后根据 LegacyBundleMeta 打包成帧，支持 actor_queue
   *          和 immediate 两种束模式。
   */
  void translate_legacy_packet(std::uint64_t session_id, const LegacyPacket& packet,
                                std::vector<client_v1::Frame>& frames);

  /**
   * @brief 翻译遗留协议包为 Client v1 消息列表
   * @param session_id 会话 ID
   * @param packet 遗留协议包
   * @param messages 输出参数，转换后的 Client v1 消息列表
   *
   * @details 核心协议转换函数，包含约 80+ 种 kSm* 消息类型的 switch 分支。
   *          负责将遗留服务器帧的各字段(ident, recog, param, tag, series, body)
   *          映射到对应的 client_v1::Message 子类型。
   *
   *          主要的转换类别包括：
   *          - 世界状态：kSmClearObjects, kSmChangeMap, kSmMapDescription
   *          - 角色登录/出生：kSmLogon, kSmNewMap, kSmAlive
   *          - 角色动作：kSmTurn/Walk/Run/Hit/Spell/Struck/Death
   *          - 物品系统：kSmBagItems, kSmSendUseItems, kSmAddItem/DelItem/UpdateItem
   *          - NPC/商人：kSmMerchantSay, kSmSendGoodsList, kSmSendBuyPrice
   *          - 交易系统：kSmDealMenu, kSmDealCancel/Success
   *          - 组队/公会：通过 kSmHear 文本匹配处理
   *          - UI 更新：kSmAbility, kSmHealthSpellChanged, kSmLevelUp, kSmWinExp
   *          - 魔法系统：kSmSendMyMagic, kSmAddMagic, kSmDelMagic, kSmMagicLvExp
   */
  void translate_legacy_packet_messages(std::uint64_t session_id, const LegacyPacket& packet,
                                         std::vector<client_v1::Message>& messages);

  /// @}

  /// @name 组队/交易/公会运行时状态结构体
  /// @{

  /**
   * @struct GroupRuntimeState
   * @brief 组队运行时状态
   * @details 跟踪组队中所有成员的会话 ID 列表
   */
  struct GroupRuntimeState {
    std::vector<std::uint64_t> members{}; ///< 组队成员会话 ID 列表
  };

  /**
   * @struct GuildRuntimeState
   * @brief 公会运行时状态(客户端缓存)
   * @details 在网关层缓存公会信息，避免每次查询数据库。
   *          @warning 与 WorldService 中的公会数据不同步，
   *           仅用于 Client v1 协议的公会面板显示。
   */
  struct GuildRuntimeState {
    std::string name{};                                    ///< 公会名称
    std::string notice{};                                  ///< 公会公告
    std::vector<client_v1::GuildMemberState> members{};    ///< 公会成员列表
    std::vector<std::string> ranks{};                      ///< 公会等级列表
  };
  /// @}

  /// @name 加锁查询辅助函数(必须在 mutex_ 保护下调用)
  /// @{

  /**
   * @brief 通过角色名查找会话 ID(加锁)
   * @param name 角色名
   * @return 会话 ID，未找到时返回 nullopt
   */
  [[nodiscard]] std::optional<std::uint64_t> find_session_by_character_locked(
      std::string_view name) const;

  /**
   * @brief 获取组队状态(加锁)
   * @param session_id 会话 ID
   * @return 组队状态，包含成员列表和可见性
   */
  [[nodiscard]] client_v1::GroupState group_state_locked(std::uint64_t session_id) const;

  /**
   * @brief 获取组队广播状态列表(加锁)
   * @param group_id 组队 ID
   * @return 所有成员的组队状态对列表
   */
  [[nodiscard]] std::vector<std::pair<std::uint64_t, client_v1::GroupState>>
  group_broadcast_locked(std::uint64_t group_id) const;

  /**
   * @brief 获取交易状态(加锁)
   * @param session_id 会话 ID
   * @return 交易状态，包含双方物品/金币/确认状态
   */
  [[nodiscard]] client_v1::TradeState trade_state_locked(std::uint64_t session_id) const;

  /**
   * @brief 获取交易双方状态列表(加锁)
   * @param session_id 会话 ID
   * @return 双方交易状态对列表
   */
  [[nodiscard]] std::vector<std::pair<std::uint64_t, client_v1::TradeState>>
  trade_pair_states_locked(std::uint64_t session_id) const;

  /**
   * @brief 清除交易状态(加锁)
   * @param state 会话状态引用
   */
  void clear_trade_locked(SessionState& state);

  /**
   * @brief 清除待确认交易状态(加锁，双向)
   * @param session_id 会话 ID
   */
  void clear_pending_trade_locked(std::uint64_t session_id);

  /**
   * @brief 从背包中获取交易物品(加锁)
   * @param state 会话状态
   * @param make_index 物品 make_index
   * @param name 物品名
   * @return 物品槽状态，若物品已在交易列表中或未找到则返回 nullopt
   */
  [[nodiscard]] std::optional<client_v1::ItemSlotState> trade_item_from_bag_locked(
      const SessionState& state, std::int32_t make_index, std::string_view name) const;

  /**
   * @brief 确保公会成员信息存在且在线状态正确(加锁)
   * @param state 会话状态
   *
   * @details 如果角色有公会，确保 guilds_ 缓存中存在该公会的记录，
   *          并遍历所有在线会话更新成员的在线状态。
   */
  void ensure_guild_member_locked(SessionState& state);

  /**
   * @brief 获取公会状态(加锁)
   * @param session_id 会话 ID
   * @return 公会状态，包含成员列表、等级、公告和管理权限
   */
  [[nodiscard]] client_v1::GuildState guild_state_locked(std::uint64_t session_id);

  /**
   * @brief 获取公会广播状态列表(加锁)
   * @param guild_name 公会名
   * @return 所有公会成员且公会面板可见的会话的公会状态对列表
   */
  [[nodiscard]] std::vector<std::pair<std::uint64_t, client_v1::GuildState>>
  guild_broadcast_locked(std::string_view guild_name);
  /// @}

  /**
   * @brief 根据地图 ID 查找地图配置
   * @param map_id 地图 ID
   * @return 地图配置，未找到时返回 nullopt
   */
  [[nodiscard]] std::optional<MapConfig> find_map(std::string_view map_id) const;

  /**
   * @brief 获取会话状态(加锁)
   * @param session_id 会话 ID
   * @return 会话状态的副本，会话不存在时返回 nullopt
   */
  [[nodiscard]] std::optional<SessionState> session(std::uint64_t session_id) const;

  std::shared_ptr<ClientV1AdmissionRegistry> admissions_{};  ///< Client v1 准入注册表
  std::unique_ptr<Repository> repository_{};                 ///< 数据仓库(SQLite 数据库访问)
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};           ///< 消息总线端点
  std::thread bus_thread_{};                                 ///< 消息总线处理线程
  std::atomic_bool bus_running_{false};                      ///< 总线线程运行标志
  mutable std::mutex mutex_{};                               ///< 会话/组队/公会数据互斥锁
  std::unordered_map<std::uint64_t, SessionState> sessions_{}; ///< 会话 ID -> 会话状态映射表
  std::uint64_t next_group_id_{1};                           ///< 下一个组队 ID(自增)
  std::unordered_map<std::uint64_t, GroupRuntimeState> groups_{}; ///< 组队 ID -> 组队状态映射表
  std::unordered_map<std::string, GuildRuntimeState> guilds_{};  ///< 公会名 -> 公会状态映射表
};

}  // namespace mir2
