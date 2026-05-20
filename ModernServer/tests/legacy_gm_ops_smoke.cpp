#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_gm_ops_smoke failed at " << stage << '\n';
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

mir2::CharacterRecord make_character(std::string account_id, std::string name,
                                     std::string map_id, std::int32_t x,
                                     std::int32_t y) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account_id);
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = x;
  character.y = y;
  character.dir = 2;
  character.ability.level = 20;
  character.ability.hp = 20;
  character.ability.max_hp = 20;
  character.ability.mp = 20;
  character.ability.max_mp = 20;
  return character;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
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

bool has_audit(const mir2::RuntimeDispatch& dispatch, std::string_view category,
               std::string_view needle) {
  for (const auto& audit : dispatch.audit_events) {
    if (audit.category == category &&
        audit.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool has_force_disconnect(const mir2::RuntimeDispatch& dispatch,
                          std::uint64_t session_id) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id == session_id &&
        event.kind == mir2::SessionEventKind::force_disconnect) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime make_runtime(const std::filesystem::path& admin_list) {
  mir2::HostConfig config;
  config.runtime.legacy_admin_list = admin_list;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  config.maps.push_back(mir2::MapConfig{"1", "Field", {}, 100, 100, 15, 15});

  mir2::MonsterDefConfig hen;
  hen.name = "Hen";
  hen.level = 1;
  config.monsters.push_back(hen);

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

bool check_queries_and_modes(const std::filesystem::path& admin_list) {
  auto runtime = make_runtime(admin_list);
  enter(runtime, 1, make_character("acct_sys", "SysHero", "0", 10, 10));
  enter(runtime, 2, make_character("acct_bob", "Bob", "0", 12, 10));
  enter(runtime, 3, make_character("acct_charlie", "Charlie", "1", 20, 20));

  const auto gm_mode = say(runtime, 1, "@GameMaster", 1000);
  const auto observer_mode = say(runtime, 1, "@Ob", 1020);
  const auto superman_mode = say(runtime, 1, "@Superman", 1040);
  const auto map = say(runtime, 1, "@Map", 1060);
  const auto human = say(runtime, 1, "@Human 0", 1080);
  const auto info = say(runtime, 1, "@Info Bob", 1100);
  const auto mob_level = say(runtime, 1, "@MobLevel", 1120);
  const auto mob_count = say(runtime, 1, "@MobCount 0", 1140);

  return has_text(gm_mode, 1, "进入管理员模式") &&
         has_audit(gm_mode, "gm.command.ok", "cmd=GameMaster") &&
         has_text(observer_mode, 1, "进入观察模式") &&
         has_text(superman_mode, 1, "进入无敌模式") &&
         has_text(map, 1, "地图: 0") &&
         has_text(human, 1, "地图: 0当前人数=2") &&
         has_text(info, 1, "Bob Lv.20 0 12 10") &&
         has_text(mob_level, 1, "Hen 1") &&
         has_text(mob_count, 1, "地图: 0当前怪物=0");
}

bool check_move_recall_and_kick(const std::filesystem::path& admin_list) {
  auto runtime = make_runtime(admin_list);
  enter(runtime, 1, make_character("acct_sys", "SysHero", "0", 10, 10));
  enter(runtime, 2, make_character("acct_bob", "Bob", "0", 12, 10));

  const auto move = say(runtime, 1, "@Move 1", 1000);
  const auto moved = runtime.snapshot_character_actor("SysHero");
  if (!has_audit(move, "gm.command.ok", "cmd=Move") || !moved.has_value() ||
      moved->map_id != "1") {
    return false;
  }

  const auto pmove = say(runtime, 1, "@PMove 0 30 30", 1300);
  const auto positioned = runtime.snapshot_character_actor("SysHero");
  if (!has_audit(pmove, "gm.command.ok", "cmd=PositionMove") ||
      !positioned.has_value() || positioned->map_id != "0" ||
      positioned->x != 30 || positioned->y != 30) {
    return false;
  }

  const auto recall = say(runtime, 1, "@Recall Bob", 1600);
  const auto recalled = runtime.snapshot_character_actor("Bob");
  if (!has_audit(recall, "gm.command.ok", "cmd=Recall") ||
      !recalled.has_value() || recalled->map_id != "0" ||
      recalled->x != 31 || recalled->y != 30) {
    return false;
  }

  const auto kick = say(runtime, 1, "@Kick Bob", 1900);
  return has_audit(kick, "gm.command.ok", "cmd=Kick") &&
         has_force_disconnect(kick, 2) &&
         !runtime.snapshot_character_actor("Bob").has_value();
}

bool check_ting(const std::filesystem::path& admin_list) {
  auto runtime = make_runtime(admin_list);
  enter(runtime, 1, make_character("acct_sys", "SysHero", "0", 10, 10));
  enter(runtime, 2, make_character("acct_charlie", "Charlie", "1", 20, 20));
  enter(runtime, 3, make_character("acct_dave", "Dave", "1", 30, 30));
  enter(runtime, 4, make_character("acct_eve", "Eve", "1", 31, 30));
  enter(runtime, 5, make_character("acct_far", "Far", "1", 50, 50));

  const auto ting = say(runtime, 1, "@Ting Charlie", 1000);
  const auto charlie = runtime.snapshot_character_actor("Charlie");
  if (!has_audit(ting, "gm.command.ok", "cmd=Ting") ||
      !charlie.has_value() || charlie->map_id != "0") {
    return false;
  }

  const auto super_ting = say(runtime, 1, "@SuperTing Dave 2", 1300);
  const auto dave = runtime.snapshot_character_actor("Dave");
  const auto eve = runtime.snapshot_character_actor("Eve");
  const auto far = runtime.snapshot_character_actor("Far");
  return has_audit(super_ting, "gm.command.ok", "cmd=SuperTing") &&
         dave.has_value() && dave->map_id == "0" &&
         eve.has_value() && eve->map_id == "0" &&
         far.has_value() && far->map_id == "1";
}

bool check_denied_and_failed(const std::filesystem::path& admin_list) {
  auto runtime = make_runtime(admin_list);
  enter(runtime, 1, make_character("acct_normal", "Normal", "0", 10, 10));
  enter(runtime, 2, make_character("acct_sys", "SysHero", "0", 12, 10));

  const auto denied = say(runtime, 1, "@Move 1", 1000);
  const auto failed = say(runtime, 2, "@Info Nobody", 1200);

  return has_audit(denied, "gm.command.denied", "cmd=Move") &&
         !has_text(denied, 1, "Normal: @Move 1") &&
         has_audit(failed, "gm.command.failed", "cmd=Info") &&
         !has_text(failed, 2, "Nobody Lv.");
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_ops_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);
  const auto admin_list = temp_root / "AdminList.txt";
  {
    std::ofstream output(admin_list);
    output << "1SysHero\n";
  }

  if (!check_queries_and_modes(admin_list)) {
    return fail("queries and modes");
  }
  if (!check_move_recall_and_kick(admin_list)) {
    return fail("move recall kick");
  }
  if (!check_ting(admin_list)) {
    return fail("ting");
  }
  if (!check_denied_and_failed(admin_list)) {
    return fail("denied and failed");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
