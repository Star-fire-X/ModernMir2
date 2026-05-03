#include "protocol/legacy_protocol.hpp"

#include <algorithm>

namespace mir2 {

namespace {

constexpr std::size_t kMaxLegacyFrameBytes = 64 * 1024;

}  // namespace

std::vector<std::uint8_t> LegacyProtocolCodec::encode(const LegacyPacket& packet) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(packet.body.size() + 2);
  bytes.push_back(static_cast<std::uint8_t>('#'));
  bytes.insert(bytes.end(), packet.body.begin(), packet.body.end());
  bytes.push_back(static_cast<std::uint8_t>('!'));
  return bytes;
}

std::vector<LegacyPacket> LegacyProtocolCodec::drain_packets(std::vector<std::uint8_t>& buffer) {
  std::vector<LegacyPacket> packets;
  std::size_t consumed = 0;
  while (consumed < buffer.size()) {
    while (consumed < buffer.size() && buffer[consumed] != static_cast<std::uint8_t>('#')) {
      ++consumed;
    }
    if (consumed >= buffer.size()) {
      break;
    }

    const auto search_limit =
        buffer.begin() + static_cast<std::ptrdiff_t>(
                             std::min(buffer.size(), consumed + kMaxLegacyFrameBytes + 1));
    const auto frame_end =
        std::find(buffer.begin() + static_cast<std::ptrdiff_t>(consumed + 1), search_limit,
                  static_cast<std::uint8_t>('!'));
    if (frame_end == search_limit) {
      if (search_limit != buffer.end()) {
        ++consumed;
        continue;
      }
      break;
    }

    std::string payload(buffer.begin() + static_cast<std::ptrdiff_t>(consumed + 1), frame_end);

    LegacyPacket packet;
    if (!payload.empty()) {
      packet.body.assign(payload.begin(), payload.end());
      packet.header.length = static_cast<std::int32_t>(packet.body.size());
      packets.push_back(std::move(packet));
    }
    consumed = static_cast<std::size_t>(std::distance(buffer.begin(), frame_end)) + 1;
  }

  if (consumed > 0) {
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
  }
  return packets;
}

}  // namespace mir2
