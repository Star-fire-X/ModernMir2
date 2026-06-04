/**
 * @file client_v1_legacy_command_decoder.hpp
 * @brief Client v1 协议命令到规范命令的转换器
 *
 * @details 本文件声明了将 Client v1 协议的各种意图/请求结构
 * 转换为规范化游戏命令（CanonicalLegacyCommand）的系列函数。
 *
 * Client v1 协议使用结构化的意图消息（ActionIntent、SpellIntent 等）
 * 而非常见的 Legacy 协议的二进制消息格式。本模块负责在这些结构化消息
 * 与服务器内部统一的 CanonicalLegacyCommand 之间建立桥接。
 *
 * @note 每个函数对应一种 Client v1 协议的请求类型，包括移动、战斗、
 *       物品操作、NPC 交互、交易等全游戏操作。
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/canonical_legacy_command.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

/**
 * @brief 解码 Client v1 动作意图（移动/攻击/转身）
 *
 * @param session_id 会话标识符
 * @param intent Client v1 的动作意图消息
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_action_command(
    std::uint64_t session_id, const client_v1::ActionIntent& intent);

/**
 * @brief 解码 Client v1 施法意图
 *
 * @param session_id 会话标识符
 * @param intent Client v1 的施法意图消息
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_spell_command(
    std::uint64_t session_id, const client_v1::SpellIntent& intent);

/**
 * @brief 解码 Client v1 拾取物品意图
 *
 * @param session_id 会话标识符
 * @param intent Client v1 的拾取意图消息
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_pickup_command(
    std::uint64_t session_id, const client_v1::PickupIntent& intent);

/**
 * @brief 解码 Client v1 使用物品意图
 *
 * @param session_id 会话标识符
 * @param intent Client v1 的使用物品意图消息
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_use_item_command(
    std::uint64_t session_id, const client_v1::UseItemIntent& intent);

/**
 * @brief 解码 Client v1 穿戴装备请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的穿戴装备请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_equip_item_command(
    std::uint64_t session_id, const client_v1::EquipItemRequest& request);

/**
 * @brief 解码 Client v1 脱下装备请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的脱下装备请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_unequip_item_command(
    std::uint64_t session_id, const client_v1::UnequipItemRequest& request);

/**
 * @brief 解码 Client v1 丢弃物品请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的丢弃物品请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_drop_item_command(
    std::uint64_t session_id, const client_v1::DropItemRequest& request);

/**
 * @brief 解码 Client v1 丢弃金币请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的丢弃金币请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_drop_gold_command(
    std::uint64_t session_id, const client_v1::DropGoldRequest& request);

/**
 * @brief 解码 Client v1 复活请求
 *
 * @param session_id 会话标识符
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_revive_command(std::uint64_t session_id);

/**
 * @brief 解码 Client v1 点击 NPC 请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的点击 NPC 请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_npc_click_command(
    std::uint64_t session_id, const client_v1::NpcClickRequest& request);

/**
 * @brief 解码 Client v1 NPC 对话框选择
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param selection 玩家选择的选项文本
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_npc_dialog_select_command(
    std::uint64_t session_id, std::uint64_t merchant_id, std::string_view selection);

/**
 * @brief 解码 Client v1 从商人处购买物品请求
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param request Client v1 的购买请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_buy_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantBuyRequest& request);

/**
 * @brief 解码 Client v1 向商人出售物品请求
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param request Client v1 的出售请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_sell_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellRequest& request);

/**
 * @brief 解码 Client v1 查询出售价格请求
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param request Client v1 的查询出售价格请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_sell_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantSellPriceRequest& request);

/**
 * @brief 解码 Client v1 查询修理费用请求
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param request Client v1 的查询修理费用请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_repair_price_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairPriceRequest& request);

/**
 * @brief 解码 Client v1 修理物品请求
 *
 * @param session_id 会话标识符
 * @param merchant_id NPC/商人的实体 ID
 * @param request Client v1 的修理请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_merchant_repair_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::MerchantRepairRequest& request);

/**
 * @brief 解码 Client v1 存入仓库请求
 *
 * @param session_id 会话标识符
 * @param merchant_id 仓库管理员 NPC 的实体 ID
 * @param request Client v1 的存入请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_storage_deposit_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageDepositRequest& request);

/**
 * @brief 解码 Client v1 从仓库取回请求
 *
 * @param session_id 会话标识符
 * @param merchant_id 仓库管理员 NPC 的实体 ID
 * @param request Client v1 的取回请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_storage_withdraw_command(
    std::uint64_t session_id, std::uint64_t merchant_id,
    const client_v1::StorageWithdrawRequest& request);

/**
 * @brief 解码 Client v1 查询背包物品请求
 *
 * @param session_id 会话标识符
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_query_bag_items_command(
    std::uint64_t session_id);

/**
 * @brief 解码 Client v1 查询仓库物品请求
 *
 * @param session_id 会话标识符
 * @param merchant_id 仓库管理员 NPC 的实体 ID
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_query_storage_items_command(
    std::uint64_t session_id, std::uint64_t merchant_id);

/**
 * @brief 解码 Client v1 发起交易请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的交易发起请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_try_command(
    std::uint64_t session_id, const client_v1::TradeTryRequest& request);

/**
 * @brief 解码 Client v1 取消交易请求
 *
 * @param session_id 会话标识符
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_cancel_command(
    std::uint64_t session_id);

/**
 * @brief 解码 Client v1 交易添加物品请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的交易添加物品请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_add_item_command(
    std::uint64_t session_id, const client_v1::TradeAddItemRequest& request);

/**
 * @brief 解码 Client v1 交易移除物品请求
 *
 * @param session_id 会话标识符
 * @param request Client v1 的交易移除物品请求
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_remove_item_command(
    std::uint64_t session_id, const client_v1::TradeRemoveItemRequest& request);

/**
 * @brief 解码 Client v1 交易设置金币请求
 *
 * @param session_id 会话标识符
 * @param amount 设置的金币数量
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_set_gold_command(
    std::uint64_t session_id, std::int32_t amount);

/**
 * @brief 解码 Client v1 确认交易请求
 *
 * @param session_id 会话标识符
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_trade_accept_command(
    std::uint64_t session_id);

/**
 * @brief 解码 Client v1 聊天消息
 *
 * @param session_id 会话标识符
 * @param chat Client v1 的聊天发送消息
 * @return CanonicalLegacyCommand 转换后的规范化命令
 */
[[nodiscard]] CanonicalLegacyCommand decode_client_v1_chat_command(
    std::uint64_t session_id, const client_v1::ChatSend& chat);

}  // namespace mir2
