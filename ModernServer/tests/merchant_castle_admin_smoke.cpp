#include <optional>
#include <iostream>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

const mir2::PersistRequest* find_persist_request(const mir2::RuntimeDispatch& dispatch,
                                                 mir2::PersistRequestKind kind) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == kind) {
      return &request;
    }
  }
  return nullptr;
}

std::string decode_message_text(std::string_view body) { return mir2::legacy_decode_string(body); }

int fail(const char* stage) {
  std::cerr << "merchant_castle_admin_smoke failed at " << stage << '\n';
  return 1;
}

mir2::LogicCommand make_menu_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                     std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.text = std::move(action);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});
  config.npcs.push_back(
      mir2::NpcConfig{"castle_steward",
                      "0",
                      "Castle Steward",
                      11,
                      10,
                      "",
                      "none",
                      {},
                      {mir2::NpcDialogSectionConfig{
                          "@main",
                          "Castle Steward\\"
                          "<Show/@castle_show>\\"
                          "<Set Lord/@guild_lord DragonSlayers Gawain>\\"
                          "<Set Owner/@castle_owner PhoenixHall>\\"
                          "<Set War Date/@castle_wardate 2026-05-03 19:45>\\"
                          "<Set Rivals/@castle_wars AzureSky, WhiteTiger>\\"
                          "<Set Fees/@castle_fees 53000 17500>\\"
                          "<Leave/@exit>"}}});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.castle_dialog.castle_name = "Sabuk";
  snapshot.castle_dialog.owner_guild = "DragonSlayers";
  snapshot.castle_dialog.lord = "Arthur";
  snapshot.castle_dialog.castle_war_date = "2026-05-01 20:00";
  snapshot.castle_dialog.list_of_war = "WolfPack";
  snapshot.castle_dialog.guild_war_fee = 45000;
  snapshot.castle_dialog.upgrade_weapon_fee = 15000;
  snapshot.guilds.push_back(mir2::GuildState{"DragonSlayers", "Arthur", {"Arthur", "Gawain"}});
  snapshot.guilds.push_back(mir2::GuildState{"PhoenixHall", "Percival", {"Percival"}});
  runtime.set_guild_castle_snapshot(snapshot);

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 12;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return fail("login new map");
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@castle_show")));
  const auto show_dispatch = runtime.tick();
  const auto show_packet = find_packet(show_dispatch, mir2::kSmHear);
  if (!show_packet.has_value() ||
      decode_message_text(show_packet->body).find("Castle=Sabuk Owner=DragonSlayers Lord=Arthur") ==
          std::string::npos ||
      !show_dispatch.persist_requests.empty()) {
    return fail("castle show");
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(12, 1, "@guild_lord DragonSlayers Gawain")));
  const auto guild_dispatch = runtime.tick();
  const auto* guild_request =
      find_persist_request(guild_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto guild_notice = find_packet(guild_dispatch, mir2::kSmHear);
  if (guild_request == nullptr || guild_request->reply_to != "world_service" ||
      guild_request->guild_state.guild_name != "DragonSlayers" ||
      guild_request->guild_state.lord != "Gawain" ||
      guild_request->guild_state.members.size() != 2 ||
      !guild_notice.has_value() ||
      decode_message_text(guild_notice->body).find("Guild lord update queued.") == std::string::npos) {
    return fail("guild lord");
  }
  snapshot.guilds[0].lord = "Gawain";
  snapshot.castle_dialog.lord = "Gawain";
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(12, 1, "@castle_owner PhoenixHall")));
  const auto owner_dispatch = runtime.tick();
  const auto* owner_request =
      find_persist_request(owner_dispatch, mir2::PersistRequestKind::save_castle_state);
  const auto owner_notice = find_packet(owner_dispatch, mir2::kSmHear);
  if (owner_request == nullptr || owner_request->reply_to != "world_service" ||
      owner_request->castle_name != "Sabuk" ||
      owner_request->payload_json.find("\"owner_guild\":\"PhoenixHall\"") == std::string::npos ||
      !owner_notice.has_value() ||
      decode_message_text(owner_notice->body).find("Castle owner update queued.") ==
          std::string::npos) {
    return fail("castle owner");
  }
  snapshot.castle_dialog.owner_guild = "PhoenixHall";
  snapshot.castle_dialog.lord = "Percival";
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(12, 1, "@castle_wardate 2026-05-03 19:45")));
  const auto war_date_dispatch = runtime.tick();
  const auto* war_date_request =
      find_persist_request(war_date_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (war_date_request == nullptr ||
      war_date_request->payload_json.find("\"castle_war_date\":\"2026-05-03 19:45\"") ==
          std::string::npos) {
    return fail("war date");
  }
  snapshot.castle_dialog.castle_war_date = "2026-05-03 19:45";
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(12, 1, "@castle_wars AzureSky, WhiteTiger")));
  const auto wars_dispatch = runtime.tick();
  const auto* wars_request =
      find_persist_request(wars_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (wars_request == nullptr ||
      wars_request->payload_json.find("\"list_of_war\":\"AzureSky, WhiteTiger\"") ==
          std::string::npos) {
    return fail("wars");
  }
  snapshot.castle_dialog.list_of_war = "AzureSky, WhiteTiger";
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(12, 1, "@castle_fees 53000 17500")));
  const auto fees_dispatch = runtime.tick();
  const auto* fees_request =
      find_persist_request(fees_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (fees_request == nullptr || fees_request->payload_json.find("\"guild_war_fee\":53000") ==
                                     std::string::npos ||
      fees_request->payload_json.find("\"upgrade_weapon_fee\":17500") == std::string::npos) {
    return fail("fees");
  }
  snapshot.castle_dialog.guild_war_fee = 53000;
  snapshot.castle_dialog.upgrade_weapon_fee = 17500;
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@castle_show")));
  const auto show_updated_dispatch = runtime.tick();
  const auto updated_show_packet = find_packet(show_updated_dispatch, mir2::kSmHear);
  if (!updated_show_packet.has_value()) {
    return fail("updated show packet");
  }
  const auto updated_text = decode_message_text(updated_show_packet->body);
  if (updated_text.find("Castle=Sabuk Owner=PhoenixHall Lord=Percival") == std::string::npos ||
      updated_text.find("WarDate=2026-05-03 19:45") == std::string::npos ||
      updated_text.find("WarPreview=AzureSky, WhiteTiger") == std::string::npos ||
      updated_text.find("Fees=53000/17500") == std::string::npos) {
    return fail("updated show text");
  }

  return 0;
}
