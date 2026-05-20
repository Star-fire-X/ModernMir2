#include "world/legacy_gm_commands.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "util/string_utils.hpp"

namespace mir2 {
namespace {

std::string legacy_command_key(std::string_view value) {
  return util::lower_copy(std::string(value));
}

bool is_ascii(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](const char ch) {
    return static_cast<unsigned char>(ch) < 0x80;
  });
}

bool ascii_equals_ci(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

bool alias_matches(std::string_view command, std::string_view alias) {
  if (is_ascii(command) && is_ascii(alias)) {
    return ascii_equals_ci(command, alias);
  }
  return command == alias;
}

LegacyGmCommandDefinition command(std::string canonical_name,
                                  LegacyUserDegree minimum_degree,
                                  LegacyGmCommandImplementation implementation,
                                  std::string dependency,
                                  std::vector<std::string> aliases = {}) {
  aliases.push_back(canonical_name);
  return LegacyGmCommandDefinition{std::move(canonical_name), std::move(aliases),
                                   minimum_degree, implementation,
                                   std::move(dependency)};
}

}  // namespace

const std::vector<LegacyGmCommandDefinition>& legacy_gm_command_definitions() {
  static const auto definitions = std::vector<LegacyGmCommandDefinition>{
      command("拒绝私聊", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xCB\xBD\xC1\xC4", 8)}),
      command("允许私聊", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xCB\xBD\xC1\xC4", 8)}),
      command("拒绝", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8", 4)}),
      command("拒绝喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xBA\xB0\xBB\xB0", 8)}),
      command("允许喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xBA\xB0\xBB\xB0", 8)}),
      command("允许行会喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xD4\xCA\xD0\xED\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12)}),
      command("拒绝行会喊话", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::implemented, "chat",
              {std::string("\xBE\xDC\xBE\xF8\xD0\xD0\xBB\xE1\xBA\xB0\xBB\xB0", 12)}),
      command("H", LegacyUserDegree::normal, LegacyGmCommandImplementation::pending,
              "help", {"HELP"}),
      command("AttackMode", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::pending, "combat"),
      command("Rest", LegacyUserDegree::normal, LegacyGmCommandImplementation::pending,
              "pet"),
      command("Searching", LegacyUserDegree::normal,
              LegacyGmCommandImplementation::pending, "player_search"),

      command("!", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),
      command("$", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),
      command("#", LegacyUserDegree::observer,
              LegacyGmCommandImplementation::implemented, "chat_broadcast"),

      command("Move", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "map"),
      command("PositionMove", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "map", {"PMove"}),
      command("Info", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_query"),
      command("MobLevel", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "monster_query"),
      command("MobCount", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "monster_query"),
      command("Human", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_query"),
      command("Map", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "map"),
      command("Kick", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "session"),
      command("Ting", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "session"),
      command("SuperTing", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "session"),
      command("Shutup", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("ReleaseShutup", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("ShutupList", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "chat"),
      command("GameMaster", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode"),
      command("Observer", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode", {"Ob"}),
      command("Superman", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "gm_mode"),
      command("Level", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "player_state"),
      command("SabukWallGold", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "castle"),
      command("Recall", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "map"),
      command("flag", LegacyUserDegree::sysop, LegacyGmCommandImplementation::implemented,
              "quest"),
      command("showopen", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("showunit", LegacyUserDegree::sysop,
              LegacyGmCommandImplementation::implemented, "quest"),

      command("Attack", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "combat"),
      command("Mob", LegacyUserDegree::admin, LegacyGmCommandImplementation::implemented,
              "monster_spawn"),
      command("RecallMob", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "monster_spawn"),
      command("LuckyPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("彩票查询", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "lottery"),
      command("ReloadGuild", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild"),
      command("ReloadGuildAll", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild"),
      command("ReloadLineNotice", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "notice"),
      command("ReadAbuseInformation", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "chat_filter"),
      command("Backstep", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "movement"),
      command("EnergyWave", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "combat"),
      command("FreePenalty", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("PKpoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("IncPkPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("ChangeLuck", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Hunger", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "player_state"),
      command("hair", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Training", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("DeleteSkill", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("ChangeJob", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("ChangeGender", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("NameColor", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("Mission", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("MobPlace", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "monster_spawn"),
      command("Transparency", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "gm_mode", {"tp"}),
      command("DeleteItem", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "item"),
      command("Level0", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("setflag", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("setopen", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("setunit", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::implemented, "quest"),
      command("Reconnection", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "session"),
      command("DisableFilter", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "chat_filter"),
      command("CHGUSERFULL", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "runtime_config"),
      command("CHGZENFASTSTEP", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "runtime_config"),
      command("ContestPoint", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("StartContest", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("EndContest", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("Announcement", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "guild_contest"),
      command("OXQuizRoom", LegacyUserDegree::admin,
              LegacyGmCommandImplementation::pending, "quiz"),

      command("Make", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "item"),
      command("DelGold", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("AddGold", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("Test_GOLD_Change", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "gold"),
      command("WeaponRefinery", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "weapon"),
      command("ReloadAdmin", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "admin"),
      command("ReloadNpc", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "npc"),
      command("ReloadMonItems", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "monster_drop"),
      command("ReloadDiary", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "quest"),
      command("AdjustLevel", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("AdjustExp", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("AddGuild", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "guild"),
      command("DelGuild", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "guild"),
      command("ChangeSabukLord", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "castle"),
      command("ForcedWallconquestWar", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "castle"),
      command("AddToItemEvent", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("AddToItemEventAsPieces", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("ItemEventList", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("StartingGiftNo", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("DeleteAllItemEven", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("StartItemEvent", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("ItemEventTerm", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::pending, "item_event"),
      command("AdjustTestLevel", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "player_state"),
      command("OPTraining", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("OPDeleteSkill", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "skill"),
      command("ChangeWeaponDura", LegacyUserDegree::superadmin,
              LegacyGmCommandImplementation::implemented, "weapon")};
  return definitions;
}

const LegacyGmCommandDefinition* find_legacy_gm_command(std::string_view command_name) {
  for (const auto& definition : legacy_gm_command_definitions()) {
    for (const auto& alias : definition.aliases) {
      if (alias_matches(command_name, alias)) {
        return &definition;
      }
    }
  }
  return nullptr;
}

std::unordered_map<std::string, LegacyUserDegree> load_legacy_admin_list(
    const std::filesystem::path& path) {
  std::unordered_map<std::string, LegacyUserDegree> result;
  std::ifstream input(path);
  if (!input.is_open()) {
    return result;
  }

  std::string line;
  while (std::getline(input, line)) {
    line = util::trim(line);
    if (line.empty() || line.front() == ';') {
      continue;
    }
    LegacyUserDegree degree = LegacyUserDegree::normal;
    if (line.front() == '3') {
      degree = LegacyUserDegree::superadmin;
    } else if (line.front() == '*') {
      degree = LegacyUserDegree::admin;
    } else if (line.front() == '1') {
      degree = LegacyUserDegree::sysop;
    } else if (line.front() == '2') {
      degree = LegacyUserDegree::observer;
    } else {
      continue;
    }
    auto name = util::trim(line.substr(1));
    if (!name.empty()) {
      result[legacy_command_key(name)] = degree;
    }
  }
  return result;
}

LegacyUserDegree legacy_heuristic_user_degree(std::string_view account_id) {
  const auto lowered = legacy_command_key(account_id);
  if (lowered == "guest" || lowered == "admin" || util::starts_with(lowered, "gm")) {
    return LegacyUserDegree::admin;
  }
  return LegacyUserDegree::normal;
}

bool legacy_user_degree_at_least(LegacyUserDegree actual, LegacyUserDegree required) {
  return static_cast<std::uint8_t>(actual) >= static_cast<std::uint8_t>(required);
}

std::string_view legacy_user_degree_name(LegacyUserDegree degree) {
  switch (degree) {
    case LegacyUserDegree::normal:
      return "normal";
    case LegacyUserDegree::observer:
      return "observer";
    case LegacyUserDegree::sysop:
      return "sysop";
    case LegacyUserDegree::admin:
      return "admin";
    case LegacyUserDegree::superadmin:
      return "superadmin";
  }
  return "normal";
}

}  // namespace mir2
