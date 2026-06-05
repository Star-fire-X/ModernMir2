/**
 * @file legacy_protocol.cpp
 * @brief 遗留协议成帧层实现
 *
 * @details 实现了 LegacyProtocolCodec 的两个核心方法：
 *
 * encode(): 创建 #...#! 格式的下行帧。
 * 将数据包 body 包裹在 # 和 ! 之间发送给旧版客户端。
 *
 * drain_packets(): 从上行字节流中提取帧。
 * 旧版客户端发送的每个数据包以 # 起始、以 ! 结束，
 * 并且在 # 后紧跟一位十进制数字作为简易校验码。
 * 该数字在提取时被剥离，以保持与 Delphi M2Server 的兼容性。
 *
 * @note kMaxLegacyFrameBytes = 64KB 是单个帧的最大长度限制，
 *       超过此限制的帧被视为无效并丢弃。
 */

#include "protocol/legacy_protocol.hpp"

#include <algorithm>

namespace mir2 {

namespace {

/** @brief 单个 Legacy 帧的最大字节数（64KB） */
constexpr std::size_t kMaxLegacyFrameBytes = 64 * 1024;

}  // namespace

std::vector<std::uint8_t> LegacyProtocolCodec::encode(const LegacyPacket& packet) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(packet.body.size() + 2);
  // 帧起始标记
  bytes.push_back(static_cast<std::uint8_t>('#'));
  // 数据负载
  bytes.insert(bytes.end(), packet.body.begin(), packet.body.end());
  // 帧结束标记
  bytes.push_back(static_cast<std::uint8_t>('!'));
  return bytes;
}

std::vector<LegacyPacket> LegacyProtocolCodec::drain_packets(std::vector<std::uint8_t>& buffer) {
  std::vector<LegacyPacket> packets;
  std::size_t consumed = 0;  // 已成功处理的字节数

  while (consumed < buffer.size()) {
    // 跳过起始标记前的非帧数据
    while (consumed < buffer.size() && buffer[consumed] != static_cast<std::uint8_t>('#')) {
      ++consumed;
    }
    if (consumed >= buffer.size()) {
      break;
    }

    // 查找帧结束标记 !，最长搜索 kMaxLegacyFrameBytes
    const auto search_limit =
        buffer.begin() + static_cast<std::ptrdiff_t>(
                             std::min(buffer.size(), consumed + kMaxLegacyFrameBytes + 1));
    const auto frame_end =
        std::find(buffer.begin() + static_cast<std::ptrdiff_t>(consumed + 1), search_limit,
                  static_cast<std::uint8_t>('!'));
    if (frame_end == search_limit) {
      // 没有找到结束标记
      if (search_limit != buffer.end()) {
        // 帧已超过最大长度，跳过起始标记继续搜索
        ++consumed;
        continue;
      }
      // 数据不足，等待更多数据
      break;
    }

    // 提取帧负载（# 和 ! 之间的字节）
    std::string payload(buffer.begin() + static_cast<std::ptrdiff_t>(consumed + 1), frame_end);

    // 旧版客户端在负载前添加一位十进制校验码，需要剥离
    if (!payload.empty() && payload.front() >= '0' && payload.front() <= '9') {
      payload.erase(0, 1);
    }

    // 构造数据包
    LegacyPacket packet;
    if (!payload.empty()) {
      packet.body.assign(payload.begin(), payload.end());
      packet.header.length = static_cast<std::int32_t>(packet.body.size());
      packets.push_back(std::move(packet));
    }
    consumed = static_cast<std::size_t>(std::distance(buffer.begin(), frame_end)) + 1;
  }

  // 移除已处理的字节
  if (consumed > 0) {
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
  }
  return packets;
}

}  // namespace mir2
