#include <optional>
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

bool has_notice_text(const mir2::RuntimeDispatch& dispatch, std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmHear) {
      continue;
    }
    if (mir2::legacy_decode_string(decoded->body).find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
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

mir2::LogicCommand make_enter_command(std::uint64_t session_id, std::string account_id,
                                      std::string character_name, std::int32_t x) {
  mir2::CharacterRecord character;
  character.account_id = account_id;
  character.character_name = character_name;
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.mp = 15;
  character.ability.max_hp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  character.gold = 100000;

  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = std::move(account_id);
  command.character_name = character.character_name;
  command.map_id = "0";
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  return command;
}

mir2::LogicCommand make_say_command(std::uint64_t session_id, std::string text) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.castle_name = "Sabuk Prime";
  config.runtime.default_castle_war_date = "Every Friday 20:00";
  config.runtime.no_active_wars_text = "No sieges registered.";
  config.runtime.unclaimed_castle_owner = "Vacant Crown";
  config.runtime.unclaimed_castle_lord = "No Regent";
  config.runtime.castle_owner_role_label = "Sabuk Sovereign";
  config.runtime.castle_claim_summary_template =
      "Claim posted: <$GUILD> now watches over <$CASTLE>.";
  config.runtime.castle_war_summary_template =
      "War docket: <$TARGETGUILD> entered <$CASTLE> for <$GOLD> gold.";
  config.runtime.guild_create_summary_template = "Guild charter sealed for <$GUILD>.";
  config.runtime.guild_apply_summary_template = "Join petition sent to <$GUILD>.";
  config.runtime.guild_withdraw_summary_template = "Join petition withdrawn from <$GUILD>.";
  config.runtime.guild_approve_summary_template = "Clearance granted to <$TARGET>.";
  config.runtime.guild_reject_summary_template = "Clearance denied for <$TARGET>.";
  config.runtime.guild_kick_summary_template = "Removal order: <$TARGET>.";
  config.runtime.guild_title_summary_template = "Rank writ: <$TARGET> = <$TITLE>.";
  config.runtime.guild_transfer_summary_template = "Crown passed to <$TARGET>.";
  config.runtime.guild_leave_summary_template = "Exit logged from <$GUILD>.";
  config.runtime.guild_leave_transfer_summary_template =
      "Exit logged from <$GUILD>; successor <$NEWLORD>.";
  config.runtime.guild_disband_summary_template = "<$GUILD> dissolved by last departure.";
  config.runtime.guild_membership_cleared_summary_template = "Guild record cleared.";
  config.runtime.guild_apply_alert_template = "Guild desk: <$TARGET> seeks <$GUILD>.";
  config.runtime.guild_withdraw_alert_template =
      "Guild desk: <$TARGET> withdrew from <$GUILD> queue.";
  config.runtime.guild_approved_notice_template = "Approval note: <$GUILD> accepted you.";
  config.runtime.guild_rejected_notice_template = "Rejection note: <$GUILD> declined you.";
  config.runtime.guild_removed_notice_template = "Removal notice: <$GUILD> dismissed you.";
  config.runtime.guild_new_lord_notice_template = "Coronation note: you now lead <$GUILD>.";
  config.runtime.guild_title_changed_notice_template = "Title note: <$TITLE>.";
  config.runtime.guild_create_choose_name_template =
      "Charter clerk: submit a guild name first.";
  config.runtime.guild_not_found_template = "Guild desk: no charter for <$GUILD>.";
  config.runtime.guild_apply_already_pending_template =
      "Guild desk: petition already pending with <$GUILD>.";
  config.runtime.castle_war_usage_template =
      "War office: choose a rival guild before filing the siege.";
  config.runtime.castle_war_target_missing_template =
      "War office: no guild record for <$TARGETGUILD>.";
  config.runtime.guild_war_fee = 45000;
  config.runtime.upgrade_weapon_fee = 15000;
  config.runtime.guild_create_fee = 12000;
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.castle_dialog.owner_guild = "Unclaimed";
  snapshot.castle_dialog.lord = "Unclaimed";
  snapshot.guilds.push_back(
      mir2::GuildState{"PhoenixHall", "Percival", {"Percival", "Lancelot"}});
  runtime.set_guild_castle_snapshot(snapshot);

  static_cast<void>(runtime.route_logic_command(make_enter_command(12, "guest", "Hero", 10)));
  static_cast<void>(runtime.route_logic_command(make_enter_command(13, "guest", "Ally", 11)));
  static_cast<void>(runtime.route_logic_command(make_enter_command(14, "guest", "Visitor", 12)));
  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(14, "@guild create")));
  const auto create_usage_dispatch = runtime.tick();
  if (!has_notice_text(create_usage_dispatch, "Charter clerk: submit a guild name first.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(14, "@guild join NoGuild")));
  const auto join_missing_dispatch = runtime.tick();
  if (!has_notice_text(join_missing_dispatch, "Guild desk: no charter for NoGuild.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@castle show")));
  const auto castle_show_dispatch = runtime.tick();
  if (!has_notice_text(castle_show_dispatch, "Castle=Sabuk Prime") ||
      !has_notice_text(castle_show_dispatch, "Owner=Vacant Crown") ||
      !has_notice_text(castle_show_dispatch, "Lord=No Regent") ||
      !has_notice_text(castle_show_dispatch, "WarDate=Every Friday 20:00") ||
      !has_notice_text(castle_show_dispatch, "WarPreview=No sieges registered.") ||
      !has_notice_text(castle_show_dispatch, "Fees=45000/15000")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild create DragonSlayers")));
  const auto create_dispatch = runtime.tick();
  const auto* create_guild_request =
      find_persist_request(create_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* create_character_request =
      find_persist_request(create_dispatch, mir2::PersistRequestKind::save_character);
  if (create_guild_request == nullptr || create_character_request == nullptr ||
      create_guild_request->guild_state.guild_name != "DragonSlayers" ||
      create_guild_request->guild_state.lord != "Hero" ||
      create_guild_request->guild_state.members.size() != 1 ||
      create_character_request->character.guild_name != "DragonSlayers" ||
      create_character_request->character.guild_title != "Lord" ||
      create_character_request->character.gold != 88000 ||
      !has_notice_text(create_dispatch, "Guild charter sealed for DragonSlayers.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@guild join DragonSlayers")));
  const auto join_dispatch = runtime.tick();
  const auto* join_guild_request =
      find_persist_request(join_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* join_character_request =
      find_persist_request(join_dispatch, mir2::PersistRequestKind::save_character);
  if (join_guild_request == nullptr || join_character_request != nullptr ||
      join_guild_request->guild_state.members.size() != 1 ||
      join_guild_request->guild_state.applicants.size() != 1 ||
      join_guild_request->guild_state.applicants.front() != "Ally" ||
      !has_notice_text(join_dispatch, "Join petition sent to DragonSlayers.") ||
      !has_notice_text(join_dispatch, "Guild desk: Ally seeks DragonSlayers.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@guild join DragonSlayers")));
  const auto duplicate_join_dispatch = runtime.tick();
  if (!has_notice_text(duplicate_join_dispatch,
                       "Guild desk: petition already pending with DragonSlayers.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild applicants")));
  const auto applicants_dispatch = runtime.tick();
  if (!has_notice_text(applicants_dispatch, "GuildApplicants=Ally")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild approve Ally")));
  const auto approve_dispatch = runtime.tick();
  const auto* approve_guild_request =
      find_persist_request(approve_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* approve_character_request =
      find_persist_request(approve_dispatch, mir2::PersistRequestKind::save_character);
  if (approve_guild_request == nullptr || approve_character_request == nullptr ||
      approve_guild_request->guild_state.members.size() != 2 ||
      !approve_guild_request->guild_state.applicants.empty() ||
      approve_character_request->character.character_name != "Ally" ||
      approve_character_request->character.guild_name != "DragonSlayers" ||
      approve_character_request->character.guild_title != "Member" ||
      !has_notice_text(approve_dispatch, "Clearance granted to Ally.") ||
      !has_notice_text(approve_dispatch, "Approval note: DragonSlayers accepted you.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(14, "@guild join DragonSlayers")));
  const auto visitor_apply_dispatch = runtime.tick();
  const auto* visitor_apply_request =
      find_persist_request(visitor_apply_dispatch, mir2::PersistRequestKind::save_guild_state);
  if (visitor_apply_request == nullptr ||
      visitor_apply_request->guild_state.applicants.size() != 1 ||
      visitor_apply_request->guild_state.applicants.front() != "Visitor") {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild reject Visitor")));
  const auto reject_dispatch = runtime.tick();
  const auto* reject_guild_request =
      find_persist_request(reject_dispatch, mir2::PersistRequestKind::save_guild_state);
  if (reject_guild_request == nullptr || !reject_guild_request->guild_state.applicants.empty() ||
      !has_notice_text(reject_dispatch, "Clearance denied for Visitor.") ||
      !has_notice_text(reject_dispatch, "Rejection note: DragonSlayers declined you.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild info")));
  const auto info_dispatch = runtime.tick();
  if (!has_notice_text(info_dispatch, "Guild=DragonSlayers") ||
      !has_notice_text(info_dispatch, "Role=Lord") ||
      !has_notice_text(info_dispatch, "Lord=Hero") ||
      !has_notice_text(info_dispatch, "CastleRole=Rival") ||
      !has_notice_text(info_dispatch, "Members/Applicants=2/0") ||
      !has_notice_text(info_dispatch, "Preview=Hero, Ally") ||
      !has_notice_text(info_dispatch, "ApplicantPreview=None")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild title Ally Vanguard")));
  const auto title_dispatch = runtime.tick();
  const auto* title_character_request =
      find_persist_request(title_dispatch, mir2::PersistRequestKind::save_character);
  if (title_character_request == nullptr ||
      title_character_request->character.character_name != "Ally" ||
      title_character_request->character.guild_title != "Vanguard" ||
      !has_notice_text(title_dispatch, "Rank writ: Ally = Vanguard.") ||
      !has_notice_text(title_dispatch, "Title note: Vanguard.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(12, "@guild transfer Ally")));
  const auto transfer_dispatch = runtime.tick();
  const auto* transfer_guild_request =
      find_persist_request(transfer_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* transfer_character_request =
      find_persist_request(transfer_dispatch, mir2::PersistRequestKind::save_character);
  if (transfer_guild_request == nullptr || transfer_character_request == nullptr ||
      transfer_guild_request->guild_state.lord != "Ally" ||
      transfer_character_request->character.character_name != "Hero" ||
      transfer_character_request->character.guild_title != "Member" ||
      !has_notice_text(transfer_dispatch, "Crown passed to Ally.") ||
      !has_notice_text(transfer_dispatch, "Coronation note: you now lead DragonSlayers.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@guild info")));
  const auto ally_info_dispatch = runtime.tick();
  if (!has_notice_text(ally_info_dispatch, "Guild=DragonSlayers") ||
      !has_notice_text(ally_info_dispatch, "Role=Lord") ||
      !has_notice_text(ally_info_dispatch, "Lord=Ally") ||
      !has_notice_text(ally_info_dispatch, "CastleRole=Rival") ||
      !has_notice_text(ally_info_dispatch, "Members/Applicants=2/0") ||
      !has_notice_text(ally_info_dispatch, "Preview=Hero, Ally") ||
      !has_notice_text(ally_info_dispatch, "ApplicantPreview=None")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@castle claim")));
  const auto claim_dispatch = runtime.tick();
  const auto* claim_request =
      find_persist_request(claim_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (claim_request == nullptr ||
      claim_request->payload_json.find("\"owner_guild\":\"DragonSlayers\"") == std::string::npos ||
      !has_notice_text(claim_dispatch, "Claim posted: DragonSlayers now watches over Sabuk Prime.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@castle war")));
  const auto war_usage_dispatch = runtime.tick();
  if (!has_notice_text(war_usage_dispatch,
                       "War office: choose a rival guild before filing the siege.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@castle war GhostGuild")));
  const auto war_missing_dispatch = runtime.tick();
  if (!has_notice_text(war_missing_dispatch, "War office: no guild record for GhostGuild.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@castle show")));
  const auto claimed_castle_show_dispatch = runtime.tick();
  if (!has_notice_text(claimed_castle_show_dispatch, "Owner=DragonSlayers") ||
      !has_notice_text(claimed_castle_show_dispatch, "Lord=Ally") ||
      !has_notice_text(claimed_castle_show_dispatch, "OwnerRole=Sabuk Sovereign")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@castle war PhoenixHall")));
  const auto war_dispatch = runtime.tick();
  const auto* war_request =
      find_persist_request(war_dispatch, mir2::PersistRequestKind::save_castle_state);
  const auto* war_character_request =
      find_persist_request(war_dispatch, mir2::PersistRequestKind::save_character);
  if (war_request == nullptr || war_character_request == nullptr ||
      war_request->payload_json.find("\"list_of_war\":\"PhoenixHall\"") == std::string::npos ||
      war_character_request->character.character_name != "Ally" ||
      war_character_request->character.gold != 55000 ||
      !has_notice_text(war_dispatch,
                       "War docket: PhoenixHall entered Sabuk Prime for 45000 gold.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@guild kick Hero")));
  const auto kick_dispatch = runtime.tick();
  const auto* kick_guild_request =
      find_persist_request(kick_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* kick_character_request =
      find_persist_request(kick_dispatch, mir2::PersistRequestKind::save_character);
  if (kick_guild_request == nullptr || kick_character_request == nullptr ||
      kick_guild_request->guild_state.lord != "Ally" ||
      kick_guild_request->guild_state.members.size() != 1 ||
      kick_guild_request->guild_state.members.front() != "Ally" ||
      kick_character_request->character.character_name != "Hero" ||
      !kick_character_request->character.guild_name.empty() ||
      !kick_character_request->character.guild_title.empty() ||
      !has_notice_text(kick_dispatch, "Removal order: Hero.") ||
      !has_notice_text(kick_dispatch, "Removal notice: DragonSlayers dismissed you.")) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_say_command(13, "@guild leave")));
  const auto disband_dispatch = runtime.tick();
  const auto* delete_guild_request =
      find_persist_request(disband_dispatch, mir2::PersistRequestKind::delete_guild);
  const auto* leave_member_character_request =
      find_persist_request(disband_dispatch, mir2::PersistRequestKind::save_character);
  const auto* unclaim_castle_request =
      find_persist_request(disband_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (delete_guild_request == nullptr || leave_member_character_request == nullptr ||
      unclaim_castle_request == nullptr ||
      delete_guild_request->guild_name != "DragonSlayers" ||
      !leave_member_character_request->character.guild_name.empty() ||
      !leave_member_character_request->character.guild_title.empty() ||
      unclaim_castle_request->payload_json.find("\"owner_guild\":\"\"") ==
          std::string::npos ||
      unclaim_castle_request->payload_json.find("\"list_of_war\":\"\"") ==
          std::string::npos ||
      !has_notice_text(disband_dispatch, "DragonSlayers dissolved by last departure.")) {
    return 1;
  }

  return 0;
}
