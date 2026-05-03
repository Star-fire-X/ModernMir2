#include <cassert>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::CharacterRecord make_character() {
  mir2::CharacterRecord character;
  character.account_id = "acct";
  character.character_name = "Hero";
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.max_weight = 100;
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

bool has_trace(const mir2::RuntimeDispatch& dispatch, std::string_view action,
               std::string_view label) {
  for (const auto& trace : dispatch.legacy_traces) {
    if (trace.stage == "LegacyScript" && trace.action == action && trace.label == label) {
      return true;
    }
  }
  return false;
}

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                               std::string action = {}) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = 1;
  command.text = std::move(action);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "ScriptMap", {}, 0, 0, 20, 20});

  mir2::NpcConfig npc;
  npc.id = "sage";
  npc.map_id = "0";
  npc.name = "Old Sage";
  npc.x = 11;
  npc.y = 10;
  npc.service = "none";
  npc.dialog_sections.push_back({"@main", "Welcome, <$USERNAME>\\<About/@about>\\<Exit/@exit>"});
  npc.dialog_sections.push_back({"@about", "About section, <$USERNAME>\\<Back/@main>"});
  config.npcs.push_back(npc);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.enqueue_ready_user(make_ready(12)));

  const auto notice_dispatch = runtime.tick(1000);
  assert(!find_packet(notice_dispatch, mir2::kSmMerchantSay).has_value());
  assert(runtime.legacy_session_state(12) == mir2::LegacyPlayerState::initialize_pending);

  const auto click_enqueue = runtime.route_logic_command(npc_command(mir2::LogicCommandKind::click_npc, 12));
  assert(click_enqueue.session_events.empty());
  assert(runtime.legacy_session_inbox_size(12) == 1);

  const auto initialize_dispatch = runtime.tick(1251);
  assert(!find_packet(initialize_dispatch, mir2::kSmMerchantSay).has_value());
  assert(runtime.legacy_session_state(12) == mir2::LegacyPlayerState::running);
  assert(runtime.legacy_session_inbox_size(12) == 1);

  const auto main_dispatch = runtime.tick(1502);
  const auto main_packet = find_packet(main_dispatch, mir2::kSmMerchantSay);
  assert(main_packet.has_value());
  const auto main_text = mir2::legacy_decode_string(main_packet->body);
  assert(main_text.find("Old Sage/Welcome, Hero\\") == 0);
  assert(main_text.find("<About/@about>") != std::string::npos);
  assert(runtime.legacy_session_inbox_size(12) == 0);

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 12, "@about")));
  const auto about_dispatch = runtime.tick(1753);
  const auto about_packet = find_packet(about_dispatch, mir2::kSmMerchantSay);
  assert(about_packet.has_value());
  const auto about_text = mir2::legacy_decode_string(about_packet->body);
  assert(about_text.find("Old Sage/About section, Hero\\") == 0);

  static_cast<void>(runtime.route_logic_command(
      npc_command(mir2::LogicCommandKind::merchant_select, 12, "@missing")));
  const auto missing_dispatch = runtime.tick(2004);
  assert(!find_packet(missing_dispatch, mir2::kSmMerchantSay).has_value());
  assert(has_trace(missing_dispatch, "missing_section", "@missing"));

  return 0;
}
