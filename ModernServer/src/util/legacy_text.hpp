/**
 * @file legacy_text.hpp
 * @brief 遗留系统文本编码转换工具集
 * @details 提供 GBK/遗留编码与 UTF-8 之间的双向转换功能，以及 UTF-8 校验、
 *          BOM 剥离、路径编码转换和遗留文本文件读取等实用工具。
 *          所有函数均为 inline 实现，适合作为轻量级头文件库使用。
 * @note 在 Windows 平台（_WIN32）下使用系统 API（MultiByteToWideChar / WideCharToMultiByte）
 *       进行编码转换，代码页 936 对应简体中文 GBK 编码。
 * @warning 非 Windows 平台下 legacy_text_to_utf8() 和 utf8_text_to_legacy() 仅能处理
 *          已经是合法 UTF-8 的输入，无法进行真正的 GBK/UTF-8 双向转换。
 */

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

/**
 * @brief 判断字符串是否为合法的 UTF-8 编码
 * @details 按照 RFC 3629 标准逐字节校验 UTF-8 编码序列：
 *          - 单字节：0x00 ~ 0x7F
 *          - 双字节：0xC2 ~ 0xDF 后跟 0x80 ~ 0xBF
 *          - 三字节：0xE0 ~ 0xEF 后跟两个 0x80 ~ 0xBF
 *          - 四字节：0xF0 ~ 0xF4 后跟三个 0x80 ~ 0xBF
 *          同时检查过短编码（overlong）、超范围编码和代理对（U+D800 ~ U+DFFF）等非法序列。
 * @param text 待检查的字符串视图
 * @return true 表示字符串是合法的 UTF-8 编码；false 表示包含非法序列
 * @note 该函数不会修改输入，仅进行只读校验。时间复杂度 O(n)。
 */
inline bool is_valid_utf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    // ASCII 范围（0x00 ~ 0x7F），单字节字符，直接跳过
    if (lead <= 0x7f) {
      ++index;
      continue;
    }

    std::uint32_t codepoint = 0;
    std::size_t needed = 0;
    // 根据首字节的高位模式判断编码序列长度
    if (lead >= 0xc2 && lead <= 0xdf) {
      codepoint = lead & 0x1f;
      needed = 1;  // 双字节序列：110xxxxx 10xxxxxx
    } else if (lead >= 0xe0 && lead <= 0xef) {
      codepoint = lead & 0x0f;
      needed = 2;  // 三字节序列：1110xxxx 10xxxxxx 10xxxxxx
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      codepoint = lead & 0x07;
      needed = 3;  // 四字节序列：11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    } else {
      return false;  // 非法首字节（如 0x80~0xBF 或 0xF5~0xFF）
    }

    // 检查是否有足够的后续字节可供读取
    if (index + needed >= text.size()) {
      return false;
    }
    // 校验续字节格式：每个续字节必须以二进制 10 开头（即 0x80 ~ 0xBF）
    for (std::size_t offset = 1; offset <= needed; ++offset) {
      const auto ch = static_cast<unsigned char>(text[index + offset]);
      if ((ch & 0xc0) != 0x80) {
        return false;
      }
      codepoint = (codepoint << 6) | (ch & 0x3f);
    }

    // 检查过短编码、超范围编码和代理对
    if ((needed == 2 && codepoint < 0x800) ||
        (needed == 3 && (codepoint < 0x10000 || codepoint > 0x10ffff)) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      return false;
    }
    index += needed + 1;
  }
  return true;
}

/**
 * @brief 去除 UTF-8 BOM（Byte Order Mark）并返回视图
 * @details BOM 为 UTF-8 编码文件开头的三个字节：0xEF 0xBB 0xBF。
 *          该函数仅在字符串开头检测到 BOM 时去除，否则原样返回。
 *          返回的是原字符串的视图，不涉及内存分配或拷贝。
 * @param text 原始字符串视图
 * @return 去除 BOM 后的字符串视图
 */
inline std::string_view strip_utf8_bom_view(std::string_view text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
      static_cast<unsigned char>(text[1]) == 0xbb &&
      static_cast<unsigned char>(text[2]) == 0xbf) {
    text.remove_prefix(3);
  }
  return text;
}

/**
 * @brief 将遗留编码（GBK）文本转换为 UTF-8 编码
 * @details 转换策略：
 *          1. 先尝试去除 BOM 并检查是否为合法 UTF-8，如果是则直接返回（避免重复转换）
 *          2. 否则在 Windows 上通过 MultiByteToWideChar 使用代码页 936（GBK）
 *             将字节转换为宽字符（UTF-16），再通过 WideCharToMultiByte 转换为 UTF-8
 *          3. 如果转换失败，回退返回原始字节
 *          该策略兼顾了性能（避免不必要的转换）和兼容性（遗留文件可能已经是 UTF-8）。
 * @param bytes 遗留编码的字节数据
 * @return UTF-8 编码的字符串
 * @note 转换路径为 GBK -> UTF-16 -> UTF-8，使用 MB_ERR_INVALID_CHARS 标志
 *       可在遇到非法编码序列时返回错误而非静默替换。
 */
inline std::string legacy_text_to_utf8(std::string_view bytes) {
  const auto without_bom = strip_utf8_bom_view(bytes);
  // 如果已经是合法的 UTF-8，直接返回，无需经过系统 API 转换
  if (is_valid_utf8(without_bom)) {
    return std::string(without_bom);
  }

#ifdef _WIN32
  if (!bytes.empty()) {
    // GBK -> 宽字符（UTF-16）：先查询所需缓冲区大小
    const auto wide_size =
        MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.data(),
                            static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_size > 0) {
      std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
      if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.data(),
                              static_cast<int>(bytes.size()), wide.data(),
                              wide_size) == wide_size) {
        // 宽字符（UTF-16）-> UTF-8：第二次转换
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

  // 回退：无法转换时返回原始字节（在非 Windows 平台或转换失败时生效）
  return std::string(bytes);
}

/**
 * @brief 将 UTF-8 编码文本转换为遗留编码（GBK）
 * @details 转换策略：
 *          1. 先去除 BOM，如果为空则直接返回空字符串
 *          2. 在 Windows 上通过 MultiByteToWideChar 使用 CP_UTF8 将 UTF-8
 *             转换为宽字符（UTF-16），再通过 WideCharToMultiByte 使用代码页 936 转换为 GBK
 *          3. 如果转换失败，回退返回去除 BOM 后的原始字符串（UTF-8）
 * @param utf8_text UTF-8 编码的输入文本
 * @return GBK 编码的遗留字符串
 * @note 使用 WC_NO_BEST_FIT_CHARS 标志，确保在遇到无法精确映射的字符时
 *       返回失败而非使用"最佳匹配"替代字符，以避免数据丢失。
 */
inline std::string utf8_text_to_legacy(std::string_view utf8_text) {
  const auto without_bom = strip_utf8_bom_view(utf8_text);
  if (without_bom.empty()) {
    return {};
  }

#ifdef _WIN32
  if (is_valid_utf8(without_bom)) {
    // UTF-8 -> 宽字符（UTF-16）：先查询所需缓冲区大小
    const auto wide_size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, without_bom.data(),
                            static_cast<int>(without_bom.size()), nullptr, 0);
    if (wide_size > 0) {
      std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, without_bom.data(),
                              static_cast<int>(without_bom.size()), wide.data(),
                              wide_size) == wide_size) {
        // 宽字符（UTF-16）-> GBK：第二次转换
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

  // 回退：无法转换时返回去 BOM 后的 UTF-8 字符串
  return std::string(without_bom);
}

/**
 * @brief 从 UTF-8 字符串创建文件系统路径
 * @details 将 std::string_view 中的每个 char 转换为 char8_t，
 *          然后构造 std::filesystem::path。这是为了确保非 ASCII
 *          路径在 Windows 上能够正确处理 Unicode 字符。
 * @param text UTF-8 编码的路径字符串
 * @return 转换后的 filesystem::path 对象
 * @note C++20 中 std::filesystem::path 可以从 std::u8string 构造，
 *       避免了 char 到 wchar_t 的隐式转换问题。
 */
inline std::filesystem::path path_from_utf8(std::string_view text) {
  std::u8string value;
  value.reserve(text.size());
  for (const char ch : text) {
    value.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
  }
  return std::filesystem::path(value);
}

/**
 * @brief 将文件系统路径转换为 UTF-8 字符串
 * @details 调用 path.generic_u8string() 获取通用格式的 UTF-8 路径，
 *          并转换为 std::string 返回。使用正斜杠（/）作为路径分隔符，
 *          确保跨平台一致性。
 * @param path 文件系统路径对象
 * @return UTF-8 编码的路径字符串
 * @note generic_u8string() 始终使用正斜杠，与平台无关。
 * @see path_from_utf8() 反向转换函数
 */
inline std::string path_to_utf8_string(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

/**
 * @brief 将路径组件转换为安全的 ASCII 表示（URL 百分比编码风格）
 * @details 对路径中的每个字节进行检查：
 *          - 字母数字和 . / - / _ 保持原样
 *          - 其他字符按 %XX 格式进行百分比编码
 *          如果结果为空、"." 或 ".."，则返回下划线 "_" 以防路径遍历攻击。
 * @param text 原始路径组件字符串
 * @return ASCII 安全编码后的字符串
 * @warning 此函数用于确保遗留系统中可能包含非 ASCII 字符的路径名
 *          在文件系统中是安全的。返回 "_" 的退化情况可防止
 *          ".." 路径遍历攻击和空路径导致的问题。
 */
inline std::string ascii_path_component(std::string_view text) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(text.size());
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    // 判断是否为安全字符：字母、数字、点、连字符、下划线
    const auto safe = (byte >= '0' && byte <= '9') ||
                      (byte >= 'A' && byte <= 'Z') ||
                      (byte >= 'a' && byte <= 'z') ||
                      byte == '.' || byte == '-' || byte == '_';
    if (safe) {
      escaped.push_back(static_cast<char>(byte));
      continue;
    }
    // 非安全字符进行百分比编码（如中文、空格等 -> %E4%BD%A0 格式）
    escaped.push_back('%');
    escaped.push_back(kHex[byte >> 4]);
    escaped.push_back(kHex[byte & 0x0f]);
  }
  // 防止空路径或路径遍历（"." 表示当前目录，".." 表示父目录）
  if (escaped.empty() || escaped == "." || escaped == "..") {
    return "_";
  }
  return escaped;
}

/**
 * @brief 将整个路径转换为 ASCII 安全路径
 * @details 遍历路径的每个组件，对每个组件调用 ascii_path_component()
 *          进行编码，然后重新组合为完整的 filesystem::path。
 *          用于处理遗留系统中可能包含非 ASCII 字符的路径。
 * @param path 原始文件系统路径
 * @return ASCII 安全编码后的文件系统路径
 * @see ascii_path_component() 单组件编码函数
 */
inline std::filesystem::path ascii_path(const std::filesystem::path& path) {
  std::filesystem::path encoded;
  for (const auto& component : path) {
    encoded /= ascii_path_component(path_to_utf8_string(component));
  }
  return encoded;
}

/**
 * @brief 将遗留文本按行分割
 * @details 支持三种换行格式：\n（Unix/LF）、\r\n（Windows/CRLF）和 \r（Mac/CR）。
 *          连续的换行符会产生空行字符串。如果末尾行不为空也会被包含。
 * @param text 待分割的文本
 * @return 各行字符串构成的向量
 * @note 与 std::getline 不同，该函数正确处理了所有三种换行格式，
 *       且不会忽略末尾空行（但也不会在末尾产生多余空行）。
 */
inline std::vector<std::string> split_legacy_text_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::string line;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto ch = text[index];
    if (ch == '\r' || ch == '\n') {
      lines.push_back(std::move(line));
      line.clear();
      // 处理 Windows 风格的 CRLF 换行（\r\n），跳过紧跟的 \n
      if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        ++index;
      }
      continue;
    }
    line.push_back(ch);
  }
  // 包含末尾没有换行符的最后一行（确保不丢失数据）
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

/**
 * @brief 以二进制模式读取遗留文本文件并返回各行内容
 * @details 以二进制模式打开文件以避免平台相关的换行符转换，
 *          读取全部字节后调用 legacy_text_to_utf8() 将遗留编码
 *          转换为 UTF-8，然后按行分割返回。
 * @param path 文件路径
 * @return UTF-8 编码的各行字符串向量；文件无法打开时返回空向量
 * @see legacy_text_to_utf8() 编码转换函数
 * @see split_legacy_text_lines() 行分割函数
 */
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
