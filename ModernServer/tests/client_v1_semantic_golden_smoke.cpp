#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "protocol/client_v1_legacy_command_decoder.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "shared/legacy/action_ids.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "client_v1_semantic_golden_smoke failed at " << stage << '\n';
  return 1;
}

bool same_message(const mir2::LegacyDefaultMessage& lhs,
                  const mir2::LegacyDefaultMessage& rhs) {
  return lhs.recog == rhs.recog && lhs.ident == rhs.ident && lhs.param == rhs.param &&
         lhs.tag == rhs.tag && lhs.series == rhs.series;
}

bool expect_common(const mir2::CanonicalLegacyCommand& command,
                   mir2::CanonicalLegacyCommandKind canonical_kind,
                   mir2::LogicCommandKind logic_kind) {
  const auto logic = mir2::to_logic_command(command);
  return command.source_protocol == mir2::CanonicalSourceProtocol::client_v1 &&
         command.session_id == 99 && command.kind == canonical_kind &&
         logic.kind == logic_kind && logic.session_id == command.session_id &&
         logic.x == command.x && logic.y == command.y && logic.dir == command.dir &&
         logic.target_actor_id == command.target_actor_id &&
         logic.item_make_index == command.item_make_index &&
         logic.item_slot == command.item_slot && logic.amount == command.amount &&
         logic.text == command.text && same_message(logic.game_message, command.game_message);
}

std::string gbk_text() {
  std::string text;
  text.push_back(static_cast<char>(0xB0));
  text.push_back(static_cast<char>(0xA1));
  text += "Hero";
  return text;
}

bool check_action_goldens() {
  auto command = mir2::decode_client_v1_action_command(
      99, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::turn, 11, 12, 3,
                                        0, 0});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::turn,
                     mir2::LogicCommandKind::turn) ||
      command.game_message.ident != mir2::kCmTurn ||
      command.game_message.recog != mir2::make_long(11, 12) ||
      command.game_message.tag != 3) {
    return false;
  }

  command = mir2::decode_client_v1_action_command(
      99, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::run, 21, 22, 5,
                                        0, 0});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::run,
                     mir2::LogicCommandKind::run) ||
      command.game_message.ident != mir2::kCmRun ||
      command.game_message.recog != mir2::make_long(21, 22) ||
      command.game_message.tag != 5) {
    return false;
  }

  command = mir2::decode_client_v1_action_command(
      99, mir2::client_v1::ActionIntent{mir2::client_v1::WorldActionKind::attack, 31, 32,
                                        7, 0x10203, mir2::legacy::kSmPowerHit});
  return expect_common(command, mir2::CanonicalLegacyCommandKind::attack,
                       mir2::LogicCommandKind::attack) &&
         command.target_actor_id == 0x10203 && command.game_message.ident == mir2::kCmPowerHit &&
         command.game_message.recog == mir2::make_long(31, 32) && command.game_message.tag == 7;
}

bool check_spell_and_inventory_goldens() {
  auto command = mir2::decode_client_v1_spell_command(
      99, mir2::client_v1::SpellIntent{41, 42, 6, 0x12345, 9});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::spell,
                     mir2::LogicCommandKind::spell) ||
      command.text != "9" || command.game_message.ident != mir2::kCmSpell ||
      command.game_message.recog != mir2::make_long(41, 42) ||
      command.game_message.param != 0x2345 || command.game_message.tag != 9 ||
      command.game_message.series != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_pickup_command(99, mir2::client_v1::PickupIntent{51, 52});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::pickup_item,
                     mir2::LogicCommandKind::pickup_item) ||
      command.game_message.ident != mir2::kCmPickup || command.game_message.param != 51 ||
      command.game_message.tag != 52) {
    return false;
  }

  command = mir2::decode_client_v1_use_item_command(
      99, mir2::client_v1::UseItemIntent{1001, 3, "Potion"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::eat_item,
                     mir2::LogicCommandKind::eat_item) ||
      command.item_make_index != 1001 || command.item_slot != 3 || command.text != "Potion" ||
      command.game_message.ident != mir2::kCmEat || command.game_message.recog != 1001) {
    return false;
  }

  command = mir2::decode_client_v1_equip_item_command(
      99, mir2::client_v1::EquipItemRequest{2, 1002, "Sword"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::take_on_item,
                     mir2::LogicCommandKind::take_on_item) ||
      command.item_slot != 2 || command.game_message.ident != mir2::kCmTakeOnItem ||
      command.game_message.recog != 1002 || command.game_message.param != 2) {
    return false;
  }

  command = mir2::decode_client_v1_unequip_item_command(
      99, mir2::client_v1::UnequipItemRequest{2, 1002, "Sword"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::take_off_item,
                     mir2::LogicCommandKind::take_off_item) ||
      command.item_slot != 2 || command.game_message.ident != mir2::kCmTakeOffItem ||
      command.game_message.recog != 1002 || command.game_message.param != 2) {
    return false;
  }

  command = mir2::decode_client_v1_drop_item_command(
      99, mir2::client_v1::DropItemRequest{1003, "Ore"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::drop_item,
                     mir2::LogicCommandKind::drop_item) ||
      command.text != "Ore" || command.game_message.ident != mir2::kCmDropItem ||
      command.game_message.recog != 1003) {
    return false;
  }

  command = mir2::decode_client_v1_drop_gold_command(
      99, mir2::client_v1::DropGoldRequest{250});
  return expect_common(command, mir2::CanonicalLegacyCommandKind::drop_gold,
                       mir2::LogicCommandKind::drop_gold) &&
         command.amount == 250 && command.game_message.ident == mir2::kCmDropGold &&
         command.game_message.recog == 250;
}

bool check_npc_merchant_storage_goldens() {
  auto command =
      mir2::decode_client_v1_npc_click_command(99, mir2::client_v1::NpcClickRequest{42});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::click_npc,
                     mir2::LogicCommandKind::click_npc) ||
      command.target_actor_id != 42 || command.game_message.ident != mir2::kCmClickNpc ||
      command.game_message.recog != 42) {
    return false;
  }

  command = mir2::decode_client_v1_npc_dialog_select_command(99, 42, "@buy");
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::merchant_select,
                     mir2::LogicCommandKind::merchant_select) ||
      command.text != "@buy" || command.game_message.ident != mir2::kCmMerchantDlgSelect ||
      command.game_message.recog != 42) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_buy_command(
      99, 42, mir2::client_v1::MerchantBuyRequest{0, 0x12345, "Drug"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::buy_item,
                     mir2::LogicCommandKind::buy_item) ||
      command.game_message.ident != mir2::kCmUserBuyItem ||
      command.game_message.param != 0x2345 || command.game_message.tag != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_sell_price_command(
      99, 42, mir2::client_v1::MerchantSellPriceRequest{0, 0x12346, "Ruby"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::query_sell_price,
                     mir2::LogicCommandKind::query_sell_price) ||
      command.game_message.ident != mir2::kCmMerchantQuerySellPrice ||
      command.game_message.param != 0x2346 || command.game_message.tag != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_merchant_repair_command(
      99, 42, mir2::client_v1::MerchantRepairRequest{0, 0x12347, "Sword"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::repair_item,
                     mir2::LogicCommandKind::repair_item) ||
      command.game_message.ident != mir2::kCmUserRepairItem ||
      command.game_message.param != 0x2347 || command.game_message.tag != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_storage_deposit_command(
      99, 42, mir2::client_v1::StorageDepositRequest{0, 0x12348, "Ring"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::storage_item,
                     mir2::LogicCommandKind::storage_item) ||
      command.game_message.ident != mir2::kCmUserStorageItem ||
      command.game_message.param != 0x2348 || command.game_message.tag != 0x0001) {
    return false;
  }

  command = mir2::decode_client_v1_query_storage_items_command(99, 42);
  return expect_common(command, mir2::CanonicalLegacyCommandKind::query_storage_items,
                       mir2::LogicCommandKind::query_storage_items) &&
         command.target_actor_id == 42 &&
         command.game_message.ident == mir2::kCmUserTakeBackStorageItem &&
         command.game_message.recog == 42;
}

bool check_trade_and_chat_goldens() {
  const auto name = gbk_text();
  auto command = mir2::decode_client_v1_trade_try_command(
      99, mir2::client_v1::TradeTryRequest{name});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::trade_try,
                     mir2::LogicCommandKind::trade_try) ||
      command.text != name || command.game_message.ident != mir2::kCmDealTry) {
    return false;
  }

  command = mir2::decode_client_v1_trade_add_item_command(
      99, mir2::client_v1::TradeAddItemRequest{1008, "Gem"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::trade_add_item,
                     mir2::LogicCommandKind::trade_add_item) ||
      command.game_message.ident != mir2::kCmDealAddItem ||
      command.game_message.recog != 1008) {
    return false;
  }

  command = mir2::decode_client_v1_trade_remove_item_command(
      99, mir2::client_v1::TradeRemoveItemRequest{1008, "Gem"});
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::trade_remove_item,
                     mir2::LogicCommandKind::trade_remove_item) ||
      command.game_message.ident != mir2::kCmDealDelItem ||
      command.game_message.recog != 1008) {
    return false;
  }

  command = mir2::decode_client_v1_trade_set_gold_command(99, 123);
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::trade_set_gold,
                     mir2::LogicCommandKind::trade_set_gold) ||
      command.amount != 123 || command.game_message.ident != mir2::kCmDealChangeGold ||
      command.game_message.recog != 123) {
    return false;
  }

  command = mir2::decode_client_v1_trade_accept_command(99);
  if (!expect_common(command, mir2::CanonicalLegacyCommandKind::trade_accept,
                     mir2::LogicCommandKind::trade_accept) ||
      command.game_message.ident != mir2::kCmDealEnd) {
    return false;
  }

  command = mir2::decode_client_v1_chat_command(99, mir2::client_v1::ChatSend{name});
  return expect_common(command, mir2::CanonicalLegacyCommandKind::say,
                       mir2::LogicCommandKind::say) &&
         command.text == name && command.game_message.ident == 0;
}

}  // namespace

int main() {
  if (!check_action_goldens()) {
    return fail("action");
  }
  if (!check_spell_and_inventory_goldens()) {
    return fail("spell inventory");
  }
  if (!check_npc_merchant_storage_goldens()) {
    return fail("npc merchant storage");
  }
  if (!check_trade_and_chat_goldens()) {
    return fail("trade chat");
  }
  return 0;
}
