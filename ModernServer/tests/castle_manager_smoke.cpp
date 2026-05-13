#include "world/castle_manager.hpp"

#include <cassert>
#include <string>
#include <vector>

int main() {
  mir2::CastleManager castle("Sabuk");
  castle.set_owner("OwnerGuild", "OwnerLord");

  assert(castle.propose_castle_war("OwnerGuild", true, true, 10) ==
         mir2::CastleWarOpResult::already_owner);
  assert(castle.propose_castle_war("AttackGuild", false, true, 10) ==
         mir2::CastleWarOpResult::requester_not_lord);
  assert(castle.propose_castle_war("AttackGuild", true, false, 10) ==
         mir2::CastleWarOpResult::missing_zuma_piece);
  assert(castle.propose_castle_war("AttackGuild", true, true, 10) ==
         mir2::CastleWarOpResult::ok);
  assert(castle.registrations().size() == 1);
  assert(castle.registrations()[0].guild_name == "AttackGuild");
  assert(castle.registrations()[0].attack_day == 14);
  assert(castle.propose_castle_war("AttackGuild", true, true, 10) ==
         mir2::CastleWarOpResult::already_registered);

  std::vector<mir2::CastleWarEvent> events;
  assert(castle.start_due_war(13, 1000, events) ==
         mir2::CastleWarOpResult::no_due_war);
  assert(events.empty());
  assert(castle.start_due_war(14, 2000, events) == mir2::CastleWarOpResult::ok);
  assert(castle.under_attack());
  assert(castle.castle_attack_started_ms() == 2000);
  assert(castle.rush_guilds().size() == 2);
  assert(castle.rush_guilds()[0] == "AttackGuild");
  assert(castle.rush_guilds()[1] == "OwnerGuild");
  assert(events.size() == 1);
  assert(events[0].type == mir2::CastleWarEventType::start);
  assert(events[0].guild_name == "Sabuk");

  assert(castle.try_occupy("AttackGuild", {mir2::CastleCoreOccupant{"AttackGuild", true}},
                           2000 + mir2::CastleManager::kOccupationDelayMs - 1, events) ==
         mir2::CastleWarOpResult::occupation_too_early);
  assert(castle.try_occupy("OtherGuild", {mir2::CastleCoreOccupant{"OtherGuild", true}},
                           2000 + mir2::CastleManager::kOccupationDelayMs, events) ==
         mir2::CastleWarOpResult::guild_not_registered);
  assert(castle.try_occupy("AttackGuild",
                           {mir2::CastleCoreOccupant{"AttackGuild", true},
                            mir2::CastleCoreOccupant{"OwnerGuild", true}},
                           2000 + mir2::CastleManager::kOccupationDelayMs, events) ==
         mir2::CastleWarOpResult::core_not_controlled);
  assert(castle.try_occupy("AttackGuild",
                           {mir2::CastleCoreOccupant{"AttackGuild", true},
                            mir2::CastleCoreOccupant{"OwnerGuild", false}},
                           2000 + mir2::CastleManager::kOccupationDelayMs, events) ==
         mir2::CastleWarOpResult::ok);
  assert(castle.owner_guild() == "AttackGuild");
  assert(castle.under_attack());
  assert(events.size() == 2);
  assert(events[1].type == mir2::CastleWarEventType::owner_changed);
  assert(events[1].guild_name == "AttackGuild");

  castle.run(2000 + mir2::CastleManager::kWarDurationMs -
                 mir2::CastleManager::kTimeoutWarningLeadMs,
             events);
  assert(events.size() == 3);
  assert(events[2].type == mir2::CastleWarEventType::timeout_warning);
  castle.run(2000 + mir2::CastleManager::kWarDurationMs, events);
  assert(!castle.under_attack());
  assert(castle.rush_guilds().empty());
  assert(events.size() == 4);
  assert(events[3].type == mir2::CastleWarEventType::finish);

  mir2::CastleManager unowned("Sabuk");
  assert(unowned.propose_castle_war("SoloGuild", true, true, 20) ==
         mir2::CastleWarOpResult::ok);
  events.clear();
  assert(unowned.start_due_war(24, 3000, events) == mir2::CastleWarOpResult::ok);
  assert(unowned.rush_guilds().size() == 1);
  assert(unowned.try_occupy("SoloGuild", {mir2::CastleCoreOccupant{"SoloGuild", true}},
                            3000 + mir2::CastleManager::kOccupationDelayMs, events) ==
         mir2::CastleWarOpResult::ok);
  assert(!unowned.under_attack());
  assert(events.size() == 3);
  assert(events[1].type == mir2::CastleWarEventType::owner_changed);
  assert(events[2].type == mir2::CastleWarEventType::finish);

  return 0;
}
