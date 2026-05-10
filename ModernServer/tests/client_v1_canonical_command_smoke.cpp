#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "protocol/client_v1_legacy_command_decoder.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "shared/legacy/action_ids.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "client_v1_canonical_command_smoke failed at " << stage << '\n';
  return 1;
}

bool same_message(const mir2::LegacyDefaultMessage& lhs,
                  const mir2::LegacyDefaultMessage& rhs) {
  return lhs.ident == rhs.ident && lhs.recog == rhs.recog && lhs.param == rhs.param &&
         lhs.tag == rhs.tag && lhs.series == rhs.series;
}

bool check_common(const mir2::CanonicalLegacyCommand& canonical,
                  mir2::CanonicalLegacyCommandKind canonical_kind,
                  mir2::LogicCommandKind logic_kind) {
  const auto logic = mir2::to_logic_command(canonical);
  return canonical.source_protocol == mir2::CanonicalSourceProtocol::client_v1 &&
         canonical.kind == canonical_kind && canonical.session_id == 77 &&
         logic.kind == logic_kind && logic.session_id == canonical.session_id &&
         logic.x == canonical.x && logic.y == canonical.y && logic.dir == canonical.dir &&
         logic.target_actor_id == canonical.target_actor_id &&
         logic.item_make_index == canonical.item_make_index &&
         logic.item_slot == canonical.item_slot && logic.amount == canonical.amount &&
         logic.text == canonical.text && same_message(logic.game_message, canonical.game_message);
}

bool check_action() {
  auto command = mir2::decode_client_v1_action_command(
      77, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::walk, 11, 12, 3,
                                        0, 0});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::walk,
                    mir2::LogicCommandKind::walk) ||
      command.x != 11 || command.y != 12 || command.dir != 3 ||
      command.game_message.ident != mir2::kCmWalk) {
    return false;
  }

  command = mir2::decode_client_v1_action_command(
      77, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 13, 14,
                                        5, 999, mir2::legacy::kSmPowerHit});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::attack,
                    mir2::LogicCommandKind::attack) ||
      command.target_actor_id != 999 || command.game_message.ident != mir2::kCmPowerHit) {
    return false;
  }

  command = mir2::decode_client_v1_action_command(
      77, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 13, 14,
                                        5, 999, mir2::legacy::kCmFireHit});
  return check_common(command, mir2::CanonicalLegacyCommandKind::attack,
                      mir2::LogicCommandKind::attack) &&
         command.game_message.ident == mir2::kCmFireHit;
}

bool check_combat_and_items() {
  auto command = mir2::decode_client_v1_spell_command(
      77, mir2::client_v1::SpellIntent{21, 22, 6, 0x12345, 9});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::spell,
                    mir2::LogicCommandKind::spell) ||
      command.x != 21 || command.y != 22 || command.dir != 6 ||
      command.target_actor_id != 0x12345 || command.text != "9" ||
      command.game_message.ident != mir2::kCmSpell || command.game_message.param != 0x2345 ||
      command.game_message.series != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_pickup_command(77, mir2::client_v1::PickupIntent{31, 32});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::pickup_item,
                    mir2::LogicCommandKind::pickup_item) ||
      command.x != 31 || command.y != 32 || command.game_message.ident != mir2::kCmPickup) {
    return false;
  }

  command = mir2::decode_client_v1_use_item_command(
      77, mir2::client_v1::UseItemIntent{1001, 3, "Potion"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::eat_item,
                    mir2::LogicCommandKind::eat_item) ||
      command.item_make_index != 1001 || command.item_slot != 3 || command.text != "Potion" ||
      command.game_message.ident != mir2::kCmEat) {
    return false;
  }

  command = mir2::decode_client_v1_equip_item_command(
      77, mir2::client_v1::EquipItemRequest{1, 1002, "Sword"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::take_on_item,
                    mir2::LogicCommandKind::take_on_item) ||
      command.item_make_index != 1002 || command.item_slot != 1 ||
      command.game_message.ident != mir2::kCmTakeOnItem) {
    return false;
  }

  command = mir2::decode_client_v1_unequip_item_command(
      77, mir2::client_v1::UnequipItemRequest{1, 1002, "Sword"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::take_off_item,
                    mir2::LogicCommandKind::take_off_item) ||
      command.item_make_index != 1002 || command.item_slot != 1 ||
      command.game_message.ident != mir2::kCmTakeOffItem) {
    return false;
  }

  command = mir2::decode_client_v1_drop_item_command(
      77, mir2::client_v1::DropItemRequest{1003, "Ore"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::drop_item,
                    mir2::LogicCommandKind::drop_item) ||
      command.item_make_index != 1003 || command.text != "Ore" ||
      command.game_message.ident != mir2::kCmDropItem) {
    return false;
  }

  command = mir2::decode_client_v1_revive_command(77);
  return check_common(command, mir2::CanonicalLegacyCommandKind::revive,
                      mir2::LogicCommandKind::revive);
}

bool check_npc_merchant_storage() {
  auto command =
      mir2::decode_client_v1_npc_click_command(77, mir2::client_v1::NpcClickRequest{42});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::click_npc,
                    mir2::LogicCommandKind::click_npc) ||
      command.target_actor_id != 42 || command.game_message.ident != mir2::kCmClickNpc) {
    return false;
  }

  command = mir2::decode_client_v1_npc_dialog_select_command(77, 42, "@buy");
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::merchant_select,
                    mir2::LogicCommandKind::merchant_select) ||
      command.target_actor_id != 42 || command.text != "@buy" ||
      command.game_message.ident != mir2::kCmMerchantDlgSelect) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_buy_command(
      77, 42, mir2::client_v1::MerchantBuyRequest{0, 555, "Drug"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::buy_item,
                    mir2::LogicCommandKind::buy_item) ||
      command.target_actor_id != 42 || command.item_make_index != 555 ||
      command.text != "Drug" || command.game_message.ident != mir2::kCmUserBuyItem) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_sell_command(
      77, 42, mir2::client_v1::MerchantSellRequest{0, 1004, "Ruby"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::sell_item,
                    mir2::LogicCommandKind::sell_item) ||
      command.item_make_index != 1004 || command.text != "Ruby" ||
      command.game_message.ident != mir2::kCmUserSellItem) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_sell_price_command(
      77, 42, mir2::client_v1::MerchantSellPriceRequest{0, 1004, "Ruby"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::query_sell_price,
                    mir2::LogicCommandKind::query_sell_price) ||
      command.game_message.ident != mir2::kCmMerchantQuerySellPrice) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_repair_price_command(
      77, 42, mir2::client_v1::MerchantRepairPriceRequest{0, 1005, "Sword"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::query_repair_cost,
                    mir2::LogicCommandKind::query_repair_cost) ||
      command.game_message.ident != mir2::kCmMerchantQueryRepairCost) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_repair_command(
      77, 42, mir2::client_v1::MerchantRepairRequest{0, 1005, "Sword"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::repair_item,
                    mir2::LogicCommandKind::repair_item) ||
      command.game_message.ident != mir2::kCmUserRepairItem) {
    return false;
  }

  command = mir2::decode_client_v1_storage_deposit_command(
      77, 42, mir2::client_v1::StorageDepositRequest{0, 1006, "Ring"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::storage_item,
                    mir2::LogicCommandKind::storage_item) ||
      command.game_message.ident != mir2::kCmUserStorageItem) {
    return false;
  }

  command = mir2::decode_client_v1_storage_withdraw_command(
      77, 42, mir2::client_v1::StorageWithdrawRequest{0, 1007, "Ring"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::take_back_storage_item,
                    mir2::LogicCommandKind::take_back_storage_item) ||
      command.game_message.ident != mir2::kCmUserTakeBackStorageItem) {
    return false;
  }

  command = mir2::decode_client_v1_query_bag_items_command(77);
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::query_bag_items,
                    mir2::LogicCommandKind::query_bag_items)) {
    return false;
  }

  command = mir2::decode_client_v1_query_storage_items_command(77, 42);
  return check_common(command, mir2::CanonicalLegacyCommandKind::query_storage_items,
                      mir2::LogicCommandKind::query_storage_items) &&
         command.target_actor_id == 42 &&
         command.game_message.ident == mir2::kCmUserTakeBackStorageItem;
}

bool check_trade_chat() {
  std::string legacy_bytes;
  legacy_bytes.push_back(static_cast<char>(0xB0));
  legacy_bytes.push_back(static_cast<char>(0xA1));
  legacy_bytes += "Other";

  auto command = mir2::decode_client_v1_trade_try_command(
      77, mir2::client_v1::TradeTryRequest{legacy_bytes});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_try,
                    mir2::LogicCommandKind::trade_try) ||
      command.text != legacy_bytes) {
    return false;
  }

  command = mir2::decode_client_v1_trade_cancel_command(77);
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_cancel,
                    mir2::LogicCommandKind::trade_cancel)) {
    return false;
  }

  command = mir2::decode_client_v1_trade_add_item_command(
      77, mir2::client_v1::TradeAddItemRequest{1008, "Gem"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_add_item,
                    mir2::LogicCommandKind::trade_add_item) ||
      command.item_make_index != 1008 || command.text != "Gem") {
    return false;
  }

  command = mir2::decode_client_v1_trade_remove_item_command(
      77, mir2::client_v1::TradeRemoveItemRequest{1008, "Gem"});
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_remove_item,
                    mir2::LogicCommandKind::trade_remove_item) ||
      command.item_make_index != 1008 || command.text != "Gem") {
    return false;
  }

  command = mir2::decode_client_v1_trade_set_gold_command(77, 123);
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_set_gold,
                    mir2::LogicCommandKind::trade_set_gold) ||
      command.amount != 123) {
    return false;
  }

  command = mir2::decode_client_v1_trade_accept_command(77);
  if (!check_common(command, mir2::CanonicalLegacyCommandKind::trade_accept,
                    mir2::LogicCommandKind::trade_accept)) {
    return false;
  }

  command = mir2::decode_client_v1_chat_command(77, mir2::client_v1::ChatSend{legacy_bytes});
  return check_common(command, mir2::CanonicalLegacyCommandKind::say,
                      mir2::LogicCommandKind::say) &&
         command.text == legacy_bytes;
}

}  // namespace

int main() {
  if (!check_action()) {
    return fail("action");
  }
  if (!check_combat_and_items()) {
    return fail("combat and items");
  }
  if (!check_npc_merchant_storage()) {
    return fail("npc merchant storage");
  }
  if (!check_trade_chat()) {
    return fail("trade chat");
  }
  return 0;
}
