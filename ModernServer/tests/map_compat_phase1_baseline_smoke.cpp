#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_map_environment.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(int stage, std::string_view message) {
  std::cerr << "map_compat_phase1_baseline_smoke failed at stage " << stage << ": " << message
            << '\n';
  return stage;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint64_t session_id,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view stage,
               std::string_view action, std::string_view label = {}) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == stage && trace.action == action &&
        (label.empty() || trace.label == label)) {
      return true;
    }
  }
  return false;
}

mir2::CharacterRecord make_character(std::string name, std::int32_t x, std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = y;
  character.ability.level = 20;
  character.ability.hp = 40;
  character.ability.max_hp = 40;
  character.ability.max_weight = 200;
  character.ability.max_wear_weight = 200;
  character.ability.max_hand_weight = 200;
  return character;
}

mir2::LogicCommand make_enter(std::uint64_t session_id, const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  return command;
}

bool check_area_state_baseline() {
  mir2::HostConfig config;
  mir2::MapConfig map;
  map.id = "0";
  map.title = "SafeMap";
  map.width = 100;
  map.height = 100;
  map.allow_pk = true;
  map.safe_zones.push_back(mir2::MapZoneConfig{20, 20, 21, 21});
  config.maps.push_back(map);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  const auto hero = make_character("Hero", 20, 20);
  static_cast<void>(runtime.route_logic_command(make_enter(1, hero)));
  const auto dispatch = runtime.tick();
  const auto area = find_packet(dispatch, 1, mir2::kSmAreaState);
  if (!area.has_value()) {
    return false;
  }

  const auto current_safe_bit = (area->message.recog & 1) != 0;
  constexpr bool kDelphiExpectedAreaSafeBitForGenericSafeZone = false;
  if (!current_safe_bit) {
    return false;
  }
  return current_safe_bit != kDelphiExpectedAreaSafeBitForGenericSafeZone;
}

bool check_mapquest_empty_trigger_baseline() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "QuestMap", {}, 20, 20, 10, 10});
  mir2::MapQuestConfig quest;
  quest.map_id = "0";
  quest.set_number = 0;
  quest.value = 0;
  quest.qfile = "entry.txt";
  quest.dialog_sections.push_back({"@main", "#ACT\nSET [1] 1\n"});
  config.map_quests.push_back(quest);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  const auto hero = make_character("QuestHero", 10, 10);
  static_cast<void>(runtime.route_logic_command(make_enter(7, hero)));
  const auto dispatch = runtime.tick();
  const auto snapshot = runtime.snapshot_character_actor("QuestHero");
  if (!snapshot.has_value()) {
    return false;
  }

  const auto current_triggers_enter = (snapshot->quest_marks[0] & 0x80U) != 0U &&
                                      has_trace(dispatch, "LegacyScript", "mapquest_trigger",
                                                "enter");
  constexpr bool kDelphiExpectedEnterTriggerForEmptyMonItem = false;
  return current_triggers_enter && current_triggers_enter != kDelphiExpectedEnterTriggerForEmptyMonItem;
}

bool check_itemshow_body_baseline() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ItemMap", {}, 20, 20, 10, 10});
  config.items.push_back(mir2::ItemConfig{1, "Token", 1, 1, 0, 0, 2, 100, -1, 0, 0});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  auto hero = make_character("DropHero", 10, 10);
  hero.bag_items[0].make_index = 9001;
  hero.bag_items[0].index = 1;
  hero.bag_items[0].dura = 1;
  hero.bag_items[0].dura_max = 1;
  static_cast<void>(runtime.route_logic_command(make_enter(9, hero)));
  static_cast<void>(runtime.tick(20));

  mir2::LogicCommand drop;
  drop.kind = mir2::LogicCommandKind::drop_item;
  drop.session_id = 9;
  drop.item_make_index = 9001;
  drop.text = "Token";
  static_cast<void>(runtime.route_logic_command(drop));
  const auto dispatch = runtime.tick(300);
  const auto show = find_packet(dispatch, 9, mir2::kSmItemShow);
  if (!show.has_value()) {
    return false;
  }

  return mir2::legacy_decode_string(show->body) == "Token";
}

bool check_event_wire_gap_baseline(const std::filesystem::path& modern_server_root) {
  const auto legacy_types = read_text(modern_server_root / "src" / "protocol" / "legacy_types.hpp");
  const auto packets = read_text(modern_server_root / "src" / "world" / "map_actor_packets.hpp");
  if (legacy_types.empty() || packets.empty()) {
    return false;
  }
  return legacy_types.find("kSmShowEvent") == std::string::npos &&
         legacy_types.find("kSmHideEvent") == std::string::npos &&
         packets.find("make_show_event_packet") == std::string::npos &&
         packets.find("make_hide_event_packet") == std::string::npos;
}

bool check_canfly_canfirefly_split() {
  auto map = std::make_shared<mir2::legacy::MapDocument>();
  map->width = 4;
  map->height = 4;
  map->cells.resize(16);
  map->cells[0 * 4 + 3].bk_img = 0x8000U;
  mir2::LegacyMapEnvironment env(4, 4, map);
  return !env.can_fly_line(0, 0, 3, 0) && env.can_fire_fly_line(0, 0, 3, 0);
}

bool check_map3_patch_points_in_doc(const std::filesystem::path& modern_server_root) {
  const auto doc = read_text(modern_server_root / "docs" / "map_compat_phase1_findings.md");
  if (doc.empty()) {
    return false;
  }
  for (int index = 1; index <= 7; ++index) {
    const auto token = "MAP3_PATCH_" + std::to_string(index);
    if (doc.find(token) == std::string::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  const auto modern_server_root = std::filesystem::path(__FILE__).parent_path().parent_path();

  if (!check_area_state_baseline()) {
    return fail(1, "area-state baseline");
  }
  if (!check_mapquest_empty_trigger_baseline()) {
    return fail(2, "mapquest empty-mon-item enter baseline");
  }
  if (!check_itemshow_body_baseline()) {
    return fail(3, "itemshow body baseline");
  }
  if (!check_event_wire_gap_baseline(modern_server_root)) {
    return fail(4, "event wire gap baseline");
  }
  if (!check_canfly_canfirefly_split()) {
    return fail(5, "CanFly/CanFireFly baseline");
  }
  if (!check_map3_patch_points_in_doc(modern_server_root)) {
    return fail(6, "Map3 patch-point baseline doc");
  }
  return 0;
}
