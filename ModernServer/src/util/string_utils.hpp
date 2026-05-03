#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mir2::util {

inline std::string trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }).base(),
              value.end());
  return value;
}

inline std::vector<std::string> split(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream stream{std::string(text)};
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    parts.push_back(trim(item));
  }
  return parts;
}

inline bool starts_with(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

inline std::string lower_copy(std::string_view text) {
  std::string lowered{text};
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

}  // namespace mir2::util
