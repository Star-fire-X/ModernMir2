#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mir2::util {

inline bool is_valid_utf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead <= 0x7f) {
      ++index;
      continue;
    }

    std::uint32_t codepoint = 0;
    std::size_t needed = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      codepoint = lead & 0x1f;
      needed = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      codepoint = lead & 0x0f;
      needed = 2;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      codepoint = lead & 0x07;
      needed = 3;
    } else {
      return false;
    }

    if (index + needed >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= needed; ++offset) {
      const auto ch = static_cast<unsigned char>(text[index + offset]);
      if ((ch & 0xc0) != 0x80) {
        return false;
      }
      codepoint = (codepoint << 6) | (ch & 0x3f);
    }

    if ((needed == 2 && codepoint < 0x800) ||
        (needed == 3 && (codepoint < 0x10000 || codepoint > 0x10ffff)) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      return false;
    }
    index += needed + 1;
  }
  return true;
}

inline std::string_view strip_utf8_bom_view(std::string_view text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
      static_cast<unsigned char>(text[1]) == 0xbb &&
      static_cast<unsigned char>(text[2]) == 0xbf) {
    text.remove_prefix(3);
  }
  return text;
}

inline std::string legacy_text_to_utf8(std::string_view bytes) {
  const auto without_bom = strip_utf8_bom_view(bytes);
  if (is_valid_utf8(without_bom)) {
    return std::string(without_bom);
  }

#ifdef _WIN32
  if (!bytes.empty()) {
    const auto wide_size =
        MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.data(),
                            static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_size > 0) {
      std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
      if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.data(),
                              static_cast<int>(bytes.size()), wide.data(),
                              wide_size) == wide_size) {
        const auto utf8_size =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size,
                                nullptr, 0, nullptr, nullptr);
        if (utf8_size > 0) {
          std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
          if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                  wide_size, utf8.data(), utf8_size, nullptr,
                                  nullptr) == utf8_size) {
            return utf8;
          }
        }
      }
    }
  }
#endif

  return std::string(bytes);
}

inline std::string utf8_text_to_legacy(std::string_view utf8_text) {
  const auto without_bom = strip_utf8_bom_view(utf8_text);
  if (without_bom.empty()) {
    return {};
  }

#ifdef _WIN32
  if (is_valid_utf8(without_bom)) {
    const auto wide_size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, without_bom.data(),
                            static_cast<int>(without_bom.size()), nullptr, 0);
    if (wide_size > 0) {
      std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, without_bom.data(),
                              static_cast<int>(without_bom.size()), wide.data(),
                              wide_size) == wide_size) {
        const auto legacy_size =
            WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, wide.data(), wide_size,
                                nullptr, 0, nullptr, nullptr);
        if (legacy_size > 0) {
          std::string legacy(static_cast<std::size_t>(legacy_size), '\0');
          if (WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, wide.data(), wide_size,
                                  legacy.data(), legacy_size, nullptr, nullptr) ==
              legacy_size) {
            return legacy;
          }
        }
      }
    }
  }
#endif

  return std::string(without_bom);
}

inline std::filesystem::path path_from_utf8(std::string_view text) {
  std::u8string value;
  value.reserve(text.size());
  for (const char ch : text) {
    value.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
  }
  return std::filesystem::path(value);
}

inline std::string path_to_utf8_string(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

inline std::string ascii_path_component(std::string_view text) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(text.size());
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    const auto safe = (byte >= '0' && byte <= '9') ||
                      (byte >= 'A' && byte <= 'Z') ||
                      (byte >= 'a' && byte <= 'z') ||
                      byte == '.' || byte == '-' || byte == '_';
    if (safe) {
      escaped.push_back(static_cast<char>(byte));
      continue;
    }
    escaped.push_back('%');
    escaped.push_back(kHex[byte >> 4]);
    escaped.push_back(kHex[byte & 0x0f]);
  }
  if (escaped.empty() || escaped == "." || escaped == "..") {
    return "_";
  }
  return escaped;
}

inline std::filesystem::path ascii_path(const std::filesystem::path& path) {
  std::filesystem::path encoded;
  for (const auto& component : path) {
    encoded /= ascii_path_component(path_to_utf8_string(component));
  }
  return encoded;
}

inline std::vector<std::string> split_legacy_text_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::string line;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto ch = text[index];
    if (ch == '\r' || ch == '\n') {
      lines.push_back(std::move(line));
      line.clear();
      if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        ++index;
      }
      continue;
    }
    line.push_back(ch);
  }
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

inline std::vector<std::string> read_legacy_text_lines(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  const std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  return split_legacy_text_lines(legacy_text_to_utf8(bytes));
}

}  // namespace mir2::util
