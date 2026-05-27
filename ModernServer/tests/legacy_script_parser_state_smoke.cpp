#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "config/config_loader.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << content;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::string merchant_say_text(const mir2::RuntimeDispatch& dispatch) {
  const auto packet = find_packet(dispatch, mir2::kSmMerchantSay);
  assert(packet.has_value());
  return mir2::legacy_decode_string(packet->body);
}

bool has_notice(const mir2::RuntimeDispatch& dispatch, std::string_view text) {
  for (const auto& event : dispatch.session_events) {
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == mir2::kSmHear &&
        mir2::legacy_decode_string(decoded->body) == text) {
      return true;
    }
  }
  return false;
}

mir2::LogicCommand enter_world(std::uint64_t session_id, std::string account,
                               std::string name, std::string map_id) {
  mir2::CharacterRecord character;
  character.account_id = account;
  character.character_name = name;
  character.map_id = map_id;
  character.x = 10;
  character.y = 10;
  character.ability.level = 10;
  character.ability.max_hp = 30;
  character.ability.hp = 30;
  character.ability.max_weight = 100;

  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand npc_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                               std::uint64_t npc_id) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  return command;
}

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                                mir2::LogicCommand command) {
  const auto session_id = command.session_id;
  const auto inbox_before = runtime.legacy_session_inbox_size(session_id);
  const auto routed = runtime.route_logic_command(std::move(command));
  assert(routed.session_events.empty());
  assert(runtime.legacy_session_inbox_size(session_id) == inbox_before + 1);
  now_ms += 251;
  auto dispatch = runtime.tick(now_ms);
  assert(runtime.legacy_session_inbox_size(session_id) == inbox_before);
  return dispatch;
}

std::int32_t apple_count(const mir2::CharacterRecord& character) {
  std::int32_t count = 0;
  for (const auto& item : character.bag_items) {
    if (item.index == 1) {
      ++count;
    }
  }
  return count;
}

void write_base_config(const std::filesystem::path& root) {
  write_file(root / "server.toml",
             "log_dir = \"logs\"\n"
             "data_dir = \"data\"\n"
             "status_file = \"runtime/status.json\"\n");
  write_file(root / "ports.toml",
             "[login_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 5500\n\n"
             "[game_gateway]\n"
             "address = \"127.0.0.1\"\n"
             "port = 7000\n");
  write_file(root / "runtime" / "logic.toml",
             "tick_ms = 20\n"
             "player_budget_ms = 30\n"
             "monster_budget_ms = 30\n"
             "spawn_budget_ms = 30\n"
             "npc_budget_ms = 5\n"
             "net_flush_budget_ms = 30\n");
  write_file(root / "maps" / "0.toml",
             "id = \"0\"\n"
             "title = \"Town\"\n"
             "width = 20\n"
             "height = 20\n"
             "home_x = 10\n"
             "home_y = 10\n");
  write_file(root / "maps" / "1.toml",
             "id = \"1\"\n"
             "title = \"Field\"\n"
             "width = 20\n"
             "height = 20\n"
             "home_x = 10\n"
             "home_y = 10\n");
  write_file(root / "items" / "default_items.toml",
             "[[items]]\n"
             "id = 1\n"
             "name = \"Apple\"\n"
             "weight = 1\n"
             "dura_max = 1000\n"
             "equip_slot = -1\n");
  write_file(root / "npcs" / "default_npcs.toml",
             "npcs = [\n"
             "  { id = \"writer\", map_id = \"0\", name = \"Writer\", x = 11, y = 10, "
             "script = \"npc_scripts/Npc_def/writer-0.txt\", service = \"none\" },\n"
             "  { id = \"reader\", map_id = \"1\", name = \"Reader\", x = 11, y = 10, "
             "script = \"npc_scripts/Npc_def/reader-1.txt\", service = \"none\" },\n"
             "  { id = \"mover\", map_id = \"0\", name = \"Mover\", x = 12, y = 10, "
             "script = \"npc_scripts/Npc_def/mover-0.txt\", service = \"none\" }\n"
             "]\n");
  write_file(root / "npc_scripts" / "Npc_def" / "writer-0.txt",
             "#DEFINE @GREETING Hidden\n"
             "[@main]\n"
             "#SAY\n"
             "@GREETING, <$USERNAME>\\\n"
             "Visible\\Path\n"
             "/ Hidden comment\n"
             "#IF\n"
             "CHECKLEVEL 1\n"
             "#ACT\n"
             "#DEFINE @ITEM Apple\n"
             "GIVE @ITEM 1\n"
             "SENDMSG 0 Keep\\Together\n"
             "ADDNAMELIST Shared Hero\n"
             "ADDIDLIST SharedId acct\n");
  write_file(root / "npc_scripts" / "Npc_def" / "reader-1.txt",
             "[@main]\n"
             "#IF\n"
             "CHECKNAMELIST Shared Hero\n"
             "CHECKIDLIST SharedId acct\n"
             "#SAY\n"
             "SharedOk\n"
             "#ELSESAY\n"
             "SharedFail\n");
  write_file(root / "npc_scripts" / "Npc_def" / "mover-0.txt",
             "[@main]\n"
             "#IF\n"
             "CHECKLEVEL 1\n"
             "#ACT\n"
             "MAPMOVE 1 10 10\n");
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_legacy_script_parser_state_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  write_base_config(root);
  write_file(root / "npc_scripts" / "Startup" / "StartupQuest.txt",
             "[@main]\n"
             "#IF\n"
             "CHECKLEVEL 1\n"
             "#ACT\n"
             "GIVE Apple 1\n"
             "ADDNAMELIST Startup Hero\n");

  mir2::ConfigLoader loader;
  auto config = loader.load(root);
  assert(!config.startup_quest_dialog_sections.empty());
  config.runtime.data_dir = root / "data";

  std::uint64_t now_ms = 1000;
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  static_cast<void>(runtime.route_logic_command(enter_world(12, "acct", "Hero", "0")));
  static_cast<void>(runtime.tick(now_ms));
  now_ms += 251;
  static_cast<void>(runtime.tick(now_ms));

  auto hero = runtime.snapshot_character_actor("Hero");
  assert(hero.has_value());
  assert(apple_count(*hero) == 1);

  const auto writer_dispatch =
      route_due(runtime, now_ms, npc_command(mir2::LogicCommandKind::click_npc, 12, 1));
  const auto writer_text = merchant_say_text(writer_dispatch);
  assert(writer_text.find("Writer/@GREETING, Hero\\Visible\\Path") == 0);
  assert(writer_text.find("Hidden comment") == std::string::npos);
  assert(has_notice(writer_dispatch, "Keep\\Together"));
  hero = runtime.snapshot_character_actor("Hero");
  assert(hero.has_value());
  assert(apple_count(*hero) == 2);

  static_cast<void>(
      route_due(runtime, now_ms, npc_command(mir2::LogicCommandKind::click_npc, 12, 3)));
  hero = runtime.snapshot_character_actor("Hero");
  assert(hero.has_value());
  assert(hero->map_id == "1");
  assert(apple_count(*hero) == 2);

  static_cast<void>(runtime.route_logic_command(enter_world(13, "other", "Other", "1")));
  now_ms += 251;
  static_cast<void>(runtime.tick(now_ms));
  now_ms += 251;
  static_cast<void>(runtime.tick(now_ms));
  const auto shared_dispatch =
      route_due(runtime, now_ms, npc_command(mir2::LogicCommandKind::click_npc, 13, 2));
  assert(merchant_say_text(shared_dispatch).find("Reader/SharedOk") == 0);

  auto persisted_config = loader.load(root);
  persisted_config.runtime.data_dir = root / "data";
  mir2::LogicRuntime persisted_runtime(persisted_config);
  persisted_runtime.initialize();
  std::uint64_t persisted_now_ms = 3000;
  static_cast<void>(
      persisted_runtime.route_logic_command(enter_world(14, "fresh", "Fresh", "1")));
  static_cast<void>(persisted_runtime.tick(persisted_now_ms));
  persisted_now_ms += 251;
  static_cast<void>(persisted_runtime.tick(persisted_now_ms));
  const auto persisted_dispatch = route_due(
      persisted_runtime, persisted_now_ms,
      npc_command(mir2::LogicCommandKind::click_npc, 14, 2));
  assert(merchant_say_text(persisted_dispatch).find("Reader/SharedOk") == 0);

  const auto missing_root =
      std::filesystem::temp_directory_path() / "mir2_legacy_script_parser_state_missing_smoke";
  std::filesystem::remove_all(missing_root, ignored);
  write_base_config(missing_root);
  auto missing_config = loader.load(missing_root);
  assert(missing_config.startup_quest_dialog_sections.empty());
  missing_config.runtime.data_dir = missing_root / "data";
  mir2::LogicRuntime missing_runtime(missing_config);
  missing_runtime.initialize();
  static_cast<void>(
      missing_runtime.route_logic_command(enter_world(15, "plain", "Plain", "0")));
  static_cast<void>(missing_runtime.tick(5000));
  const auto plain = missing_runtime.snapshot_character_actor("Plain");
  assert(plain.has_value());
  assert(apple_count(*plain) == 0);

  std::filesystem::remove_all(root, ignored);
  std::filesystem::remove_all(missing_root, ignored);
  return 0;
}
