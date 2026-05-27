#include <algorithm>
#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LegacyUserItem make_item(std::uint16_t index, std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = index;
  item.make_index = make_index;
  item.dura = 1000;
  item.dura_max = 1000;
  return item;
}

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.gold = 100;
  character.ability.level = 12;
  character.ability.hp = 30;
  character.ability.max_hp = 30;
  character.ability.max_weight = 1000;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.bag_items[0] = make_item(1, 101);
  character.equipped_items[mir2::kEquipWeapon] = make_item(2, 201);
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

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::string action = {}) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = 12;
  command.target_actor_id = 1;
  command.text = std::move(action);
  return command;
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

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view action) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" && trace.action == action;
                     });
}

bool has_trace_label(const mir2::RuntimeDispatch& dispatch, std::string_view action,
                     std::string_view label) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [&](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" && trace.action == action &&
                              trace.label == label;
                     });
}

bool has_notice(const mir2::RuntimeDispatch& dispatch, std::string_view text) {
  return std::any_of(dispatch.session_events.begin(), dispatch.session_events.end(),
                     [&](const mir2::SessionEvent& event) {
                       const auto decoded = mir2::decode_legacy_game_packet(event.packet);
                       return decoded.has_value() && decoded->message.ident == mir2::kSmHear &&
                              mir2::legacy_decode_string(decoded->body) == text;
                     });
}

std::string merchant_say_text(const mir2::RuntimeDispatch& dispatch) {
  const auto packet = find_packet(dispatch, mir2::kSmMerchantSay);
  assert(packet.has_value());
  return mir2::legacy_decode_string(packet->body);
}

bool has_unsupported_script_trace(const mir2::RuntimeDispatch& dispatch) {
  return std::any_of(dispatch.legacy_traces.begin(), dispatch.legacy_traces.end(),
                     [](const mir2::LegacyRuntimeTrace& trace) {
                       return trace.stage == "LegacyScript" &&
                              (trace.action == "unsupported_action" ||
                               trace.action == "unsupported_condition");
                     });
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_script_command_decode_full_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);

  mir2::HostConfig config;
  config.runtime.data_dir = temp_root / "data";
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{
      .id = "0",
      .title = "ScriptMap",
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
      .name = "Sword",
      .weight = 1,
      .std_mode = 5,
      .dura_max = 1000,
      .equip_slot = static_cast<std::int32_t>(mir2::kEquipWeapon),
  });

  mir2::NpcConfig npc;
  npc.id = "rewarder";
  npc.map_id = "0";
  npc.name = "Rewarder";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back({"@main",
                                 R"(#ACT
SET [1] 1
SETOPEN [2] 1
SETUNIT [3] 1
MOV P1 10
MOV P2 20
MOV G0 30
MOV D0 4
SETDAILYQUEST 7
ADDNAMELIST Names Hero
ADDNAMELIST Ids acct
GOTO @check)"});
  npc.dialog_sections.push_back({"@check",
                                 R"(#IF
CHECK [1] 1
CHECKOPEN [2] 1
CHECKUNIT [3] 1
RANDOM 1
GENDER MAN
DAYTIME DAY
CHECKLEVEL 10
CHECKJOB WARRIOR
CHECKITEM Apple 1
CHECKITEMW Sword 1
CHECKGOLD 100
ISTAKEITEM Apple 1
CHECKDURA Apple 0
CHECKDURAEVA Sword 0
DAYOFWEEK
HOUR 0 23
MIN 0 59
CHECKPKPOINT 0
CHECKLUCKYPOINT 0
CHECKMONMAP 0
CHECKMONAREA 0
CHECKHUM 1
CHECKBAGGAGE 1
CHECKNAMELIST Names Hero
CHECK_DELETE_NAMELIST Names Hero
CHECK_DELETE_IDLIST Ids acct
CHECKDAILYQUEST 7
RANDOMEX 100 100
EQUAL P1 10
LARGE P2 P1
SMALL P1 P2
EQUAL G0 30
EQUAL D0 4
LARGE G0 P2
SMALL D0 P1
#ACT
MOV P3 5
INC P3 2
DEC P3 1
SUM P3 P1
MOVR P5 1
MOV G1 7
INC G1 2
DEC G1 1
SUM G0 G1
MOV D0 11
MOV D1 12
MOV D2 13
MOV D3 14
MOV D4 15
MOV D5 16
MOV D6 17
MOV D7 18
MOV D8 19
MOV D9 20
INC D1 1
DEC D1 1
MOVR D2 1
SUM D0 D1
GIVE Gold 1
TAKE Gold 1
GIVE Apple 1
TAKECHECKITEM Apple 1
GIVE Apple 1
TAKE Apple 1
TAKEW Sword 1
SENDMSG 6 System ready <$USERNAME>
SYSMSG Direct notice <$NPCNAME>
MAPMOVE 0 10 10
MAP 0
PARAM1 0
PARAM2 12
PARAM3 12
MONGEN Rat 1 0
MONCLEAR Rat
TIMERECALL 1
BREAKTIMERECALL
EXCHANGEMAP 0 0
RECALLMAP
ADDBATCH A
BATCHDELAY 1
BATCHMOVE 0 10 10
RANDOMSETDAILYQUEST 9 10
CALL [common] @called
#ELSEACT
SAY Failed)"});
  npc.dialog_sections.push_back({"@called",
                                 R"(#ACT
GOQUEST @after_go)"});
  npc.dialog_sections.push_back({"@after_go",
                                 R"(#IF
CHECKLEVEL 0
#SAY Values <$STR(P9)> <$STR(G9)> <$STR(D9)>
#ACT
PLAYDICE 1 @done)"});
  npc.dialog_sections.push_back({"@done",
                                 R"(#ACT
SAY OK
ENDQUEST)"});
  npc.dialog_sections.push_back({"@close",
                                 R"(#ACT
CLOSE)"});
  npc.dialog_sections.push_back({"@break",
                                 R"(#ACT
BREAK)"});
  config.npcs.push_back(npc);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter_world()));
  const auto enter_dispatch = runtime.tick(1000);
  assert(find_packet(enter_dispatch, mir2::kSmNewMap).has_value());

  static_cast<void>(
      runtime.route_logic_command(npc_command(mir2::LogicCommandKind::click_npc)));
  const auto script_dispatch = runtime.tick(1502);
  assert(!has_unsupported_script_trace(script_dispatch));
  assert(has_trace(script_dispatch, "RANDOM"));
  assert(has_trace(script_dispatch, "RANDOMEX"));
  assert(has_trace(script_dispatch, "MOVR"));
  assert(has_trace(script_dispatch, "RANDOMSETDAILYQUEST"));
  assert(has_trace(script_dispatch, "variable"));
  assert(has_trace(script_dispatch, "param"));
  assert(has_trace_label(script_dispatch, "sendmsg", "System ready Hero"));
  assert(has_trace_label(script_dispatch, "sysmsg", "Direct notice Rewarder"));
  assert(has_notice(script_dispatch, "System ready Hero"));
  assert(has_notice(script_dispatch, "Direct notice Rewarder"));
  assert(has_trace(script_dispatch, "takew"));
  assert(has_trace(script_dispatch, "mongen"));
  assert(has_trace(script_dispatch, "monclear"));
  assert(has_trace(script_dispatch, "time_recall"));
  assert(has_trace(script_dispatch, "time_recall_cancel"));
  assert(has_trace(script_dispatch, "deferred_action"));
  assert(has_trace(script_dispatch, "goto"));
  assert(has_trace(script_dispatch, "call"));
  assert(has_trace(script_dispatch, "goquest"));
  assert(has_trace(script_dispatch, "playdice"));
  assert(!has_trace(script_dispatch, "endquest"));
  assert(!has_trace_label(script_dispatch, "say", "OK"));
  const auto values_text = merchant_say_text(script_dispatch);
  assert(values_text.find("Rewarder/Values 16 38 23") == 0);
  const auto dice_packet = find_packet(script_dispatch, mir2::kSmPlayDice);
  assert(dice_packet.has_value());
  assert(dice_packet->message.recog == 1);
  assert(dice_packet->message.param == 1);
  mir2::LegacyMessageBodyWL dice_body;
  const auto encoded_dice_body_size = mir2::legacy_encode_buffer(&dice_body, sizeof(dice_body)).size();
  assert(mir2::legacy_decode_buffer(dice_packet->body.substr(0, encoded_dice_body_size),
                                    &dice_body, sizeof(dice_body)));
  assert(dice_body.lparam1 == mir2::make_long(mir2::make_word(11, 12),
                                              mir2::make_word(0, 14)));
  assert(dice_body.lparam2 == mir2::make_long(mir2::make_word(15, 16),
                                              mir2::make_word(17, 18)));
  assert(dice_body.ltag1 == mir2::make_long(mir2::make_word(19, 23), 0));
  assert(mir2::legacy_decode_string(dice_packet->body.substr(encoded_dice_body_size)) ==
         "@done");

  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->quest_marks[0] == 0x80);
  assert(snapshot->quest_open_units[0] == 0x40);
  assert(snapshot->quest_units[0] == 0x20);
  assert(snapshot->script_params[1] == 10);
  assert(snapshot->script_params[2] == 20);
  assert(snapshot->script_params[3] == 6);
  assert(snapshot->script_params[9] == 16);
  assert(snapshot->daily_quest == 9 || snapshot->daily_quest == 10);
  assert(snapshot->equipped_items[mir2::kEquipWeapon].index == 0);

  static_cast<void>(
      runtime.route_logic_command(npc_command(mir2::LogicCommandKind::merchant_select, "@close")));
  const auto close_dispatch = runtime.tick(1753);
  assert(!has_unsupported_script_trace(close_dispatch));
  assert(has_trace(close_dispatch, "close"));

  static_cast<void>(
      runtime.route_logic_command(npc_command(mir2::LogicCommandKind::merchant_select, "@break")));
  const auto break_dispatch = runtime.tick(2004);
  assert(!has_unsupported_script_trace(break_dispatch));
  assert(has_trace(break_dispatch, "break"));

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
