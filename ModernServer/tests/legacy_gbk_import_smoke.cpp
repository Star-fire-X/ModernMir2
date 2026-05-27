#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/config_loader.hpp"
#include "importer/legacy_importer.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::string gbk(std::initializer_list<unsigned int> values) {
  std::string text;
  text.reserve(values.size());
  for (const auto value : values) {
    text.push_back(static_cast<char>(value));
  }
  return text;
}

std::string utf8(std::u8string_view text) {
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path path_from_utf8(std::u8string_view text) {
  return std::filesystem::path(std::u8string(text));
}

void write_bytes(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

const mir2::MapConfig* find_map(const mir2::HostConfig& config, std::string_view id) {
  for (const auto& map : config.maps) {
    if (map.id == id) {
      return &map;
    }
  }
  return nullptr;
}

const mir2::NpcConfig* find_npc(const mir2::HostConfig& config, std::string_view name) {
  for (const auto& npc : config.npcs) {
    if (npc.name == name) {
      return &npc;
    }
  }
  return nullptr;
}

const mir2::SpawnConfig* find_spawn(const mir2::HostConfig& config, std::string_view name) {
  for (const auto& spawn : config.spawns) {
    if (spawn.name == name) {
      return &spawn;
    }
  }
  return nullptr;
}

const mir2::ItemConfig* find_item(const mir2::HostConfig& config, std::string_view name) {
  for (const auto& item : config.items) {
    if (item.name == name) {
      return &item;
    }
  }
  return nullptr;
}

const mir2::MonsterDropConfig* find_drop(const mir2::HostConfig& config,
                                         std::string_view monster_name,
                                         std::string_view item_name) {
  for (const auto& drop : config.monster_drops) {
    if (drop.monster_name == monster_name && drop.item_name == item_name) {
      return &drop;
    }
  }
  return nullptr;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.level = 12;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  return character;
}

mir2::LogicCommand enter_world() {
  auto character = make_character();
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = 12;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand click_npc(std::uint64_t actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = 12;
  command.target_actor_id = actor_id;
  return command;
}

bool contains_text(const std::string& haystack, std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

bool dialog_contains(const std::vector<mir2::NpcDialogSectionConfig>& sections,
                     std::string_view needle) {
  for (const auto& section : sections) {
    if (contains_text(section.text, needle)) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_legacy_gbk_import";
  const auto legacy = root / "legacy";
  const auto output = root / "output";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  const auto bichon = gbk({0xB1, 0xC8, 0xC6, 0xE6, 0xCA, 0xA1});
  const auto npc_name = gbk({0xD6, 0xD0, 0xCE, 0xC4, 0xC9, 0xCC, 0xC8, 0xCB});
  const auto guard_name = gbk({0xCA, 0xD8, 0xCE, 0xC0});
  const auto welcome = gbk({0xBB, 0xB6, 0xD3, 0xAD, 0xD3, 0xC2, 0xCA, 0xBF});
  const auto quest_done = gbk({0xC8, 0xCE, 0xCE, 0xF1, 0xCD, 0xEA, 0xB3, 0xC9});
  const auto quest_file = gbk({0xC8, 0xEB, 0xBF, 0xDA, 0xC8, 0xCE, 0xCE, 0xF1}) + ".txt";
  const auto quest_file_two = quest_done + ".txt";
  const auto monster = gbk({0xB2, 0xE2, 0xCA, 0xD4, 0xB9, 0xD6});
  const auto item = gbk({0xC4, 0xBE, 0xBD, 0xA3});
  const auto quest_item = gbk({0xB2, 0xE2, 0xCA, 0xD4, 0xCE, 0xEF, 0xC6, 0xB7});
  const auto define_file = npc_name + ".txt";
  const auto call_file = guard_name + ".txt";

  write_bytes(legacy / "!SetUp.txt",
              "[Setup]\n"
              "HomeMap=0\n"
              "HomeX=10\n"
              "HomeY=10\n");
  write_bytes(legacy / "Envir" / "StartPoint.txt", "0 10 10\n");
  write_bytes(legacy / "Envir" / "MapInfo.txt",
              std::string{"[0 "} + bichon + " 0] CHECKQUEST(" + quest_file + ")\n");
  write_bytes(legacy / "MakeItem.txt", item + std::string{"\n"});
  write_bytes(legacy / "Envir" / "MonGen.txt",
              std::string{"0 15 15 "} + monster + " 0 1 1 0\n");
  write_bytes(legacy / "Envir" / "MonZen.txt", "");
  write_bytes(legacy / "Envir" / "MonItems" / "Oma.txt",
              std::string{"1/1 "} + item + " 1\n");
  write_bytes(legacy / "Envir" / "merchant.txt",
              std::string{"9gbk 0 11 10 "} + npc_name + "\n");
  write_bytes(legacy / "Envir" / "Npcs.txt",
              guard_name + std::string{" 0 0 12 10 0 0\n"} +
                  item + " 0 0 13 10 0 0\n");
  write_bytes(legacy / "Envir" / "Npc_def" / path_from_utf8(u8"守卫-0.txt"),
              std::string{"[@main]\n"
                          "#SAY\n"} +
                  guard_name + "\\\n");
  write_bytes(legacy / "Envir" / "Npc_def" / path_from_utf8(u8"木剑-0.txt"),
              std::string{"[@main]\n"
                          "#SAY\n"} +
                  item + "\\\n");
  const auto merchant_script = std::string{"#INCLUDE "} + define_file + "\n" +
                               "[@main]\n"
                               "#SAY\n" +
                               welcome + "\\\n"
                               "#CALL [" +
                               call_file + "] @called\n";
  write_bytes(legacy / "Envir" / "market_def" / "9gbk-0.txt", merchant_script);
  write_bytes(legacy / "Envir" / "Defines" / path_from_utf8(u8"中文商人.txt"),
              std::string{"#DEFINE @WELCOME "} + welcome + "\n");
  write_bytes(legacy / "Envir" / "QuestDiary" / path_from_utf8(u8"守卫.txt"),
              std::string{"[@called]\n"
                          "#SAY\n"} +
                  quest_done + "\\\n");
  write_bytes(legacy / "Envir" / "MapQuest.txt",
              std::string{"0 [0] 0 "} + monster + " " + quest_item + " " + quest_file + "\n" +
                  "0 [1] 0 " + monster + " * " + quest_file_two + "\n");
  write_bytes(legacy / "Envir" / "MapQuest_def" / path_from_utf8(u8"入口任务.txt"),
              std::string{"[@main]\n"
                          "#SAY\n"} +
                  quest_done + "\\\n");
  write_bytes(legacy / "Envir" / "MapQuest_def" / path_from_utf8(u8"任务完成.txt"),
              std::string{"[@main]\n"
                          "#SAY\n"} +
                  welcome + "\\\n");

  mir2::LegacyImporter importer;
  const auto report = importer.import_tree(legacy, output);
  assert(report.map_count == 1);
  assert(report.npc_count == 3);
  assert(report.map_quest_count == 2);
  assert(report.item_count == 1);
  assert(report.spawn_count == 1);
  assert(report.monster_drop_count == 1);

  mir2::ConfigLoader loader;
  auto config = loader.load(output);
  const auto* map = find_map(config, "0");
  assert(map != nullptr);
  assert(map->title == utf8(u8"比奇省"));
  assert(map->check_quest.has_value());
  assert(!map->check_quest->dialog_sections.empty());
  assert(contains_text(map->check_quest->dialog_sections.front().text, utf8(u8"任务完成")));

  const auto* merchant = find_npc(config, utf8(u8"中文商人"));
  assert(merchant != nullptr);
  assert(!merchant->dialog_sections.empty());
  assert(dialog_contains(merchant->dialog_sections, utf8(u8"欢迎勇士")));
  assert(dialog_contains(merchant->dialog_sections, utf8(u8"任务完成")));
  const auto* guard = find_npc(config, utf8(u8"守卫"));
  assert(guard != nullptr);
  assert(!guard->dialog_sections.empty());
  assert(contains_text(guard->dialog_sections.front().text, utf8(u8"守卫")));
  const auto* sword_npc = find_npc(config, utf8(u8"木剑"));
  assert(sword_npc != nullptr);
  assert(!sword_npc->dialog_sections.empty());
  assert(contains_text(sword_npc->dialog_sections.front().text, utf8(u8"木剑")));

  assert(find_item(config, utf8(u8"木剑")) != nullptr);
  assert(find_spawn(config, utf8(u8"测试怪")) != nullptr);
  assert(find_drop(config, "Oma", utf8(u8"木剑")) != nullptr);
  assert(config.map_quests.size() == 2);
  assert(config.map_quests.front().monster_name == utf8(u8"测试怪"));
  assert(config.map_quests.front().item_name == utf8(u8"测试物品"));
  assert(!config.map_quests.front().dialog_sections.empty());
  assert(contains_text(config.map_quests.front().dialog_sections.front().text,
                       utf8(u8"任务完成")));
  assert(!config.map_quests[1].dialog_sections.empty());
  assert(contains_text(config.map_quests[1].dialog_sections.front().text,
                       utf8(u8"欢迎勇士")));

  config.maps.front().width = 30;
  config.maps.front().height = 30;
  config.maps.front().home_x = 10;
  config.maps.front().home_y = 10;
  config.npcs.erase(std::remove_if(config.npcs.begin(), config.npcs.end(),
                                   [&](const mir2::NpcConfig& npc) {
                                     return npc.name != utf8(u8"中文商人");
                                   }),
                    config.npcs.end());

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter_world()));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.route_logic_command(click_npc(1)));
  const auto dialog_dispatch = runtime.tick(1500);
  const auto say_packet = find_packet(dialog_dispatch, mir2::kSmMerchantSay);
  assert(say_packet.has_value());
  const auto say_text = mir2::legacy_decode_text(say_packet->body);
  assert(contains_text(say_text, utf8(u8"中文商人")));
  assert(contains_text(say_text, utf8(u8"欢迎勇士")));

  std::filesystem::remove_all(root, ignored);
  return 0;
}
