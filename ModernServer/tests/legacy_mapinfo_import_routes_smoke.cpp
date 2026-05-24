#include <cassert>
#include <filesystem>
#include <fstream>

#include "config/config_loader.hpp"
#include "importer/legacy_importer.hpp"

namespace {

void write_text(const std::filesystem::path& path, const char* text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

const mir2::MapConfig* find_map(const mir2::HostConfig& config, const std::string& id) {
  for (const auto& map : config.maps) {
    if (map.id == id) {
      return &map;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_legacy_mapinfo_import_routes";
  const auto legacy = root / "legacy";
  const auto output = root / "output";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  write_text(legacy / "!SetUp.txt",
             "[Setup]\n"
             "HomeMap=0\n"
             "HomeX=330\n"
             "HomeY=270\n");
  write_text(legacy / "Envir" / "StartPoint.txt",
             "0 330 270\n"
             "1 50 60\n");
  write_text(legacy / "Envir" / "MapInfo.txt",
             "[0 Bichon Province 0] SAFE DAY NORECALL NEEDSET_OFF(9) L7\n"
             "0 331 270 -> 1 50 60\n"
             "[1 Cave Path 0] FIGHT3 DARK QUIZ NODRUG NORECONNECT(0) "
             "CHECKQUEST(entry.txt) NEEDSET_ON(12)\n"
             "1 49 60 -> 0 330 270\n");
  write_text(legacy / "Envir" / "MapQuest_def" / "entry.txt",
             "[@main]\n"
             "#ACT\n"
             "SET [12] 1\n");
  write_text(legacy / "Envir" / "MonZen.txt", "");

  mir2::LegacyImporter importer;
  const auto report = importer.import_tree(legacy, output);
  assert(report.map_count == 2);

  mir2::ConfigLoader loader;
  const auto config = loader.load(output);
  assert(config.budgets.tick_ms == 10);
  const auto* home = find_map(config, "0");
  const auto* cave = find_map(config, "1");
  assert(home != nullptr && cave != nullptr);
  assert(home->law_full);
  assert(home->daylight);
  assert(home->need_level == 7);
  assert(home->need_set_number == 9);
  assert(home->need_set_value == 0);
  assert(!home->allow_pk);
  assert(home->safe_zones.size() == 1);
  assert(home->safe_zones.front().x == 320 && home->safe_zones.front().y == 260);
  assert(home->safe_zones.front().width == 21 && home->safe_zones.front().height == 21);
  assert(home->gates.size() == 1);
  assert(home->gates.front().x == 331 && home->gates.front().y == 270);
  assert(home->gates.front().target_map_id == "1");
  assert(home->gates.front().target_x == 50 && home->gates.front().target_y == 60);

  assert(cave->fight3_zone);
  assert(cave->darkness);
  assert(cave->quiz_zone);
  assert(cave->no_drug);
  assert(cave->no_reconnect);
  assert(cave->back_map == "0");
  assert(cave->need_set_number == 12);
  assert(cave->need_set_value == 1);
  assert(cave->check_quest.has_value());
  assert(cave->check_quest->qfile.find("entry.txt") != std::string::npos);
  assert(!cave->check_quest->dialog_sections.empty());
  assert(cave->gates.size() == 1);

  const auto repeat = importer.import_tree(legacy, output);
  assert(repeat.map_count == 2);
  const auto repeated_config = loader.load(output);
  const auto* repeated_home = find_map(repeated_config, "0");
  assert(repeated_home != nullptr && repeated_home->gates.size() == 1);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
