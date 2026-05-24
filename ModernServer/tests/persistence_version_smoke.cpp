#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sqlite3.h"
#include "storage/repository.hpp"

namespace {

constexpr std::uint64_t kDeletedCharacterSaveVersionBarrier = 1ULL << 62;

int fail(const char* stage) {
  std::cerr << "persistence_version_smoke failed at " << stage << '\n';
  return 1;
}

void exec_sql(sqlite3* database, const char* sql) {
  char* error = nullptr;
  if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error != nullptr ? error : "sqlite_exec failed";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

mir2::CharacterRecord make_character(std::int32_t gold, std::uint64_t save_version) {
  mir2::CharacterRecord record;
  record.account_id = "guest";
  record.character_name = "VersionHero";
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;
  record.gold = gold;
  record.save_version = save_version;
  return record;
}

bool old_schema_migrates_to_zero_version(const std::filesystem::path& source_root,
                                         const std::filesystem::path& database_path) {
  sqlite3* database = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &database) != SQLITE_OK) {
    return false;
  }
  try {
    exec_sql(database,
             "CREATE TABLE characters(account_id TEXT NOT NULL, character_name TEXT NOT NULL,"
             " map_id TEXT NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL,"
             " level INTEGER, hp INTEGER, mp INTEGER, inventory_json TEXT NOT NULL DEFAULT '[]');");
    exec_sql(database,
             "INSERT INTO characters(account_id, character_name, map_id, x, y, level, hp, mp,"
             " inventory_json) VALUES('guest', 'MigratedHero', '0', 11, 22, 7, 33, 44, '[]');");
    sqlite3_close(database);
    database = nullptr;
  } catch (...) {
    sqlite3_close(database);
    throw;
  }

  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  const auto migrated = repository.load_character("guest", "MigratedHero");
  return migrated.has_value() && migrated->save_version == 0 && migrated->x == 11 &&
         migrated->y == 22 && migrated->ability.level == 7;
}

bool stale_save_is_rejected(const std::filesystem::path& source_root,
                            const std::filesystem::path& database_path) {
  mir2::Repository repository(database_path);
  repository.ensure_schema(source_root / "schema" / "mir2.sql");

  auto first = make_character(100, 1);
  if (!repository.save_character(first)) {
    return false;
  }
  auto second = first;
  second.gold = 200;
  second.x = 331;
  second.save_version = 2;
  if (!repository.save_character(second)) {
    return false;
  }
  auto stale = first;
  stale.gold = 9999;
  stale.x = 399;
  stale.bag_items[0].make_index = 9001;
  stale.bag_items[0].index = 2;
  stale.save_version = 1;
  if (repository.save_character(stale)) {
    return false;
  }

  auto loaded = repository.load_character("guest", "VersionHero");
  if (!loaded.has_value() || loaded->gold != 200 || loaded->x != 331 ||
      loaded->save_version != 2 || loaded->bag_items[0].make_index != 0) {
    return false;
  }

  auto equal = *loaded;
  equal.gold = 300;
  equal.save_version = 2;
  if (!repository.save_character(equal)) {
    return false;
  }
  loaded = repository.load_character("guest", "VersionHero");
  if (!loaded.has_value() || loaded->gold != 300 || loaded->save_version != 2) {
    return false;
  }

  if (!repository.delete_character("guest", "VersionHero")) {
    return false;
  }
  auto future = equal;
  future.gold = 999;
  future.save_version = 1000;
  if (repository.save_character(stale) || repository.save_character(equal) ||
      repository.save_character(future) ||
      repository.load_character("guest", "VersionHero").has_value()) {
    return false;
  }

  auto recreated = make_character(400, 0);
  if (!repository.create_character(recreated)) {
    return false;
  }
  loaded = repository.load_character("guest", "VersionHero");
  return loaded.has_value() && loaded->gold == 400 &&
         loaded->save_version == kDeletedCharacterSaveVersionBarrier + 1;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_persistence_version_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  try {
    if (!old_schema_migrates_to_zero_version(source_root, temp_root / "old.sqlite")) {
      return fail("old schema migration");
    }
    if (!stale_save_is_rejected(source_root, temp_root / "version.sqlite")) {
      return fail("stale save rejection");
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return fail("exception");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
