#include <optional>
#include <string>

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

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                                mir2::LogicCommand command) {
  static_cast<void>(runtime.route_logic_command(std::move(command)));
  now_ms += 251;
  return runtime.tick(now_ms);
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "MerchantMap", {}, 0, 0, 20, 20});
  config.items.push_back(mir2::ItemConfig{1, "Potion", 1, 40, 0, 1, 1, 1000, -1, 0, 0});
  config.npcs.push_back(
      mir2::NpcConfig{"merchant_1", "0", "Trader", 11, 10, "merchant_1.txt", "sell_repair", {1}});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 1000;

  mir2::CharacterRecord hero;
  hero.account_id = "guest";
  hero.character_name = "Hero";
  hero.map_id = "0";
  hero.x = 10;
  hero.y = 10;

  mir2::LogicCommand enter;
  enter.kind = mir2::LogicCommandKind::enter_world;
  enter.session_id = 10;
  enter.account_id = "guest";
  enter.character_name = "Hero";
  enter.map_id = "0";
  enter.x = 10;
  enter.y = 10;
  enter.character = hero;
  static_cast<void>(runtime.route_logic_command(enter));

  const auto login_dispatch = runtime.tick(now_ms);
  if (!find_packet(login_dispatch, mir2::kSmNewMap).has_value()) {
    return 1;
  }
  now_ms += 251;
  static_cast<void>(runtime.tick(now_ms));

  mir2::LogicCommand click_npc;
  click_npc.kind = mir2::LogicCommandKind::click_npc;
  click_npc.session_id = 10;
  click_npc.target_actor_id = 1;
  const auto dialog_dispatch = route_due(runtime, now_ms, std::move(click_npc));
  const auto merchant_say = find_packet(dialog_dispatch, mir2::kSmMerchantSay);
  if (!merchant_say.has_value()) {
    return 1;
  }
  const auto merchant_text = decode_merchant_dialog(merchant_say->body);
  if (merchant_text.find("Trader/") != 0 || merchant_text.find("<Buy/@buy>") == std::string::npos ||
      merchant_text.find("<Sell/@sell>") == std::string::npos ||
      merchant_text.find("<Repair/@repair>") == std::string::npos ||
      merchant_text.find("<Leave/@exit>") == std::string::npos) {
    return 1;
  }

  const auto buy_dispatch = route_due(runtime, now_ms, make_menu_command(10, 1, "@buy"));
  if (!find_packet(buy_dispatch, mir2::kSmSendGoodsList).has_value()) {
    return 1;
  }

  const auto repair_dispatch = route_due(runtime, now_ms, make_menu_command(10, 1, "@repair"));
  if (!find_packet(repair_dispatch, mir2::kSmSendUserRepair).has_value()) {
    return 1;
  }

  const auto main_dispatch = route_due(runtime, now_ms, make_menu_command(10, 1, "@main"));
  if (!find_packet(main_dispatch, mir2::kSmMerchantSay).has_value()) {
    return 1;
  }

  const auto close_dispatch = route_due(runtime, now_ms, make_menu_command(10, 1, "@exit"));
  if (!find_packet(close_dispatch, mir2::kSmMerchantDlgClose).has_value()) {
    return 1;
  }

  return 0;
}
