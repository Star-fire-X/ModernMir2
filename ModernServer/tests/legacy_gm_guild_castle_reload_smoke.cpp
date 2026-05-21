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
  std::cerr << "legacy_gm_guild_castle_reload_smoke failed at " << stage << '\n';
  return 1;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
}

mir2::CharacterRecord character(std::string account, std::string name) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
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

bool has_persist(const mir2::RuntimeDispatch& dispatch, mir2::PersistRequestKind kind) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == kind) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime make_runtime(const std::filesystem::path& admin_list) {
  mir2::HostConfig config;
  config.runtime.legacy_admin_list = admin_list;
  config.runtime.castle_name = "Sabuk";
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_guild_castle_reload_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);
  const auto admin_list = temp_root / "AdminList.txt";
  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
  }

  auto runtime = make_runtime(admin_list);
  enter(runtime, 1, character("superadmin", "Admin"));
  enter(runtime, 2, character("acct_bob", "Bob"));
  enter(runtime, 3, character("acct_new", "NewSys"));

  const auto add = say(runtime, 1, "@AddGuild TestGuild Bob", 1000);
  auto bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->guild_name != "TestGuild" ||
      bob->guild_title != "Lord" ||
      !has_audit(add, "gm.command.ok", "guild_created") ||
      !has_persist(add, mir2::PersistRequestKind::save_guild_state)) {
    return fail("add guild");
  }

  const auto castle = say(runtime, 1, "@ChangeSabukLord TestGuild", 1200);
  if (!has_audit(castle, "gm.command.ok", "castle_lord_changed") ||
      !has_persist(castle, mir2::PersistRequestKind::save_castle_state)) {
    return fail("change sabuk lord");
  }

  const auto wall_gold = say(runtime, 1, "@SabukWallGold", 1400);
  if (!has_audit(wall_gold, "gm.command.ok", "cmd=SabukWallGold")) {
    return fail("sabuk wall gold");
  }

  const auto del = say(runtime, 1, "@DelGuild TestGuild", 1600);
  bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || !bob->guild_name.empty() ||
      !has_audit(del, "gm.command.ok", "guild_deleted") ||
      !has_persist(del, mir2::PersistRequestKind::delete_guild)) {
    return fail("del guild");
  }

  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
    output << "1NewSys\n";
  }
  const auto denied_before_reload = say(runtime, 3, "@Move 0", 1750);
  if (!has_audit(denied_before_reload, "gm.command.denied", "cmd=Move")) {
    return fail("admin denied before reload");
  }
  const auto reloaded = say(runtime, 1, "@ReloadAdmin", 1800);
  if (!has_audit(reloaded, "gm.command.ok", "cmd=ReloadAdmin")) {
    return fail("reload admin");
  }
  const auto move = say(runtime, 3, "@Move 0", 2000);
  if (!has_audit(move, "gm.command.ok", "cmd=Move")) {
    return fail("reloaded admin degree");
  }

  const auto pending = say(runtime, 1, "@StartContest", 2200);
  if (!has_audit(pending, "gm.command.pending", "cmd=StartContest")) {
    return fail("pending contest");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
