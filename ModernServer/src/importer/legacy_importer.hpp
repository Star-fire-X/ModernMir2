#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mir2 {

struct LegacyImportReport {
  std::size_t map_count{0};
  std::size_t spawn_count{0};
  std::size_t monster_count{0};
  std::size_t monster_drop_count{0};
  std::size_t npc_count{0};
  std::size_t map_quest_count{0};
  std::size_t item_count{0};
  std::size_t magic_count{0};
  std::vector<std::string> warnings{};
};

class LegacyImporter {
 public:
  LegacyImportReport import_tree(const std::filesystem::path& legacy_root,
                                 const std::filesystem::path& output_root) const;
};

}  // namespace mir2
