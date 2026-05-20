#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/legacy_gm_commands.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_gm_command_registry_smoke failed at " << stage << '\n';
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

mir2::CharacterRecord make_character(std::string account_id, std::string name) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account_id);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = 10;
  character.y = 10;
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

bool check_registry_definitions() {
  const auto* pmove = mir2::find_legacy_gm_command("pMove");
  const auto* observer = mir2::find_legacy_gm_command("Ob");
  const auto* transparency = mir2::find_legacy_gm_command("tp");
  const auto* make = mir2::find_legacy_gm_command("Make");
  const auto* reload_guild_all = mir2::find_legacy_gm_command("ReloadGuildAll");
  const auto* shutup = mir2::find_legacy_gm_command("Shutup");
  const auto* gbk_refuse = mir2::find_legacy_gm_command(
      std::string_view("\xBE\xDC\xBE\xF8\xCB\xBD\xC1\xC4", 8));

  return mir2::legacy_gm_command_definitions().size() >= 81 &&
         pmove != nullptr && pmove->canonical_name == "PositionMove" &&
         pmove->minimum_degree == mir2::LegacyUserDegree::sysop &&
         observer != nullptr && observer->canonical_name == "Observer" &&
         transparency != nullptr && transparency->canonical_name == "Transparency" &&
         make != nullptr && make->minimum_degree == mir2::LegacyUserDegree::superadmin &&
         make->implementation == mir2::LegacyGmCommandImplementation::implemented &&
         reload_guild_all != nullptr &&
         reload_guild_all->implementation == mir2::LegacyGmCommandImplementation::pending &&
         shutup != nullptr &&
         shutup->implementation == mir2::LegacyGmCommandImplementation::implemented &&
         gbk_refuse != nullptr && gbk_refuse->canonical_name == "拒绝私聊" &&
         mir2::find_legacy_gm_command("NoSuchCmd") == nullptr;
}

bool check_admin_list_loader(const std::filesystem::path& temp_root) {
  const auto path = temp_root / "AdminList.txt";
  {
    std::ofstream output(path);
    output << ";comment\n";
    output << "*AdminHero\n";
    output << "1SysHero\n";
    output << "2WatchHero\n";
    output << "3SuperHero\n";
    output << "PlainHero\n";
  }
  const auto entries = mir2::load_legacy_admin_list(path);
  return entries.size() == 4 &&
         entries.at("adminhero") == mir2::LegacyUserDegree::admin &&
         entries.at("syshero") == mir2::LegacyUserDegree::sysop &&
         entries.at("watchhero") == mir2::LegacyUserDegree::observer &&
         entries.at("superhero") == mir2::LegacyUserDegree::superadmin;
}

bool check_runtime_dispatch(const std::filesystem::path& admin_list) {
  mir2::HostConfig config;
  config.runtime.legacy_admin_list = admin_list;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  enter(runtime, 1, make_character("acct_normal", "Normal"));
  enter(runtime, 2, make_character("acct_admin", "AdminHero"));
  enter(runtime, 3, make_character("acct_watch", "WatchHero"));
  enter(runtime, 4, make_character("acct_bob", "Bob"));

  const auto denied = say(runtime, 1, "@Shutup Bob", 1500);
  const auto pending = say(runtime, 2, "@ReloadGuildAll", 1700);
  const auto ok = say(runtime, 2, "@Shutup Bob", 1900);
  const auto broadcast = say(runtime, 3, "@!notice", 2100);
  const auto unknown = say(runtime, 1, "@NoSuchCmd", 2300);

  return has_audit(denied, "gm.command.denied", "cmd=Shutup") &&
         !has_text(denied, 1, "Bob禁止聊天 + 5分钟") &&
         has_audit(pending, "gm.command.pending", "cmd=ReloadGuildAll") &&
         !has_text(pending, 2, "AdminHero: @ReloadGuildAll") &&
         has_audit(ok, "gm.command.ok", "cmd=Shutup") &&
         has_text(ok, 2, "Bob禁止聊天 + 5分钟") &&
         has_audit(broadcast, "gm.command.ok", "cmd=!") &&
         has_text(broadcast, 1, "(公告)notice") &&
         unknown.audit_events.empty() &&
         !has_text(unknown, 1, "Normal: @NoSuchCmd");
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_command_registry_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  if (!check_registry_definitions()) {
    return fail("registry definitions");
  }
  if (!check_admin_list_loader(temp_root)) {
    return fail("admin list loader");
  }
  if (!check_runtime_dispatch(temp_root / "AdminList.txt")) {
    return fail("runtime dispatch");
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
