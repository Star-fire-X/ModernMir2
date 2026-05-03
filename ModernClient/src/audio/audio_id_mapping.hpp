#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mir2::client {

class AudioIdMapping {
 public:
  bool load_from_file(const std::filesystem::path& sound_list_path);
  void clear();

  [[nodiscard]] const std::wstring* path_for(int sound_id) const;
  [[nodiscard]] std::filesystem::path resolve_path(
      const std::filesystem::path& asset_root,
      std::wstring_view relative_path) const;

  [[nodiscard]] std::size_t size() const { return paths_.size(); }
  [[nodiscard]] int dynamic_base() const { return dynamic_base_; }
  [[nodiscard]] const std::vector<std::wstring>& paths() const {
    return paths_;
  }

 private:
  void append_delphi_hardcoded_extras();
  void assign_dynamic_sound_ids();
  void reset_dynamic_sound_ids();

  std::vector<std::wstring> paths_;
  int dynamic_base_ = -1;
};

}  // namespace mir2::client
