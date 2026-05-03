#pragma once

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mir2::client::text {

inline std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) {
    return {};
  }

  auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
  auto code_page = CP_UTF8;
  auto flags = MB_ERR_INVALID_CHARS;
  if (size <= 0) {
    code_page = CP_ACP;
    flags = 0;
    size = MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()),
                               nullptr, 0);
  }
  if (size <= 0) {
    return {};
  }

  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), wide.data(),
                      size);
  return wide;
}

inline std::string wide_to_utf8(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }

  const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                      nullptr, nullptr);
  return utf8;
}

}  // namespace mir2::client::text
