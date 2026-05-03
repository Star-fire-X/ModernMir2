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
    packet.header.length = -static_cast<std::int32_t>(packet.body.size());
  }
  return packet;
}

std::optional<DecodedLegacyGamePacket> decode_legacy_game_packet(const LegacyPacket& packet) {
  const std::string payload(packet.body.begin(), packet.body.end());
  if (payload.size() < legacy_message_encoded_size()) {
    return std::nullopt;
  }

  DecodedLegacyGamePacket decoded;
  const auto message = legacy_decode_message(payload.substr(0, legacy_message_encoded_size()));
  if (!message.has_value()) {
    return std::nullopt;
  }
  decoded.message = *message;
  decoded.body = payload.substr(legacy_message_encoded_size());
  return decoded;
}

}  // namespace mir2
