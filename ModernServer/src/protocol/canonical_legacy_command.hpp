/**
 * @file canonical_legacy_command.hpp
 * @brief 遗留游戏命令的规范表示
 *
 * @details 本文件定义了服务器内部使用的规范化游戏命令表示形式。
 * 来自不同协议版本（传统框架协议、Client v1）的客户端命令，
 * 都会被统一规范化为 CanonicalLegacyCommand 结构，
 * 上层业务逻辑只需处理这一种格式，无需关注底层协议差异。
 * 该规范化层是 ModernServer 协议适配架构的核心组成部分。
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

/**
 * @enum CanonicalSourceProtocol
 * @brief 标识原始命令来源的协议类型
 *
 * @details 用于记录该规范命令最初是从哪种协议解码而来的，
 * 便于在需要时追溯原始协议上下文。
 */
enum class CanonicalSourceProtocol {
  legacy_framed,  ///< 传统框架协议（#...!  格式，适用于旧版客户端）
  client_v1       ///< Client v1 协议（新版二进制协议）
};

/**
 * @enum CanonicalLegacyCommandKind
 * @brief 规范化的游戏命令类型枚举
 *
 * @details 将所有协议版本中可能出现的游戏命令统一映射到该枚举。
 * 涵盖移动（turn/walk/run）、战斗（attack/spell）、
 * 聊天（say）、NPC 交互（click_npc/merchant_select）、
 * 物品操作（drop/pickup/take_on/take_off/eat）、
 * 交易（trade_*）以及各种查询操作。
 */
enum class CanonicalLegacyCommandKind {
  turn,                    ///< 转身
  walk,                    ///< 行走
  run,                     ///< 跑步
  attack,                  ///< 攻击
  spell,                   ///< 施法
  say,                     ///< 说话/聊天
  click_npc,               ///< 点击 NPC
  merchant_select,         ///< NPC 对话框选项选择
  query_username,          ///< 查询用户名
  query_bag_items,         ///< 查询背包物品
  query_storage_items,     ///< 查询仓库物品
  query_detail_goods,      ///< 查询商品详细信息
  query_sell_price,        ///< 查询出售价格
  query_repair_cost,       ///< 查询修理费用
  drop_item,               ///< 丢弃物品
  pickup_item,             ///< 拾取物品
  take_on_item,            ///< 穿戴装备
  take_off_item,           ///< 脱下装备
  eat_item,                ///< 使用物品（吃药/食物）
  drop_gold,               ///< 丢弃金币
  revive,                  ///< 复活
  buy_item,                ///< 购买物品
  sell_item,               ///< 出售物品
  repair_item,             ///< 修理物品
  storage_item,            ///< 存入仓库
  take_back_storage_item,  ///< 从仓库取回物品
  trade_try,               ///< 发起交易请求
  trade_cancel,            ///< 取消交易
  trade_add_item,          ///< 交易中添加物品
  trade_remove_item,       ///< 交易中移除物品
  trade_set_gold,          ///< 交易中设置金币数量
  trade_accept             ///< 确认交易
};

/**
 * @enum CanonicalParseStatus
 * @brief 命令解析结果状态枚举
 *
 * @details 表示从原始数据包解析规范命令的结果状态。
 * ok 表示解析成功；malformed_packet 表示数据包格式损坏无法解码；
 * unsupported_ident 表示命令标识符在当前版本中不被支持。
 */
enum class CanonicalParseStatus {
  ok,                ///< 解析成功
  malformed_packet,  ///< 数据包格式错误
  unsupported_ident  ///< 不支持的命令标识符
};

/**
 * @struct CanonicalLegacyCommand
 * @brief 规范化的遗留游戏命令结构体
 *
 * @details 该结构体是服务器内部命令处理的统一数据载体。
 * 无论来自何种协议版本的原始命令，都会被转换为该结构体，
 * 包含命令类型、来源协议标识、会话 ID、坐标信息、
 * 目标演员 ID、物品索引/槽位、数量、文本内容，
 * 以及原始的 Legacy 消息和数据包（用于需要转发原始包的场景）。
 */
struct CanonicalLegacyCommand {
  CanonicalSourceProtocol source_protocol{CanonicalSourceProtocol::legacy_framed};  ///< 命令来源协议
  CanonicalLegacyCommandKind kind{CanonicalLegacyCommandKind::turn};                ///< 命令类型
  std::uint64_t session_id{0};    ///< 会话标识符
  std::int32_t x{0};              ///< 目标 X 坐标
  std::int32_t y{0};              ///< 目标 Y 坐标
  std::uint8_t dir{0};            ///< 方向
  std::uint64_t target_actor_id{0};  ///< 目标演员/实体 ID
  std::int32_t item_make_index{0};   ///< 物品制作索引（用于标识具体物品实例）
  std::int32_t item_slot{-1};        ///< 物品槽位（穿戴或背包中的位置，-1 表示未指定）
  std::int32_t amount{0};            ///< 数量（金币、物品数量等）
  std::string text{};                ///< 文本内容（聊天消息、NPC 选择文本等）
  LegacyDefaultMessage game_message{};  ///< 原始的 Legacy 协议消息头
  LegacyPacket packet{};                ///< 原始的完整数据包
};

/**
 * @struct CanonicalLegacyDecodeResult
 * @brief 规范化命令解码结果结构体
 *
 * @details 封装解码操作的结果，包含解析状态、可选的规范化命令，
 * 以及提取出的 Legacy 协议消息头。通过 std::optional 区分
 * 成功与失败两种情况。
 */
struct CanonicalLegacyDecodeResult {
  CanonicalParseStatus status{CanonicalParseStatus::malformed_packet};  ///< 解析状态
  std::optional<CanonicalLegacyCommand> command{};  ///< 解码成功的命令（仅 status 为 ok 时有值）
  LegacyDefaultMessage game_message{};  ///< 从中提取的 Legacy 消息头
};

/**
 * @brief 将原始 Legacy 数据包解码为规范化游戏命令
 *
 * @details 从 Legacy 协议原始数据包中解析出规范化的游戏命令。
 * 首先通过 decode_legacy_game_packet() 解码游戏数据包，
 * 然后根据消息标识符（ident）将不同的客户端命令映射到
 * 对应的 CanonicalLegacyCommandKind，并提取坐标、方向、
 * 目标 ID、物品索引等参数填充到命令结构体中。
 *
 * @param session_id 会话标识符
 * @param packet 原始的 Legacy 协议数据包
 * @return CanonicalLegacyDecodeResult 解码结果，包含解析状态和规范化命令
 *
 * @see decode_legacy_game_packet()
 * @see CanonicalLegacyCommand
 */
[[nodiscard]] CanonicalLegacyDecodeResult decode_legacy_game_command(
    std::uint64_t session_id, const LegacyPacket& packet);

/**
 * @brief 将规范化命令转换为业务逻辑层可处理的 LogicCommand
 *
 * @details 将内部规范表示的命令转换为 LogicCommand 结构，
 * 以便传递给上层游戏逻辑处理引擎。转换过程包括：
 * 1. 通过 to_logic_kind() 将 CanonicalLegacyCommandKind 映射到 LogicCommandKind
 * 2. 逐字段复制命令参数
 * 3. 保留原始消息和数据包引用
 *
 * @param command 规范化的游戏命令
 * @return LogicCommand 可供逻辑层处理的命令结构
 *
 * @see CanonicalLegacyCommand
 * @see LogicCommand
 */
[[nodiscard]] LogicCommand to_logic_command(const CanonicalLegacyCommand& command);

}  // namespace mir2
