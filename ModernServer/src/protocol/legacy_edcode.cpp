/**
 * @file legacy_edcode.cpp
 * @brief 遗留协议 EdCode 编解码实现
 *
 * @details 本文件实现了 Legacy 协议特有的 6-bit 编解码算法。
 * 该算法将二进制数据映射到 ASCII 可打印字符范围（0x3C-0x7B），
 * 使得编码后的数据可以通过纯文本通道传输。
 *
 * encode_6bit_buf() 编码原理：
 * - 将输入字节流按 6 位一组分割
 * - 每组 6 位值加上偏移量 0x3C（60）得到可打印 ASCII 字符
 * - 输入每 3 字节产生 4 个编码字符（压缩比 4:3）
 * - 剩余位作为最后一次输出
 *
 * decode_6bit_buf() 解码原理：
 * - 将编码字符减去偏移量 0x3C 还原为 6 位值
 * - 按位拼接还原为原始字节流
 * - 使用位掩码表 kMasks 处理边界对齐
 *
 * @note 该实现与 Delphi 原版 EDCode.pas 完全兼容。
 * @warning 编码/解码函数不对输入数据做合法性校验（解码函数除外），
 *          调用者应确保输入数据格式正确。
 */

#include "protocol/legacy_edcode.hpp"

#include <array>
#include <cstring>
#include <vector>

#include "util/legacy_text.hpp"

namespace mir2 {

namespace {

/**
 * @brief 6-bit 编码核心函数
 *
 * @details 将源缓冲区中的二进制数据编码为 6-bit 格式字符串。
 * 每 6 位添加偏移量 0x3C 得到一个可打印 ASCII 字符。
 * 数学原理：每 3 字节（24 位）拆分为 4 个 6 位值（4 x 6 = 24 位）。
 *
 * @param src 源数据缓冲区
 * @param[out] dest 目标字符串缓冲区
 * @param src_len 源数据长度
 * @param dest_len 目标缓冲区大小
 */
void encode_6bit_buf(const std::uint8_t* src, char* dest, std::size_t src_len, std::size_t dest_len) {
  std::size_t rest_count = 0;  // 当前积累的剩余位数
  std::uint8_t rest = 0;       // 剩余位暂存
  std::size_t dest_pos = 0;    // 目标缓冲区写入位置

  for (std::size_t index = 0; index < src_len; ++index) {
    if (dest_pos >= dest_len) {
      break;
    }

    const auto ch = src[index];
    // 取当前字节与剩余位拼接，提取高 (2 + rest_count) 位
    const auto made = static_cast<std::uint8_t>((rest | (ch >> (2 + rest_count))) & 0x3F);
    // 保存当前字节剩余的低 (6 - (2 + rest_count)) 位为新的剩余位
    rest = static_cast<std::uint8_t>(((ch << (8 - (2 + rest_count))) >> 2) & 0x3F);
    rest_count += 2;

    if (rest_count < 6) {
      // 剩余位还没攒够 6 位，只输出当前编码字符
      dest[dest_pos++] = static_cast<char>(made + 0x3C);
    } else {
      // 剩余位已满 6 位，连续输出两个字符
      dest[dest_pos++] = static_cast<char>(made + 0x3C);
      if (dest_pos < dest_len) {
        dest[dest_pos++] = static_cast<char>(rest + 0x3C);
      }
      rest_count = 0;
      rest = 0;
    }
  }

  // 处理最后剩余的位
  if (rest_count > 0 && dest_pos < dest_len) {
    dest[dest_pos++] = static_cast<char>(rest + 0x3C);
  }
  if (dest_pos < dest_len) {
    dest[dest_pos] = '\0';
  }
}

/**
 * @brief 6-bit 解码核心函数
 *
 * @details 将 6-bit 编码格式的字符串解码为原始二进制数据。
 * 是 encode_6bit_buf 的逆过程。
 *
 * 使用位掩码表 kMasks 来处理不同偏移边界下的位提取：
 * - kMasks[0] = 0xFC (位偏移 2 时提取高 6 位)
 * - kMasks[1] = 0xF8 (位偏移 3 时提取高 5 位)
 * - 以此类推
 *
 * @param source 编码字符串
 * @param[out] dest 目标二进制缓冲区
 * @param dest_len 目标缓冲区大小
 * @return true 解码成功
 * @return false 解码失败（包含无效字符）
 */
bool decode_6bit_buf(std::string_view source, std::uint8_t* dest, std::size_t dest_len) {
  static constexpr std::array<std::uint8_t, 5> kMasks{{0xFC, 0xF8, 0xF0, 0xE0, 0xC0}};
  std::size_t bit_pos = 2;          // 当前位偏移
  std::size_t made_bit = 0;         // 已生成的位数
  std::size_t dest_pos = 0;         // 目标缓冲区写入位置
  std::uint8_t tmp = 0;             // 字节临时拼接

  for (const char raw : source) {
    if (dest_pos >= dest_len) {
      break;
    }
    // 将字符减去偏移 0x3C 还原为 6 位值
    const auto shifted = static_cast<int>(static_cast<unsigned char>(raw)) - 0x3C;
    if (shifted < 0 || shifted > 64) {
      return false;  // 字符范围无效
    }
    const auto ch = static_cast<std::uint8_t>(shifted);

    if ((made_bit + 6) >= 8) {
      // 当前已积累的位加上新的 6 位已达到或超过 8 位，输出一个完整字节
      dest[dest_pos++] =
          static_cast<std::uint8_t>(tmp | ((ch & 0x3F) >> (6 - static_cast<int>(bit_pos))));
      made_bit = 0;
      if (bit_pos < 6) {
        bit_pos += 2;
      } else {
        bit_pos = 2;
        continue;
      }
    }

    // 暂存当前字符的剩余位
    tmp = static_cast<std::uint8_t>((ch << bit_pos) & kMasks[bit_pos - 2]);
    made_bit += 8 - bit_pos;
  }

  if (dest_pos < dest_len) {
    dest[dest_pos] = 0;
  }
  return true;
}

}  // namespace

std::string legacy_encode_string(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  // 最多需要 ((len * 4) / 3) + 8 个编码字符
  std::string encoded(((text.size() * 4) / 3) + 8, '\0');
  encode_6bit_buf(reinterpret_cast<const std::uint8_t*>(text.data()), encoded.data(), text.size(),
                  encoded.size());
  // 截断到实际编码长度（以 null 终止符位置为界）
  encoded.resize(std::strlen(encoded.c_str()));
  return encoded;
}

std::string legacy_decode_string(std::string_view encoded) {
  if (encoded.empty()) {
    return {};
  }
  // 解码需要最多 2x + 4 字节
  std::vector<std::uint8_t> decoded(encoded.size() * 2 + 4, 0);
  if (!decode_6bit_buf(encoded, decoded.data(), decoded.size())) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(decoded.data()));
}

std::string legacy_encode_text(std::string_view utf8_text) {
  // 先转换编码（UTF-8 -> Legacy GBK），再进行 6-bit 编码
  return legacy_encode_string(util::utf8_text_to_legacy(utf8_text));
}

std::string legacy_decode_text(std::string_view encoded_text) {
  // 先 6-bit 解码，再转换编码（Legacy GBK -> UTF-8）
  return util::legacy_text_to_utf8(legacy_decode_string(encoded_text));
}

std::string legacy_encode_buffer(const void* data, std::size_t size) {
  if (data == nullptr || size == 0) {
    return {};
  }
  std::string encoded((size * 2) + 8, '\0');
  encode_6bit_buf(reinterpret_cast<const std::uint8_t*>(data), encoded.data(), size, encoded.size());
  encoded.resize(std::strlen(encoded.c_str()));
  return encoded;
}

bool legacy_decode_buffer(std::string_view encoded, void* data, std::size_t size) {
  if (data == nullptr) {
    return false;
  }
  std::vector<std::uint8_t> decoded(size + 1, 0);
  if (!decode_6bit_buf(encoded, decoded.data(), decoded.size())) {
    return false;
  }
  std::memcpy(data, decoded.data(), size);
  return true;
}

std::string legacy_encode_message(const LegacyDefaultMessage& message) {
  return legacy_encode_buffer(&message, sizeof(message));
}

std::optional<LegacyDefaultMessage> legacy_decode_message(std::string_view encoded) {
  if (encoded.size() < legacy_message_encoded_size()) {
    return std::nullopt;
  }

  LegacyDefaultMessage message{};
  if (!legacy_decode_buffer(encoded.substr(0, legacy_message_encoded_size()), &message,
                            sizeof(message))) {
    return std::nullopt;
  }
  return message;
}

std::size_t legacy_message_encoded_size() {
  // 使用静态局部变量缓存编码后的大小，避免重复计算
  static const auto size = [] {
    const LegacyDefaultMessage message{};
    return legacy_encode_message(message).size();
  }();
  return size;
}

}  // namespace mir2
