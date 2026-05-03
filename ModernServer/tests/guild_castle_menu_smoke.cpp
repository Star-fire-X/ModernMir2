#include <optional>
#include <string>
#include <iostream>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

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

const mir2::PersistRequest* find_persist_request(const mir2::RuntimeDispatch& dispatch,
                                                 mir2::PersistRequestKind kind) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == kind) {
      return &request;
    }
  }
  return nullptr;
}

const mir2::PersistRequest* find_character_save(const mir2::RuntimeDispatch& dispatch,
                                                std::string_view character_name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character.character_name == character_name) {
      return &request;
    }
  }
  return nullptr;
}

std::string decode_dialog_text(std::string_view body) { return mir2::legacy_decode_string(body); }

mir2::LogicCommand make_menu_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                     std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand make_click_npc_command(std::uint64_t session_id, std::uint64_t merchant_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  return command;
}

int fail(int code) {
  std::cerr << "fail_" << code << '\n';
  return code;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.runtime.guild_war_fee = 32000;
  config.runtime.upgrade_weapon_fee = 11000;
  config.runtime.guild_create_fee = 12000;
  config.runtime.castle_owner_role_label = "Sabuk Sovereign";
  config.runtime.castle_owner_guild_role_label = "Steward";
  config.runtime.castle_challenger_role_label = "Siege Challenger";
  config.runtime.castle_rival_role_label = "Watching Rival";
  config.runtime.castle_unknown_role_label = "No Guild Record";
  config.runtime.castle_war_entry_listed_label = "Enrolled";
  config.runtime.castle_war_entry_unlisted_label = "Not Enrolled";
  config.runtime.castle_war_status_active_label = "Marching";
  config.runtime.castle_war_status_available_label = "Open";
  config.runtime.castle_role_change_owner_label = "Crowned Steward";
  config.runtime.castle_role_change_challenger_label = "Siege Challenger Mark";
  config.runtime.castle_claim_summary_template =
      "Steward report: <$GUILD> now governs <$CASTLE>.";
  config.runtime.castle_war_summary_template =
      "Siege bulletin: <$TARGETGUILD> registered at <$CASTLE> for <$GOLD> gold.";
  config.runtime.guild_create_summary_template = "Banner raised for <$GUILD>.";
  config.runtime.guild_apply_summary_template = "Petition filed with <$GUILD>.";
  config.runtime.guild_withdraw_summary_template = "Petition withdrawn from <$GUILD>.";
  config.runtime.guild_approve_summary_template = "Applicant <$TARGET> welcomed aboard.";
  config.runtime.guild_reject_summary_template = "Applicant <$TARGET> turned away.";
  config.runtime.guild_kick_summary_template = "Roster cut: <$TARGET>.";
  config.runtime.guild_title_summary_template = "Title order: <$TARGET> -> <$TITLE>.";
  config.runtime.guild_transfer_summary_template = "Leadership baton passed to <$TARGET>.";
  config.runtime.guild_leave_summary_template = "Departure logged from <$GUILD>.";
  config.runtime.guild_leave_transfer_summary_template =
      "Departure logged from <$GUILD>; successor <$NEWLORD>.";
  config.runtime.guild_disband_summary_template = "Guild <$GUILD> stands dissolved.";
  config.runtime.guild_membership_cleared_summary_template = "Membership record cleared.";
  config.runtime.guild_create_choose_name_template = "Charter filing needs a guild name.";
  config.runtime.guild_create_name_unavailable_template =
      "Charter name collision for <$GUILD>.";
  config.runtime.guild_create_need_gold_template =
      "Treasury shortfall: <$GUILD> needs <$GOLD> gold.";
  config.runtime.guild_apply_choose_guild_template = "Select a guild before filing a petition.";
  config.runtime.guild_not_found_template = "No guild ledger for <$GUILD>.";
  config.runtime.guild_apply_already_pending_template =
      "Petition already pending with <$GUILD>.";
  config.runtime.castle_war_usage_template = "Choose a guild target before filing the siege.";
  config.runtime.castle_war_target_missing_template =
      "No siege dossier for <$TARGETGUILD>.";
  config.maps.push_back(mir2::MapConfig{"0", "CastleMap", {}, 0, 0, 10, 10});
  config.npcs.push_back(
      mir2::NpcConfig{"guild_steward", "0", "Guild Steward", 11, 10, "", "guild_castle"});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  mir2::GuildCastleSnapshot snapshot;
  snapshot.castle_dialog.castle_name = "Sabuk";
  snapshot.castle_dialog.owner_guild = "PhoenixHall";
  snapshot.castle_dialog.lord = "Percival";
  snapshot.castle_dialog.castle_war_date = "2026-05-01 20:00";
  snapshot.castle_dialog.list_of_war =
      "WolfPack, AzureSky, WhiteTiger, IronFist, Stormborn, MoonShade, RedBanner";
  snapshot.guilds.push_back(mir2::GuildState{"DragonSlayers",
                                             "Hero",
                                             {"Hero", "Ally", "Bran", "Cid", "Doran", "Ector",
                                              "Faye"},
                                             {"Visitor", "Nia", "Oren", "Piper", "Quinn", "Rowan",
                                              "Sage"}});
  snapshot.guilds.push_back(mir2::GuildState{"PhoenixHall", "Percival", {"Percival"}, {}});
  snapshot.guilds.push_back(mir2::GuildState{"AzureSky", "Aeron", {"Aeron"}, {}});
  snapshot.guilds.push_back(mir2::GuildState{"WhiteTiger", "Borin", {"Borin"}, {}});
  snapshot.guilds.push_back(mir2::GuildState{"IronFist", "Cora", {"Cora"}, {}});
  snapshot.guilds.push_back(mir2::GuildState{"Stormborn", "Dain", {"Dain"}, {}});
  snapshot.guilds.push_back(mir2::GuildState{"MoonShade", "Eira", {"Eira"}, {"Nyx"}});
  snapshot.guilds.push_back(mir2::GuildState{"RedBanner", "Flint", {"Flint"}, {}});
  runtime.set_guild_castle_snapshot(snapshot);

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.guild_name = "DragonSlayers";
  hero.guild_title = "Lord";
  hero.gold = 100000;
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;

  mir2::CharacterRecord visitor;
  visitor.account_id = "guest";
  visitor.character_name = "Visitor";
  visitor.map_id = "0";
  visitor.x = 10;
  visitor.y = 11;

  mir2::CharacterRecord ally;
  ally.account_id = "guest";
  ally.character_name = "Ally";
  ally.guild_name = "DragonSlayers";
  ally.guild_title = "Member";
  ally.map_id = "0";
  ally.x = 9;
  ally.y = 10;

  mir2::CharacterRecord bran;
  bran.account_id = "guest";
  bran.character_name = "Bran";
  bran.guild_name = "DragonSlayers";
  bran.guild_title = "Member";
  bran.map_id = "0";
  bran.x = 8;
  bran.y = 10;

  mir2::CharacterRecord founder;
  founder.account_id = "guest";
  founder.character_name = "Founder";
  founder.gold = 50000;
  founder.map_id = "0";
  founder.x = 12;
  founder.y = 10;

  mir2::LogicCommand enter_hero;
  enter_hero.kind = mir2::LogicCommandKind::enter_world;
  enter_hero.session_id = 21;
  enter_hero.account_id = hero.account_id;
  enter_hero.character_name = hero.character_name;
  enter_hero.map_id = hero.map_id;
  enter_hero.x = hero.x;
  enter_hero.y = hero.y;
  enter_hero.character = hero;
  static_cast<void>(runtime.route_logic_command(enter_hero));

  mir2::LogicCommand enter_visitor;
  enter_visitor.kind = mir2::LogicCommandKind::enter_world;
  enter_visitor.session_id = 22;
  enter_visitor.account_id = visitor.account_id;
  enter_visitor.character_name = visitor.character_name;
  enter_visitor.map_id = visitor.map_id;
  enter_visitor.x = visitor.x;
  enter_visitor.y = visitor.y;
  enter_visitor.character = visitor;
  static_cast<void>(runtime.route_logic_command(enter_visitor));

  mir2::LogicCommand enter_ally;
  enter_ally.kind = mir2::LogicCommandKind::enter_world;
  enter_ally.session_id = 23;
  enter_ally.account_id = ally.account_id;
  enter_ally.character_name = ally.character_name;
  enter_ally.map_id = ally.map_id;
  enter_ally.x = ally.x;
  enter_ally.y = ally.y;
  enter_ally.character = ally;
  static_cast<void>(runtime.route_logic_command(enter_ally));

  mir2::LogicCommand enter_bran;
  enter_bran.kind = mir2::LogicCommandKind::enter_world;
  enter_bran.session_id = 24;
  enter_bran.account_id = bran.account_id;
  enter_bran.character_name = bran.character_name;
  enter_bran.map_id = bran.map_id;
  enter_bran.x = bran.x;
  enter_bran.y = bran.y;
  enter_bran.character = bran;
  static_cast<void>(runtime.route_logic_command(enter_bran));

  mir2::LogicCommand enter_founder;
  enter_founder.kind = mir2::LogicCommandKind::enter_world;
  enter_founder.session_id = 25;
  enter_founder.account_id = founder.account_id;
  enter_founder.character_name = founder.character_name;
  enter_founder.map_id = founder.map_id;
  enter_founder.x = founder.x;
  enter_founder.y = founder.y;
  enter_founder.character = founder;
  static_cast<void>(runtime.route_logic_command(enter_founder));

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, 21, mir2::kSmNewMap).has_value() ||
      !find_packet(login_dispatch, 22, mir2::kSmNewMap).has_value() ||
      !find_packet(login_dispatch, 23, mir2::kSmNewMap).has_value() ||
      !find_packet(login_dispatch, 24, mir2::kSmNewMap).has_value() ||
      !find_packet(login_dispatch, 25, mir2::kSmNewMap).has_value()) {
    return fail(1);
  }

  static_cast<void>(runtime.route_logic_command(make_click_npc_command(21, 1)));
  const auto hero_menu_dispatch = runtime.tick();
  const auto hero_menu_packet = find_packet(hero_menu_dispatch, 21, mir2::kSmMerchantSay);
  if (!hero_menu_packet.has_value()) {
    return fail(2);
  }
  const auto hero_menu_text = decode_dialog_text(hero_menu_packet->body);
  if (hero_menu_text.find("Guild Steward/") != 0 ||
      hero_menu_text.find("<Guild/@guild_menu>") == std::string::npos ||
      hero_menu_text.find("<Castle/@castle_menu>") == std::string::npos ||
      hero_menu_text.find("<Leave/@exit>") == std::string::npos) {
    return fail(3);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_menu")));
  const auto guild_menu_dispatch = runtime.tick();
  const auto guild_menu_packet = find_packet(guild_menu_dispatch, 21, mir2::kSmMerchantSay);
  if (!guild_menu_packet.has_value()) {
    return fail(4);
  }
  const auto guild_menu_text = decode_dialog_text(guild_menu_packet->body);
  if (guild_menu_text.find("<Info/@guild_info>") == std::string::npos ||
      guild_menu_text.find("<Members/@guild_members>") == std::string::npos ||
      guild_menu_text.find("<Applicants/@guild_applicants>") == std::string::npos ||
      guild_menu_text.find("<Leave/@guild_leave_confirm>") == std::string::npos) {
    return fail(5);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_info")));
  const auto guild_info_dispatch = runtime.tick();
  const auto guild_info_packet = find_packet(guild_info_dispatch, 21, mir2::kSmMerchantSay);
  if (!guild_info_packet.has_value()) {
    return fail(6);
  }
  const auto guild_info_text = decode_dialog_text(guild_info_packet->body);
  if (guild_info_text.find("Guild: DragonSlayers") == std::string::npos ||
      guild_info_text.find("Your Role: Lord") == std::string::npos ||
      guild_info_text.find("Lord: Hero") == std::string::npos ||
      guild_info_text.find("Castle Role: Watching Rival") == std::string::npos ||
      guild_info_text.find("Members/Applicants: 7/7") == std::string::npos ||
      guild_info_text.find("Preview: Hero, Ally, Bran +4 more") == std::string::npos ||
      guild_info_text.find("Applicant Preview: Visitor, Nia, Oren +4 more") ==
          std::string::npos ||
      guild_info_text.find("<Members/@guild_members>") == std::string::npos ||
      guild_info_text.find("<Applicants/@guild_applicants>") == std::string::npos ||
      guild_info_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(7);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_members")));
  const auto members_page_one_dispatch = runtime.tick();
  const auto members_page_one_packet =
      find_packet(members_page_one_dispatch, 21, mir2::kSmMerchantSay);
  if (!members_page_one_packet.has_value()) {
    return fail(8);
  }
  const auto members_page_one_text = decode_dialog_text(members_page_one_packet->body);
  if (members_page_one_text.find("DragonSlayers (1/2)") == std::string::npos ||
      members_page_one_text.find("Member: Ally") == std::string::npos ||
      members_page_one_text.find("<Manage Ally/@guild_member 1 Ally>") == std::string::npos ||
      members_page_one_text.find("<Next/@guild_members 2>") == std::string::npos) {
    return fail(9);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_member 1 Ally")));
  const auto member_manage_dispatch = runtime.tick();
  const auto member_manage_packet = find_packet(member_manage_dispatch, 21, mir2::kSmMerchantSay);
  if (!member_manage_packet.has_value()) {
    return fail(10);
  }
  const auto member_manage_text = decode_dialog_text(member_manage_packet->body);
  if (member_manage_text.find("DragonSlayers / Ally") == std::string::npos ||
      member_manage_text.find("Title: Member") == std::string::npos ||
      member_manage_text.find("<Titles/@guild_member_titles 1 1 Ally>") ==
          std::string::npos ||
      member_manage_text.find("<Transfer Leadership/@guild_transfer_confirm 1 Ally>") ==
          std::string::npos ||
      member_manage_text.find("<Kick Member/@guild_kick_confirm 1 Ally>") ==
          std::string::npos ||
      member_manage_text.find("<Back/@guild_members 1>") == std::string::npos) {
    return fail(11);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_transfer_confirm 1 Ally")));
  const auto transfer_confirm_dispatch = runtime.tick();
  const auto transfer_confirm_packet =
      find_packet(transfer_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!transfer_confirm_packet.has_value()) {
    return fail(12);
  }
  const auto transfer_confirm_text = decode_dialog_text(transfer_confirm_packet->body);
  if (transfer_confirm_text.find("Current Lord: Hero") == std::string::npos ||
      transfer_confirm_text.find("Status: Online") == std::string::npos ||
      transfer_confirm_text.find("New Lord: Ally") == std::string::npos ||
      transfer_confirm_text.find("<Confirm/@guild_transfer_exec 1 Ally>") == std::string::npos ||
      transfer_confirm_text.find("<Back/@guild_member 1 Ally>") == std::string::npos) {
    return fail(13);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_member_titles 1 1 Ally")));
  const auto title_page_one_dispatch = runtime.tick();
  const auto title_page_one_packet = find_packet(title_page_one_dispatch, 21, mir2::kSmMerchantSay);
  if (!title_page_one_packet.has_value()) {
    return fail(14);
  }
  const auto title_page_one_text = decode_dialog_text(title_page_one_packet->body);
  if (title_page_one_text.find("Roles (1/2) Core Roles") == std::string::npos ||
      title_page_one_text.find("Current: Member") == std::string::npos ||
      title_page_one_text.find("<Member (Current)/@guild_title_confirm 1 1 Ally Member>") ==
          std::string::npos ||
      title_page_one_text.find("<Deputy/@guild_title_confirm 1 1 Ally Deputy>") ==
          std::string::npos ||
      title_page_one_text.find("<Elder/@guild_title_confirm 1 1 Ally Elder>") ==
          std::string::npos ||
      title_page_one_text.find("<Next/@guild_member_titles 1 2 Ally>") == std::string::npos) {
    return fail(15);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_member_titles 1 2 Ally")));
  const auto title_page_two_dispatch = runtime.tick();
  const auto title_page_two_packet = find_packet(title_page_two_dispatch, 21, mir2::kSmMerchantSay);
  if (!title_page_two_packet.has_value()) {
    return fail(16);
  }
  const auto title_page_two_text = decode_dialog_text(title_page_two_packet->body);
  if (title_page_two_text.find("Roles (2/2) Field Roles") == std::string::npos ||
      title_page_two_text.find("<Vanguard/@guild_title_confirm 1 2 Ally Vanguard>") ==
          std::string::npos ||
      title_page_two_text.find("<Scout/@guild_title_confirm 1 2 Ally Scout>") ==
          std::string::npos ||
      title_page_two_text.find("<Quartermaster/@guild_title_confirm 1 2 Ally Quartermaster>") ==
          std::string::npos ||
      title_page_two_text.find("<Prev/@guild_member_titles 1 1 Ally>") == std::string::npos ||
      title_page_two_text.find("<Back/@guild_member 1 Ally>") == std::string::npos) {
    return fail(17);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_title_confirm 1 1 Ally Deputy")));
  const auto title_confirm_dispatch = runtime.tick();
  const auto title_confirm_packet = find_packet(title_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!title_confirm_packet.has_value()) {
    return fail(101);
  }
  const auto title_confirm_text = decode_dialog_text(title_confirm_packet->body);
  if (title_confirm_text.find("DragonSlayers / Ally") == std::string::npos ||
      title_confirm_text.find("Current: Member") == std::string::npos ||
      title_confirm_text.find("New Title: Deputy") == std::string::npos ||
      title_confirm_text.find("<Confirm/@guild_title_exec 1 1 Ally Deputy>") ==
          std::string::npos ||
      title_confirm_text.find("<Back/@guild_member_titles 1 1 Ally>") == std::string::npos) {
    return fail(102);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_title_exec 1 1 Ally Deputy")));
  const auto member_title_dispatch = runtime.tick();
  const auto member_title_packet = find_packet(member_title_dispatch, 21, mir2::kSmMerchantSay);
  const auto* member_title_request = find_character_save(member_title_dispatch, "Ally");
  if (!member_title_packet.has_value() || member_title_request == nullptr ||
      member_title_request->character.guild_title != "Deputy") {
    return fail(103);
  }
  const auto member_title_text = decode_dialog_text(member_title_packet->body);
  if (member_title_text.find("Result: Success") == std::string::npos ||
      member_title_text.find("Title order: Ally -> Deputy.") == std::string::npos ||
      member_title_text.find("New Title: Deputy") == std::string::npos ||
      member_title_text.find("<Back/@guild_member_titles 1 1 Ally>") == std::string::npos) {
    return fail(104);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_members 2")));
  const auto members_page_two_dispatch = runtime.tick();
  const auto members_page_two_packet =
      find_packet(members_page_two_dispatch, 21, mir2::kSmMerchantSay);
  if (!members_page_two_packet.has_value()) {
    return fail(19);
  }
  const auto members_page_two_text = decode_dialog_text(members_page_two_packet->body);
  if (members_page_two_text.find("DragonSlayers (2/2)") == std::string::npos ||
      members_page_two_text.find("Member: Faye") == std::string::npos ||
      members_page_two_text.find("<Prev/@guild_members 1>") == std::string::npos) {
    return fail(20);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_applicants")));
  const auto applicants_page_one_dispatch = runtime.tick();
  const auto applicants_page_one_packet =
      find_packet(applicants_page_one_dispatch, 21, mir2::kSmMerchantSay);
  if (!applicants_page_one_packet.has_value()) {
    return fail(21);
  }
  const auto applicants_page_one_text = decode_dialog_text(applicants_page_one_packet->body);
  if (applicants_page_one_text.find("DragonSlayers (1/2)") == std::string::npos ||
      applicants_page_one_text.find("Applicant: Visitor") == std::string::npos ||
      applicants_page_one_text.find("<Review Visitor/@guild_applicant 1 Visitor>") ==
          std::string::npos ||
      applicants_page_one_text.find("<Next/@guild_applicants 2>") == std::string::npos) {
    return fail(22);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_applicant 1 Visitor")));
  const auto applicant_review_dispatch = runtime.tick();
  const auto applicant_review_packet =
      find_packet(applicant_review_dispatch, 21, mir2::kSmMerchantSay);
  if (!applicant_review_packet.has_value()) {
    return fail(23);
  }
  const auto applicant_review_text = decode_dialog_text(applicant_review_packet->body);
  if (applicant_review_text.find("DragonSlayers / Visitor") == std::string::npos ||
      applicant_review_text.find("Application: Visitor") == std::string::npos ||
      applicant_review_text.find("Status: Online") == std::string::npos ||
      applicant_review_text.find("<Approve/@guild_approve_confirm 1 Visitor>") ==
          std::string::npos ||
      applicant_review_text.find("<Reject/@guild_reject_confirm 1 Visitor>") ==
          std::string::npos ||
      applicant_review_text.find("<Back/@guild_applicants 1>") == std::string::npos) {
    return fail(24);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_approve_confirm 1 Visitor")));
  const auto approve_confirm_dispatch = runtime.tick();
  const auto approve_confirm_packet =
      find_packet(approve_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!approve_confirm_packet.has_value()) {
    return fail(25);
  }
  const auto approve_confirm_text = decode_dialog_text(approve_confirm_packet->body);
  if (approve_confirm_text.find("Status: Online") == std::string::npos ||
      approve_confirm_text.find("Guild: DragonSlayers") == std::string::npos ||
      approve_confirm_text.find("<Confirm/@guild_approve_exec 1 Visitor>") ==
          std::string::npos ||
      approve_confirm_text.find("<Back/@guild_applicant 1 Visitor>") == std::string::npos) {
    return fail(26);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_applicants 2")));
  const auto applicants_page_two_dispatch = runtime.tick();
  const auto applicants_page_two_packet =
      find_packet(applicants_page_two_dispatch, 21, mir2::kSmMerchantSay);
  if (!applicants_page_two_packet.has_value()) {
    return fail(27);
  }
  const auto applicants_page_two_text = decode_dialog_text(applicants_page_two_packet->body);
  if (applicants_page_two_text.find("DragonSlayers (2/2)") == std::string::npos ||
      applicants_page_two_text.find("Applicant: Sage") == std::string::npos ||
      applicants_page_two_text.find("<Review Sage/@guild_applicant 2 Sage>") ==
          std::string::npos ||
      applicants_page_two_text.find("<Prev/@guild_applicants 1>") == std::string::npos) {
    return fail(28);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_applicant 2 Sage")));
  const auto sage_review_dispatch = runtime.tick();
  const auto sage_review_packet = find_packet(sage_review_dispatch, 21, mir2::kSmMerchantSay);
  if (!sage_review_packet.has_value()) {
    return fail(105);
  }
  const auto sage_review_text = decode_dialog_text(sage_review_packet->body);
  if (sage_review_text.find("DragonSlayers / Sage") == std::string::npos ||
      sage_review_text.find("<Reject/@guild_reject_confirm 2 Sage>") == std::string::npos) {
    return fail(106);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_reject_confirm 2 Sage")));
  const auto reject_confirm_dispatch = runtime.tick();
  const auto reject_confirm_packet =
      find_packet(reject_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!reject_confirm_packet.has_value()) {
    return fail(107);
  }
  const auto reject_confirm_text = decode_dialog_text(reject_confirm_packet->body);
  if (reject_confirm_text.find("Status: Offline") == std::string::npos ||
      reject_confirm_text.find("Guild: DragonSlayers") == std::string::npos ||
      reject_confirm_text.find("<Confirm/@guild_reject_exec 2 Sage>") ==
          std::string::npos ||
      reject_confirm_text.find("<Back/@guild_applicant 2 Sage>") == std::string::npos) {
    return fail(108);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_reject_exec 2 Sage")));
  const auto reject_dispatch = runtime.tick();
  const auto reject_result_packet = find_packet(reject_dispatch, 21, mir2::kSmMerchantSay);
  const auto* reject_guild_request =
      find_persist_request(reject_dispatch, mir2::PersistRequestKind::save_guild_state);
  if (!reject_result_packet.has_value() || reject_guild_request == nullptr ||
      reject_guild_request->guild_state.applicants.size() != 6) {
    return fail(109);
  }
  const auto reject_result_text = decode_dialog_text(reject_result_packet->body);
  if (reject_result_text.find("Result: Success") == std::string::npos ||
      reject_result_text.find("Applicant Sage turned away.") == std::string::npos ||
      reject_result_text.find("Applicants Remaining: 6") == std::string::npos ||
      reject_result_text.find("<Back/@guild_applicants 2>") == std::string::npos) {
    return fail(110);
  }

  static_cast<void>(runtime.route_logic_command(make_click_npc_command(22, 1)));
  static_cast<void>(runtime.tick());
  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_menu")));
  const auto visitor_guild_menu_dispatch = runtime.tick();
  const auto visitor_guild_menu_packet =
      find_packet(visitor_guild_menu_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_guild_menu_packet.has_value()) {
    return fail(29);
  }
  const auto visitor_guild_menu_text = decode_dialog_text(visitor_guild_menu_packet->body);
  if (visitor_guild_menu_text.find("<Create Guild/@guild_create_menu>") ==
          std::string::npos ||
      visitor_guild_menu_text.find("<Guild Directory/@guild_directory>") ==
          std::string::npos ||
      visitor_guild_menu_text.find("<My Applications/@guild_my_applications>") ==
          std::string::npos) {
    return fail(30);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_create_confirm VisitorGuild")));
  const auto visitor_create_confirm_dispatch = runtime.tick();
  const auto visitor_create_confirm_packet =
      find_packet(visitor_create_confirm_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_create_confirm_packet.has_value()) {
    return fail(181);
  }
  const auto visitor_create_confirm_text =
      decode_dialog_text(visitor_create_confirm_packet->body);
  if (visitor_create_confirm_text.find("Guild: VisitorGuild") == std::string::npos ||
      visitor_create_confirm_text.find("Founding Fee: 12000") == std::string::npos ||
      visitor_create_confirm_text.find("Gold: 0") == std::string::npos ||
      visitor_create_confirm_text.find("Status: Need 12000 Gold") == std::string::npos ||
      visitor_create_confirm_text.find("<View Result/@guild_create_exec VisitorGuild>") ==
          std::string::npos) {
    return fail(182);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_create_exec VisitorGuild")));
  const auto visitor_create_fail_dispatch = runtime.tick();
  const auto visitor_create_fail_packet =
      find_packet(visitor_create_fail_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_create_fail_packet.has_value()) {
    return fail(183);
  }
  const auto visitor_create_fail_text =
      decode_dialog_text(visitor_create_fail_packet->body);
  if (visitor_create_fail_text.find("Result: Failed") == std::string::npos ||
      visitor_create_fail_text.find("Summary: Treasury shortfall: VisitorGuild needs 12000 gold.") ==
          std::string::npos ||
      visitor_create_fail_text.find("Guild Snapshot: VisitorGuild") == std::string::npos ||
      visitor_create_fail_text.find("Guild Status: Need Gold") == std::string::npos ||
      visitor_create_fail_text.find("Treasury: 12000 / 0") == std::string::npos ||
      visitor_create_fail_text.find("<Back/@guild_create_menu>") == std::string::npos) {
    return fail(184);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_create_confirm DragonSlayers")));
  const auto visitor_duplicate_confirm_dispatch = runtime.tick();
  const auto visitor_duplicate_confirm_packet =
      find_packet(visitor_duplicate_confirm_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_duplicate_confirm_packet.has_value()) {
    return fail(185);
  }
  const auto visitor_duplicate_confirm_text =
      decode_dialog_text(visitor_duplicate_confirm_packet->body);
  if (visitor_duplicate_confirm_text.find("Guild: DragonSlayers") == std::string::npos ||
      visitor_duplicate_confirm_text.find("Status: Name Unavailable") == std::string::npos ||
      visitor_duplicate_confirm_text.find("<View Result/@guild_create_exec DragonSlayers>") ==
          std::string::npos) {
    return fail(186);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_create_exec DragonSlayers")));
  const auto visitor_duplicate_result_dispatch = runtime.tick();
  const auto visitor_duplicate_result_packet =
      find_packet(visitor_duplicate_result_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_duplicate_result_packet.has_value()) {
    return fail(187);
  }
  const auto visitor_duplicate_result_text =
      decode_dialog_text(visitor_duplicate_result_packet->body);
  if (visitor_duplicate_result_text.find("Result: Failed") == std::string::npos ||
      visitor_duplicate_result_text.find("Summary: Charter name collision for DragonSlayers.") ==
          std::string::npos ||
      visitor_duplicate_result_text.find("Guild Snapshot: DragonSlayers") ==
          std::string::npos ||
      visitor_duplicate_result_text.find("Guild Status: Name Unavailable") ==
          std::string::npos ||
      visitor_duplicate_result_text.find("<Back/@guild_create_menu>") ==
          std::string::npos) {
    return fail(188);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_directory")));
  const auto visitor_directory_dispatch = runtime.tick();
  const auto visitor_directory_packet =
      find_packet(visitor_directory_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_directory_packet.has_value()) {
    return fail(129);
  }
  const auto visitor_directory_text = decode_dialog_text(visitor_directory_packet->body);
  if (visitor_directory_text.find("Guilds (1/2)") == std::string::npos ||
      visitor_directory_text.find("Guild: DragonSlayers") == std::string::npos ||
      visitor_directory_text.find("Castle Role: Watching Rival") == std::string::npos ||
      visitor_directory_text.find("Members/Applicants: 7/6") == std::string::npos ||
      visitor_directory_text.find("Preview: Hero, Ally") == std::string::npos ||
      visitor_directory_text.find("<View DragonSlayers/@guild_browse directory 1 DragonSlayers>") ==
          std::string::npos ||
      visitor_directory_text.find("<Pending DragonSlayers/@guild_apply_status DragonSlayers>") ==
          std::string::npos ||
      visitor_directory_text.find("Guild: PhoenixHall") == std::string::npos ||
      visitor_directory_text.find("Castle Role: Steward") == std::string::npos ||
      visitor_directory_text.find("Members/Applicants: 1/0") == std::string::npos ||
      visitor_directory_text.find("Preview: Percival") == std::string::npos ||
      visitor_directory_text.find("<Apply PhoenixHall/@guild_apply_confirm PhoenixHall>") ==
          std::string::npos ||
      visitor_directory_text.find("<Next/@guild_directory 2>") == std::string::npos) {
    return fail(130);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_browse directory 1 DragonSlayers")));
  const auto dragon_browse_dispatch = runtime.tick();
  const auto dragon_browse_packet = find_packet(dragon_browse_dispatch, 22, mir2::kSmMerchantSay);
  if (!dragon_browse_packet.has_value()) {
    return fail(139);
  }
  const auto dragon_browse_text = decode_dialog_text(dragon_browse_packet->body);
  if (dragon_browse_text.find("Guild: DragonSlayers") == std::string::npos) {
    return fail(140);
  }
  if (dragon_browse_text.find("Lord: Hero") == std::string::npos) {
    return fail(141);
  }
  if (dragon_browse_text.find("Castle Role: Watching Rival") == std::string::npos) {
    return fail(142);
  }
  if (dragon_browse_text.find("Members/Applicants: 7/6") == std::string::npos) {
    return fail(143);
  }
  if (dragon_browse_text.find("Preview: Hero, Ally, Bran +4 more") == std::string::npos) {
    return fail(143);
  }
  if (dragon_browse_text.find("Applicant Preview: Visitor, Nia, Oren +3 more") ==
      std::string::npos) {
    return fail(143);
  }
  if (dragon_browse_text.find("Castle: None") == std::string::npos) {
    return fail(144);
  }
  if (dragon_browse_text.find("Status: Pending") == std::string::npos) {
    return fail(145);
  }
  if (dragon_browse_text.find("<View Members/@guild_roster directory 1 1 DragonSlayers>") ==
      std::string::npos) {
    return fail(146);
  }
  if (dragon_browse_text.find(
          "<View Applicants/@guild_applicant_roster directory 1 1 DragonSlayers>") ==
      std::string::npos) {
    return fail(147);
  }
  if (dragon_browse_text.find("<Pending Application/@guild_apply_status DragonSlayers>") ==
      std::string::npos) {
    return fail(148);
  }
  if (dragon_browse_text.find("<Back/@guild_directory 1>") == std::string::npos) {
    return fail(149);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_roster directory 1 1 DragonSlayers")));
  const auto dragon_roster_dispatch = runtime.tick();
  const auto dragon_roster_packet = find_packet(dragon_roster_dispatch, 22, mir2::kSmMerchantSay);
  if (!dragon_roster_packet.has_value()) {
    return fail(143);
  }
  const auto dragon_roster_text = decode_dialog_text(dragon_roster_packet->body);
  if (dragon_roster_text.find("DragonSlayers (1/2)") == std::string::npos ||
      dragon_roster_text.find("Member: Hero") == std::string::npos ||
      dragon_roster_text.find("Member: Ector") == std::string::npos ||
      dragon_roster_text.find("<Next/@guild_roster directory 1 2 DragonSlayers>") ==
          std::string::npos ||
      dragon_roster_text.find("<Back/@guild_browse directory 1 DragonSlayers>") ==
          std::string::npos) {
    return fail(144);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_roster directory 1 2 DragonSlayers")));
  const auto dragon_roster_page_two_dispatch = runtime.tick();
  const auto dragon_roster_page_two_packet =
      find_packet(dragon_roster_page_two_dispatch, 22, mir2::kSmMerchantSay);
  if (!dragon_roster_page_two_packet.has_value()) {
    return fail(145);
  }
  const auto dragon_roster_page_two_text =
      decode_dialog_text(dragon_roster_page_two_packet->body);
  if (dragon_roster_page_two_text.find("DragonSlayers (2/2)") == std::string::npos ||
      dragon_roster_page_two_text.find("Member: Faye") == std::string::npos ||
      dragon_roster_page_two_text.find("<Prev/@guild_roster directory 1 1 DragonSlayers>") ==
          std::string::npos) {
    return fail(146);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(22, 1, "@guild_applicant_roster directory 1 1 DragonSlayers")));
  const auto dragon_applicant_roster_dispatch = runtime.tick();
  const auto dragon_applicant_roster_packet =
      find_packet(dragon_applicant_roster_dispatch, 22, mir2::kSmMerchantSay);
  if (!dragon_applicant_roster_packet.has_value()) {
    return fail(147);
  }
  const auto dragon_applicant_roster_text =
      decode_dialog_text(dragon_applicant_roster_packet->body);
  if (dragon_applicant_roster_text.find("DragonSlayers (1/1)") == std::string::npos ||
      dragon_applicant_roster_text.find("Applicant: Visitor") == std::string::npos ||
      dragon_applicant_roster_text.find("Applicant: Rowan") == std::string::npos ||
      dragon_applicant_roster_text.find("<Back/@guild_browse directory 1 DragonSlayers>") ==
          std::string::npos) {
    return fail(148);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_my_applications")));
  const auto visitor_my_apps_dispatch = runtime.tick();
  const auto visitor_my_apps_packet =
      find_packet(visitor_my_apps_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_my_apps_packet.has_value()) {
    return fail(131);
  }
  const auto visitor_my_apps_text = decode_dialog_text(visitor_my_apps_packet->body);
  if (visitor_my_apps_text.find("Applications (1/1)") == std::string::npos ||
      visitor_my_apps_text.find("Guild: DragonSlayers") == std::string::npos ||
      visitor_my_apps_text.find("Castle Role: Watching Rival") == std::string::npos ||
      visitor_my_apps_text.find("Members/Applicants: 7/6") == std::string::npos ||
      visitor_my_apps_text.find("Preview: Hero, Ally") == std::string::npos ||
      visitor_my_apps_text.find("<View DragonSlayers/@guild_browse applications 1 DragonSlayers>") ==
          std::string::npos ||
      visitor_my_apps_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(132);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_apply_status DragonSlayers")));
  const auto pending_status_dispatch = runtime.tick();
  const auto pending_status_packet = find_packet(pending_status_dispatch, 22, mir2::kSmMerchantSay);
  if (!pending_status_packet.has_value()) {
    return fail(119);
  }
  const auto pending_status_text = decode_dialog_text(pending_status_packet->body);
  if (pending_status_text.find("Applicant: Visitor") == std::string::npos ||
      pending_status_text.find("Guild: DragonSlayers") == std::string::npos ||
      pending_status_text.find("Status: Pending") == std::string::npos ||
      pending_status_text.find("<Withdraw/@guild_withdraw_confirm DragonSlayers>") ==
          std::string::npos ||
      pending_status_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(120);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_apply_exec DragonSlayers")));
  const auto duplicate_apply_dispatch = runtime.tick();
  const auto duplicate_apply_packet =
      find_packet(duplicate_apply_dispatch, 22, mir2::kSmMerchantSay);
  if (!duplicate_apply_packet.has_value()) {
    return fail(189);
  }
  const auto duplicate_apply_text = decode_dialog_text(duplicate_apply_packet->body);
  if (duplicate_apply_text.find("Result: Failed") == std::string::npos ||
      duplicate_apply_text.find("Summary: Petition already pending with DragonSlayers.") ==
          std::string::npos ||
      duplicate_apply_text.find("Guild Snapshot: DragonSlayers") == std::string::npos ||
      duplicate_apply_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(190);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_apply_confirm PhoenixHall")));
  const auto apply_confirm_dispatch = runtime.tick();
  const auto apply_confirm_packet = find_packet(apply_confirm_dispatch, 22, mir2::kSmMerchantSay);
  if (!apply_confirm_packet.has_value()) {
    return fail(115);
  }
  const auto apply_confirm_text = decode_dialog_text(apply_confirm_packet->body);
  if (apply_confirm_text.find("Applicant: Visitor") == std::string::npos ||
      apply_confirm_text.find("Guild: PhoenixHall") == std::string::npos ||
      apply_confirm_text.find("Lord: Percival") == std::string::npos ||
      apply_confirm_text.find("Status: Ready") == std::string::npos ||
      apply_confirm_text.find("<Confirm/@guild_apply_exec PhoenixHall>") ==
          std::string::npos ||
      apply_confirm_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(116);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_apply_exec PhoenixHall")));
  const auto apply_dispatch = runtime.tick();
  const auto apply_result_packet = find_packet(apply_dispatch, 22, mir2::kSmMerchantSay);
  const auto* apply_guild_request =
      find_persist_request(apply_dispatch, mir2::PersistRequestKind::save_guild_state);
  if (!apply_result_packet.has_value() || apply_guild_request == nullptr ||
      apply_guild_request->guild_state.guild_name != "PhoenixHall" ||
      apply_guild_request->guild_state.applicants.size() != 1 ||
      apply_guild_request->guild_state.applicants.front() != "Visitor") {
    return fail(117);
  }
  const auto apply_result_text = decode_dialog_text(apply_result_packet->body);
  if (apply_result_text.find("Result: Success") == std::string::npos ||
      apply_result_text.find("Petition filed with PhoenixHall.") == std::string::npos ||
      apply_result_text.find("Applicants Pending: 1") == std::string::npos ||
      apply_result_text.find("<Guild/@guild_menu>") == std::string::npos ||
      apply_result_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(118);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_menu")));
  const auto visitor_guild_menu_after_apply_dispatch = runtime.tick();
  const auto visitor_guild_menu_after_apply_packet =
      find_packet(visitor_guild_menu_after_apply_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_guild_menu_after_apply_packet.has_value()) {
    return fail(121);
  }
  const auto visitor_guild_menu_after_apply_text =
      decode_dialog_text(visitor_guild_menu_after_apply_packet->body);
  if (visitor_guild_menu_after_apply_text.find("<My Applications/@guild_my_applications>") ==
      std::string::npos) {
    return fail(122);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_directory")));
  const auto visitor_directory_after_apply_dispatch = runtime.tick();
  const auto visitor_directory_after_apply_packet =
      find_packet(visitor_directory_after_apply_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_directory_after_apply_packet.has_value()) {
    return fail(133);
  }
  const auto visitor_directory_after_apply_text =
      decode_dialog_text(visitor_directory_after_apply_packet->body);
  if (visitor_directory_after_apply_text.find(
          "<Pending PhoenixHall/@guild_apply_status PhoenixHall>") == std::string::npos) {
    return fail(134);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_my_applications")));
  const auto visitor_my_apps_after_apply_dispatch = runtime.tick();
  const auto visitor_my_apps_after_apply_packet =
      find_packet(visitor_my_apps_after_apply_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_my_apps_after_apply_packet.has_value()) {
    return fail(135);
  }
  const auto visitor_my_apps_after_apply_text =
      decode_dialog_text(visitor_my_apps_after_apply_packet->body);
  if (visitor_my_apps_after_apply_text.find("Applications (1/1)") == std::string::npos ||
      visitor_my_apps_after_apply_text.find("Guild: PhoenixHall") == std::string::npos ||
      visitor_my_apps_after_apply_text.find(
          "<View PhoenixHall/@guild_browse applications 1 PhoenixHall>") == std::string::npos) {
    return fail(136);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_browse applications 1 PhoenixHall")));
  const auto phoenix_browse_dispatch = runtime.tick();
  const auto phoenix_browse_packet = find_packet(phoenix_browse_dispatch, 22, mir2::kSmMerchantSay);
  if (!phoenix_browse_packet.has_value()) {
    return fail(141);
  }
  const auto phoenix_browse_text = decode_dialog_text(phoenix_browse_packet->body);
  if (phoenix_browse_text.find("Guild: PhoenixHall") == std::string::npos ||
      phoenix_browse_text.find("Lord: Percival") == std::string::npos ||
      phoenix_browse_text.find("Castle Role: Steward") == std::string::npos ||
      phoenix_browse_text.find("Members/Applicants: 1/1") == std::string::npos ||
      phoenix_browse_text.find("Preview: Percival") == std::string::npos ||
      phoenix_browse_text.find("Applicant Preview: Visitor") == std::string::npos ||
      phoenix_browse_text.find("Castle: Owner of Sabuk") == std::string::npos ||
      phoenix_browse_text.find("Castle Lord: Percival") == std::string::npos ||
      phoenix_browse_text.find("Status: Pending") == std::string::npos ||
      phoenix_browse_text.find("<View Members/@guild_roster applications 1 1 PhoenixHall>") ==
          std::string::npos ||
      phoenix_browse_text.find(
          "<View Applicants/@guild_applicant_roster applications 1 1 PhoenixHall>") ==
          std::string::npos ||
      phoenix_browse_text.find("<Pending Application/@guild_apply_status PhoenixHall>") ==
          std::string::npos ||
      phoenix_browse_text.find("<Back/@guild_my_applications 1>") == std::string::npos) {
    return fail(142);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(22, 1, "@guild_applicant_roster applications 1 1 PhoenixHall")));
  const auto phoenix_applicant_roster_dispatch = runtime.tick();
  const auto phoenix_applicant_roster_packet =
      find_packet(phoenix_applicant_roster_dispatch, 22, mir2::kSmMerchantSay);
  if (!phoenix_applicant_roster_packet.has_value()) {
    return fail(151);
  }
  const auto phoenix_applicant_roster_text =
      decode_dialog_text(phoenix_applicant_roster_packet->body);
  if (phoenix_applicant_roster_text.find("PhoenixHall (1/1)") == std::string::npos ||
      phoenix_applicant_roster_text.find("Applicant: Visitor") == std::string::npos ||
      phoenix_applicant_roster_text.find("<Back/@guild_browse applications 1 PhoenixHall>") ==
          std::string::npos) {
    return fail(152);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_apply_status PhoenixHall")));
  const auto phoenix_status_dispatch = runtime.tick();
  const auto phoenix_status_packet = find_packet(phoenix_status_dispatch, 22, mir2::kSmMerchantSay);
  if (!phoenix_status_packet.has_value()) {
    return fail(123);
  }
  const auto phoenix_status_text = decode_dialog_text(phoenix_status_packet->body);
  if (phoenix_status_text.find("Guild: PhoenixHall") == std::string::npos ||
      phoenix_status_text.find("Status: Pending") == std::string::npos ||
      phoenix_status_text.find("<Withdraw/@guild_withdraw_confirm PhoenixHall>") ==
          std::string::npos) {
    return fail(124);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_withdraw_confirm PhoenixHall")));
  const auto withdraw_confirm_dispatch = runtime.tick();
  const auto withdraw_confirm_packet =
      find_packet(withdraw_confirm_dispatch, 22, mir2::kSmMerchantSay);
  if (!withdraw_confirm_packet.has_value()) {
    return fail(125);
  }
  const auto withdraw_confirm_text = decode_dialog_text(withdraw_confirm_packet->body);
  if (withdraw_confirm_text.find("Guild: PhoenixHall") == std::string::npos ||
      withdraw_confirm_text.find("Status: Pending") == std::string::npos ||
      withdraw_confirm_text.find("<Confirm/@guild_withdraw_exec PhoenixHall>") ==
          std::string::npos ||
      withdraw_confirm_text.find("<Back/@guild_apply_status PhoenixHall>") ==
          std::string::npos) {
    return fail(126);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(22, 1, "@guild_withdraw_exec PhoenixHall")));
  const auto withdraw_dispatch = runtime.tick();
  const auto withdraw_result_packet = find_packet(withdraw_dispatch, 22, mir2::kSmMerchantSay);
  const auto* withdraw_guild_request =
      find_persist_request(withdraw_dispatch, mir2::PersistRequestKind::save_guild_state);
  if (!withdraw_result_packet.has_value() || withdraw_guild_request == nullptr ||
      withdraw_guild_request->guild_state.guild_name != "PhoenixHall" ||
      !withdraw_guild_request->guild_state.applicants.empty()) {
    return fail(127);
  }
  const auto withdraw_result_text = decode_dialog_text(withdraw_result_packet->body);
  if (withdraw_result_text.find("Result: Success") == std::string::npos ||
      withdraw_result_text.find("Petition withdrawn from PhoenixHall.") ==
          std::string::npos ||
      withdraw_result_text.find("Applicants Pending: 0") == std::string::npos ||
      withdraw_result_text.find("<Guild/@guild_menu>") == std::string::npos ||
      withdraw_result_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(128);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(22, 1, "@guild_my_applications")));
  const auto visitor_my_apps_after_withdraw_dispatch = runtime.tick();
  const auto visitor_my_apps_after_withdraw_packet =
      find_packet(visitor_my_apps_after_withdraw_dispatch, 22, mir2::kSmMerchantSay);
  if (!visitor_my_apps_after_withdraw_packet.has_value()) {
    return fail(137);
  }
  const auto visitor_my_apps_after_withdraw_text =
      decode_dialog_text(visitor_my_apps_after_withdraw_packet->body);
  if (visitor_my_apps_after_withdraw_text.find("Applications (1/1)") == std::string::npos ||
      visitor_my_apps_after_withdraw_text.find("Guild: DragonSlayers") == std::string::npos ||
      visitor_my_apps_after_withdraw_text.find("Guild: PhoenixHall") != std::string::npos) {
    return fail(138);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_approve_exec 1 Visitor")));
  const auto approve_dispatch = runtime.tick();
  const auto approve_result_packet = find_packet(approve_dispatch, 21, mir2::kSmMerchantSay);
  const auto* approve_guild_request =
      find_persist_request(approve_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* approve_character_request = find_character_save(approve_dispatch, "Visitor");
  if (!approve_result_packet.has_value() || approve_guild_request == nullptr || approve_character_request == nullptr ||
      approve_guild_request->guild_state.guild_name != "DragonSlayers" ||
      approve_guild_request->guild_state.members.size() != 8 ||
      approve_guild_request->guild_state.applicants.size() != 5 ||
      approve_character_request->character.guild_name != "DragonSlayers" ||
      approve_character_request->character.guild_title != "Member") {
    return fail(31);
  }
  const auto approve_result_text = decode_dialog_text(approve_result_packet->body);
  if (approve_result_text.find("Result: Success") == std::string::npos ||
      approve_result_text.find("Summary: Applicant Visitor welcomed aboard.") ==
          std::string::npos ||
      approve_result_text.find("Guild Snapshot: DragonSlayers") == std::string::npos ||
      approve_result_text.find("Counts: -/5") == std::string::npos ||
      approve_result_text.find("Applicant Visitor welcomed aboard.") == std::string::npos ||
      approve_result_text.find("Applicants Remaining: 5") == std::string::npos ||
      approve_result_text.find("<Guild/@guild_info>") == std::string::npos ||
      approve_result_text.find("<Back/@guild_applicants 1>") == std::string::npos) {
    return fail(32);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_menu")));
  const auto castle_menu_dispatch = runtime.tick();
  const auto castle_menu_packet = find_packet(castle_menu_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_menu_packet.has_value()) {
    return fail(33);
  }
  const auto castle_menu_text = decode_dialog_text(castle_menu_packet->body);
  if (castle_menu_text.find("<Show Castle/@castle_show>") == std::string::npos ||
      castle_menu_text.find("<Active Wars/@castle_wars>") == std::string::npos ||
      castle_menu_text.find("<Claim Castle/@castle_claim_confirm>") == std::string::npos ||
      castle_menu_text.find("<Declare War/@castle_war_targets>") == std::string::npos) {
    return fail(34);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_show")));
  const auto castle_show_dispatch = runtime.tick();
  const auto castle_show_packet = find_packet(castle_show_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_show_packet.has_value()) {
    return fail(30);
  }
  const auto castle_show_text = decode_dialog_text(castle_show_packet->body);
  if (castle_show_text.find("Castle: Sabuk") == std::string::npos ||
      castle_show_text.find("Owner Guild: PhoenixHall") == std::string::npos ||
      castle_show_text.find("Owner Lord: Percival") == std::string::npos ||
      castle_show_text.find("Owner Role: Sabuk Sovereign") == std::string::npos ||
      castle_show_text.find("War Date: 2026-05-01 20:00") == std::string::npos ||
      castle_show_text.find("War Count: 7") == std::string::npos ||
      castle_show_text.find("War Preview: WolfPack, AzureSky, WhiteTiger +4 more") ==
          std::string::npos ||
      castle_show_text.find("Fees: 32000/11000") == std::string::npos ||
      castle_show_text.find("<View Owner Guild/@guild_browse castle_show 1 PhoenixHall>") ==
          std::string::npos ||
      castle_show_text.find("<View Owner Members/@guild_roster castle_show 1 1 PhoenixHall>") ==
          std::string::npos ||
      castle_show_text.find("<Active Wars/@castle_wars>") == std::string::npos ||
      castle_show_text.find("<Claim Castle/@castle_claim_confirm>") == std::string::npos ||
      castle_show_text.find("<Back/@castle_menu>") == std::string::npos) {
    return fail(31);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_browse castle_show 1 PhoenixHall")));
  const auto castle_owner_browse_dispatch = runtime.tick();
  const auto castle_owner_browse_packet =
      find_packet(castle_owner_browse_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_owner_browse_packet.has_value()) {
    return fail(157);
  }
  const auto castle_owner_browse_text = decode_dialog_text(castle_owner_browse_packet->body);
  if (castle_owner_browse_text.find("Guild: PhoenixHall") == std::string::npos ||
      castle_owner_browse_text.find("Castle: Owner of Sabuk") == std::string::npos ||
      castle_owner_browse_text.find("<View Members/@guild_roster castle_show 1 1 PhoenixHall>") ==
          std::string::npos ||
      castle_owner_browse_text.find("<Back/@castle_show>") == std::string::npos) {
    return fail(158);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_roster castle_show 1 1 PhoenixHall")));
  const auto castle_owner_roster_dispatch = runtime.tick();
  const auto castle_owner_roster_packet =
      find_packet(castle_owner_roster_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_owner_roster_packet.has_value()) {
    return fail(159);
  }
  const auto castle_owner_roster_text = decode_dialog_text(castle_owner_roster_packet->body);
  if (castle_owner_roster_text.find("PhoenixHall (1/1)") == std::string::npos ||
      castle_owner_roster_text.find("Member: Percival") == std::string::npos ||
      castle_owner_roster_text.find("<Back/@castle_show>") ==
          std::string::npos) {
    return fail(160);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_wars")));
  const auto wars_page_one_dispatch = runtime.tick();
  const auto wars_page_one_packet = find_packet(wars_page_one_dispatch, 21, mir2::kSmMerchantSay);
  if (!wars_page_one_packet.has_value()) {
    return fail(32);
  }
  const auto wars_page_one_text = decode_dialog_text(wars_page_one_packet->body);
  if (wars_page_one_text.find("Sabuk (1/2)") == std::string::npos ||
      wars_page_one_text.find("War: WolfPack") == std::string::npos ||
      wars_page_one_text.find("Role: Siege Challenger") == std::string::npos ||
      wars_page_one_text.find("Guild Data: Unknown") == std::string::npos ||
      wars_page_one_text.find("War: MoonShade") == std::string::npos ||
      wars_page_one_text.find("Members/Applicants: 1/1") == std::string::npos ||
      wars_page_one_text.find("Preview: Eira") == std::string::npos ||
      wars_page_one_text.find("<View WolfPack/@castle_guild_browse wars 1 WolfPack>") ==
          std::string::npos ||
      wars_page_one_text.find("<Next/@castle_wars 2>") == std::string::npos) {
    return fail(33);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@castle_guild_browse wars 1 WolfPack")));
  const auto war_detail_dispatch = runtime.tick();
  const auto war_detail_packet = find_packet(war_detail_dispatch, 21, mir2::kSmMerchantSay);
  if (!war_detail_packet.has_value()) {
    return fail(153);
  }
  const auto war_detail_text = decode_dialog_text(war_detail_packet->body);
  if (war_detail_text.find("Castle: Sabuk") == std::string::npos ||
      war_detail_text.find("Target Guild: WolfPack") == std::string::npos ||
      war_detail_text.find("War Entry: Enrolled") == std::string::npos ||
      war_detail_text.find("Guild Data: Unknown") == std::string::npos ||
      war_detail_text.find("Castle Role: Siege Challenger") == std::string::npos ||
      war_detail_text.find("War Status: Marching") == std::string::npos ||
      war_detail_text.find("<Back/@castle_wars 1>") == std::string::npos) {
    return fail(154);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_wars 2")));
  const auto wars_page_two_dispatch = runtime.tick();
  const auto wars_page_two_packet = find_packet(wars_page_two_dispatch, 21, mir2::kSmMerchantSay);
  if (!wars_page_two_packet.has_value()) {
    return fail(34);
  }
  const auto wars_page_two_text = decode_dialog_text(wars_page_two_packet->body);
  if (wars_page_two_text.find("Sabuk (2/2)") == std::string::npos ||
      wars_page_two_text.find("War: RedBanner") == std::string::npos ||
      wars_page_two_text.find("<Prev/@castle_wars 1>") == std::string::npos) {
    return fail(35);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_war_targets")));
  const auto war_targets_page_one_dispatch = runtime.tick();
  const auto war_targets_page_one_packet =
      find_packet(war_targets_page_one_dispatch, 21, mir2::kSmMerchantSay);
  if (!war_targets_page_one_packet.has_value()) {
    return fail(36);
  }
  const auto war_targets_page_one_text = decode_dialog_text(war_targets_page_one_packet->body);
  if (war_targets_page_one_text.find("Sabuk (1/2)") == std::string::npos ||
      war_targets_page_one_text.find("Guild: PhoenixHall") == std::string::npos ||
      war_targets_page_one_text.find("Role: Steward") == std::string::npos ||
      war_targets_page_one_text.find("Members/Applicants: 1/0") == std::string::npos ||
      war_targets_page_one_text.find("Preview: Percival") == std::string::npos ||
      war_targets_page_one_text.find(
          "<View PhoenixHall/@castle_guild_browse targets 1 PhoenixHall>") ==
          std::string::npos ||
      war_targets_page_one_text.find("Guild: MoonShade") == std::string::npos ||
      war_targets_page_one_text.find("Role: Siege Challenger") == std::string::npos ||
      war_targets_page_one_text.find("<War PhoenixHall/@castle_war_confirm 1 PhoenixHall>") ==
          std::string::npos ||
      war_targets_page_one_text.find("<War MoonShade/@castle_war_confirm 1 MoonShade>") ==
          std::string::npos ||
      war_targets_page_one_text.find("<Next/@castle_war_targets 2>") == std::string::npos) {
    return fail(37);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(21, 1, "@castle_guild_browse targets 1 PhoenixHall")));
  const auto target_detail_dispatch = runtime.tick();
  const auto target_detail_packet = find_packet(target_detail_dispatch, 21, mir2::kSmMerchantSay);
  if (!target_detail_packet.has_value()) {
    return fail(155);
  }
  const auto target_detail_text = decode_dialog_text(target_detail_packet->body);
  if (target_detail_text.find("Castle: Sabuk") == std::string::npos ||
      target_detail_text.find("Target Guild: PhoenixHall") == std::string::npos ||
      target_detail_text.find("War Entry: Not Enrolled") == std::string::npos ||
      target_detail_text.find("Lord: Percival") == std::string::npos ||
      target_detail_text.find("Members: 1") == std::string::npos ||
      target_detail_text.find("Roster Preview: Percival") == std::string::npos ||
      target_detail_text.find("Applicant Preview: None") == std::string::npos ||
      target_detail_text.find("Castle Role: Steward") == std::string::npos ||
      target_detail_text.find("Castle: Owner of Sabuk") == std::string::npos ||
      target_detail_text.find("Castle Lord: Percival") == std::string::npos ||
      target_detail_text.find("<View Members/@guild_roster castle_targets 1 1 PhoenixHall>") ==
          std::string::npos ||
      target_detail_text.find("<Browse Guild/@guild_browse castle_targets 1 PhoenixHall>") ==
          std::string::npos ||
      target_detail_text.find("War Status: Open") == std::string::npos ||
      target_detail_text.find("War Fee: 32000") == std::string::npos ||
      target_detail_text.find("Gold: 100000") == std::string::npos ||
      target_detail_text.find("<Confirm War/@castle_war_confirm 1 PhoenixHall>") ==
          std::string::npos ||
      target_detail_text.find("<Back/@castle_war_targets 1>") == std::string::npos) {
    return fail(156);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(21, 1, "@guild_browse castle_targets 1 PhoenixHall")));
  const auto castle_target_browse_dispatch = runtime.tick();
  const auto castle_target_browse_packet =
      find_packet(castle_target_browse_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_target_browse_packet.has_value()) {
    return fail(161);
  }
  const auto castle_target_browse_text = decode_dialog_text(castle_target_browse_packet->body);
  if (castle_target_browse_text.find("Guild: PhoenixHall") == std::string::npos ||
      castle_target_browse_text.find("<View Members/@guild_roster castle_targets 1 1 PhoenixHall>") ==
          std::string::npos ||
      castle_target_browse_text.find("<Back/@castle_guild_browse targets 1 PhoenixHall>") ==
          std::string::npos) {
    return fail(162);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(21, 1, "@guild_roster castle_targets 1 1 PhoenixHall")));
  const auto castle_target_roster_dispatch = runtime.tick();
  const auto castle_target_roster_packet =
      find_packet(castle_target_roster_dispatch, 21, mir2::kSmMerchantSay);
  if (!castle_target_roster_packet.has_value()) {
    return fail(163);
  }
  const auto castle_target_roster_text = decode_dialog_text(castle_target_roster_packet->body);
  if (castle_target_roster_text.find("PhoenixHall (1/1)") == std::string::npos ||
      castle_target_roster_text.find("Member: Percival") == std::string::npos ||
      castle_target_roster_text.find("<Back/@castle_guild_browse targets 1 PhoenixHall>") ==
          std::string::npos) {
    return fail(164);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@castle_guild_browse targets 1 MoonShade")));
  const auto moonshade_detail_dispatch = runtime.tick();
  const auto moonshade_detail_packet =
      find_packet(moonshade_detail_dispatch, 21, mir2::kSmMerchantSay);
  if (!moonshade_detail_packet.has_value()) {
    return fail(165);
  }
  const auto moonshade_detail_text = decode_dialog_text(moonshade_detail_packet->body);
  if (moonshade_detail_text.empty()) {
    return fail(166);
  }

  static_cast<void>(runtime.route_logic_command(
      make_menu_command(21, 1, "@guild_applicant_roster castle_targets 1 1 MoonShade")));
  const auto moonshade_applicant_dispatch = runtime.tick();
  const auto moonshade_applicant_packet =
      find_packet(moonshade_applicant_dispatch, 21, mir2::kSmMerchantSay);
  if (!moonshade_applicant_packet.has_value()) {
    return fail(167);
  }
  const auto moonshade_applicant_text = decode_dialog_text(moonshade_applicant_packet->body);
  if (moonshade_applicant_text.find("MoonShade (1/1)") == std::string::npos ||
      moonshade_applicant_text.find("Applicant: Nyx") == std::string::npos ||
      moonshade_applicant_text.find("<Back/@castle_guild_browse targets 1 MoonShade>") ==
          std::string::npos) {
    return fail(168);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_war_targets 2")));
  const auto war_targets_page_two_dispatch = runtime.tick();
  const auto war_targets_page_two_packet =
      find_packet(war_targets_page_two_dispatch, 21, mir2::kSmMerchantSay);
  if (!war_targets_page_two_packet.has_value()) {
    return fail(38);
  }
  const auto war_targets_page_two_text = decode_dialog_text(war_targets_page_two_packet->body);
  if (war_targets_page_two_text.find("Sabuk (2/2)") == std::string::npos ||
      war_targets_page_two_text.find("<War RedBanner/@castle_war_confirm 2 RedBanner>") ==
          std::string::npos ||
      war_targets_page_two_text.find("<Prev/@castle_war_targets 1>") == std::string::npos) {
    return fail(39);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_claim_confirm")));
  const auto claim_confirm_dispatch = runtime.tick();
  const auto claim_confirm_packet = find_packet(claim_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!claim_confirm_packet.has_value()) {
    return fail(40);
  }
  const auto claim_confirm_text = decode_dialog_text(claim_confirm_packet->body);
  if (claim_confirm_text.find("Current Owner: PhoenixHall") == std::string::npos ||
      claim_confirm_text.find("New Owner: DragonSlayers") == std::string::npos ||
      claim_confirm_text.find("<Confirm/@castle_claim>") == std::string::npos ||
      claim_confirm_text.find("<Back/@castle_menu>") == std::string::npos) {
    return fail(41);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@castle_war_confirm 1 PhoenixHall")));
  const auto war_confirm_dispatch = runtime.tick();
  const auto war_confirm_packet = find_packet(war_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!war_confirm_packet.has_value()) {
    return fail(42);
  }
  const auto war_confirm_text = decode_dialog_text(war_confirm_packet->body);
  if (war_confirm_text.find("Target Guild: PhoenixHall") == std::string::npos ||
      war_confirm_text.find("War Fee: 32000") == std::string::npos ||
      war_confirm_text.find("Gold: 100000") == std::string::npos ||
      war_confirm_text.find("<Confirm/@castle_war PhoenixHall>") == std::string::npos ||
      war_confirm_text.find("<Back/@castle_war_targets 1>") == std::string::npos) {
    return fail(43);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_war PhoenixHall")));
  const auto war_dispatch = runtime.tick();
  const auto war_result_packet = find_packet(war_dispatch, 21, mir2::kSmMerchantSay);
  const auto* war_request =
      find_persist_request(war_dispatch, mir2::PersistRequestKind::save_castle_state);
  const auto* war_character_request = find_character_save(war_dispatch, "Hero");
  if (!war_result_packet.has_value() || war_request == nullptr || war_character_request == nullptr) {
    return fail(44);
  }
  const auto war_result_text = decode_dialog_text(war_result_packet->body);
  if (war_result_text.find("Result: Success") == std::string::npos ||
      war_result_text.find(
          "Summary: Siege bulletin: PhoenixHall registered at Sabuk for 32000 gold.") ==
          std::string::npos ||
      war_result_text.find("Castle Snapshot: Sabuk") == std::string::npos ||
      war_result_text.find("Owner Snapshot: PhoenixHall / Percival") == std::string::npos ||
      war_result_text.find("War Snapshot: 8 / WolfPack, AzureSky, WhiteTiger +5 more") ==
          std::string::npos ||
      war_result_text.find("Role Change: DragonSlayers -> Siege Challenger Mark") ==
          std::string::npos ||
      war_result_text.find("Siege bulletin: PhoenixHall registered at Sabuk for 32000 gold.") ==
          std::string::npos ||
      war_result_text.find("Gold: 68000") == std::string::npos ||
      war_result_text.find("Owner Guild: PhoenixHall") == std::string::npos ||
      war_result_text.find("Owner Lord: Percival") == std::string::npos ||
      war_result_text.find("War Count: 8") == std::string::npos ||
      war_result_text.find("War Preview: WolfPack, AzureSky, WhiteTiger +5 more") ==
          std::string::npos ||
      war_result_text.find("Guild Role Change: DragonSlayers -> Siege Challenger Mark") ==
          std::string::npos ||
      war_result_text.find("<Castle/@castle_show>") == std::string::npos ||
      war_result_text.find("<Back/@castle_menu>") == std::string::npos ||
      war_request->payload_json.find("\"list_of_war\":\"WolfPack, AzureSky, WhiteTiger, IronFist, Stormborn, MoonShade, RedBanner, PhoenixHall\"") ==
          std::string::npos ||
      war_character_request->character.gold != 68000) {
    return fail(45);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@castle_claim")));
  const auto claim_dispatch = runtime.tick();
  const auto claim_result_packet = find_packet(claim_dispatch, 21, mir2::kSmMerchantSay);
  const auto* claim_request =
      find_persist_request(claim_dispatch, mir2::PersistRequestKind::save_castle_state);
  if (!claim_result_packet.has_value() || claim_request == nullptr ||
      claim_request->payload_json.find("\"owner_guild\":\"DragonSlayers\"") == std::string::npos ||
      claim_request->payload_json.find("\"castle_war_date\":\"2026-05-01 20:00\"") ==
          std::string::npos) {
    return fail(46);
  }
  const auto claim_result_text = decode_dialog_text(claim_result_packet->body);
  if (claim_result_text.find("Result: Success") == std::string::npos ||
      claim_result_text.find("Steward report: DragonSlayers now governs Sabuk.") ==
          std::string::npos ||
      claim_result_text.find("Previous Owner: PhoenixHall") == std::string::npos ||
      claim_result_text.find("New Owner: DragonSlayers") == std::string::npos ||
      claim_result_text.find("Owner Guild: DragonSlayers") == std::string::npos ||
      claim_result_text.find("Owner Lord: Hero") == std::string::npos ||
      claim_result_text.find("War Count: 8") == std::string::npos ||
      claim_result_text.find("War Preview: WolfPack, AzureSky, WhiteTiger +5 more") ==
          std::string::npos ||
      claim_result_text.find("Guild Role Change: Watching Rival -> Crowned Steward") ==
          std::string::npos ||
      claim_result_text.find("<Castle/@castle_show>") == std::string::npos ||
      claim_result_text.find("<Back/@castle_menu>") == std::string::npos) {
    return fail(47);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_kick_confirm 1 Bran")));
  const auto kick_confirm_dispatch = runtime.tick();
  const auto kick_confirm_packet = find_packet(kick_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!kick_confirm_packet.has_value()) {
    return fail(48);
  }
  const auto kick_confirm_text = decode_dialog_text(kick_confirm_packet->body);
  if (kick_confirm_text.find("DragonSlayers / Bran") == std::string::npos ||
      kick_confirm_text.find("Status: Online") == std::string::npos ||
      kick_confirm_text.find("Title: Member") == std::string::npos ||
      kick_confirm_text.find("<Confirm/@guild_kick_exec 1 Bran>") == std::string::npos ||
      kick_confirm_text.find("<Back/@guild_member 1 Bran>") == std::string::npos) {
    return fail(49);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_kick_exec 1 Bran")));
  const auto kick_dispatch = runtime.tick();
  const auto kick_result_packet = find_packet(kick_dispatch, 21, mir2::kSmMerchantSay);
  const auto* kick_guild_request =
      find_persist_request(kick_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* kick_character_request = find_character_save(kick_dispatch, "Bran");
  if (!kick_result_packet.has_value() || kick_guild_request == nullptr ||
      kick_character_request == nullptr ||
      kick_guild_request->guild_state.members.size() != 7 ||
      !kick_character_request->character.guild_name.empty() ||
      !kick_character_request->character.guild_title.empty()) {
    return fail(50);
  }
  const auto kick_result_text = decode_dialog_text(kick_result_packet->body);
  if (kick_result_text.find("Result: Success") == std::string::npos ||
      kick_result_text.find("Roster cut: Bran.") == std::string::npos ||
      kick_result_text.find("Members Remaining: 7") == std::string::npos ||
      kick_result_text.find("<Guild/@guild_info>") == std::string::npos ||
      kick_result_text.find("<Back/@guild_members 1>") == std::string::npos) {
    return fail(51);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(21, 1, "@guild_transfer_exec 1 Ally")));
  const auto transfer_dispatch = runtime.tick();
  const auto transfer_result_packet = find_packet(transfer_dispatch, 21, mir2::kSmMerchantSay);
  const auto* transfer_guild_request =
      find_persist_request(transfer_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* transfer_hero_request = find_character_save(transfer_dispatch, "Hero");
  const auto* transfer_ally_request = find_character_save(transfer_dispatch, "Ally");
  if (!transfer_result_packet.has_value() || transfer_guild_request == nullptr ||
      transfer_hero_request == nullptr || transfer_ally_request == nullptr ||
      transfer_guild_request->guild_state.lord != "Ally" ||
      transfer_hero_request->character.guild_title != "Member" ||
      transfer_ally_request->character.guild_title != "Lord") {
    return fail(52);
  }
  const auto transfer_result_text = decode_dialog_text(transfer_result_packet->body);
  if (transfer_result_text.find("Result: Success") == std::string::npos ||
      transfer_result_text.find("Summary: Leadership baton passed to Ally.") ==
          std::string::npos ||
      transfer_result_text.find("Guild Snapshot: DragonSlayers") == std::string::npos ||
      transfer_result_text.find("Leadership: Hero -> Ally") == std::string::npos ||
      transfer_result_text.find("Leadership baton passed to Ally.") == std::string::npos ||
      transfer_result_text.find("Previous Lord: Hero") == std::string::npos ||
      transfer_result_text.find("New Lord: Ally") == std::string::npos ||
      transfer_result_text.find("<Guild/@guild_info>") == std::string::npos ||
      transfer_result_text.find("<Back/@guild_members 1>") == std::string::npos) {
    return fail(53);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_leave_confirm")));
  const auto leave_confirm_dispatch = runtime.tick();
  const auto leave_confirm_packet = find_packet(leave_confirm_dispatch, 21, mir2::kSmMerchantSay);
  if (!leave_confirm_packet.has_value()) {
    return fail(111);
  }
  const auto leave_confirm_text = decode_dialog_text(leave_confirm_packet->body);
  if (leave_confirm_text.find("Character: Hero") == std::string::npos ||
      leave_confirm_text.find("Guild: DragonSlayers") == std::string::npos ||
      leave_confirm_text.find("Role: Member") == std::string::npos ||
      leave_confirm_text.find("<Confirm/@guild_leave_exec>") == std::string::npos ||
      leave_confirm_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(112);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(21, 1, "@guild_leave_exec")));
  const auto leave_dispatch = runtime.tick();
  const auto leave_result_packet = find_packet(leave_dispatch, 21, mir2::kSmMerchantSay);
  const auto* leave_guild_request =
      find_persist_request(leave_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* leave_character_request = find_character_save(leave_dispatch, "Hero");
  if (!leave_result_packet.has_value() || leave_guild_request == nullptr ||
      leave_character_request == nullptr ||
      leave_guild_request->guild_state.members.size() != 6 ||
      leave_guild_request->guild_state.lord != "Ally" ||
      !leave_character_request->character.guild_name.empty() ||
      !leave_character_request->character.guild_title.empty()) {
    return fail(113);
  }
  const auto leave_result_text = decode_dialog_text(leave_result_packet->body);
  if (leave_result_text.find("Result: Success") == std::string::npos ||
      leave_result_text.find("Departure logged from DragonSlayers.") == std::string::npos ||
      leave_result_text.find("Members Remaining: 6") == std::string::npos ||
      leave_result_text.find("<Guild/@guild_menu>") == std::string::npos ||
      leave_result_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(114);
  }

  static_cast<void>(runtime.route_logic_command(make_click_npc_command(25, 1)));
  static_cast<void>(runtime.tick());
  static_cast<void>(runtime.route_logic_command(make_menu_command(25, 1, "@guild_menu")));
  const auto founder_guild_menu_dispatch = runtime.tick();
  const auto founder_guild_menu_packet =
      find_packet(founder_guild_menu_dispatch, 25, mir2::kSmMerchantSay);
  if (!founder_guild_menu_packet.has_value()) {
    return fail(171);
  }
  const auto founder_guild_menu_text = decode_dialog_text(founder_guild_menu_packet->body);
  if (founder_guild_menu_text.find("<Create Guild/@guild_create_menu>") == std::string::npos ||
      founder_guild_menu_text.find("<Guild Directory/@guild_directory>") == std::string::npos) {
    return fail(172);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(25, 1, "@guild_create_menu")));
  const auto founder_create_menu_dispatch = runtime.tick();
  const auto founder_create_menu_packet =
      find_packet(founder_create_menu_dispatch, 25, mir2::kSmMerchantSay);
  if (!founder_create_menu_packet.has_value()) {
    return fail(173);
  }
  const auto founder_create_menu_text = decode_dialog_text(founder_create_menu_packet->body);
  if (founder_create_menu_text.find("Founder: Founder") == std::string::npos ||
      founder_create_menu_text.find("Founding Fee: 12000") == std::string::npos ||
      founder_create_menu_text.find("Gold: 50000") == std::string::npos ||
      founder_create_menu_text.find("<Create FounderGuild/@guild_create_confirm FounderGuild>") ==
          std::string::npos ||
      founder_create_menu_text.find("<Create FounderHall/@guild_create_confirm FounderHall>") ==
          std::string::npos ||
      founder_create_menu_text.find("<Create FounderLegion/@guild_create_confirm FounderLegion>") ==
          std::string::npos ||
      founder_create_menu_text.find("<Back/@guild_menu>") == std::string::npos) {
    return fail(174);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(25, 1, "@guild_create_confirm FounderGuild")));
  const auto founder_create_confirm_dispatch = runtime.tick();
  const auto founder_create_confirm_packet =
      find_packet(founder_create_confirm_dispatch, 25, mir2::kSmMerchantSay);
  if (!founder_create_confirm_packet.has_value()) {
    return fail(175);
  }
  const auto founder_create_confirm_text =
      decode_dialog_text(founder_create_confirm_packet->body);
  if (founder_create_confirm_text.find("Founder: Founder") == std::string::npos ||
      founder_create_confirm_text.find("Guild: FounderGuild") == std::string::npos ||
      founder_create_confirm_text.find("Founding Fee: 12000") == std::string::npos ||
      founder_create_confirm_text.find("Gold: 50000") == std::string::npos ||
      founder_create_confirm_text.find("Status: Ready") == std::string::npos ||
      founder_create_confirm_text.find("<Confirm/@guild_create_exec FounderGuild>") ==
          std::string::npos ||
      founder_create_confirm_text.find("<Back/@guild_create_menu>") == std::string::npos) {
    return fail(176);
  }

  static_cast<void>(
      runtime.route_logic_command(make_menu_command(25, 1, "@guild_create_exec FounderGuild")));
  const auto founder_create_dispatch = runtime.tick();
  const auto founder_create_packet = find_packet(founder_create_dispatch, 25, mir2::kSmMerchantSay);
  const auto* founder_create_guild_request =
      find_persist_request(founder_create_dispatch, mir2::PersistRequestKind::save_guild_state);
  const auto* founder_create_character_request =
      find_character_save(founder_create_dispatch, "Founder");
  if (!founder_create_packet.has_value() || founder_create_guild_request == nullptr ||
      founder_create_character_request == nullptr ||
      founder_create_guild_request->guild_state.guild_name != "FounderGuild" ||
      founder_create_guild_request->guild_state.lord != "Founder" ||
      founder_create_character_request->character.guild_name != "FounderGuild" ||
      founder_create_character_request->character.guild_title != "Lord") {
    return fail(177);
  }
  const auto founder_create_result_text = decode_dialog_text(founder_create_packet->body);
  if (founder_create_result_text.find("Result: Success") == std::string::npos ||
      founder_create_result_text.find("Summary: Banner raised for FounderGuild.") ==
          std::string::npos ||
      founder_create_result_text.find("Guild Snapshot: FounderGuild") == std::string::npos ||
      founder_create_result_text.find("Guild Status: Founded") == std::string::npos ||
      founder_create_result_text.find("Treasury: 12000 / 38000") == std::string::npos ||
      founder_create_result_text.find("Counts: 1/0") == std::string::npos ||
      founder_create_result_text.find("Creation Fee: 12000") == std::string::npos ||
      founder_create_result_text.find("Gold: 38000") == std::string::npos ||
      founder_create_result_text.find("<Guild/@guild_menu>") == std::string::npos ||
      founder_create_result_text.find("<Back/@guild_create_menu>") == std::string::npos) {
    return fail(178);
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(25, 1, "@guild_menu")));
  const auto founder_post_create_menu_dispatch = runtime.tick();
  const auto founder_post_create_menu_packet =
      find_packet(founder_post_create_menu_dispatch, 25, mir2::kSmMerchantSay);
  if (!founder_post_create_menu_packet.has_value()) {
    return fail(179);
  }
  const auto founder_post_create_menu_text =
      decode_dialog_text(founder_post_create_menu_packet->body);
  if (founder_post_create_menu_text.find("<Info/@guild_info>") == std::string::npos ||
      founder_post_create_menu_text.find("<Members/@guild_members>") == std::string::npos ||
      founder_post_create_menu_text.find("<Leave/@guild_leave_confirm>") ==
          std::string::npos) {
    return fail(180);
  }

  return 0;
}
