#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/canonical_legacy_command.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

[[nodiscard]] CanonicalLegacyCommand decode_client_v1_action_command(
    std::uint64_t session_id, const client_v1::ActionIntent& intent);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_spell_command(
    std::uint64_t session_id, const client_v1::SpellIntent& intent);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_pickup_command(
    std::uint64_t session_id, const client_v1::PickupIntent& intent);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_use_item_command(
    std::uint64_t session_id, const client_v1::UseItemIntent& intent);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_equip_item_command(
    std::uint64_t session_id, const client_v1::EquipItemRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_unequip_item_command(
    std::uint64_t session_id, const client_v1::UnequipItemRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_drop_item_command(
    std::uint64_t session_id, const client_v1::DropItemRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_drop_gold_command(
    std::uint64_t session_id, const client_v1::DropGoldRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_revive_command(std::uint64_t session_id);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_npc_click_command(
    std::uint64_t session_id, const client_v1::NpcClickRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_npc_dialog_select_command(
    std::uint64_t session_id, std::uint64_t merchant_id, std::string_view selection);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_buy_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantBuyRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_sell_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_sell_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellPriceRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_repair_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairPriceRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_repair_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_storage_deposit_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageDepositRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_storage_withdraw_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageWithdrawRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_query_bag_items_command(
    std::uint64_t session_id);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_query_storage_items_command(
    std::uint64_t session_id, std::uint64_t merchant_id);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_try_command(
    std::uint64_t session_id, const client_v1::TradeTryRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_cancel_command(
    std::uint64_t session_id);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_add_item_command(
    std::uint64_t session_id, const client_v1::TradeAddItemRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_remove_item_command(
    std::uint64_t session_id, const client_v1::TradeRemoveItemRequest& request);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_set_gold_command(
    std::uint64_t session_id, std::int32_t amount);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_accept_command(
    std::uint64_t session_id);
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_chat_command(
    std::uint64_t session_id, const client_v1::ChatSend& chat);

}  // namespace mir2
