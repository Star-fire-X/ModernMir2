#pragma once

#include <cstdint>
#include <vector>

#include "core/messages.hpp"

namespace mir2 {

class LegacyProtocolCodec {
 public:
  static std::vector<std::uint8_t> encode(const LegacyPacket& packet);
  static std::vector<LegacyPacket> drain_packets(std::vector<std::uint8_t>& buffer);
};

}  // namespace mir2
