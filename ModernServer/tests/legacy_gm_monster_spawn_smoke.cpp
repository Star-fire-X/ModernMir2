#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_gm_monster_spawn_smoke failed at " << stage << '\n';
  return 1;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
}

mir2::CharacterRecord character(std::string account, std::string name) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.dir = 2;
  record.ability.level = 20;
  record.ability.hp = 20;
  record.ability.max_hp = 20;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  return record;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = record;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

mir2::RuntimeDispatch say(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          std::string text, std::uint64_t now_ms) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  command.timestamp_ms = now_ms;
  auto dispatch = runtime.route_logic_command(command);
  append_dispatch(dispatch, runtime.tick(now_ms));
  return dispatch;
}

bool has_audit(const mir2::RuntimeDispatch& dispatch, std::string_view category,
               std::string_view needle) {
  for (const auto& audit : dispatch.audit_events) {
    if (audit.category == category && audit.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  const auto admin_list =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_monster_spawn_admin.txt";
  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
  }
  config.runtime.legacy_admin_list = admin_list;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "OtherMap", {}, 100, 100, 10, 10});
  mir2::MonsterDefConfig hen;
  hen.name = "Hen";
  hen.level = 1;
  hen.hp = 12;
  hen.dc = 3;
  config.monsters.push_back(hen);
  mir2::MonsterDefConfig boar;
  boar.name = "Red Boar King";
  boar.level = 3;
  boar.hp = 30;
  boar.dc = 5;
  config.monsters.push_back(boar);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

}  // namespace

int main() {
  auto runtime = make_runtime();
  enter(runtime, 1, character("superadmin", "Admin"));

  const auto recall = say(runtime, 1, "@RecallMob Hen 1 2", 1000);
  if (!has_audit(recall, "gm.command.ok", "cmd=RecallMob") ||
      runtime.legacy_monster_group_count() != 1) {
    return fail("recall mob");
  }

  static_cast<void>(say(runtime, 1, "@PMove 0 20 20", 1200));
  const auto mob = say(runtime, 1, "@Mob Hen 2", 1400);
  if (!has_audit(mob, "gm.command.ok", "cmd=Mob") ||
      runtime.legacy_monster_group_count() != 2) {
    return fail("mob");
  }

  static_cast<void>(say(runtime, 1, "@PMove 0 25 20", 1450));
  const auto multi_mob = say(runtime, 1, "@Mob Red Boar King 2", 1500);
  if (!has_audit(multi_mob, "gm.command.ok", "cmd=Mob") ||
      runtime.legacy_monster_group_count() != 3) {
    return fail("multi word mob");
  }

  const auto mission = say(runtime, 1, "@Mission 0 20 20", 1600);
  if (!has_audit(mission, "gm.command.ok", "mission_set")) {
    return fail("mission");
  }

  static_cast<void>(say(runtime, 1, "@PMove 1 30 30", 1700));
  const auto mob_place = say(runtime, 1, "@MobPlace 12 12 Red Boar King 1", 1800);
  if (!has_audit(mob_place, "gm.command.ok", "cmd=MobPlace") ||
      runtime.legacy_monster_group_count() != 4 ||
      !runtime.legacy_monster_snapshot("0", 7).has_value()) {
    return fail("mob place");
  }

  const auto unknown = say(runtime, 1, "@Mob MissingMonster 1", 2000);
  if (!has_audit(unknown, "gm.command.failed", "monster_spawn_failed")) {
    return fail("unknown monster");
  }

  return 0;
}
