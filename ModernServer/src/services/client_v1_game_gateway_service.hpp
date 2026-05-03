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
#include <vector>

#include "config/models.hpp"
#include "core/local_bus.hpp"
#include "protocol/legacy_types.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_gateway_service_base.hpp"
#include "storage/repository.hpp"

namespace mir2 {

class ClientV1GameGatewayService : public ClientV1GatewayServiceBase {
 public:
  explicit ClientV1GameGatewayService(std::shared_ptr<ClientV1AdmissionRegistry> admissions);

  void start(HostContext& context) override;
  void stop() override;
  void join() override;

 protected:
  PortBinding binding(const HostContext& context) const override;
  void handle_message(std::uint64_t session_id, const std::string& peer_address,
                      const client_v1::Message& message) override;
  void handle_connected(std::uint64_t session_id, const std::string& peer_address) override;
  void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason) override;

 private:
  struct SessionState {
    bool greeted{false};
    bool entered_world{false};
    bool pending_login_notice{false};
    bool world_result_sent{false};
    std::string account_id{};
    std::string character_name{};
    std::uint64_t actor_id{0};
    CharacterRecord character{};
    std::optional<client_v1::ActionIntent> pending_action{};
    std::array<client_v1::ItemState, kMaxBagItems> bag_items{};
    std::array<client_v1::ItemState, kMaxEquipSlots> equipment_items{};
    std::vector<client_v1::MagicEntry> magics{};
    std::uint64_t current_merchant_id{0};
    std::int32_t pending_sell_item_make_index{0};
    std::string pending_sell_item_name{};
    std::int32_t pending_repair_item_make_index{0};
    std::string pending_repair_item_name{};
    bool allow_group{false};
    bool group_visible{false};
    bool trade_visible{false};
    std::string trade_remote_name{};
    std::int32_t trade_local_gold{0};
    bool trade_local_accept{false};
    bool guild_visible{false};
  };

  void handle_client_hello(std::uint64_t session_id, const client_v1::ClientHello& hello);
  void handle_enter_world_request(std::uint64_t session_id,
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
  void post_enter_world(std::uint64_t session_id, const SessionState& state);
  void post_logic_command(LogicCommand command);
  void bus_loop();
  void handle_session_event(const SessionEvent& event);
  void translate_legacy_packet(std::uint64_t session_id, const LegacyPacket& packet,
                               std::vector<client_v1::Message>& messages);

  [[nodiscard]] std::optional<MapConfig> find_map(std::string_view map_id) const;
  [[nodiscard]] std::optional<SessionState> session(std::uint64_t session_id) const;

  std::shared_ptr<ClientV1AdmissionRegistry> admissions_{};
  std::unique_ptr<Repository> repository_{};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::thread bus_thread_{};
  std::atomic_bool bus_running_{false};
  mutable std::mutex mutex_{};
  std::unordered_map<std::uint64_t, SessionState> sessions_{};
};

}  // namespace mir2
