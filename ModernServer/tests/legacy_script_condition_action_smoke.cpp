#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LegacyUserItem make_apple(std::int32_t make_index) {
  mir2::LegacyUserItem item;
  item.index = 1;
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
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.max_weight = 100;
  character.bag_items[0] = make_apple(101);
  return character;
}

mir2::LegacyReadyUser make_ready(std::uint64_t session_id) {
  auto character = make_character();
  mir2::LegacyReadyUser ready;
  ready.session_id = session_id;
  ready.account_id = character.account_id;
  ready.character_name = character.character_name;
  ready.map_id = character.map_id;
  ready.x = character.x;
  ready.y = character.y;
  ready.character = std::move(character);
  return ready;
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
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyScript" && trace.action == action) {
      return true;
    }
  }
  return false;
}

std::string merchant_say_text(const mir2::RuntimeDispatch& dispatch) {
  const auto packet = find_packet(dispatch, mir2::kSmMerchantSay);
  assert(packet.has_value());
  return mir2::legacy_decode_string(packet->body);
}

mir2::LogicCommand click_npc(std::uint64_t session_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = session_id;
  command.target_actor_id = 1;
  return command;
}

mir2::LogicCommand select_npc(std::uint64_t session_id, std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = 1;
  command.text = std::move(action);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 1;
  config.maps.push_back(mir2::MapConfig{"0", "ScriptMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Apple", 1, 10, 0, 0, 1, 1000, -1, 0, 0});
  config.items.push_back(mir2::ItemConfig{2, "Anvil", 200, 10, 0, 0, 2, 1000, -1, 0, 0});

  mir2::NpcConfig npc;
  npc.id = "rewarder";
  npc.map_id = "0";
  npc.name = "Rewarder";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back(
      {"@main",
       "#IF\\CHECKLEVEL 10\\CHECKGOLD 50\\CHECKITEM Apple 1\\RANDOM 10\\#ACT\\"
       "#TAKE Gold 50\\#TAKE Apple 1\\#GIVE Gold 75\\#GIVE Apple 1\\#SAY Reward\\"
       "#ELSESAY Failed"});
  npc.dialog_sections.push_back(
      {"@fail",
       "#IF\\CHECKLEVEL 99\\#ACT\\#SAY ShouldNotShow\\#ELSESAY Failed"});
  npc.dialog_sections.push_back(
      {"@bagok", "#IF\\CHECKBAGGAGE Apple\\#SAY BagOk\\#ELSESAY BagFail"});
  npc.dialog_sections.push_back(
      {"@bagfail", "#IF\\CHECKBAGGAGE Anvil\\#SAY BagOk\\#ELSESAY BagFail"});
  npc.dialog_sections.push_back(
      {"@daily", "#IF\\IFGETDAILYQUEST\\#SAY DailyFree\\#ELSESAY DailyBusy"});
  npc.dialog_sections.push_back(
      {"@setdaily", "#IF\\CHECKLEVEL 1\\#ACT\\SETDAILYQUEST 2\\GOTO @daily"});
  npc.dialog_sections.push_back(
      {"@dura", "#IF\\CHECKDURAEVA Apple 1\\#SAY DuraOk\\#ELSESAY DuraFail"});
  npc.dialog_sections.push_back(
      {"@elesact",
       "#IF\\CHECKLEVEL 99\\#ACT\\#SAY ShouldNotShow\\#ELESACT\\SAY ElesActOk"});
  config.npcs.push_back(npc);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(12)));
  static_cast<void>(runtime.tick(1000));
  static_cast<void>(runtime.tick(1251));

  static_cast<void>(runtime.route_logic_command(click_npc(12)));
  const auto dispatch = runtime.tick(1502);
  const auto packet = find_packet(dispatch, mir2::kSmMerchantSay);
  assert(packet.has_value());
  const auto text = mir2::legacy_decode_string(packet->body);
  assert(text.find("Rewarder/Reward") == 0);
  assert(has_trace(dispatch, "RANDOM"));
  assert(has_trace(dispatch, "take_gold"));
  assert(has_trace(dispatch, "give_item"));

  const auto snapshot = runtime.snapshot_character_actor("Hero");
  assert(snapshot.has_value());
  assert(snapshot->gold == 125);
  assert(snapshot->bag_items[0].index == 1 || snapshot->bag_items[1].index == 1);
  assert(runtime.legacy_random_state() != 1);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@fail")));
  const auto failed_dispatch = runtime.tick(1753);
  const auto failed_text = merchant_say_text(failed_dispatch);
  assert(failed_text.find("Rewarder/Failed") == 0);
  assert(failed_text.find("ShouldNotShow") == std::string::npos);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@bagok")));
  assert(merchant_say_text(runtime.tick(2004)).find("Rewarder/BagOk") == 0);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@bagfail")));
  assert(merchant_say_text(runtime.tick(2255)).find("Rewarder/BagFail") == 0);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@daily")));
  assert(merchant_say_text(runtime.tick(2506)).find("Rewarder/DailyFree") == 0);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@setdaily")));
  assert(merchant_say_text(runtime.tick(2757)).find("Rewarder/DailyBusy") == 0);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@dura")));
  assert(merchant_say_text(runtime.tick(3008)).find("Rewarder/DuraOk") == 0);

  static_cast<void>(runtime.route_logic_command(select_npc(12, "@elesact")));
  assert(merchant_say_text(runtime.tick(3259)).find("Rewarder/ElesActOk") == 0);

  return 0;
}
