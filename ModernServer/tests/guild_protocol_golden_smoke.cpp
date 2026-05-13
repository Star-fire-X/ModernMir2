#include "world/castle_manager.hpp"
#include "world/guild_manager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int fail(std::string_view stage) {
  std::cerr << "guild_protocol_golden_smoke failed at " << stage << '\n';
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

bool contains_in_order(const std::string& text,
                       const std::vector<std::string_view>& tokens) {
  std::size_t offset = 0;
  for (const auto token : tokens) {
    const auto pos = text.find(token, offset);
    if (pos == std::string::npos) {
      std::cerr << "missing ordered token: " << token << '\n';
      return false;
    }
    offset = pos + token.size();
  }
  return true;
}

std::string case_text(const std::string& text, std::string_view case_name) {
  const auto name_token = std::string{"\"name\": \""} + std::string(case_name) + "\"";
  const auto begin = text.find(name_token);
  if (begin == std::string::npos) {
    return {};
  }
  const auto next = text.find("\"name\": \"", begin + name_token.size());
  return text.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

bool check_fixture_constants(const std::string& constants) {
  return contains_all(constants,
                      {
                          "\"CM_OPENGUILDDLG\": 1035",
                          "\"CM_GUILDMEMBERLIST\": 1037",
                          "\"CM_GUILDADDMEMBER\": 1038",
                          "\"CM_GUILDDELMEMBER\": 1039",
                          "\"CM_GUILDMAKEALLY\": 1044",
                          "\"SM_GUILDMESSAGE\": 104",
                          "\"SM_OPENGUILDDLG\": 753",
                          "\"SM_SENDGUILDMEMBERLIST\": 756",
                          "\"SM_GUILDADDMEMBER_OK\": 757",
                          "\"SM_BUILDGUILD_OK\": 762",
                          "\"SM_GUILDMAKEALLY_OK\": 768",
                          "\"guild_state\": 554",
                      });
}

bool check_fixture_sequences(const std::string& sequences) {
  const auto add_member = case_text(sequences, "add_member_success_then_client_refresh");
  const auto guild_chat = case_text(sequences, "guild_chat");
  const auto make_ally = case_text(sequences, "make_ally_success");
  const auto guild_war = case_text(sequences, "declare_guild_war_success");
  const auto castle_start = case_text(sequences, "castle_war_start_at_20");
  const auto castle_owner = case_text(sequences, "castle_owner_change_from_player_run");
  if (add_member.empty() || guild_chat.empty() || make_ally.empty() ||
      guild_war.empty() || castle_start.empty() || castle_owner.empty()) {
    return false;
  }

  return contains_in_order(add_member,
                           {
                               "CM_GUILDADDMEMBER",
                               "AddMember",
                               "MemberLogin",
                               "SM_GUILDADDMEMBER_OK",
                               "CM_GUILDMEMBERLIST",
                               "SM_SENDGUILDMEMBERLIST",
                           }) &&
         contains_in_order(guild_chat,
                           {
                               "normal chat text",
                               "GuildMsg",
                               "SM_GUILDMESSAGE",
                               "ISM_GUILDMSG",
                           }) &&
         contains_in_order(make_ally,
                           {
                               "CM_GUILDMAKEALLY",
                               "MakeAllyGuild",
                               "GuildMsg",
                               "MemberNameChanged",
                               "SM_GUILDMAKEALLY_OK",
                           }) &&
         contains_in_order(guild_war,
                           {
                               "DecGold/GoldChanged",
                               "requester_guild",
                               "target_guild",
                               "ISM_RELOADGUILD",
                           }) &&
         contains_in_order(castle_start,
                           {
                               "Move attackers into RushGuildList",
                               "Add OwnerGuild to RushGuildList",
                               "StartCastleWar/UserNameChanged",
                               "SaveAttackerList",
                               "ISM_RELOADCASTLEINFO",
                               "SM_SYSMESSAGE",
                               "ActivateMainDoor(true)",
                           }) &&
         contains_in_order(castle_owner,
                           {
                               "CheckCastleWarWinCondition",
                               "ChangeCastleOwner",
                               "SaveToFile",
                               "MemberNameChanged",
                               "SM_SYSMESSAGE",
                               "ISM_CHANGECASTLEOWNER",
                               "FinishCastleWar",
                           });
}

bool check_guild_manager_alignment() {
  mir2::GuildManager manager;
  auto* guild = manager.create_guild("TraceGuild", "Lord", "Guild Lord", 100);
  if (guild == nullptr ||
      manager.add_member_by_lord("TraceGuild", "Lord", "Member", {true, true, true}) !=
          mir2::GuildMemberOpResult::ok) {
    return false;
  }
  const auto* found = manager.find_guild("TraceGuild");
  if (found == nullptr || found->ranks().size() != 2 ||
      found->ranks()[0].rank != mir2::kGuildLordRank ||
      found->ranks()[1].rank != mir2::kGuildDefaultRank) {
    return false;
  }

  auto* red = manager.create_guild("RedGuild", "RedLord");
  auto* blue = manager.create_guild("BlueGuild", "BlueLord");
  if (red == nullptr || blue == nullptr ||
      manager.declare_guild_war("RedGuild", "RedLord", "BlueGuild", 1000) !=
          mir2::GuildRelationOpResult::ok) {
    return false;
  }
  red = manager.find_guild("RedGuild");
  blue = manager.find_guild("BlueGuild");
  return red != nullptr && blue != nullptr && red->is_hostile_guild("BlueGuild") &&
         blue->is_hostile_guild("RedGuild") && red->hostile_guilds()[0].remain_ms ==
             3ULL * 60ULL * 60ULL * 1000ULL;
}

bool check_castle_manager_alignment() {
  mir2::CastleManager castle("Sabuk");
  castle.set_owner("OwnerGuild", "OwnerLord");
  if (castle.propose_castle_war("AttackGuild", true, true, 10) !=
      mir2::CastleWarOpResult::ok) {
    return false;
  }

  std::vector<mir2::CastleWarEvent> events;
  if (castle.start_due_war(14, 20ULL * 60ULL * 60ULL * 1000ULL, events) !=
      mir2::CastleWarOpResult::ok) {
    return false;
  }
  if (castle.rush_guilds().size() != 2 || castle.rush_guilds()[0] != "AttackGuild" ||
      castle.rush_guilds()[1] != "OwnerGuild" || events.size() != 1 ||
      events[0].type != mir2::CastleWarEventType::start) {
    return false;
  }

  if (castle.try_occupy("AttackGuild",
                        {mir2::CastleCoreOccupant{"AttackGuild", true}},
                        20ULL * 60ULL * 60ULL * 1000ULL +
                            mir2::CastleManager::kOccupationDelayMs,
                        events) != mir2::CastleWarOpResult::ok) {
    return false;
  }
  return castle.owner_guild() == "AttackGuild" && events.size() == 2 &&
         events[1].type == mir2::CastleWarEventType::owner_changed;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto fixture_root = source_root / "tests" / "golden" / "guild_phase1";
  const auto constants = read_text(fixture_root / "guild_protocol_constants.json");
  const auto sequences = read_text(fixture_root / "guild_sequence_cases.json");

  if (!check_fixture_constants(constants)) {
    return fail("fixture constants");
  }
  if (!check_fixture_sequences(sequences)) {
    return fail("fixture sequences");
  }
  if (!check_guild_manager_alignment()) {
    return fail("guild manager alignment");
  }
  if (!check_castle_manager_alignment()) {
    return fail("castle manager alignment");
  }
  return 0;
}
