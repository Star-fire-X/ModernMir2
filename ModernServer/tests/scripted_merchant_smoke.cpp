#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

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

mir2::LogicCommand make_menu_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                     std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand make_buy_command(std::uint64_t session_id, std::uint64_t merchant_id,
                                    std::string item_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::buy_item;
  command.session_id = session_id;
  command.target_actor_id = merchant_id;
  command.text = std::move(item_name);
  return command;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_scripted_merchant_smoke";
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
             "title = \"ScriptMap\"\n"
             "width = 0\n"
             "height = 0\n"
             "home_x = 10\n"
             "home_y = 10\n");
  write_file(root / "items" / "default_items.toml",
             "items = [\n"
             "  { id = 1, name = \"Potion\", weight = 1, price = 40, std_mode = 0, shape = 1, "
             "looks = 1, dura_max = 1000, equip_slot = -1, hp_add = 0, mp_add = 0 }\n"
             "]\n");
  write_file(root / "npcs" / "default_npcs.toml",
             "npcs = [\n"
             "  { id = \"merchant_1\", map_id = \"0\", name = \"Script Trader\", x = 11, y = 10, "
             "script = \"npc_scripts/market_def/merchant_1-0.txt\", service = \"sell_repair\", "
             "merchant_goods = [] }\n"
             "]\n");
  write_file(root / "npc_scripts" / "market_def" / "merchant_1-0.txt",
             "%200\n"
             "+40\n"
             "+1\n"
             "\n"
             "[@main]\n"
             "Welcome from script, <$USERNAME>\\\n"
             "<Browse/@buy>\\\n"
             "<Fix/@repair>\\\n"
             "<Leave/@exit>\n"
             "\n"
             "[@buy]\n"
             "Choose what you need, <$USERNAME>\\\n"
             "<Back/@main>\n"
             "\n"
             "[@repair]\n"
             "I can repair your gear, <$USERNAME>\\\n"
             "<Back/@main>\n"
             "\n"
             "[goods]\n"
             "; item count refresh-hour\n"
             "Potion 2 1\n");

  mir2::ConfigLoader loader;
  auto config = loader.load(root);
  if (config.npcs.size() != 1 || config.npcs.front().price_rate_percent != 200 ||
      config.npcs.front().legacy_deal_std_modes.size() != 2 ||
      config.npcs.front().legacy_deal_std_modes[0] != 40 ||
      config.npcs.front().legacy_deal_std_modes[1] != 1 ||
      config.npcs.front().merchant_products.size() != 1 ||
      config.npcs.front().merchant_products.front().item_name != "Potion" ||
      config.npcs.front().merchant_products.front().count != 2 ||
      config.npcs.front().merchant_products.front().refresh_hours != 1) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  mir2::LogicRuntime runtime(std::move(config));
  runtime.initialize();

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;
  hero.gold = 100;
  hero.ability.max_hp = 15;
  hero.ability.max_mp = 10;
  hero.ability.max_exp = 100;
  hero.ability.max_weight = 30;
  hero.ability.max_wear_weight = 100;
  hero.ability.max_hand_weight = 100;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 11;
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
  click_npc.session_id = 11;
  click_npc.target_actor_id = 1;
  static_cast<void>(runtime.route_logic_command(click_npc));
  const auto dialog_dispatch = runtime.tick();
  const auto merchant_say = find_packet(dialog_dispatch, mir2::kSmMerchantSay);
  if (!merchant_say.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto main_text = decode_merchant_dialog(merchant_say->body);
  if (main_text.find("Script Trader/Welcome from script, Hero\\") != 0 ||
      main_text.find("<Browse/@buy>") == std::string::npos ||
      main_text.find("<Fix/@repair>") == std::string::npos) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(11, 1, "@buy")));
  const auto buy_dispatch = runtime.tick();
  const auto buy_say = find_packet(buy_dispatch, mir2::kSmMerchantSay);
  const auto goods_list = find_packet(buy_dispatch, mir2::kSmSendGoodsList);
  if (!buy_say.has_value() || !goods_list.has_value() ||
      goods_list->message.recog != 1 || goods_list->message.param != 1) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto buy_text = decode_merchant_dialog(buy_say->body);
  if (buy_text.find("Script Trader/Choose what you need, Hero\\") != 0) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto goods_body = mir2::legacy_decode_string(goods_list->body);
  if (goods_body.find("Potion/0/80/2/") == std::string::npos) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_buy_command(11, 1, "Potion")));
  const auto potion_buy_dispatch = runtime.tick();
  const auto add_item = find_packet(potion_buy_dispatch, mir2::kSmAddItem);
  const auto buy_ok = find_packet(potion_buy_dispatch, mir2::kSmBuyItemSuccess);
  if (!add_item.has_value() || !buy_ok.has_value() || buy_ok->message.recog != 20) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(11, 1, "@buy")));
  const auto buy_after_dispatch = runtime.tick();
  const auto goods_after_buy = find_packet(buy_after_dispatch, mir2::kSmSendGoodsList);
  if (!goods_after_buy.has_value() ||
      mir2::legacy_decode_string(goods_after_buy->body).find("Potion/0/80/1/") ==
          std::string::npos) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_buy_command(11, 1, "Potion")));
  const auto potion_fail_dispatch = runtime.tick();
  const auto buy_fail = find_packet(potion_fail_dispatch, mir2::kSmBuyItemFail);
  if (!buy_fail.has_value() || buy_fail->message.recog != 3) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(make_menu_command(11, 1, "@repair")));
  const auto repair_dispatch = runtime.tick();
  const auto repair_say = find_packet(repair_dispatch, mir2::kSmMerchantSay);
  const auto repair_menu = find_packet(repair_dispatch, mir2::kSmSendUserRepair);
  if (!repair_say.has_value() || !repair_menu.has_value()) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }
  const auto repair_text = decode_merchant_dialog(repair_say->body);
  if (repair_text.find("Script Trader/I can repair your gear, Hero\\") != 0) {
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  std::filesystem::remove_all(root, ec);
  return 0;
}
