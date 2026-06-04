/**
 * @file client_v1_legacy_command_decoder.cpp
 * @brief Client v1 协议命令到规范命令转换的实现
 *
 * @details 本文件实现了 client_v1_legacy_command_decoder.hpp 中声明的所有转换函数。
 * 核心任务是将 Client v1 协议的结构化消息（如 ActionIntent、SpellIntent 等）
 * 转换为服务器内部统一的 CanonicalLegacyCommand 格式。
 *
 * 转换过程中需要：
 * 1. 将 Client v1 的动作类型映射为 CanonicalLegacyCommandKind
 * 2. 生成与 Legacy 协议兼容的 LegacyDefaultMessage（用于向后兼容）
 * 3. 提取并转换文本编码（使用 legacy_encode_text 确保兼容性）
 *
 * @note 大部分命令都使用 make_client_v1_command() 工厂函数统一创建命令框架，
 *       然后设置特定于该命令类型的字段值。
 * @see client_v1_legacy_command_decoder.hpp
 */

#include "protocol/client_v1_legacy_command_decoder.hpp"

#include <algorithm>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_string.hpp"
#include "shared/legacy/action_ids.hpp"

namespace mir2 {

namespace {

/**
 * @brief 构造一个 Client v1 来源的规范化命令框架
 *
 * @details 创建一个 CanonicalLegacyCommand 实例，设置来源协议为 client_v1、
 * 会话 ID 和命令类型。这是所有 Client v1 命令解码的统一入口工厂函数。
 *
 * @param session_id 会话标识符
 * @param kind 规范化命令类型
 * @return CanonicalLegacyCommand 构造完成的规范命令（不含具体参数）
 */
CanonicalLegacyCommand make_client_v1_command(std::uint64_t session_id,
                                              CanonicalLegacyCommandKind kind) {
  CanonicalLegacyCommand command;
  command.source_protocol = CanonicalSourceProtocol::client_v1;
  command.kind = kind;
  command.session_id = session_id;
  return command;
}

/**
 * @brief 获取运行时使用的 Legacy 动作消息标识符
 *
 * @details 根据 Client v1 的 WorldActionKind 和客户端请求的 Legacy 标识符，
 * 确定实际用于 LegacyDefaultMessage 的消息标识符。
 * 对于攻击动作，会通过 action_ids 模块进行规范化（将 sm_attack_ident 转换为 cm_attack_ident）。
 *
 * @param kind Client v1 的世界动作类型
 * @param requested_ident 客户端请求的 Legacy 标识符（仅攻击类型使用）
 * @return std::uint16_t 对应的 Legacy 消息标识符（kCmTurn/Walk/Run/Hit）
 */
std::uint16_t runtime_action_ident(client_v1::WorldActionKind kind,
                                   std::uint16_t requested_ident) {
  switch (kind) {
    case client_v1::WorldActionKind::turn:
      return kCmTurn;
    case client_v1::WorldActionKind::walk:
      return kCmWalk;
    case client_v1::WorldActionKind::run:
      return kCmRun;
    case client_v1::WorldActionKind::attack:
      return legacy::sm_attack_ident_to_cm(
          legacy::normalize_attack_ident_to_sm(requested_ident));
  }
  return kCmHit;
}

/**
 * @brief 将 Client v1 动作类型映射为规范化命令类型
 *
 * @param kind Client v1 的世界动作类型
 * @return CanonicalLegacyCommandKind 对应的规范化命令类型
 */
CanonicalLegacyCommandKind command_kind_for_action(client_v1::WorldActionKind kind) {
  switch (kind) {
    case client_v1::WorldActionKind::turn:
      return CanonicalLegacyCommandKind::turn;
    case client_v1::WorldActionKind::walk:
      return CanonicalLegacyCommandKind::walk;
    case client_v1::WorldActionKind::run:
      return CanonicalLegacyCommandKind::run;
    case client_v1::WorldActionKind::attack:
      return CanonicalLegacyCommandKind::attack;
  }
  return CanonicalLegacyCommandKind::walk;
}

}  // namespace

CanonicalLegacyCommand decode_client_v1_action_command(
    std::uint64_t session_id, const client_v1::ActionIntent& intent) {
  auto command = make_client_v1_command(session_id, command_kind_for_action(intent.kind));
  command.x = intent.x;
  command.y = intent.y;
  command.dir = intent.dir;
  command.target_actor_id = intent.target_actor_id;
  // 构建兼容 Legacy 协议的消息头
  command.game_message = make_default_message(
      runtime_action_ident(intent.kind, intent.legacy_ident), make_long(intent.x, intent.y), 0,
      intent.dir, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_spell_command(
    std::uint64_t session_id, const client_v1::SpellIntent& intent) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::spell);
  command.x = intent.x;
  command.y = intent.y;
  command.dir = intent.dir;
  command.target_actor_id = intent.target_actor_id;
  // 法术 ID 以文本形式保存在 text 字段中
  command.text = copy_legacy_bytes(std::to_string(intent.magic_id));
  // Legacy 消息头：target_actor_id 拆分为 param（低16位）和 series（高16位）
  command.game_message =
      make_default_message(kCmSpell, make_long(intent.x, intent.y),
                           static_cast<std::uint16_t>(intent.target_actor_id & 0xFFFFU),
                           intent.magic_id,
                           static_cast<std::uint16_t>((intent.target_actor_id >> 16U) & 0xFFFFU));
  return command;
}

CanonicalLegacyCommand decode_client_v1_pickup_command(
    std::uint64_t session_id, const client_v1::PickupIntent& intent) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::pickup_item);
  command.x = intent.x;
  command.y = intent.y;
  command.game_message = make_default_message(kCmPickup, 0, intent.x, intent.y, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_use_item_command(
    std::uint64_t session_id, const client_v1::UseItemIntent& intent) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::eat_item);
  command.item_make_index = intent.item_make_index;
  command.item_slot = intent.item_slot;
  command.text = copy_legacy_bytes(intent.name);
  command.game_message = make_default_message(kCmEat, intent.item_make_index, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_equip_item_command(
    std::uint64_t session_id, const client_v1::EquipItemRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::take_on_item);
  command.item_make_index = request.item_make_index;
  command.item_slot = request.equipment_slot;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmTakeOnItem, request.item_make_index,
                           static_cast<std::uint16_t>(std::max(request.equipment_slot, 0)), 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_unequip_item_command(
    std::uint64_t session_id, const client_v1::UnequipItemRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::take_off_item);
  command.item_make_index = request.item_make_index;
  command.item_slot = request.equipment_slot;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmTakeOffItem, request.item_make_index,
                           static_cast<std::uint16_t>(std::max(request.equipment_slot, 0)), 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_drop_item_command(
    std::uint64_t session_id, const client_v1::DropItemRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::drop_item);
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message = make_default_message(kCmDropItem, request.item_make_index, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_drop_gold_command(
    std::uint64_t session_id, const client_v1::DropGoldRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::drop_gold);
  command.amount = request.amount;
  command.game_message = make_default_message(kCmDropGold, request.amount, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_revive_command(std::uint64_t session_id) {
  return make_client_v1_command(session_id, CanonicalLegacyCommandKind::revive);
}

CanonicalLegacyCommand decode_client_v1_npc_click_command(
    std::uint64_t session_id, const client_v1::NpcClickRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::click_npc);
  command.target_actor_id = request.actor_id;
  command.game_message =
      make_default_message(kCmClickNpc, static_cast<std::int32_t>(request.actor_id), 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_npc_dialog_select_command(
    std::uint64_t session_id, std::uint64_t merchant_id, std::string_view selection) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::merchant_select);
  command.target_actor_id = merchant_id;
  command.text = copy_legacy_bytes(selection);
  command.game_message =
      make_default_message(kCmMerchantDlgSelect, static_cast<std::int32_t>(merchant_id), 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_merchant_buy_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantBuyRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::buy_item);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_server_index;
  command.text = copy_legacy_bytes(request.name);
  // item_server_index 拆分为低16位/高16位填入 Legacy 消息头的 param/tag
  command.game_message = make_default_message(
      kCmUserBuyItem, static_cast<std::int32_t>(merchant_id),
      low_word(request.item_server_index), high_word(request.item_server_index), 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_merchant_sell_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::sell_item);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmUserSellItem, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_merchant_sell_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellPriceRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::query_sell_price);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmMerchantQuerySellPrice, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_merchant_repair_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairPriceRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::query_repair_cost);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmMerchantQueryRepairCost, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_merchant_repair_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::repair_item);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmUserRepairItem, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_storage_deposit_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageDepositRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::storage_item);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmUserStorageItem, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_storage_withdraw_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageWithdrawRequest& request) {
  auto command =
      make_client_v1_command(session_id, CanonicalLegacyCommandKind::take_back_storage_item);
  command.target_actor_id = merchant_id;
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message =
      make_default_message(kCmUserTakeBackStorageItem, static_cast<std::int32_t>(merchant_id),
                           low_word(request.item_make_index), high_word(request.item_make_index),
                           0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_query_bag_items_command(std::uint64_t session_id) {
  return make_client_v1_command(session_id, CanonicalLegacyCommandKind::query_bag_items);
}

CanonicalLegacyCommand decode_client_v1_query_storage_items_command(
    std::uint64_t session_id, std::uint64_t merchant_id) {
  auto command =
      make_client_v1_command(session_id, CanonicalLegacyCommandKind::query_storage_items);
  command.target_actor_id = merchant_id;
  // 复用 kCmUserTakeBackStorageItem 作为 Legacy 消息标识符
  command.game_message =
      make_default_message(kCmUserTakeBackStorageItem, static_cast<std::int32_t>(merchant_id),
                           0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_try_command(
    std::uint64_t session_id, const client_v1::TradeTryRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_try);
  command.text = copy_legacy_bytes(request.target_name);
  command.game_message = make_default_message(kCmDealTry, 0, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_cancel_command(std::uint64_t session_id) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_cancel);
  command.game_message = make_default_message(kCmDealCancel, 0, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_add_item_command(
    std::uint64_t session_id, const client_v1::TradeAddItemRequest& request) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_add_item);
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message = make_default_message(kCmDealAddItem, request.item_make_index, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_remove_item_command(
    std::uint64_t session_id, const client_v1::TradeRemoveItemRequest& request) {
  auto command =
      make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_remove_item);
  command.item_make_index = request.item_make_index;
  command.text = copy_legacy_bytes(request.name);
  command.game_message = make_default_message(kCmDealDelItem, request.item_make_index, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_set_gold_command(
    std::uint64_t session_id, std::int32_t amount) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_set_gold);
  command.amount = amount;
  command.game_message = make_default_message(kCmDealChangeGold, amount, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_trade_accept_command(std::uint64_t session_id) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::trade_accept);
  command.game_message = make_default_message(kCmDealEnd, 0, 0, 0, 0);
  return command;
}

CanonicalLegacyCommand decode_client_v1_chat_command(std::uint64_t session_id,
                                                     const client_v1::ChatSend& chat) {
  auto command = make_client_v1_command(session_id, CanonicalLegacyCommandKind::say);
  command.text = copy_legacy_bytes(chat.text);
  return command;
}

}  // namespace mir2
