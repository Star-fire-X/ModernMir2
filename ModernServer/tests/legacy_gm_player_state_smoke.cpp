#include <cassert>
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
  std::cerr << "legacy_gm_player_state_smoke failed at " << stage << '\n';
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

mir2::CharacterRecord character(std::string account, std::string name,
                                std::int32_t x = 10) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = 10;
  record.ability.level = 20;
  record.ability.hp = 20;
  record.ability.max_hp = 20;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  record.pk_point = 300;
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

bool has_persist_character(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == name) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime runtime() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  const auto admin_list =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_player_state_admin.txt";
  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
  }
  config.runtime.legacy_admin_list = admin_list;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  mir2::LogicRuntime result(config);
  result.initialize();
  return result;
}

}  // namespace

int main() {
  auto rt = runtime();
  enter(rt, 1, character("superadmin", "Admin"));
  enter(rt, 2, character("acct_bob", "Bob", 12));

  const auto level = say(rt, 1, "@Level 35", 1000);
  auto admin = rt.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->ability.level != 35 ||
      !has_audit(level, "gm.command.ok", "cmd=Level") ||
      !has_persist_character(level, "Admin")) {
    return fail("level");
  }

  const auto adjust = say(rt, 1, "@AdjustLevel Bob 30", 1200);
  auto bob = rt.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->ability.level != 30 ||
      !has_audit(adjust, "gm.command.ok", "cmd=AdjustLevel") ||
      !has_persist_character(adjust, "Bob")) {
    return fail("adjust level");
  }

  const auto exp = say(rt, 1, "@AdjustExp Bob 12345", 1400);
  bob = rt.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->ability.exp != 12345 ||
      !has_audit(exp, "gm.command.ok", "cmd=AdjustExp")) {
    return fail("adjust exp");
  }

  const auto pk = say(rt, 1, "@FreePenalty Bob", 1600);
  bob = rt.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->pk_point != 0 ||
      !has_audit(pk, "gm.command.ok", "cmd=FreePenalty")) {
    return fail("free penalty");
  }

  const auto luck = say(rt, 1, "@ChangeLuck 5000", 1800);
  admin = rt.snapshot_character_actor("Admin");
  if (!admin.has_value() || static_cast<int>(admin->body_luck) != 5000 ||
      !has_audit(luck, "gm.command.ok", "cmd=ChangeLuck")) {
    return fail("change luck");
  }

  const auto hair = say(rt, 1, "@hair 4", 2000);
  admin = rt.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->hair != 4 ||
      !has_audit(hair, "gm.command.ok", "cmd=hair")) {
    return fail("hair");
  }

  const auto job = say(rt, 1, "@ChangeJob wizard", 2200);
  admin = rt.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->job != 1 ||
      !has_audit(job, "gm.command.ok", "cmd=ChangeJob")) {
    return fail("change job");
  }

  const auto gender = say(rt, 1, "@ChangeGender", 2400);
  admin = rt.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->sex != 1 ||
      !has_audit(gender, "gm.command.ok", "cmd=ChangeGender")) {
    return fail("change gender");
  }

  const auto denied = say(rt, 2, "@AdjustLevel Bob 40", 2600);
  bob = rt.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->ability.level != 30 ||
      !has_audit(denied, "gm.command.denied", "cmd=AdjustLevel")) {
    return fail("denied");
  }

  const auto pending = say(rt, 1, "@Hunger 10", 2800);
  if (!has_audit(pending, "gm.command.pending", "cmd=Hunger")) {
    return fail("hunger pending");
  }

  return 0;
}
