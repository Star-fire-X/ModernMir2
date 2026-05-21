#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/config_loader.hpp"
#include "importer/legacy_importer.hpp"

namespace {

void write_text(const std::filesystem::path& path, const char* text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

const mir2::SpawnConfig* find_spawn(const mir2::HostConfig& config, const std::string& name) {
  for (const auto& spawn : config.spawns) {
    if (spawn.name == name) {
      return &spawn;
    }
  }
  return nullptr;
}

const mir2::MonsterDefConfig* find_monster(const mir2::HostConfig& config,
                                           const std::string& name) {
  for (const auto& monster : config.monsters) {
    if (monster.name == name) {
      return &monster;
    }
  }
  return nullptr;
}

const mir2::MonsterDropConfig* find_drop(const mir2::HostConfig& config,
                                         const std::string& monster_name,
                                         const std::string& item_name) {
  for (const auto& drop : config.monster_drops) {
    if (drop.monster_name == monster_name && drop.item_name == item_name) {
      return &drop;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_legacy_monster_import";
  const auto legacy = root / "legacy";
  const auto output = root / "output";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  write_text(legacy / "!SetUp.txt",
             "[Setup]\n"
             "HomeMap=0\n"
             "HomeX=30\n"
             "HomeY=30\n");
  write_text(legacy / "Envir" / "StartPoint.txt", "0 30 30\n");
  write_text(legacy / "Envir" / "MapInfo.txt", "[0 Test 0]\n");
  write_text(legacy / "Envir" / "MonGen.txt",
             "0 30 31 Oma 8 5 2 100\n"
             "0 40 41 \"Cave Maggot\" 4 2 3 50\n"
             "0 50 51 Red Boar King 6 3 4 25\n");
  write_text(legacy / "Envir" / "MonItems" / "Cave Maggot.txt",
             "1/3 \"Healing Potion\"\n"
             "2 5 Strong Oil 4\n");
  write_text(legacy / "Envir" / "MonItems" / "Oma.txt",
             "1/2 WoodenSword 2\n"
             "1/1 Gold 10\n");
  write_text(legacy / "Envir" / "MakeItem.txt",
             "WoodenSword\n"
             "Healing Potion\n"
             "Strong Oil\n");

  mir2::LegacyImporter importer;
  const auto report = importer.import_tree(legacy, output);
  assert(report.spawn_count == 3);
  assert(report.monster_drop_count == 4);

  write_text(output / "monsters" / "custom_defs.toml",
             "monsters = [\n"
             "  { name = \"CustomMob\", race_server = 81, race_image = 20, appearance = 3,"
             " level = 7, undead = true, cool_eye = 1, exp = 88, hp = 99, mp = 5,"
             " ac = 2, mac = 3, dc = 4, dc_max = 9, mc = 6, sc = 7, agility = 8,"
             " accurate = 9, walk_speed_ms = 450, walk_step = 2, walk_wait_ms = 50,"
             " attack_speed_ms = 700, ai_profile = \"aggressive\" },\n"
             "  { name = \"LowSpeedMob\", race_server = 81, walk_speed_ms = 1,"
             " attack_speed_ms = 1 },\n"
             "  { name = \"AliasMob\", race = 82, race_img = 30, img_index = 4,"
             " lv = 9, undead = 1, dcmax = 12, walk_spd = 1, walk_wait = 25,"
             " attack_spd = 1 }\n"
             "]\n");

  mir2::ConfigLoader loader;
  const auto config = loader.load(output);
  const auto* spawn = find_spawn(config, "Oma");
  assert(spawn != nullptr);
  assert(spawn->map_id == "0");
  assert(spawn->x == 30 && spawn->y == 31);
  assert(spawn->area == 8);
  assert(spawn->count == 5);
  assert(spawn->zen_time_ms == 120000);
  assert(spawn->small_zen_rate == 100);
  assert(spawn->legacy_group);
  const auto* quoted_spawn = find_spawn(config, "Cave Maggot");
  assert(quoted_spawn != nullptr);
  assert(quoted_spawn->map_id == "0");
  assert(quoted_spawn->x == 40 && quoted_spawn->y == 41);
  assert(quoted_spawn->area == 4);
  assert(quoted_spawn->count == 2);
  assert(quoted_spawn->zen_time_ms == 180000);
  assert(quoted_spawn->small_zen_rate == 50);
  assert(quoted_spawn->legacy_group);
  const auto* unquoted_spawn = find_spawn(config, "Red Boar King");
  assert(unquoted_spawn != nullptr);
  assert(unquoted_spawn->map_id == "0");
  assert(unquoted_spawn->x == 50 && unquoted_spawn->y == 51);
  assert(unquoted_spawn->area == 6);
  assert(unquoted_spawn->count == 3);
  assert(unquoted_spawn->zen_time_ms == 240000);
  assert(unquoted_spawn->small_zen_rate == 25);
  assert(unquoted_spawn->legacy_group);

  assert(!config.monster_drops.empty());
  assert(config.monster_drops.front().monster_name == "Cave Maggot");
  const auto* potion_drop = find_drop(config, "Cave Maggot", "Healing Potion");
  assert(potion_drop != nullptr);
  assert(potion_drop->sel_point == 0);
  assert(potion_drop->max_point == 3);
  assert(potion_drop->count == 1);
  const auto* oil_drop = find_drop(config, "Cave Maggot", "Strong Oil");
  assert(oil_drop != nullptr);
  assert(oil_drop->sel_point == 1);
  assert(oil_drop->max_point == 5);
  assert(oil_drop->count == 4);
  const auto* sword_drop = find_drop(config, "Oma", "WoodenSword");
  assert(sword_drop != nullptr);
  assert(sword_drop->sel_point == 0);
  assert(sword_drop->max_point == 2);
  assert(sword_drop->count == 2);
  const auto* gold_drop = find_drop(config, "Oma", "Gold");
  assert(gold_drop != nullptr && gold_drop->max_point == 1 && gold_drop->count == 10);

  const auto* custom = find_monster(config, "CustomMob");
  assert(custom != nullptr);
  assert(custom->race_server == 81);
  assert(custom->race_image == 20);
  assert(custom->appearance == 3);
  assert(custom->level == 7);
  assert(custom->undead);
  assert(custom->hp == 99);
  assert(custom->dc == 4 && custom->dc_max == 9);
  assert(custom->walk_speed_ms == 450);
  assert(custom->walk_step == 2);
  assert(custom->walk_wait_ms == 50);
  assert(custom->attack_speed_ms == 700);
  assert(custom->ai_profile == mir2::MonsterAiProfile::aggressive);

  const auto* low_speed = find_monster(config, "LowSpeedMob");
  assert(low_speed != nullptr);
  assert(low_speed->walk_speed_ms == 200);
  assert(low_speed->attack_speed_ms == 200);
  const auto* alias = find_monster(config, "AliasMob");
  assert(alias != nullptr);
  assert(alias->race_server == 82);
  assert(alias->race_image == 30);
  assert(alias->appearance == 4);
  assert(alias->level == 9);
  assert(alias->undead);
  assert(alias->dc_max == 12);
  assert(alias->walk_speed_ms == 200);
  assert(alias->walk_wait_ms == 25);
  assert(alias->attack_speed_ms == 200);

  std::filesystem::remove_all(root, ignored);
  return 0;
}
