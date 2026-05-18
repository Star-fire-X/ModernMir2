#pragma once

#include <cassert>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "render/software_renderer.hpp"

namespace mir2::client::tests {

inline std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

inline std::string manifest_slice(const std::string& manifest, const std::string& marker,
                                  const std::size_t length = 4000) {
  const auto pos = manifest.find(marker);
  assert(pos != std::string::npos);
  return manifest.substr(pos, length);
}

inline std::string manifest_object_slice(const std::string& manifest, const std::string& marker,
                                         const std::size_t length = 4000) {
  const auto pos = manifest.find(marker);
  assert(pos != std::string::npos);
  const auto begin = manifest.rfind("{", pos);
  assert(begin != std::string::npos);
  return manifest.substr(begin, length);
}

inline int json_int_after(const std::string& text, const std::string& key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  assert(std::regex_search(text, match, pattern));
  return std::stoi(match[1].str());
}

inline std::string json_string_after(const std::string& text, const std::string& key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  assert(std::regex_search(text, match, pattern));
  return match[1].str();
}

inline bool json_bool_after(const std::string& text, const std::string& key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  assert(std::regex_search(text, match, pattern));
  return match[1].str() == "true";
}

inline RectI json_rect_after(const std::string& text, const std::string& marker) {
  const auto slice = manifest_slice(text, marker);
  const std::regex pattern(
      "\"rect\"\\s*:\\s*\\{\\s*\"h\"\\s*:\\s*(-?[0-9]+),\\s*\"w\"\\s*:\\s*(-?[0-9]+),\\s*\"x\"\\s*:\\s*(-?[0-9]+),\\s*\"y\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  assert(std::regex_search(slice, match, pattern));
  return RectI{std::stoi(match[3].str()), std::stoi(match[4].str()),
               std::stoi(match[2].str()), std::stoi(match[1].str())};
}

inline RectI json_direct_rect_after(const std::string& text, const std::string& marker) {
  const auto slice = manifest_slice(text, marker);
  const std::regex pattern(
      "\\{\\s*\"h\"\\s*:\\s*(-?[0-9]+),\\s*\"w\"\\s*:\\s*(-?[0-9]+),\\s*\"x\"\\s*:\\s*(-?[0-9]+),\\s*\"y\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  assert(std::regex_search(slice, match, pattern));
  return RectI{std::stoi(match[3].str()), std::stoi(match[4].str()),
               std::stoi(match[2].str()), std::stoi(match[1].str())};
}

inline RectI json_point_after(const std::string& text, const std::string& marker) {
  const auto slice = manifest_slice(text, marker);
  const std::regex pattern("\\{\\s*\"x\"\\s*:\\s*(-?[0-9]+),\\s*\"y\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  assert(std::regex_search(slice, match, pattern));
  return RectI{std::stoi(match[1].str()), std::stoi(match[2].str()), 0, 0};
}

inline bool same_rect(const RectI lhs, const RectI rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

inline std::uint32_t pixel_at(const SoftwareSurface& surface, const int x, const int y) {
  return surface.data()[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(surface.width()) +
                        static_cast<std::size_t>(x)];
}

inline std::uint64_t checksum_fnv1a64(const SoftwareSurface& surface) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (int y = 0; y < surface.height(); ++y) {
    for (int x = 0; x < surface.width(); ++x) {
      hash ^= pixel_at(surface, x, y);
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

}  // namespace mir2::client::tests
