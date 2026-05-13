#include <cassert>
#include <string_view>

#include "world/guild_manager.hpp"

int main() {
  mir2::GuildManager manager;

  auto* guild = manager.create_guild(" DragonSlayers ", "GuildLord", "Master", 100);
  assert(guild != nullptr);
  assert(guild->name() == "DragonSlayers");
  assert(guild->member_count() == 1);
  assert(guild->lord_name().has_value());
  assert(*guild->lord_name() == std::string_view{"GuildLord"});
  assert(guild->is_lord("guildlord"));

  assert(manager.create_guild("dragonslayers", "OtherLord") == nullptr);
  assert(manager.create_guild("PhoenixHall", "guildlord") == nullptr);

  assert(guild->add_member("Ally", "Member", 200));
  assert(!guild->add_member("ally", "Member", 201));
  assert(guild->member_count() == 2);
  assert(manager.find_guild_by_member("ALLY") == guild);

  const auto& ranks = guild->ranks();
  assert(ranks.size() == 2);
  assert(ranks[0].rank == mir2::kGuildLordRank);
  assert(ranks[0].members[0].online_actor_id == 100);
  assert(ranks[1].rank == mir2::kGuildDefaultRank);
  assert(ranks[1].members[0].name == "Ally");
  assert(ranks[1].members[0].online_actor_id == 200);

  assert(guild->clear_member_online_actor("Ally", 200));
  assert(guild->find_member("Ally")->online_actor_id == 0);
  assert(!guild->clear_member_online_actor("Ally", 200));
  assert(guild->set_member_online_actor("Ally", 300));
  assert(guild->find_member("ally")->online_actor_id == 300);

  guild->set_notice_lines({"Line one", "Line two"});
  assert(guild->notice_lines().size() == 2);

  assert(guild->remove_member("Ally"));
  assert(guild->member_count() == 1);
  assert(guild->ranks().size() == 1);
  assert(!guild->remove_member("Missing"));

  assert(manager.erase_guild("DRAGONSLAYERS"));
  assert(manager.guilds().empty());
  assert(!manager.erase_guild("DRAGONSLAYERS"));

  auto* phoenix = manager.create_guild("PhoenixHall", "Percival", "Master", 400);
  assert(phoenix != nullptr);
  mir2::GuildAddMemberContext add_context;
  add_context.target_actor_id = 500;
  assert(manager.add_member_by_lord("PhoenixHall", "Guest", "Gawain", add_context) ==
         mir2::GuildMemberOpResult::requester_not_lord);
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Gawain", add_context) ==
         mir2::GuildMemberOpResult::ok);
  assert(manager.find_guild("PhoenixHall")->find_member("Gawain")->online_actor_id == 500);
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Gawain", add_context) ==
         mir2::GuildMemberOpResult::already_member);

  auto* azure = manager.create_guild("AzureSky", "Tristan");
  assert(azure != nullptr);
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Tristan", add_context) ==
         mir2::GuildMemberOpResult::target_in_other_guild);

  add_context.target_online = false;
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Offline", add_context) ==
         mir2::GuildMemberOpResult::target_not_online);
  add_context.target_online = true;
  add_context.target_facing_requester = false;
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "TurnedAway", add_context) ==
         mir2::GuildMemberOpResult::target_not_facing_requester);
  add_context.target_facing_requester = true;
  add_context.target_allows_guild = false;
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Rejects", add_context) ==
         mir2::GuildMemberOpResult::target_rejects_guild);
  add_context.target_allows_guild = true;
  add_context.max_member_count = 2;
  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Full", add_context) ==
         mir2::GuildMemberOpResult::member_limit_reached);

  assert(manager.leave_member("Percival") == mir2::GuildMemberOpResult::lord_cannot_leave);
  assert(manager.leave_member("Gawain") == mir2::GuildMemberOpResult::ok);
  assert(!manager.find_guild("PhoenixHall")->has_member("Gawain"));
  assert(manager.leave_member("Gawain") == mir2::GuildMemberOpResult::not_member);

  assert(manager.add_member_by_lord("PhoenixHall", "Percival", "Bors") ==
         mir2::GuildMemberOpResult::ok);
  assert(manager.transfer_lord("PhoenixHall", "Bors", "Percival") ==
         mir2::GuildMemberOpResult::requester_not_lord);
  assert(manager.transfer_lord("PhoenixHall", "Percival", "Bors") ==
         mir2::GuildMemberOpResult::ok);
  assert(manager.find_guild("PhoenixHall")->is_lord("Bors"));
  assert(!manager.find_guild("PhoenixHall")->is_lord("Percival"));

  assert(manager.remove_member_by_lord("PhoenixHall", "Percival", "Bors") ==
         mir2::GuildMemberOpResult::requester_not_lord);
  assert(manager.remove_member_by_lord("PhoenixHall", "Bors", "Percival") ==
         mir2::GuildMemberOpResult::ok);
  assert(!manager.find_guild("PhoenixHall")->has_member("Percival"));
  assert(manager.remove_member_by_lord("PhoenixHall", "Bors", "Bors") ==
         mir2::GuildMemberOpResult::ok);
  assert(manager.find_guild("PhoenixHall") == nullptr);

  return 0;
}
