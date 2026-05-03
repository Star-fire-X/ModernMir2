#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "config/models.hpp"
#include "storage/repository.hpp"

namespace mir2 {

struct LegacyCharacterImportOptions {
  std::filesystem::path hum_db_path{};
  std::filesystem::path mir_db_path{};
  std::string default_password{"pass"};
  const HostConfig* config{nullptr};
};

struct LegacyCharacterImportWarning {
  std::string character_name{};
  std::string kind{};
  std::string value{};
};

struct LegacyCharacterImportReport {
  std::size_t scanned{0};
  std::size_t imported{0};
  std::size_t skipped{0};
  std::size_t failed{0};
  std::vector<LegacyCharacterImportWarning> warnings{};
};

class LegacyCharacterImporter {
 public:
  [[nodiscard]] LegacyCharacterImportReport import_characters(
      const LegacyCharacterImportOptions& options, Repository& repository) const;
};

}  // namespace mir2
