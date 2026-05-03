#pragma once

#include <optional>
#include <string>

#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

struct DecodedLegacyGamePacket {
  LegacyDefaultMessage message{};
  std::string body{};
};

LegacyDefaultMessage make_default_message(std::uint16_t ident, std::int32_t recog,
                                          std::uint16_t param, std::uint16_t tag,
                                          std::uint16_t series);

LegacyPacket make_legacy_game_packet(std::uint64_t session_id, std::uint16_t user_gate_index,
                                     std::uint16_t user_list_index,
                                     const LegacyDefaultMessage& message,
                                     const std::string& body = {});

LegacyPacket make_legacy_raw_packet(std::uint64_t session_id, std::string body,
                                    std::uint16_t user_gate_index = 0,
                                    std::uint16_t user_list_index = 0);

std::optional<DecodedLegacyGamePacket> decode_legacy_game_packet(const LegacyPacket& packet);

}  // namespace mir2
