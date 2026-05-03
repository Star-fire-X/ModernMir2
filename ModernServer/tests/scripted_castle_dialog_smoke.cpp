#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "config/config_loader.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "storage/repository.hpp"
#include "world/logic_runtime.hpp"
#include "sqlite3.h"

namespace {

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

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << content;
}

bool exec_sql(sqlite3* database, std::string_view sql) {
  char* error = nullptr;
  const auto rc = sqlite3_exec(database, std::string(sql).c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    sqlite3_free(error);
    return false;
  }
  return true;
}

std::string decode_merchant_dialog(std::string_view body) {
  return mir2::legacy_decode_string(body);
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto root = std::filesystem::temp_directory_path() / "mir2_scripted_castle_dialog_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

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
             "title = \"CastleMap\"\n"
             "width = 0\n"
             "height = 0\n"
             "home_x = 10\n"
             "home_y = 10\n");
  write_file(root / "items" / "default_items.toml", "items = []\n");
  write_file(root / "npcs" / "default_npcs.toml",
             "npcs = [\n"
             "  { id = \"castle_guide\", map_id = \"0\", name = \"Castle Guide\", x = 11, y = 10, "
             "script = \"npc_scripts/Npc_def/castle_guide-0.txt\", service = \"none\" }\n"
             "]\n");
  write_file(root / "npc_scripts" / "Npc_def" / "castle_guide-0.txt",
             "[@main]\n"
             "Welcome, <$USERNAME>\\\n"
             "Guild: <$OWNERGUILD>\\\n"
             "Lord: <$LORD>\\\n"
             "War Date: <$CASTLEWARDATE>\\\n"
             "Rivals: <$LISTOFWAR>\\\n"
             "Fees: <$GUILDWARFEE>/<$UPGRADEWEAPONFEE>\n");

  mir2::Repository repository(root / "data" / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");

  sqlite3* database = nullptr;
  if (sqlite3_open((root / "data" / "mir2.sqlite").string().c_str(), &database) != SQLITE_OK) {
    if (database != nullptr) {
      sqlite3_close(database);
    }
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  const auto inserted =
      exec_sql(database,
               "INSERT INTO guilds(guild_name, payload_json) VALUES("
               "'DragonSlayers', '{\"lord\":\"Arthur\"}');") &&
      exec_sql(database,
               "INSERT INTO castle_state(castle_name, payload_json) VALUES("
               "'Sabuk', "
               "'{\"owner_guild\":\"DragonSlayers\",\"castle_war_date\":\"2026-05-01 20:00\","
               "\"list_of_war\":[\"WolfPack\",\"CrimsonMoon\"],\"guild_war_fee\":45000,"
               "\"upgrade_weapon_fee\":15000}');");
  sqlite3_close(database);
  if (!inserted) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  const auto dialog_context = repository.load_castle_dialog_context();
  if (dialog_context.owner_guild != "DragonSlayers" || dialog_context.lord != "Arthur" ||
      dialog_context.castle_war_date != "2026-05-01 20:00" ||
      dialog_context.list_of_war != "WolfPack, CrimsonMoon" ||
      dialog_context.guild_war_fee != 45000 || dialog_context.upgrade_weapon_fee != 15000) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  mir2::ConfigLoader loader;
  auto config = loader.load(root);
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();
  runtime.set_castle_dialog_context(dialog_context);

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 12;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  static_cast<void>(runtime.tick());

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 12;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto dialog_dispatch = runtime.tick();
  const auto dialog_packet = find_packet(dialog_dispatch, mir2::kSmMerchantSay);
  if (!dialog_packet.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  const auto text = decode_merchant_dialog(dialog_packet->body);
  if (text.find("Castle Guide/Welcome, Hero\\") != 0 ||
      text.find("Guild: DragonSlayers\\") == std::string::npos ||
      text.find("Lord: Arthur\\") == std::string::npos ||
      text.find("War Date: 2026-05-01 20:00\\") == std::string::npos ||
      text.find("Rivals: WolfPack, CrimsonMoon\\") == std::string::npos ||
      text.find("Fees: 45000/15000") == std::string::npos) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  std::filesystem::remove_all(root, ec);
  return 0;
}
