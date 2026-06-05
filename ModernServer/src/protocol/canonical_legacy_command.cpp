/**
 * @file canonical_legacy_command.cpp
 * @brief 遗留游戏命令规范化的实现
 *
 * @details 本文件实现了从 Legacy 协议数据包到规范化游戏命令的转换逻辑。
 * 核心函数 decode_legacy_game_command() 根据 Legacy 消息标识符（ident）
 * 将各种客户端命令映射为统一的 CanonicalLegacyCommandKind 枚举值，
 * 并从消息头中提取坐标、方向、目标 ID、物品索引等参数。
 * 此外还实现了 to_logic_command() 用于将规范命令进一步转换为
 * 上层业务逻辑层可处理的形式。
 *
 * @note 攻击类命令（普通/重击/猛击/强力/长距/横扫/火焰/十字斩）统一映射为 attack 类型。
 *       各攻击类型的原始标识符在 canonical 层面不再区分，若需区分应由上层逻辑处理。
 */

#include "protocol/canonical_legacy_command.hpp"

#include <utility>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_string.hpp"

namespace mir2 {

namespace {

/**
 * @brief 将规范化命令类型映射为逻辑层命令类型
 *
 * @details 在 CanonicalLegacyCommandKind 和 LogicCommandKind 之间建立
 * 一一对应关系。两者枚举值基本一致，但属于不同的类型系统，
 * 该函数作为类型桥接层。
 *
 * @param kind 规范化的命令类型
 * @return LogicCommandKind 映射后的逻辑命令类型
 *
 * @note 无法识别的类型默认返回 LogicCommandKind::raw_packet，
 *       确保未知类型的命令不会导致未定义行为。
 */
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
    case CanonicalLegacyCommandKind::open_door:
      return LogicCommandKind::open_door;
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

/**
 * @brief 构造一个规范化的 Legacy 游戏命令
 *
 * @details 创建一个 CanonicalLegacyCommand 实例，并填充来源协议类型
 * 为 legacy_framed、命令类型、会话 ID、原始消息和原始数据包。
 * 这是一个工厂辅助函数，用于统一规范化命令的构建过程。
 *
 * @param session_id 会话标识符
 * @param packet 原始 Legacy 数据包
 * @param message Legacy 协议消息头
 * @param kind 命令类型
 * @return CanonicalLegacyCommand 构造完成的规范化命令
 */
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

/**
 * @brief 构造一个解析成功的解码结果
 *
 * @details 将解析成功的规范化命令包装为 CanonicalLegacyDecodeResult，
 * 设置状态为 ok，并保存命令和消息引用。
 *
 * @param command 解析成功的规范化命令
 * @return CanonicalLegacyDecodeResult 包含成功状态的解码结果
 */
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
    // ─── 移动命令 ───────────────────────────────────────────────
    // 转身、行走、跑步：从消息头中提取坐标（recog 字段拆分为 x/y）
    // 和方向（tag 字段）。
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
    // ─── 攻击命令 ───────────────────────────────────────────────
    // 所有攻击变体（普通/重击/猛击/强力/长距/横扫/火焰/十字斩）
    // 统一映射为 attack 类型，保留坐标和方向信息。
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
    // ─── 施法命令 ───────────────────────────────────────────────
    // 法术命令除了坐标和方向外，还包含目标演员 ID（从 param/series 合并）
    // 以及法术 ID（通过 body 传递）。
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
    // ─── 聊天命令 ───────────────────────────────────────────────
    // 文本需要经过 Legacy 编码转换（GBK/UTF-8 转换）。
    case kCmSay: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::say);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // ─── NPC 交互 ───────────────────────────────────────────────
    // 点击 NPC：目标演员 ID 直接从 recog 字段获取。
    case kCmClickNpc: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::click_npc);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      return ok(std::move(command));
    }
    // NPC 对话框选择：包含目标 NPC ID 和选择的选项文本。
    case kCmMerchantDlgSelect: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::merchant_select);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // ─── 查询命令 ───────────────────────────────────────────────
    // 查询用户名：包含目标 ID、param（作 x）和 tag（作 y）。
    case kCmQueryUsername: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_username);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.x = decoded->message.param;
      command.y = decoded->message.tag;
      return ok(std::move(command));
    }
    // 查询背包物品：无需额外参数，直接返回命令。
    case kCmQueryBagItems:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::query_bag_items));
    // 存入仓库：包含目标 NPC ID、物品 make_index 和物品名称。
    case kCmUserStorageItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::storage_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 从仓库取回物品：参数结构与存入仓库相同。
    case kCmUserTakeBackStorageItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_back_storage_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 查询商品详细信息：使用 param 作为物品制作索引。
    case kCmUserGetDetailItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_detail_goods);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 查询出售价格：make_index 由 param 和 tag 合并而成。
    case kCmMerchantQuerySellPrice: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_sell_price);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 查询修理费用：参数结构与查询出售价格类似。
    case kCmMerchantQueryRepairCost: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::query_repair_cost);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // ─── 物品操作 ───────────────────────────────────────────────
    // 丢弃物品：recog 作为 make_index，body 保存物品名称。
    case kCmDropItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::drop_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 拾取物品：param/tag 作为掉落物的地图坐标。
    case kCmPickup: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::pickup_item);
      command.x = decoded->message.param;
      command.y = decoded->message.tag;
      return ok(std::move(command));
    }
    case kCmOpenDoor: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::open_door);
      command.x = decoded->message.param;
      command.y = decoded->message.tag;
      return ok(std::move(command));
    }
    // 穿戴装备：包含物品 make_index、槽位和名称。
    case kCmTakeOnItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_on_item);
      command.item_make_index = decoded->message.recog;
      command.item_slot = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 脱下装备：参数结构同穿戴装备。
    case kCmTakeOffItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::take_off_item);
      command.item_make_index = decoded->message.recog;
      command.item_slot = decoded->message.param;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 使用物品（食用/使用）：recog 作为 make_index。
    case kCmEat: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::eat_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 丢弃金币：recog 字段直接表示金币数量。
    case kCmDropGold: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::drop_gold);
      command.amount = decoded->message.recog;
      return ok(std::move(command));
    }
    // ─── 交易系统 ───────────────────────────────────────────────
    // 发起交易请求：body 保存目标玩家名称。
    case kCmDealTry: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_try);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 交易中添加物品：recog 作为物品 make_index。
    case kCmDealAddItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_add_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 交易中移除物品：参数同添加物品。
    case kCmDealDelItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_remove_item);
      command.item_make_index = decoded->message.recog;
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 取消交易。
    case kCmDealCancel:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::trade_cancel));
    // 交易中变更金币数量。
    case kCmDealChangeGold: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::trade_set_gold);
      command.amount = decoded->message.recog;
      return ok(std::move(command));
    }
    // 确认交易完成。
    case kCmDealEnd:
      return ok(make_command(session_id, packet, decoded->message,
                             CanonicalLegacyCommandKind::trade_accept));
    // ─── 购买/出售/修理 ─────────────────────────────────────────
    // 出售物品：包含目标 NPC ID、物品 make_index 和名称。
    case kCmUserSellItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::sell_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 购买物品：参数结构同出售物品。
    case kCmUserBuyItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::buy_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // 修理物品：参数结构同上。
    case kCmUserRepairItem: {
      auto command = make_command(session_id, packet, decoded->message,
                                  CanonicalLegacyCommandKind::repair_item);
      command.target_actor_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      command.item_make_index = make_long(decoded->message.param, decoded->message.tag);
      command.text = copy_legacy_bytes(legacy_decode_text(decoded->body));
      return ok(std::move(command));
    }
    // ─── 未知命令 ───────────────────────────────────────────────
    // 对于无法识别的消息标识符，返回 unsupported 状态。
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
