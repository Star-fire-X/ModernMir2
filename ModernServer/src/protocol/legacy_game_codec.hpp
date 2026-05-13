#pragma once

#include <optional>
#include <string>

#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

// ── Legacy protocol pipeline ───────────────────────────────────────────
//
//  Layer               │ Function                          │ Wire effect
//  ────────────────────┼───────────────────────────────────┼────────────
//  Transport           │ asio TCP send/recv                │ byte stream
//  Framing             │ LegacyProtocolCodec               │ # ... !
//  Game codec (this)   │ make/decode_legacy_game_packet    │ 6-bit header
//  Body codec          │ legacy_encode/decode_string       │ 6-bit body
//
//  Upstream (old client → server):
//    TCP bytes → drain_packets (#...! → payload)
//             → strip check-code (LegacyProtocolCodec, since PR-2)
//             → decode_legacy_game_packet (6-bit header → TDefaultMessage)
//             → body decoded with legacy_decode_string (if text body)
//
//  Downstream (server → old client):
//    TDefaultMessage + body text
//             → legacy_encode_string(body) at call site
//             → make_legacy_game_packet (6-bit encode header + concat body)
//             → LegacyProtocolCodec::encode (wrap with #...!)
//             → TCP send
//
//  This matches the Delphi EDCode.pas + GateMain.pas layering:
//    M2Server body strings are pre-encoded; RunGate only adds #...! framing.
//    There is exactly one 6-bit encode per message, never two.
// ─────────────────────────────────────────────────────────────────────────

struct DecodedLegacyGamePacket {
  LegacyDefaultMessage message{};
  std::string body{};
};

LegacyDefaultMessage make_default_message(std::uint16_t ident, std::int32_t recog,
                                          std::uint16_t param, std::uint16_t tag,
                                          std::uint16_t series);

/// Encode a game message for the legacy wire protocol.
/// 1. Applies 6-bit encoding to the message header (12 bytes → 16 chars).
/// 2. Concatenates the pre-encoded body string (callers must 6-bit encode
///    text bodies via legacy_encode_string before calling).
/// 3. The returned LegacyPacket.body is ready for #...! framing by
///    LegacyProtocolCodec::encode().
LegacyPacket make_legacy_game_packet(std::uint64_t session_id, std::uint16_t user_gate_index,
                                     std::uint16_t user_list_index,
                                     const LegacyDefaultMessage& message,
                                     const std::string& body = {});

LegacyPacket make_legacy_raw_packet(std::uint64_t session_id, std::string body,
                                    std::uint16_t user_gate_index = 0,
                                    std::uint16_t user_list_index = 0);

/// Decode a legacy game packet received from the wire.
/// The packet body is expected to be the raw bytes between # and ! after
/// check-code stripping (if applicable).  The first 16 characters are 6-bit
/// decoded into the LegacyDefaultMessage header; any remaining bytes become
/// the body string.
std::optional<DecodedLegacyGamePacket> decode_legacy_game_packet(const LegacyPacket& packet);

}  // namespace mir2
