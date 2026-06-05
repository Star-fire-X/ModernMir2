/**
 * @file encoding.hpp
 * @brief 字符编码转换工具模块
 *
 * @details 提供 UTF-8 与 UTF-16（Windows 宽字符）之间的双向转换函数。
 *          传奇客户端需要处理 GBK/ANSI 编码的旧版资源文件路径和文本，
 *          本模块在 UTF-8（现代 C++ 内部表示）和 Windows 宽字符 API 之间
 *          建立桥梁。
 *
 * 主要功能：
 * - UTF-8 字符串转 Windows 宽字符串（wstring）
 * - Windows 宽字符串转 UTF-8 字符串
 * - UTF-8 解码失败时自动回退到系统 ANSI 代码页
 *
 * @note 本模块仅在 Windows 平台有效，依赖 Win32 MultiByteToWideChar API
 */

#pragma once

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

/**
 * @namespace mir2::client::text
 * @brief 文本编码转换命名空间，包含所有编码相关的工具函数
 */
namespace mir2::client::text {

/**
 * @brief 将 UTF-8 字符串转换为 Windows 宽字符串（UTF-16）
 *
 * @details 转换流程：
 * 1. 首先尝试以 UTF-8 编码（CP_UTF8）解析输入字符串
 * 2. 如果 UTF-8 解析失败（输入可能为 GBK/ANSI 编码），则回退到系统默认代码页（CP_ACP）
 * 3. 分配目标缓冲区并执行实际转换
 *
 * @param text 输入的 UTF-8（或 ANSI）编码字符串
 * @return 转换后的宽字符串。如果输入为空或转换失败，返回空 wstring
 *
 * @note 该函数使用 MB_ERR_INVALID_CHARS 标志进行严格的 UTF-8 验证，
 *       如果输入不是合法的 UTF-8，会自动回退到 ANSI 代码页处理
 */
inline std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) {
    return {};
  }

  // 首先尝试 UTF-8 解码，计算所需缓冲区大小
  auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
  auto code_page = CP_UTF8;
  auto flags = MB_ERR_INVALID_CHARS;
  // UTF-8 解码失败时，回退到系统 ANSI 代码页（兼容 GBK 编码的旧版资源）
  if (size <= 0) {
    code_page = CP_ACP;
    flags = 0;
    size = MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()),
                               nullptr, 0);
  }
  if (size <= 0) {
    return {};
  }

  // 分配缓冲区并执行实际转换
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), wide.data(),
                      size);
  return wide;
}

/**
 * @brief 将 Windows 宽字符串（UTF-16）转换为 UTF-8 字符串
 *
 * @details 使用 Win32 WideCharToMultiByte API 将宽字符转换为 UTF-8 编码的字节序列。
 *          该函数用于将 Windows 系统 API 返回的宽字符串转换为内部使用的 UTF-8 格式。
 *
 * @param text 输入的 Windows 宽字符串（UTF-16 编码）
 * @return 转换后的 UTF-8 字符串。如果输入为空或转换失败，返回空 string
 *
 * @note 该函数始终使用 CP_UTF8 代码页进行转换，不进行代码页回退
 */
inline std::string wide_to_utf8(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }

  // 计算 UTF-8 编码所需的缓冲区大小
  const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  // 分配缓冲区并执行转换
  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                      nullptr, nullptr);
  return utf8;
}

}  // namespace mir2::client::text
