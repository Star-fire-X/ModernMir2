/**
 * @file legacy_game_codec.cpp
 * @brief 遗留游戏数据包编解码器实现
 *
 * @details 实现 Legacy 协议游戏数据包的编码和解码。
 *
 * make_legacy_game_packet() 编码流程：
 * 1. 使用 legacy_encode_message() 对 LegacyDefaultMessage 进行 6-bit 编码
 * 2. 拼接预编码的正文
 * 3. 用编码后的数据填充 LegacyPacket.body
 *
 * decode_legacy_game_packet() 解码流程：
 * 1. 从 LegacyPacket.body 提取数据
 * 2. 使用 legacy_decode_message() 从 6-bit 编码还原消息头
 * 3. 剩余部分作为正文
 *
 * @see legacy_game_codec.hpp
 * @see legacy_edcode.hpp
 */

#include "protocol/legacy_game_codec.hpp"

#include <string>

#include "protocol/legacy_edcode.hpp"

namespace mir2 {

LegacyDefaultMessage make_default_message(std::uint16_t ident, std::int32_t recog,
                                          std::uint16_t param, std::uint16_t tag,
                                          std::uint16_t series) {
  LegacyDefaultMessage message;
  message.ident = ident;
  message.recog = recog;
  message.param = param;
  message.tag = tag;
  message.series = series;
  return message;
}

LegacyPacket make_legacy_game_packet(std::uint64_t session_id, std::uint16_t user_gate_index,
                                     std::uint16_t user_list_index,
                                     const LegacyDefaultMessage& message,
                                     const std::string& body) {
  LegacyPacket packet;
  packet.header.socket_number = static_cast<std::int32_t>(session_id);
  packet.header.user_gate_index = user_gate_index;
  packet.header.ident = message.ident;
  packet.header.user_list_index = user_list_index;
  // 编码消息头 + 拼接正文
  const auto payload = legacy_encode_message(message) + body;
  packet.body.assign(payload.begin(), payload.end());
  packet.header.length = static_cast<std::int32_t>(packet.body.size());
  return packet;
}

LegacyPacket make_legacy_raw_packet(std::uint64_t session_id, std::string body,
                                    std::uint16_t user_gate_index,
                                    std::uint16_t user_list_index) {
  LegacyPacket packet;
  packet.header.socket_number = static_cast<std::int32_t>(session_id);
  packet.header.user_gate_index = user_gate_index;
  packet.header.ident = 0;
  packet.header.user_list_index = user_list_index;
  if (!body.empty()) {
    packet.body.assign(body.begin(), body.end());
    // 长度取负值以标识为原始数据包
    packet.header.length = -static_cast<std::int32_t>(packet.body.size());
  }
  return packet;
}

std::optional<DecodedLegacyGamePacket> decode_legacy_game_packet(const LegacyPacket& packet) {
  const std::string payload(packet.body.begin(), packet.body.end());
  // 确保数据至少包含完整的消息头编码
  if (payload.size() < legacy_message_encoded_size()) {
    return std::nullopt;
  }

  DecodedLegacyGamePacket decoded;
  // 解码消息头
  const auto message = legacy_decode_message(payload.substr(0, legacy_message_encoded_size()));
  if (!message.has_value()) {
    return std::nullopt;
  }
  decoded.message = *message;
  // 剩余部分作为正文
  decoded.body = payload.substr(legacy_message_encoded_size());
  return decoded;
}

}  // namespace mir2
