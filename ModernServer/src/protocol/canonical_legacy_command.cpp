#include "protocol/canonical_legacy_command.hpp"

#include <utility>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_string.hpp"

namespace mir2 {

namespace {

LogicCommandKind to_logic_kind(CanonicalLegacyCommandKind kind) {
  switch (kind) {
    case CanonicalLegacyCommandKind::turn:
      return LogicCommandKind::turn;
    case CanonicalLegacyCommandKind::walk:
      return LogicCommandKind::walk;
    case CanonicalLegacyCommandKind::run:
      return LogicCommandKind::run;
    case CanonicalLegacyCommandKind::attack:
      return LogicCommandKind::attack;
    case CanonicalLegacyCommandKind::spell:
      return LogicCommandKind::spell;
    case CanonicalLegacyCommandKind::say:
      return LogicCommandKind::say;
    case CanonicalLegacyCommandKind::click_npc:
      return LogicCommandKind::click_npc;
    case CanonicalLegacyCommandKind::merchant_select:
      return LogicCommandKind::merchant_select;
    case CanonicalLegacyCommandKind::query_username:
      return LogicCommandKind::query_username;
    case CanonicalLegacyCommandKind::query_bag_items:
      return LogicCommandKind::query_bag_items;
    case CanonicalLegacyCommandKind::query_storage_items:
      return LogicCommandKind::query_storage_items;
    case CanonicalLegacyCommandKind::query_detail_goods:
      return LogicCommandKind::query_detail_goods;
    case CanonicalLegacyCommandKind::query_sell_price:
      return LogicCommandKind::query_sell_price;
    case CanonicalLegacyCommandKind::query_repair_cost:
      return LogicCommandKind::query_repair_cost;
    case CanonicalLegacyCommandKind::drop_item:
      return LogicCommandKind::drop_item;
    case CanonicalLegacyCommandKind::pickup_item:
      return LogicCommandKind::pickup_item;
    case CanonicalLegacyCommandKind::take_on_item:
      return LogicCommandKind::take_on_item;
    case CanonicalLegacyCommandKind::take_off_item:
      return LogicCommandKind::take_off_item;
    case CanonicalLegacyCommandKind::eat_item:
      return LogicCommandKind::eat_item;
    case CanonicalLegacyCommandKind::drop_gold:
      return LogicCommandKind::drop_gold;
    case CanonicalLegacyCommandKind::revive:
      return LogicCommandKind::revive;
    case CanonicalLegacyCommandKind::buy_item:
      return LogicCommandKind::buy_item;
    case CanonicalLegacyCommandKind::sell_item:
      return LogicCommandKind::sell_item;
    case CanonicalLegacyCommandKind::repair_item:
      return LogicCommandKind::repair_item;
    case CanonicalLegacyCommandKind::storage_item:
      return LogicCommandKind::storage_item;
    case CanonicalLegacyCommandKind::take_back_storage_item:
      return LogicCommandKind::take_back_storage_item;
    case CanonicalLegacyCommandKind::trade_try:
      return LogicCommandKind::trade_try;
    case CanonicalLegacyCommandKind::trade_cancel:
      return LogicCommandKind::trade_cancel;
    case CanonicalLegacyCommandKind::trade_add_item:
      return LogicCommandKind::trade_add_item;
    case CanonicalLegacyCommandKind::trade_remove_item:
      return LogicCommandKind::trade_remove_item;
    case CanonicalLegacyCommandKind::trade_set_gold:
      return LogicCommandKind::trade_set_gold;
    case CanonicalLegacyCommandKind::trade_accept:
      return LogicCommandKind::trade_accept;
  }
  return LogicCommandKind::raw_packet;
}

CanonicalLegacyCommand make_command(std::uint64_t session_id, const LegacyPacket& packet,
                                    const LegacyDefaultMessage& message,
                                    CanonicalLegacyCommandKind kind) {
  CanonicalLegacyCommand command;
  command.source_protocol = CanonicalSourceProtocol::legacy_framed;
  command.kind = kind;
  command.session_id = session_id;
  command.game_message = message;
  command.packet = packet;
  return command;
}

CanonicalLegacyDecodeResult ok(CanonicalLegacyCommand command) {
  CanonicalLegacyDecodeResult result;
  result.status = CanonicalParseStatus::ok;
  result.game_message = command.game_message;
  result.command = std::move(command);
  return result;
}

}  // namespace

CanonicalLegacyDecodeResult decode_legacy_game_command(std::uint64_t session_id,
                                                       const LegacyPacket& packet) {
  const auto decoded = decode_legacy_game_packet(packet);
  if (!decoded.has_value()) {
    return {};
  }

  CanonicalLegacyDecodeResult unsupported;
  unsupported.status = CanonicalParseStatus::unsupported_ident;
  unsupported.game_message = decoded->message;

  switch (decoded->message.ident) {
    case kCmTurn: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::turn);
      command.x = low_word(decoded->message.recog);
      command.y = high_word(decoded->message.recog);
      command.dir = static_cast<std::uint8_t>(decoded->message.tag);
      return ok(std::move(command));
    }
    case kCmWalk: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::walk);
      command.x = low_word(decoded->message.recog);
      command.y = high_word(decoded->message.recog);
      command.dir = static_cast<std::uint8_t>(decoded->message.tag);
      return ok(std::move(command));
    }
    case kCmRun: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::run);
      command.x = low_word(decoded->message.recog);
      command.y = high_word(decoded->message.recog);
      command.dir = static_cast<std::uint8_t>(decoded->message.tag);
      return ok(std::move(command));
    }
    case kCmHit:
    case kCmHeavyHit:
    case kCmBigHit:
    case kCmPowerHit:
    case kCmLongHit:
    case kCmWideHit:
    case kCmFireHit:
    case kCmCrossHit: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::attack);
      command.x = low_word(decoded->message.recog);
      command.y = high_word(decoded->message.recog);
      command.dir = static_cast<std::uint8_t>(decoded->message.tag);
      return ok(std::move(command));
    }
    case kCmSpell: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::spell);
      command.x = low_word(decoded->message.recog);
      command.y = high_word(decoded->message.recog);
      command.target_actor_id = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(make_long(decoded->message.param, decoded->message.series)));
      command.text = copy_legacy_bytes(decoded->body);
      return ok(std::move(command));
    }
    case kCmSay: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::say);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmClickNpc: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::click_npc);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      return ok(std::move(command));
    }
    case kCmMerchantDlgSelect: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::merchant_select);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmQueryUsername: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_username);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.x = decoded->message.param;
      command.y = decoded->message.tag;
      return ok(std::move(command));
    }
    case kCmQueryBagItems:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::query_bag_items));
    case kCmUserStorageItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::storage_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmUserTakeBackStorageItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_back_storage_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmUserGetDetailItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_detail_goods);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmMerchantQuerySellPrice: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_sell_price);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmMerchantQueryRepairCost: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_repair_cost);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmDropItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::drop_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmPickup: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::pickup_item);
      command.x = decoded->message.param;
      command.y = decoded->message.tag;
      return ok(std::move(command));
    }
    case kCmTakeOnItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_on_item);
      command.item_make_index = decoded->message.recog;
      command.item_slot = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmTakeOffItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_off_item);
      command.item_make_index = decoded->message.recog;
      command.item_slot = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmEat: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::eat_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmDropGold: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::drop_gold);
      command.amount = decoded->message.recog;
      return ok(std::move(command));
    }
    case kCmDealTry: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_try);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmDealAddItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_add_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmDealDelItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_remove_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmDealCancel:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::trade_cancel));
    case kCmDealChangeGold: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_set_gold);
      command.amount = decoded->message.recog;
      return ok(std::move(command));
    }
    case kCmDealEnd:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::trade_accept));
    case kCmUserSellItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::sell_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmUserBuyItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::buy_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    case kCmUserRepairItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::repair_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    default:
      return unsupported;
  }
}

LogicCommand to_logic_command(const CanonicalLegacyCommand& command) {
  LogicCommand logic;
  logic.kind = to_logic_kind(command.kind);
  logic.session_id = command.session_id;
  logic.x = command.x;
  logic.y = command.y;
  logic.dir = command.dir;
  logic.target_actor_id = command.target_actor_id;
  logic.item_make_index = command.item_make_index;
  logic.item_slot = command.item_slot;
  logic.amount = command.amount;
  logic.text = command.text;
  logic.game_message = command.game_message;
  logic.packet = command.packet;
  return logic;
}

}  // namespace mir2
