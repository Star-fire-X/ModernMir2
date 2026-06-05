/**
 * @file legacy_protocol.hpp
 * @brief 遗留协议成帧层定义
 *
 * @details 本文件定义了 LegacyProtocolCodec 类，负责 Legacy 协议
 * 的成帧（Framing）处理。Legacy 协议使用 #...#! 格式来分隔数据包，
 * 其中 # 表示帧开始，! 表示帧结束。
 *
 * 在与旧版 Delphi 客户端通信时：
 * 1. 客户端到服务器：每个数据包以一个十进制校验码数字开头，
 *    RunGate 会剥离该数字后转发给 M2Server。
 * 2. 服务器到客户端：不包含校验码，直接包裹 #...#!。
 *
 * @see legacy_protocol.cpp 中的实现细节
 * @see legacy_game_codec.hpp 用于负载内部的游戏消息编解码
 */

#pragma once

#include <cstdint>
#include <vector>

#include "core/messages.hpp"

namespace mir2 {

/**
 * @class LegacyProtocolCodec
 * @brief 遗留协议成帧编解码器
 *
 * @details 处理 Legacy 协议的 #...#! 帧格式。
 * encode() 将数据包 body 包裹成 #<body>! 格式。
 * drain_packets() 从字节流中提取完整 #...#! 帧，
 * 并剥离客户端前置的十进制校验码。
 */
class LegacyProtocolCodec {
 public:
  /**
   * @brief 将 Legacy 数据包编码为 #...#! 帧格式
   *
   * @details 返回 "<body>!" 格式的字节序列。
   * Delphi RunGate 在服务器到客户端的方向上不添加校验码，
   * 因此编码结果仅为 "#<body>!"。
   *
   * @param packet 要发送的 Legacy 数据包
   * @return std::vector<std::uint8_t> 编码后的帧字节序列
   */
  static std::vector<std::uint8_t> encode(const LegacyPacket& packet);

  /**
   * @brief 从上游字节流中提取完整的 #...#! 帧
   *
   * @details 处理客户端到服务器方向的字节流：
   * 1. 搜索 # 字符作为帧起始标记
   * 2. 搜索 ! 字符作为帧结束标记
   * 3. 如果负载的第一个字符是十进制数字（0-9），
   *    剥离该数字（这是旧版客户端添加的校验码）
   * 4. 提取负载到 LegacyPacket.body 中
   *
   * 处理过程中会丢弃以 # 开始的无效帧（超过 64KB 未找到结束标记 !）。
   *
   * @param[in,out] buffer 包含待处理字节流的缓冲区
   *                        函数返回时会移除已处理的字节
   * @return std::vector<LegacyPacket> 提取的完整数据包列表
   */
  static std::vector<LegacyPacket> drain_packets(std::vector<std::uint8_t>& buffer);
};

}  // namespace mir2
