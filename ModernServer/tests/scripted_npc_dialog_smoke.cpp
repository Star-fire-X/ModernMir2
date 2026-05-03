#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "config/config_loader.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

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

std::string decode_merchant_dialog(std::string_view body) {
  return mir2::legacy_decode_string(body);
}

mir2::LogicCommand make_menu_command(std::uint64_t session_id, std::uint64_t npc_id,
                                     std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.text = std::move(action);
  return command;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_scripted_npc_dialog_smoke";
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
             "title = \"NpcMap\"\n"
             "width = 0\n"
             "height = 0\n"
             "home_x = 10\n"
             "home_y = 10\n");
  write_file(root / "items" / "default_items.toml", "items = []\n");
  write_file(root / "npcs" / "default_npcs.toml",
             "npcs = [\n"
             "  { id = \"sage_1\", map_id = \"0\", name = \"Old Sage\", x = 11, y = 10, "
             "script = \"npc_scripts/Npc_def/sage_1-0.txt\", service = \"none\" }\n"
             "]\n");
  write_file(root / "npc_scripts" / "Defines" / "common.txt",
             "#DEFINE @RUIN_TEXT The ruins sleep beneath the hill\n");
  write_file(root / "npc_scripts" / "Npc_def" / "called.txt",
             "[@called]\n"
             "Called through script, <$USERNAME>\\\n"
             "<Back/@main>\n");
  write_file(root / "npc_scripts" / "Npc_def" / "sage_1-0.txt",
             "#INCLUDE common.txt\n"
             "#DEFINE @GREETING Welcome\n"
             "[@main]\n"
             "@GREETING, <$USERNAME>\\\n"
             "<Ask about the ruins/@about>\\\n"
             "<Test call/@call>\\\n"
             "<Say goodbye/@exit>\n"
             "\n"
             "[@about]\n"
             "@RUIN_TEXT, <$USERNAME>\\\n"
             "<Continue/@done>\n"
             "\n"
             "[@call]\n"
             "#CALL [called.txt] @called\n"
             "\n"
             "[~@done]\n"
             "That is all I can share, <$USERNAME>\\\n"
             "<Back/@main>\n");

  mir2::ConfigLoader loader;
  auto config = loader.load(root);
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();

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

  const auto login_dispatch = runtime.tick();
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 12;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto main_dispatch = runtime.tick();
  const auto main_packet = find_packet(main_dispatch, mir2::kSmMerchantSay);
  if (!main_packet.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto main_text = decode_merchant_dialog(main_packet->body);
  if (main_text.find("Old Sage/Welcome, Hero\\") != 0 ||
      main_text.find("<Ask about the ruins/@about>") == std::string::npos) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@about")));
  const auto about_dispatch = runtime.tick();
  const auto about_packet = find_packet(about_dispatch, mir2::kSmMerchantSay);
  if (!about_packet.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto about_text = decode_merchant_dialog(about_packet->body);
  if (about_text.find("Old Sage/The ruins sleep beneath the hill, Hero\\") != 0) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@call")));
  const auto call_dispatch = runtime.tick();
  const auto call_packet = find_packet(call_dispatch, mir2::kSmMerchantSay);
  if (!call_packet.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto call_text = decode_merchant_dialog(call_packet->body);
  if (call_text.find("Old Sage/Called through script, Hero\\") != 0) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@done")));
  const auto done_dispatch = runtime.tick();
  const auto done_packet = find_packet(done_dispatch, mir2::kSmMerchantSay);
  if (!done_packet.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto done_text = decode_merchant_dialog(done_packet->body);
  if (done_text.find("Old Sage/That is all I can share, Hero\\") != 0) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(12, 1, "@exit")));
  const auto close_dispatch = runtime.tick();
  if (!find_packet(close_dispatch, mir2::kSmMerchantDlgClose).has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  std::filesystem::remove_all(root, ec);
  return 0;
}
