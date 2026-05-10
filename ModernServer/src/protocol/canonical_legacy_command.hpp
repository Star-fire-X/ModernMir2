#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/messages.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

enum class CanonicalSourceProtocol {
  legacy_framed,
  client_v1
};

enum class CanonicalLegacyCommandKind {
  turn,
  walk,
  run,
  attack,
  spell,
  say,
  click_npc,
  merchant_select,
  query_username,
  query_bag_items,
  query_storage_items,
  query_detail_goods,
  query_sell_price,
  query_repair_cost,
  drop_item,
  pickup_item,
  take_on_item,
  take_off_item,
  eat_item,
  drop_gold,
  revive,
  buy_item,
  sell_item,
  repair_item,
  storage_item,
  take_back_storage_item,
  trade_try,
  trade_cancel,
  trade_add_item,
  trade_remove_item,
  trade_set_gold,
  trade_accept
};

enum class CanonicalParseStatus {
  ok,
  malformed_packet,
  unsupported_ident
};

struct CanonicalLegacyCommand {
  CanonicalSourceProtocol source_protocol{CanonicalSourceProtocol::legacy_framed};
  CanonicalLegacyCommandKind kind{CanonicalLegacyCommandKind::turn};
  std::uint64_t session_id{0};
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint8_t dir{0};
  std::uint64_t target_actor_id{0};
  std::int32_t item_make_index{0};
  std::int32_t item_slot{-1};
  std::int32_t amount{0};
  std::string text{};
  LegacyDefaultMessage game_message{};
  LegacyPacket packet{};
};

struct CanonicalLegacyDecodeResult {
  CanonicalParseStatus status{CanonicalParseStatus::malformed_packet};
  std::optional<CanonicalLegacyCommand> command{};
  LegacyDefaultMessage game_message{};
};

[[nodiscard]] CanonicalLegacyDecodeResult decode_legacy_game_command(
    std::uint64_t session_id, const LegacyPacket& packet);

[[nodiscard]] LogicCommand to_logic_command(const CanonicalLegacyCommand& command);

}  // namespace mir2
