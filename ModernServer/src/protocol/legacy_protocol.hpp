#pragma once

#include <cstdint>
#include <vector>

#include "core/messages.hpp"

namespace mir2 {

class LegacyProtocolCodec {
 public:
  /// Wrap a legacy packet body for downstream delivery to old clients.
  /// Delphi RunGate does not add a check-code on server-to-client frames, so
  /// this returns exactly "#<body>!".
  static std::vector<std::uint8_t> encode(const LegacyPacket& packet);

  /// Drain complete "#...!" frames from an upstream legacy byte stream.
  /// Old clients prefix each client-to-server payload with a single decimal
  /// check-code; RunGate strips that digit before game-message decode, so this
  /// method does the same. The returned LegacyPacket bodies are ready for
  /// decode_legacy_game_packet().
  static std::vector<LegacyPacket> drain_packets(std::vector<std::uint8_t>& buffer);
};

}  // namespace mir2
