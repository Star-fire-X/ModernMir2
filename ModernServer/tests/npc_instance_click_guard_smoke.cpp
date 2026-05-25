#include <iterator>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id, std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_packet(const mir2::RuntimeDispatch& dispatch, std::uint64_t session_id,
                std::uint16_t ident) {
  return find_packet(dispatch, session_id, ident).has_value();
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
}

mir2::RuntimeDispatch tick_players(mir2::LogicRuntime& runtime, int count = 12) {
  mir2::RuntimeDispatch dispatch;
  for (int index = 0; index < count; ++index) {
    append_dispatch(dispatch, runtime.tick());
  }
  return dispatch;
}

mir2::NpcConfig make_shop(std::string id, std::string name, std::int32_t x) {
  mir2::NpcConfig npc;
  npc.id = std::move(id);
  npc.name = std::move(name);
  npc.map_id = "0";
  npc.x = x;
  npc.y = 10;
  npc.service = "buy";
  npc.merchant_goods.push_back(1);
  return npc;
}

mir2::CharacterRecord make_character(std::string account, std::string name, std::int32_t x,
                                     std::uint8_t dir) {
  mir2::CharacterRecord character;
  character.account_id = std::move(account);
  character.character_name = std::move(name);
  character.map_id = "0";
  character.x = x;
  character.y = 10;
  character.dir = dir;
  character.ability.level = 30;
  character.ability.max_hp = 50;
  character.ability.max_mp = 50;
  character.ability.max_exp = 100;
  return character;
}

mir2::LogicCommand enter_command(std::uint64_t session_id, const mir2::CharacterRecord& character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = character;
  return command;
}

mir2::LogicCommand click_npc(std::uint64_t session_id, std::uint64_t npc_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  return command;
}

mir2::LogicCommand merchant_select(std::uint64_t session_id, std::uint64_t npc_id,
                                   std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand trade_try(std::uint64_t session_id, std::string target_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::trade_try;
  command.session_id = session_id;
  command.text = std::move(target_name);
  return command;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  config.budgets.tick_ms = 20;
  config.maps.push_back(mir2::MapConfig{"0", "NpcClickGuards", {}, 0, 0, 40, 40});
  config.items.push_back(mir2::ItemConfig{1, "Potion", 1, 10});
  config.npcs.push_back(make_shop("near_shop", "Near Shop", 11));
  config.npcs.push_back(make_shop("far_shop", "Far Shop", 26));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();

  const auto hero = make_character("guest_a", "HeroA", 10, 2);
  const auto peer = make_character("guest_b", "HeroB", 11, 6);
  static_cast<void>(runtime.route_logic_command(enter_command(7, hero)));
  static_cast<void>(runtime.route_logic_command(enter_command(8, peer)));
  const auto login = tick_players(runtime);
  if (!has_packet(login, 7, mir2::kSmNewMap) || !has_packet(login, 8, mir2::kSmNewMap)) {
    return 1;
  }

  static_cast<void>(runtime.route_logic_command(click_npc(7, 2)));
  const auto far_click = tick_players(runtime);
  if (has_packet(far_click, 7, mir2::kSmSendGoodsList) ||
      has_packet(far_click, 7, mir2::kSmMerchantSay)) {
    return 2;
  }

  static_cast<void>(runtime.route_logic_command(click_npc(7, 1)));
  const auto near_click = tick_players(runtime);
  if (!has_packet(near_click, 7, mir2::kSmSendGoodsList)) {
    return 3;
  }

  static_cast<void>(runtime.route_logic_command(trade_try(7, "HeroB")));
  const auto trade_open = tick_players(runtime);
  if (!has_packet(trade_open, 7, mir2::kSmDealMenu) ||
      !has_packet(trade_open, 8, mir2::kSmDealMenu)) {
    return 4;
  }

  static_cast<void>(runtime.route_logic_command(click_npc(7, 1)));
  const auto trade_click = tick_players(runtime);
  if (has_packet(trade_click, 7, mir2::kSmSendGoodsList) ||
      has_packet(trade_click, 7, mir2::kSmMerchantSay)) {
    return 5;
  }

  static_cast<void>(runtime.route_logic_command(merchant_select(7, 1, "@buy")));
  const auto trade_menu_replay = tick_players(runtime);
  if (has_packet(trade_menu_replay, 7, mir2::kSmSendGoodsList) ||
      has_packet(trade_menu_replay, 7, mir2::kSmMerchantSay)) {
    return 6;
  }

  return 0;
}
