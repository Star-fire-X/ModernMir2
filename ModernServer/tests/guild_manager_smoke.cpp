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

  return 0;
}
