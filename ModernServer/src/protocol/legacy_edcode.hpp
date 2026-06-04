/**
 * @file legacy_edcode.hpp
 * @brief 遗留协议的加密/编码（EdCode）模块
 *
 * @details 本文件声明了 Legacy 协议特有的 6-bit 编码/解码函数。
 * 该编码方案是传奇（Mir2）游戏经典客户端与服务器之间通信
 * 使用的专有编码格式，将二进制数据编码为可打印的 ASCII 字符。
 *
 * 编码原理：将每 6 位数据映射为一个 ASCII 字符（范围 0x3C-0x7B），
 * 平均每 3 字节原始数据产生 4 个编码字符。
 *
 * 提供的功能：
 * - 字符串编码/解码（legacy_encode/decode_string）
 * - 文本编码/解码（含 GBK/UTF-8 转换，legacy_encode/decode_text）
 * - 二进制缓冲区编码/解码（legacy_encode/decode_buffer）
 * - LegacyDefaultMessage 消息结构编解码（legacy_encode/decode_message）
 *
 * @see legacy_edcode.cpp 中的具体实现细节
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_types.hpp"

namespace mir2 {

/**
 * @brief 将普通字符串编码为 Legacy 6-bit 编码格式
 * @param text 要编码的原始字符串
 * @return std::string 编码后的字符串（仅含可打印 ASCII 字符）
 */
std::string legacy_encode_string(std::string_view text);

/**
 * @brief 将 Legacy 6-bit 编码格式解码为普通字符串
 * @param encoded 编码后的字符串
 * @return std::string 解码后的原始字符串
 */
std::string legacy_decode_string(std::string_view encoded);

/**
 * @brief 将 UTF-8 文本编码为 Legacy 6-bit 编码格式
 * @details 先执行 UTF-8 到 Legacy 编码（GBK）的转换，再进行 6-bit 编码
 * @param utf8_text UTF-8 格式的输入文本
 * @return std::string 编码后的字符串
 */
std::string legacy_encode_text(std::string_view utf8_text);

/**
 * @brief 将 Legacy 6-bit 编码格式解码为 UTF-8 文本
 * @details 先执行 6-bit 解码，再进行 Legacy 编码（GBK）到 UTF-8 的转换
 * @param encoded_text 编码后的文本字符串
 * @return std::string 解码后的 UTF-8 文本
 */
std::string legacy_decode_text(std::string_view encoded_text);

/**
 * @brief 将二进制缓冲区编码为 Legacy 6-bit 格式
 * @param data 要编码的二进制数据指针
 * @param size 数据大小（字节）
 * @return std::string 编码后的字符串
 */
std::string legacy_encode_buffer(const void* data, std::size_t size);

/**
 * @brief 将 Legacy 6-bit 编码格式解码为二进制缓冲区
 * @param encoded 编码后的字符串
 * @param[out] data 目标缓冲区指针
 * @param size 目标缓冲区大小
 * @return true 解码成功
 * @return false 解码失败（数据无效或目标缓冲区不足）
 */
bool legacy_decode_buffer(std::string_view encoded, void* data, std::size_t size);

/**
 * @brief 将 LegacyDefaultMessage 编码为 6-bit 格式
 * @param message Legacy 协议消息头结构
 * @return std::string 编码后的字符串
 */
std::string legacy_encode_message(const LegacyDefaultMessage& message);

/**
 * @brief 从 6-bit 编码格式解码为 LegacyDefaultMessage
 * @param encoded 编码后的字符串
 * @return std::optional<LegacyDefaultMessage> 解码后的消息结构，
 *         如果输入长度不足则返回 std::nullopt
 */
std::optional<LegacyDefaultMessage> legacy_decode_message(std::string_view encoded);

/**
 * @brief 获取 LegacyDefaultMessage 编码后的固定长度
 * @details 该值在首次调用时计算并缓存。
 * @return std::size_t 编码后的字节数
 */
std::size_t legacy_message_encoded_size();

}  // namespace mir2
