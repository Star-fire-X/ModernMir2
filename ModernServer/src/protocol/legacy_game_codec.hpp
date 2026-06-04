/**
 * @file legacy_game_codec.hpp
 * @brief 遗留游戏数据包编解码器
 *
 * @details 本文件定义了 Legacy 协议游戏数据包的编解码接口。
 * 负责在 LegacyDefaultMessage 消息头和原始网络字节之间进行转换。
 *
 * 协议分层:
 *  Layer               │ 功能                             │ 网络效果
 *  ────────────────────┼──────────────────────────────────┼────────────
 *  传输层              │ asio TCP 收发                     │ 字节流
 *  成帧层              │ LegacyProtocolCodec               │ # ... !
 *  游戏编解码 (本模块) │ make/decode_legacy_game_packet    │ 6-bit 消息头
 *  正文编解码          │ legacy_encode/decode_string       │ 6-bit 正文
 *
 * 上游（旧客户端 → 服务器）:
 *    TCP 字节 → drain_packets (#...! → 负载)
 *           → 剥离校验码 (LegacyProtocolCodec)
 *           → decode_legacy_game_packet (6-bit 解码消息头 → LegacyDefaultMessage)
 *           → body 通过 legacy_decode_string 解码（如果是文本正文）
 *
 * 下游（服务器 → 旧客户端）:
 *    LegacyDefaultMessage + 正文文本
 *           → 调用方预先使用 legacy_encode_string 编码正文
 *           → make_legacy_game_packet (6-bit 编码消息头 + 拼接正文)
 *           → LegacyProtocolCodec::encode (包裹为 #...!)
 *           → TCP 发送
 *
 * @note 这与 Delphi EDCode.pas + GateMain.pas 的分层一致。
 *       M2Server 的正文字符串是预编码的，RunGate 只添加 #...! 成帧。
 *       每条消息恰好执行一次 6-bit 编码，不会重复。
 *
 * @see legacy_game_codec.cpp
 * @see legacy_edcode.hpp
 * @see legacy_protocol.hpp
 */

#pragma once

#include <optional>
#include <string>

#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

/**
 * @struct DecodedLegacyGamePacket
 * @brief 解码后的 Legacy 游戏数据包
 *
 * @details 包含解码后的消息头和正文部分。
 * 消息头由 6-bit 编码还原为结构化的 LegacyDefaultMessage，
 * 正文部分保持原始二进制（文本情况下需额外通过 legacy_decode_string 解码）。
 */
struct DecodedLegacyGamePacket {
  LegacyDefaultMessage message{};  ///< 解码后的消息头
  std::string body{};              ///< 原始正文数据（未解码文本）
};

/**
 * @brief 构造一个默认的 LegacyDefaultMessage
 *
 * @details 使用给定的参数填充 Legacy 消息头结构。
 * 这是创建 Legacy 协议消息的入口函数。
 *
 * @param ident 消息标识符
 * @param recog 识别码/承载整数值
 * @param param 参数
 * @param tag 标签
 * @param series 序列号
 * @return LegacyDefaultMessage 构造完成的消息头
 */
LegacyDefaultMessage make_default_message(std::uint16_t ident, std::int32_t recog,
                                          std::uint16_t param, std::uint16_t tag,
                                          std::uint16_t series);

/**
 * @brief 将游戏消息编码为 Legacy 协议数据包
 *
 * @details 编码步骤：
 * 1. 对消息头（12 字节）应用 6-bit 编码（成为 16 个字符）
 * 2. 拼接调用者预先 6-bit 编码的正文文本
 * 3. 返回的 LegacyPacket.body 已准备好由 LegacyProtocolCodec::encode()
 *    添加 #...! 成帧后发送
 *
 * @param session_id 会话标识符
 * @param user_gate_index 网关用户索引
 * @param user_list_index 用户列表索引
 * @param message Legacy 协议消息头
 * @param body 预编码的正文（如文本需通过 legacy_encode_string 预编码）
 * @return LegacyPacket 可在网络上发送的数据包
 */
LegacyPacket make_legacy_game_packet(std::uint64_t session_id, std::uint16_t user_gate_index,
                                     std::uint16_t user_list_index,
                                     const LegacyDefaultMessage& message,
                                     const std::string& body = {});

/**
 * @brief 构造一个原始 Legacy 数据包（不含游戏消息头编码）
 *
 * @details 用于发送不需要游戏消息头编码的原始数据。
 * 当 header.length 为负值时，表示 body 是原始数据。
 *
 * @param session_id 会话标识符
 * @param body 原始数据
 * @param user_gate_index 网关用户索引（可选）
 * @param user_list_index 用户列表索引（可选）
 * @return LegacyPacket 编码后的数据包
 */
LegacyPacket make_legacy_raw_packet(std::uint64_t session_id, std::string body,
                                    std::uint16_t user_gate_index = 0,
                                    std::uint16_t user_list_index = 0);

/**
 * @brief 解码从网络接收的 Legacy 游戏数据包
 *
 * @details 输入数据包正文为 # 和 ! 之间的原始字节（如果适用，已剥离校验码）。
 * 前 16 个字符被 6-bit 解码为 LegacyDefaultMessage 消息头，
 * 剩余字节作为正文返回。
 *
 * @param packet 从网络接收的 Legacy 数据包
 * @return std::optional<DecodedLegacyGamePacket> 解码后的游戏数据包，
 *         如果数据包太短（不足消息头编码长度）则返回 std::nullopt
 */
std::optional<DecodedLegacyGamePacket> decode_legacy_game_packet(const LegacyPacket& packet);

}  // namespace mir2
