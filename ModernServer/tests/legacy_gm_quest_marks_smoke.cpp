#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_gm_quest_marks_smoke failed at " << stage << '\n';
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

bool has_text(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
              std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && mir2::legacy_decode_string(decoded->body) == text) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  const auto admin_list =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_quest_marks_admin.txt";
  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
  }
  config.runtime.legacy_admin_list = admin_list;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, character("superadmin", "Admin"));
  enter(runtime, 2, character("acct_bob", "Bob"));

  const auto set_flag = say(runtime, 1, "@setflag Bob 1 1", 1000);
  auto bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->quest_marks[0] != 0x80 ||
      !has_audit(set_flag, "gm.command.ok", "cmd=setflag")) {
    return fail("setflag");
  }

  const auto set_open = say(runtime, 1, "@setopen Bob 8 1", 1200);
  bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->quest_open_units[0] != 0x01 ||
      !has_audit(set_open, "gm.command.ok", "cmd=setopen")) {
    return fail("setopen");
  }

  const auto set_unit = say(runtime, 1, "@setunit Bob 9 1", 1400);
  bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->quest_units[1] != 0x80 ||
      !has_audit(set_unit, "gm.command.ok", "cmd=setunit")) {
    return fail("setunit");
  }

  const auto show_flag = say(runtime, 1, "@flag Bob 1", 1600);
  const auto show_open = say(runtime, 1, "@showopen Bob 8", 1800);
  const auto show_unit = say(runtime, 1, "@showunit Bob 9", 2000);
  if (!has_text(show_flag, 1, "Bob: flag[1]=ON") ||
      !has_text(show_open, 1, "Bob: open[8]=ON") ||
      !has_text(show_unit, 1, "Bob: unit[9]=ON")) {
    return fail("show marks");
  }

  const auto bad_index = say(runtime, 1, "@setflag Bob 0 1", 2200);
  if (!has_audit(bad_index, "gm.command.failed", "reason=bad_args")) {
    return fail("bad index");
  }

  return 0;
}
