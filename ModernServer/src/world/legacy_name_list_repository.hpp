#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mir2 {

class LegacyNameListRepository {
 public:
  explicit LegacyNameListRepository(std::filesystem::path root = {});

  [[nodiscard]] bool contains(std::string_view list_name, std::string_view subject);
  [[nodiscard]] std::size_t add(std::string_view list_name, std::string_view subject);
  [[nodiscard]] std::size_t remove(std::string_view list_name, std::string_view subject);
  [[nodiscard]] std::size_t size(std::string_view list_name);

 private:
  [[nodiscard]] static std::string normalize_key(std::string_view value);
  [[nodiscard]] static std::string normalize_subject(std::string_view value);
  [[nodiscard]] std::filesystem::path list_path(std::string_view key) const;
  std::unordered_set<std::string>& mutable_list(std::string_view key);
  void load(std::string_view key);
  void save(std::string_view key) const;

  std::filesystem::path root_{};
  std::unordered_map<std::string, std::unordered_set<std::string>> lists_{};
  std::unordered_set<std::string> loaded_{};
};

}  // namespace mir2
