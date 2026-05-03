#include <filesystem>
#include <iostream>

#include "config/models.hpp"
#include "importer/legacy_character_importer.hpp"
#include "legacy_character_fixture.hpp"
#include "storage/repository.hpp"

namespace {

int fail(const char* stage) {
  std::cerr << "legacy_character_import_smoke failed at " << stage << '\n';
  return 1;
}

mir2::HostConfig make_config() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "Home", {}, 700, 700, 330, 270});
  config.items.push_back(mir2::ItemConfig{1, "Sword", 1, 100, 5, 0, 10, 1000, 1});
  mir2::MagicConfig magic;
  magic.id = 1;
  magic.name = "Fireball";
  magic.mp_cost = 4;
  magic.power = 8;
  config.magics.push_back(magic);
  return config;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_legacy_character_import_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  const auto fixture = mir2::tests::write_legacy_character_fixture(temp_root / "legacy");
  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");

  const auto config = make_config();
  mir2::LegacyCharacterImporter importer;
  mir2::LegacyCharacterImportOptions options;
  options.hum_db_path = fixture.hum_db;
  options.mir_db_path = fixture.mir_db;
  options.default_password = "secret";
  options.config = &config;

  const auto first_report = importer.import_characters(options, repository);
  if (first_report.scanned != 1 || first_report.imported != 1 || first_report.skipped != 0 ||
      first_report.failed != 0 || !first_report.warnings.empty()) {
    return fail("first report");
  }
  if (repository.count_legacy_import_records() != 1) {
    return fail("first import records");
  }

  const auto account = repository.load_account("legacyacct");
  if (!account.has_value() || account->password != "secret") {
    return fail("account");
  }

  auto character = repository.load_character_by_name("LegacyHero");
  if (!character.has_value() || character->account_id != "legacyacct" ||
      character->map_id != "0" || character->x != 123 || character->y != 234 ||
      character->dir != 3 || character->hair != 2 || character->sex != 1 ||
      character->job != 2 || character->gold != 9876 || character->ability.level != 7 ||
      character->ability.hp != 88 || character->ability.mp != 44 ||
      character->ability.exp != 12345) {
    return fail("character basics");
  }
  if (character->equipped_items[mir2::kEquipWeapon].make_index != 1001 ||
      character->equipped_items[mir2::kEquipWeapon].index != 1 ||
      character->bag_items[0].make_index != 1002 || character->storage_items[0].make_index != 1003 ||
      character->magics[0].magic_id != 1 || character->magics[0].level != 2 ||
      character->magics[0].key != '1' || character->magics[0].cur_train != 321) {
    return fail("character inventory magic");
  }

  character->gold = 4321;
  repository.save_character(*character);
  const auto second_report = importer.import_characters(options, repository);
  if (second_report.scanned != 1 || second_report.imported != 0 || second_report.skipped != 1 ||
      second_report.failed != 0) {
    return fail("second report");
  }
  const auto after_duplicate = repository.load_character_by_name("LegacyHero");
  if (!after_duplicate.has_value() || after_duplicate->gold != 4321 ||
      repository.count_legacy_import_records() != 2) {
    return fail("duplicate preserve");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
