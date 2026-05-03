#include "protocol/legacy_edcode.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace mir2 {

namespace {

void encode_6bit_buf(const std::uint8_t* src, char* dest, std::size_t src_len, std::size_t dest_len) {
  std::size_t rest_count = 0;
  std::uint8_t rest = 0;
  std::size_t dest_pos = 0;

  for (std::size_t index = 0; index < src_len; ++index) {
    if (dest_pos >= dest_len) {
      break;
    }

    const auto ch = src[index];
    const auto made = static_cast<std::uint8_t>((rest | (ch >> (2 + rest_count))) & 0x3F);
    rest = static_cast<std::uint8_t>(((ch << (8 - (2 + rest_count))) >> 2) & 0x3F);
    rest_count += 2;

    if (rest_count < 6) {
      dest[dest_pos++] = static_cast<char>(made + 0x3C);
    } else {
      dest[dest_pos++] = static_cast<char>(made + 0x3C);
      if (dest_pos < dest_len) {
        dest[dest_pos++] = static_cast<char>(rest + 0x3C);
      }
      rest_count = 0;
      rest = 0;
    }
  }

  if (rest_count > 0 && dest_pos < dest_len) {
    dest[dest_pos++] = static_cast<char>(rest + 0x3C);
  }
  if (dest_pos < dest_len) {
    dest[dest_pos] = '\0';
  }
}

bool decode_6bit_buf(std::string_view source, std::uint8_t* dest, std::size_t dest_len) {
  static constexpr std::array<std::uint8_t, 5> kMasks{{0xFC, 0xF8, 0xF0, 0xE0, 0xC0}};
  std::size_t bit_pos = 2;
  std::size_t made_bit = 0;
  std::size_t dest_pos = 0;
  std::uint8_t tmp = 0;

  for (const char raw : source) {
    if (dest_pos >= dest_len) {
      break;
    }
    const auto shifted = static_cast<int>(static_cast<unsigned char>(raw)) - 0x3C;
    if (shifted < 0 || shifted > 64) {
      return false;
    }
    const auto ch = static_cast<std::uint8_t>(shifted);

    if ((made_bit + 6) >= 8) {
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
  std::string encoded(((text.size() * 4) / 3) + 8, '\0');
  encode_6bit_buf(reinterpret_cast<const std::uint8_t*>(text.data()), encoded.data(), text.size(),
                  encoded.size());
  encoded.resize(std::strlen(encoded.c_str()));
  return encoded;
}

std::string legacy_decode_string(std::string_view encoded) {
  if (encoded.empty()) {
    return {};
  }
  std::vector<std::uint8_t> decoded(encoded.size() * 2 + 4, 0);
  if (!decode_6bit_buf(encoded, decoded.data(), decoded.size())) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(decoded.data()));
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
  static const auto size = [] {
    const LegacyDefaultMessage message{};
    return legacy_encode_message(message).size();
  }();
  return size;
}

}  // namespace mir2
