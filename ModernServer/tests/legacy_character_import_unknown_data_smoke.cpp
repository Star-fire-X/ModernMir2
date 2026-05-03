#include <algorithm>
#include <filesystem>
#include <iostream>

#include "config/models.hpp"
#include "importer/legacy_character_importer.hpp"
#include "legacy_character_fixture.hpp"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "legacy_character_import_unknown_data_smoke failed at " << stage << '\n';
  return 1;
}

bool has_warning(const mir2::LegacyCharacterImportReport& report, const std::string& kind,
                 const std::string& value) {
  return std::any_of(report.warnings.begin(), report.warnings.end(),
                     [&](const mir2::LegacyCharacterImportWarning& warning) {
                       return warning.kind == kind && warning.value == value;
                     });
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_character_import_unknown_data_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  const auto fixture = mir2::tests::write_legacy_character_fixture(
      temp_root / "legacy", "legacyacct", "UnknownHero", "missing_map", 999, 77);

  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");

  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "Home", {}, 700, 700, 330, 270});
  config.items.push_back(mir2::ItemConfig{1, "Sword", 1, 100, 5, 0, 10, 1000, 1});
  mir2::MagicConfig known_magic;
  known_magic.id = 1;
  known_magic.name = "Fireball";
  config.magics.push_back(known_magic);

  mir2::LegacyCharacterImporter importer;
  mir2::LegacyCharacterImportOptions options;
  options.hum_db_path = fixture.hum_db;
  options.mir_db_path = fixture.mir_db;
  options.config = &config;
  const auto report = importer.import_characters(options, repository);

  if (report.scanned != 1 || report.imported != 1 || report.skipped != 0 || report.failed != 0) {
    return fail("report");
  }
  if (!has_warning(report, "unknown_map", "missing_map") ||
      !has_warning(report, "unknown_item", "999") ||
      !has_warning(report, "unknown_magic", "77")) {
    return fail("warnings");
  }
  const auto character = repository.load_character_by_name("UnknownHero");
  if (!character.has_value() || character->map_id != "missing_map" ||
      character->equipped_items[mir2::kEquipWeapon].index != 999 ||
      character->bag_items[0].index != 999 || character->storage_items[0].index != 999 ||
      character->magics[0].magic_id != 77) {
    return fail("preserved unknown data");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
