#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

constexpr std::uint64_t kHeroSessionId = 12;

struct TraceSnapshot {
  std::string action{};
  bool success{false};
  std::int32_t value{0};
  std::string label{};
};

struct TraceExpectation {
  std::string action{};
  bool success{false};
  std::int32_t value{0};
  std::string label{};
};

struct ItemCountExpectation {
  std::string name{};
  std::int32_t count{0};
};

struct MonsterExpectation {
  std::string map_id{};
  std::uint64_t actor_id{0};
  bool present{false};
};

struct LegacyScriptGoldenCase {
  std::string name{};
  std::string script{};
  std::vector<mir2::SpawnConfig> spawns{};
  std::vector<mir2::CharacterRecord> extra_characters{};
  std::uint64_t npc_actor_id{1};
  std::vector<std::uint16_t> packet_idents{};
  std::string say_prefix{};
  std::int32_t gold{100};
  std::vector<ItemCountExpectation> bag_counts{};
  std::uint8_t quest_mark0{0};
  std::string map_id{"0"};
  std::int32_t x{10};
  std::int32_t y{10};
  std::vector<TraceExpectation> traces{};
  std::optional<MonsterExpectation> monster{};
};

int fail(std::string_view stage, std::string_view case_name) {
  std::cerr << "legacy_script_golden_baseline_smoke failed at " << stage
            << " case=" << case_name << '\n';
  return 1;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

bool contains_all(const std::string& text, const std::vector<std::string_view>& tokens) {
  for (const auto token : tokens) {
    if (text.find(token) == std::string::npos) {
      std::cerr << "missing token: " << token << '\n';
      return false;
    }
  }
  return true;
}

void print_packet_idents(std::string_view label, const std::vector<std::uint16_t>& idents) {
  std::cerr << label << ':';
  for (const auto ident : idents) {
    std::cerr << ' ' << ident;
  }
  std::cerr << '\n';
}

void print_traces(const std::vector<TraceSnapshot>& traces) {
  for (const auto& trace : traces) {
    std::cerr << "trace action=" << trace.action << " success=" << trace.success
              << " value=" << trace.value << " label=" << trace.label << '\n';
  }
}

std::string legacy_coin_token() {
  return "\xE9\x87\x91\xE5\xB8\x81";
}

mir2::LegacyUserItem make_item(std::uint16_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = index;
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord make_character(std::string name, std::string map_id = "0",
                                      std::int32_t x = 10, std::int32_t y = 10) {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = x;
  character.y = y;
  character.gold = 100;
  character.ability.level = 10;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.max_weight = 1000;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.bag_items[0] = make_item(1, 1001);
  return character;
}

mir2::LogicCommand enter_world(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand click_npc(std::uint64_t session_id, std::uint64_t npc_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = session_id;
  command.target_actor_id = npc_actor_id;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::vector<std::uint16_t> packet_ident_snapshot(const mir2::RuntimeDispatch& dispatch) {
  std::vector<std::uint16_t> result;
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value()) {
      result.push_back(decoded->message.ident);
    }
  }
  return result;
}

std::vector<TraceSnapshot> legacy_script_trace_snapshot(
    const mir2::RuntimeDispatch& dispatch) {
  std::vector<TraceSnapshot> result;
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage != "LegacyScript") {
      continue;
    }
    result.push_back(TraceSnapshot{trace.action, trace.success, trace.value, trace.label});
  }
  return result;
}

std::string merchant_say_text(const mir2::RuntimeDispatch& dispatch) {
  const auto packet = find_packet(dispatch, mir2::kSmMerchantSay);
  if (!packet.has_value()) {
    return {};
  }
  return mir2::legacy_decode_string(packet->body);
}

std::int32_t count_bag_items(const mir2::CharacterRecord& character,
                             const std::string& item_name) {
  const auto wanted_coin = item_name == legacy_coin_token();
  std::int32_t count = 0;
  for (const auto& item : character.bag_items) {
    if (mir2::is_empty(item)) {
      continue;
    }
    if ((item.index == 1 && item_name == "Apple") || (item.index == 2 && wanted_coin)) {
      ++count;
    }
  }
  return count;
}

mir2::HostConfig make_config(const LegacyScriptGoldenCase& test_case) {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{
      .id = "0",
      .title = "ScriptMap",
      .width = 30,
      .height = 30,
      .home_x = 10,
      .home_y = 10,
  });
  config.maps.push_back(mir2::MapConfig{
      .id = "1",
      .title = "RemoteScriptMap",
      .width = 30,
      .height = 30,
      .home_x = 10,
      .home_y = 10,
  });
  config.items.push_back(mir2::ItemConfig{
      .id = 1,
      .name = "Apple",
      .weight = 1,
      .dura_max = 1000,
      .equip_slot = -1,
  });
  config.items.push_back(mir2::ItemConfig{
      .id = 2,
      .name = legacy_coin_token(),
      .weight = 1,
      .dura_max = 1000,
      .equip_slot = -1,
  });
  config.spawns = test_case.spawns;

  mir2::NpcConfig npc;
  npc.id = "script_golden";
  npc.map_id = "0";
  npc.name = "GoldenNpc";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back({"@main", test_case.script});
  config.npcs.push_back(std::move(npc));
  return config;
}

bool has_trace_expectation(const std::vector<TraceSnapshot>& traces,
                           const TraceExpectation& expected) {
  return std::any_of(traces.begin(), traces.end(), [&](const TraceSnapshot& trace) {
    return trace.action == expected.action && trace.success == expected.success &&
           trace.value == expected.value && trace.label == expected.label;
  });
}

int assert_script_case(const LegacyScriptGoldenCase& test_case) {
  mir2::LogicRuntime runtime(make_config(test_case));
  runtime.initialize();

  static_cast<void>(runtime.route_logic_command(enter_world(kHeroSessionId,
                                                            make_character("Hero"))));
  for (std::size_t index = 0; index < test_case.extra_characters.size(); ++index) {
    static_cast<void>(runtime.route_logic_command(
        enter_world(kHeroSessionId + index + 1, test_case.extra_characters[index])));
  }
  static_cast<void>(runtime.tick(1000));

  static_cast<void>(runtime.route_logic_command(
      click_npc(kHeroSessionId, test_case.npc_actor_id)));
  const auto dispatch = runtime.tick(1502);

  const auto packet_idents = packet_ident_snapshot(dispatch);
  if (packet_idents != test_case.packet_idents) {
    print_packet_idents("expected", test_case.packet_idents);
    print_packet_idents("actual", packet_idents);
    return fail("packet snapshot", test_case.name);
  }
  if (!test_case.say_prefix.empty() &&
      merchant_say_text(dispatch).find(test_case.say_prefix) != 0) {
    return fail("merchant say", test_case.name);
  }

  const auto snapshot = runtime.snapshot_character_actor("Hero");
  if (!snapshot.has_value()) {
    return fail("character snapshot", test_case.name);
  }
  if (snapshot->gold != test_case.gold || snapshot->quest_marks[0] != test_case.quest_mark0 ||
      snapshot->map_id != test_case.map_id || snapshot->x != test_case.x ||
      snapshot->y != test_case.y) {
    return fail("state snapshot", test_case.name);
  }
  for (const auto& item_count : test_case.bag_counts) {
    if (count_bag_items(*snapshot, item_count.name) != item_count.count) {
      return fail("bag snapshot", test_case.name);
    }
  }

  const auto traces = legacy_script_trace_snapshot(dispatch);
  for (const auto& trace : test_case.traces) {
    if (!has_trace_expectation(traces, trace)) {
      std::cerr << "missing trace action=" << trace.action << " success=" << trace.success
                << " value=" << trace.value << " label=" << trace.label << '\n';
      print_traces(traces);
      return fail("trace snapshot", test_case.name);
    }
  }

  if (test_case.monster.has_value()) {
    const auto monster =
        runtime.legacy_monster_snapshot(test_case.monster->map_id, test_case.monster->actor_id);
    if (monster.has_value() != test_case.monster->present) {
      return fail("monster snapshot", test_case.name);
    }
  }

  return 0;
}

mir2::SpawnConfig remote_rat_spawn() {
  mir2::SpawnConfig spawn;
  spawn.map_id = "1";
  spawn.name = "Rat";
  spawn.x = 12;
  spawn.y = 12;
  spawn.max_hp = 12;
  spawn.count = 1;
  return spawn;
}

std::vector<LegacyScriptGoldenCase> golden_cases() {
  const auto coin = legacy_coin_token();
  return {
      LegacyScriptGoldenCase{
          .name = "take_give_legacy_coin_token_current_item_baseline",
          .script = "#IF\nCHECKLEVEL 1\n#ACT\nGIVE " + coin +
                    " 2\nTAKE " + coin + " 1\nSET [1] 1\n#SAY CoinBaseline",
          .packet_idents = {mir2::kSmTurn, mir2::kSmGoldChanged,
                            mir2::kSmGoldChanged, mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/CoinBaseline",
          .gold = 101,
          .bag_counts = {ItemCountExpectation{coin, 0}},
          .quest_mark0 = 0x80,
          .traces = {TraceExpectation{"give_gold", true, 2, "GIVE " + coin + " 2"},
                     TraceExpectation{"take_gold", true, 1, "TAKE " + coin + " 1"}},
      },
      LegacyScriptGoldenCase{
          .name = "checkmonmap_current_name_count_baseline",
          .script = "#IF\nCHECKMONMAP 1 1\n#SAY MonsterMapPass\n#ELSESAY MonsterMapFail",
          .spawns = {remote_rat_spawn()},
          .npc_actor_id = 2,
          .packet_idents = {mir2::kSmTurn, mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/MonsterMapPass",
          .traces = {TraceExpectation{"condition", true, 1, "CHECKMONMAP 1 1"}},
          .monster = MonsterExpectation{"1", 1, true},
      },
      LegacyScriptGoldenCase{
          .name = "checkhum_current_count_only_baseline",
          .script = "#IF\nCHECKHUM 1 2\n#SAY HumPass\n#ELSESAY HumFail",
          .extra_characters = {make_character("RemoteA", "1", 8, 8),
                               make_character("RemoteB", "1", 9, 8)},
          .packet_idents = {mir2::kSmTurn, mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/HumPass",
          .traces = {TraceExpectation{"condition", true, 2, "CHECKHUM 1 2"}},
      },
      LegacyScriptGoldenCase{
          .name = "monclear_current_name_baseline",
          .script = "#IF\nCHECKLEVEL 1\n#ACT\nMONCLEAR 1\n#SAY ClearBaseline",
          .spawns = {remote_rat_spawn()},
          .npc_actor_id = 2,
          .packet_idents = {mir2::kSmTurn, mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/ClearBaseline",
          .traces = {TraceExpectation{"monclear", true, 1, "MONCLEAR 1"}},
          .monster = MonsterExpectation{"1", 1, false},
      },
      LegacyScriptGoldenCase{
          .name = "istakeitem_current_checkitem_baseline",
          .script = "#IF\nISTAKEITEM Apple 1\n#SAY IsTakePass\n#ELSESAY IsTakeFail",
          .packet_idents = {mir2::kSmTurn, mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/IsTakePass",
          .bag_counts = {ItemCountExpectation{"Apple", 1}},
          .traces = {TraceExpectation{"condition", true, 1, "ISTAKEITEM Apple 1"}},
      },
      LegacyScriptGoldenCase{
          .name = "takecheckitem_current_name_delete_baseline",
          .script = "#IF\nCHECKITEM Apple 1\n#ACT\nTAKECHECKITEM Apple 1\n#SAY TakeCheck",
          .packet_idents = {mir2::kSmTurn, mir2::kSmBagItems, mir2::kSmWeightChanged,
                            mir2::kSmMerchantSay},
          .say_prefix = "GoldenNpc/TakeCheck",
          .bag_counts = {ItemCountExpectation{"Apple", 0}},
          .traces = {TraceExpectation{"takecheckitem", true, 1,
                                      "TAKECHECKITEM Apple 1"}},
      },
  };
}

bool check_fixture_text() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto fixture = read_text(source_root / "tests" / "golden" / "npc_script_pr1" /
                                 "legacy_script_baseline_cases.json");
  return contains_all(fixture,
                      {
                          "\"take_give_legacy_coin_token_current_item_baseline\"",
                          "\"checkmonmap_current_name_count_baseline\"",
                          "\"checkhum_current_count_only_baseline\"",
                          "\"monclear_current_name_baseline\"",
                          "\"istakeitem_current_checkitem_baseline\"",
                          "\"takecheckitem_current_name_delete_baseline\"",
                          "\"SM_MERCHANTSAY\": 643",
                          "\"SM_TURN\": 10",
                          "\"SM_ADDITEM\": 200",
                          "\"SM_BAGITEMS\": 201",
                          "\"SM_WEIGHTCHANGED\": 622",
                          "\"SM_GOLDCHANGED\": 653",
                          "\"legacy_coin_token\": \"\\u91d1\\u5e01\"",
                      });
}

}  // namespace

int main() {
  if (!check_fixture_text()) {
    return fail("fixture text", "npc_script_pr1");
  }

  for (const auto& test_case : golden_cases()) {
    if (const auto result = assert_script_case(test_case); result != 0) {
      return result;
    }
  }
  return 0;
}
