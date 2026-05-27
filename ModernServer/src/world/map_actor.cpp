#include "world/map_actor.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "shared/legacy/action_ids.hpp"
#include "shared/legacy/movement_rules.hpp"
#include "util/string_utils.hpp"
#include "world/legacy_chat_parser.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/legacy_magic_runtime.hpp"
#include "world/legacy_skill_formula.hpp"

namespace mir2 {

#include "world/map_actor_helpers.hpp"
#include "world/map_actor_packets.hpp"

namespace {
void append_runtime_dispatch(RuntimeDispatch& target, RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_event_creates.insert(
      target.legacy_event_creates.end(),
      std::make_move_iterator(source.legacy_event_creates.begin()),
      std::make_move_iterator(source.legacy_event_creates.end()));
  target.legacy_holy_curtain_groups.insert(
      target.legacy_holy_curtain_groups.end(),
      std::make_move_iterator(source.legacy_holy_curtain_groups.begin()),
      std::make_move_iterator(source.legacy_holy_curtain_groups.end()));
  target.legacy_random_space_moves.insert(
      target.legacy_random_space_moves.end(),
      std::make_move_iterator(source.legacy_random_space_moves.begin()),
      std::make_move_iterator(source.legacy_random_space_moves.end()));
  target.legacy_time_recall_requests.insert(
      target.legacy_time_recall_requests.end(),
      std::make_move_iterator(source.legacy_time_recall_requests.begin()),
      std::make_move_iterator(source.legacy_time_recall_requests.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

std::int32_t compute_goods_price(const LegacyUserItem& item,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                 std::optional<std::int32_t> dynamic_price = std::nullopt) {
  const auto* config = find_item_config(item_configs, item.index);
  auto price = dynamic_price.has_value() ? std::max(*dynamic_price, 0)
                                         : (config != nullptr ? std::max(config->price, 0) : 0);
  const auto dura_max = static_cast<std::int32_t>(item_dura_max(item, item_configs));
  if (price <= 0) {
    return -1;
  }
  if (config != nullptr && config->std_mode > 4 && dura_max > 0 && item.dura_max > 0) {
    if (static_cast<std::int32_t>(item.dura) <= dura_max) {
      const auto damage =
          (static_cast<double>(price) / 2.0 / static_cast<double>(dura_max)) *
          static_cast<double>(dura_max - static_cast<std::int32_t>(item.dura));
      price = std::max(2, static_cast<std::int32_t>(std::lround(static_cast<double>(price) - damage)));
    } else {
      const auto bonus =
          static_cast<double>(static_cast<std::int32_t>(item.dura) - dura_max) *
          (static_cast<double>(price) / static_cast<double>(dura_max) * 2.0);
      price += static_cast<std::int32_t>(std::lround(bonus));
    }
  }
  return price;
}

std::int32_t compute_buy_price(const LegacyUserItem& item,
                               const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                               std::optional<std::int32_t> dynamic_price = std::nullopt) {
  const auto goods_price = compute_goods_price(item, item_configs, dynamic_price);
  return goods_price >= 0 ? static_cast<std::int32_t>(std::lround(static_cast<double>(goods_price) / 2.0)) : -1;
}

bool can_sell_item(const Npc& merchant, const LegacyUserItem& item,
                   const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* config = find_item_config(item_configs, item.index);
  if (config == nullptr) {
    return false;
  }
  if (!merchant.deals_std_mode(config->std_mode)) {
    return false;
  }
  if (config->std_mode == 51) {
    return false;
  }
  if ((config->std_mode == 25 || config->std_mode == 30) && item.dura < 4000) {
    return false;
  }
  return compute_buy_price(item, item_configs, merchant.merchant_price(item.index)) >= 0;
}

std::int32_t compute_merchant_sell_price(
    const LegacyUserItem& item, const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    std::int32_t price_rate_percent) {
  const auto goods_price = std::max(compute_goods_price(item, item_configs), 0);
  if (goods_price == 0) {
    return 0;
  }
  return std::max(0, static_cast<std::int32_t>(std::lround(
                         static_cast<double>(goods_price) *
                         static_cast<double>(std::max(price_rate_percent, 0)) / 100.0)));
}

std::int32_t compute_merchant_sell_price(
    const Npc& merchant, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto goods_price =
      std::max(compute_goods_price(item, item_configs, merchant.merchant_price(item.index)), 0);
  if (goods_price == 0) {
    return 0;
  }
  return std::max(0, static_cast<std::int32_t>(std::lround(
                         static_cast<double>(goods_price) *
                         static_cast<double>(std::max(merchant.price_rate_percent(), 0)) / 100.0)));
}

std::int32_t compute_repair_cost(const LegacyUserItem& item,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                 const Npc& merchant,
                                 LegacyRepairMode repair_mode) {
  const auto* config = find_item_config(item_configs, item.index);
  if (config == nullptr || config->std_mode == 43) {
    return -1;
  }
  if (repair_mode == LegacyRepairMode::special &&
      config->std_mode != 5 && config->std_mode != 6) {
    return -1;
  }
  const auto price = compute_merchant_sell_price(merchant, item, item_configs);
  const auto dura_max = static_cast<std::int32_t>(item.dura_max);
  const auto dura = static_cast<std::int32_t>(item.dura);
  if (price <= 0) {
    return -1;
  }
  if (dura_max <= 0) {
    return repair_mode == LegacyRepairMode::special ? price * 3 : price;
  }
  if (dura >= dura_max) {
    return 0;
  }
  const auto cost = static_cast<std::int32_t>(
      std::lround(((static_cast<double>(price) / 3.0) / static_cast<double>(dura_max)) *
                  static_cast<double>(dura_max - dura)));
  return repair_mode == LegacyRepairMode::special ? cost * 3 : cost;
}

PersistRequest make_save_merchant_state_request(const Npc& merchant) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_merchant_state;
  request.merchant_state = merchant.snapshot_merchant_state();
  return request;
}

std::uint8_t clamp_desc_value(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint16_t clamp_dura_value(std::int32_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0, 65000));
}

bool item_name_equals(std::string_view left, std::string_view right) {
  return util::lower_copy(util::trim(std::string(left))) ==
         util::lower_copy(util::trim(std::string(right)));
}

struct LegacyWeaponUpgradePreparation {
  std::uint8_t updc{0};
  std::uint8_t upsc{0};
  std::uint8_t upmc{0};
  std::uint8_t durapoint{0};
  bool has_black_stone{false};
  std::vector<std::size_t> consume_slots{};
};

LegacyWeaponUpgradePreparation prepare_weapon_upgrade(
    const Player& player, const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    std::string_view black_stone_name) {
  LegacyWeaponUpgradePreparation result;
  std::vector<std::int32_t> black_stone_points;
  std::int32_t dc_top = 0;
  std::int32_t dc_second = 0;
  std::int32_t sc_top = 0;
  std::int32_t sc_second = 0;
  std::int32_t mc_top = 0;
  std::int32_t mc_second = 0;

  const auto update_top_two = [](std::int32_t value, std::int32_t& top,
                                 std::int32_t& second) {
    if (value > top) {
      second = top;
      top = value;
    } else if (value > second) {
      second = value;
    }
  };

  const auto& bag_items = player.character().bag_items;
  for (std::size_t slot = 0; slot < bag_items.size(); ++slot) {
    const auto& item = bag_items[slot];
    if (is_empty(item)) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      continue;
    }
    if (item_name_equals(config->name, black_stone_name)) {
      result.has_black_stone = true;
      black_stone_points.push_back(static_cast<std::int32_t>(
          std::lround(static_cast<double>(item.dura) / 1000.0)));
      result.consume_slots.push_back(slot);
      continue;
    }
    if (!legacy_is_upgrade_weapon_stuff(*config)) {
      continue;
    }
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    std::int32_t dc = 0;
    std::int32_t sc = 0;
    std::int32_t mc = 0;
    switch (upgraded.std_mode) {
      case 19:
      case 20:
      case 21:
      case 22:
      case 23:
      case 24:
      case 26:
        dc = packed_min(upgraded.dc) + packed_max(upgraded.dc);
        sc = packed_min(upgraded.sc) + packed_max(upgraded.sc);
        mc = packed_min(upgraded.mc) + packed_max(upgraded.mc);
        if (upgraded.std_mode == 24 || upgraded.std_mode == 26) {
          ++dc;
          ++sc;
          ++mc;
        }
        break;
      default:
        break;
    }
    update_top_two(dc, dc_top, dc_second);
    update_top_two(sc, sc_top, sc_second);
    update_top_two(mc, mc_top, mc_second);
    result.consume_slots.push_back(slot);
  }

  std::sort(black_stone_points.begin(), black_stone_points.end(), std::greater<>());
  const auto count = std::min<std::size_t>(black_stone_points.size(), 5);
  if (count > 0) {
    const auto sum = std::accumulate(black_stone_points.begin(), black_stone_points.begin() + count, 0);
    result.durapoint = clamp_desc_value(static_cast<std::int32_t>(
        std::lround(static_cast<double>(count) +
                    (static_cast<double>(sum) / static_cast<double>(count)) / 5.0 *
                        static_cast<double>(count))));
  }
  result.updc = clamp_desc_value(dc_top + dc_top / 5 + dc_second / 3);
  result.upsc = clamp_desc_value(sc_top + sc_top / 5 + sc_second / 3);
  result.upmc = clamp_desc_value(mc_top + mc_top / 5 + mc_second / 3);
  std::sort(result.consume_slots.begin(), result.consume_slots.end(), std::greater<>());
  result.consume_slots.erase(std::unique(result.consume_slots.begin(), result.consume_slots.end()),
                             result.consume_slots.end());
  return result;
}

LegacyUserItem make_runtime_merchant_item(const ItemConfig& item_config, std::int32_t make_index) {
  LegacyUserItem item;
  item.index = static_cast<std::uint16_t>(std::clamp(item_config.id, 0, 65535));
  item.make_index = make_index;
  item.dura =
      static_cast<std::uint16_t>(std::clamp(item_config.dura_max > 0 ? item_config.dura_max : 1000,
                                           0, 65535));
  item.dura_max = item.dura;
  return item;
}

bool is_dura_restored_when_sold(const ItemConfig& config) {
  if (config.std_mode == 0 || config.std_mode == 25 || config.std_mode == 30 ||
      config.std_mode == 31) {
    return true;
  }
  return config.std_mode == 3 &&
         (config.shape == 1 || config.shape == 2 || config.shape == 3 || config.shape == 5 ||
          config.shape == 9);
}

void add_merchant_goods(Npc& merchant, LegacyUserItem item,
                        const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (item.dura == 0) {
    return;
  }
  const auto* config = find_item_config(item_configs, item.index);
  if (config != nullptr && is_dura_restored_when_sold(*config)) {
    item.dura = item.dura_max;
  }
  auto& goods = merchant.merchant_items_mutable();
  const auto insert_at = std::find_if(goods.begin(), goods.end(), [&](const LegacyUserItem& existing) {
    return !is_empty(existing) && existing.index == item.index;
  });
  goods.insert(insert_at, item);
}

std::int32_t merchant_stock_count(const Npc& merchant, std::int32_t item_id) {
  return static_cast<std::int32_t>(std::count_if(
      merchant.merchant_items().begin(), merchant.merchant_items().end(),
      [&](const LegacyUserItem& item) { return !is_empty(item) && item.index == item_id; }));
}

void trim_merchant_item_tail(Npc& merchant, std::int32_t item_id, std::int32_t remove_count) {
  auto& goods = merchant.merchant_items_mutable();
  while (remove_count > 0) {
    const auto rit = std::find_if(goods.rbegin(), goods.rend(), [&](const LegacyUserItem& item) {
      return !is_empty(item) && item.index == item_id;
    });
    if (rit == goods.rend()) {
      return;
    }
    goods.erase(std::next(rit).base());
    --remove_count;
  }
}

bool price_up(Npc& merchant, std::int32_t item_id,
              const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* config = find_item_config(item_configs, item_id);
  if (config == nullptr || config->price <= 0) {
    return false;
  }
  const auto old_price = merchant.merchant_price(item_id).value_or(config->price);
  auto new_price = static_cast<std::int32_t>(std::lround(static_cast<double>(old_price) * 1.1));
  if (new_price <= old_price) {
    new_price = old_price + 1;
  }
  merchant.set_merchant_price(item_id, new_price);
  return true;
}

[[maybe_unused]] bool price_down(Npc& merchant, std::int32_t item_id,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* config = find_item_config(item_configs, item_id);
  if (config == nullptr || config->price <= 0) {
    return false;
  }
  const auto old_price = merchant.merchant_price(item_id).value_or(config->price);
  auto new_price = static_cast<std::int32_t>(std::lround(static_cast<double>(old_price) * 0.9));
  if (new_price >= old_price) {
    new_price = old_price - 1;
  }
  merchant.set_merchant_price(item_id, std::max(new_price, 1));
  return true;
}

std::vector<LegacyUserItem> collect_detail_goods(
    const Npc& merchant, std::string_view expected_name, std::int32_t top_line,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::vector<LegacyUserItem> matches;
  for (const auto& item : merchant.merchant_items()) {
    if (!is_empty(item) && item_name(item, item_configs) == expected_name) {
      matches.push_back(item);
    }
  }

  if (top_line < 0) {
    top_line = 0;
  }
  if (static_cast<std::size_t>(top_line) >= matches.size()) {
    top_line = std::max<std::int32_t>(0, static_cast<std::int32_t>(matches.size()) - 10);
  }

  const auto start = static_cast<std::size_t>(top_line);
  const auto end = std::min(matches.size(), start + 10);
  return std::vector<LegacyUserItem>(matches.begin() + start, matches.begin() + end);
}

std::optional<std::size_t> find_merchant_item_index(
    const Npc& merchant, std::string_view expected_name, std::int32_t item_make_index,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto& goods = merchant.merchant_items();
  for (std::size_t index = 0; index < goods.size(); ++index) {
    const auto& item = goods[index];
    if (is_empty(item) || item_name(item, item_configs) != expected_name) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item.index);
    const auto can_ignore_make_index =
        config != nullptr && !requires_detail_goods_list(*config);
    if (!can_ignore_make_index && item.make_index != item_make_index) {
      continue;
    }
    return index;
  }
  return std::nullopt;
}

std::optional<LegacyUserItem> take_merchant_item(
    Npc& merchant, std::string_view expected_name, std::int32_t item_make_index,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto item_index =
      find_merchant_item_index(merchant, expected_name, item_make_index, item_configs);
  if (!item_index.has_value()) {
    return std::nullopt;
  }
  auto& goods = merchant.merchant_items_mutable();
  if (*item_index >= goods.size()) {
    return std::nullopt;
  }
  auto item = goods[*item_index];
  goods.erase(goods.begin() + static_cast<std::ptrdiff_t>(*item_index));
  return item;
}

GameObject* find_attack_target_by_actor_id(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects, const GameObject& attacker,
    std::uint64_t target_actor_id, std::int32_t range) {
  if (target_actor_id == 0 || target_actor_id == attacker.id()) {
    return nullptr;
  }
  const auto it = objects.find(target_actor_id);
  if (it == objects.end() || !is_attackable_target(*it->second)) {
    return nullptr;
  }
  const auto distance =
      std::max(std::abs(it->second->x() - attacker.x()), std::abs(it->second->y() - attacker.y()));
  if (distance > std::max<std::int32_t>(range, 1)) {
    return nullptr;
  }
  return it->second.get();
}

GameObject* find_attack_target_in_front(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects, const GameObject& attacker,
    std::int32_t range) {
  const auto [dx, dy] = direction_delta(actor_dir(attacker));
  for (std::int32_t step = 1; step <= range; ++step) {
    const auto tx = attacker.x() + dx * step;
    const auto ty = attacker.y() + dy * step;
    for (auto& [actor_id, object] : objects) {
      if (actor_id == attacker.id() || !is_attackable_target(*object)) {
        continue;
      }
      if (object->x() == tx && object->y() == ty) {
        return object.get();
      }
    }
  }
  return nullptr;
}

GameObject* find_attack_target_by_position(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects, const GameObject& attacker,
    std::int32_t x, std::int32_t y, std::int32_t range = -1) {
  if (range >= 0 && std::max(std::abs(x - attacker.x()), std::abs(y - attacker.y())) >
      std::max<std::int32_t>(range, 1)) {
    return nullptr;
  }
  for (auto& [actor_id, object] : objects) {
    if (actor_id == attacker.id() || !is_attackable_target(*object)) {
      continue;
    }
    if (object->x() == x && object->y() == y) {
      return object.get();
    }
  }
  return nullptr;
}

bool target_in_attack_line(const GameObject& attacker, const GameObject& target,
                           std::int32_t range) {
  const auto [dx, dy] = direction_delta(actor_dir(attacker));
  for (std::int32_t step = 1; step <= std::max(range, 1); ++step) {
    if (target.x() == attacker.x() + dx * step &&
        target.y() == attacker.y() + dy * step) {
      return true;
    }
  }
  return false;
}

std::vector<GameObject*> collect_wide_hit_targets(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const Player& attacker, const MapConfig& map_config, std::uint64_t now_ms) {
  const auto dir = actor_dir(attacker);
  const auto [fx, fy] = direction_delta(dir);
  const auto [lx, ly] = direction_delta(static_cast<std::uint8_t>((dir + 7) % 8));
  const auto [rx, ry] = direction_delta(static_cast<std::uint8_t>((dir + 1) % 8));
  const std::array<std::pair<std::int32_t, std::int32_t>, 3> cells{{
      {attacker.x() + fx, attacker.y() + fy},
      {attacker.x() + lx, attacker.y() + ly},
      {attacker.x() + rx, attacker.y() + ry},
  }};

  std::vector<GameObject*> targets;
  targets.reserve(cells.size());
  for (const auto& cell : cells) {
    GameObject* target = nullptr;
    for (auto& [actor_id, object] : objects) {
      if (actor_id == attacker.id() || !is_attackable_target(*object)) {
        continue;
      }
      if (object->x() == cell.first && object->y() == cell.second) {
        target = object.get();
        break;
      }
    }
    if (target == nullptr) {
      continue;
    }
    if (const auto* player_target = as_player(target); player_target != nullptr &&
        !resolve_pk_block_reason(map_config, attacker, *player_target, now_ms).empty()) {
      continue;
    }
    targets.push_back(target);
  }
  return targets;
}

std::vector<GameObject*> collect_cross_hit_targets(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const Player& attacker, const MapConfig& map_config, std::uint64_t now_ms) {
  static constexpr std::array<std::uint8_t, 7> kCrossDirs{{7, 1, 2, 3, 4, 5, 6}};
  std::vector<GameObject*> targets;
  targets.reserve(kCrossDirs.size());
  const auto dir = actor_dir(attacker);
  for (const auto offset : kCrossDirs) {
    const auto [dx, dy] = direction_delta(static_cast<std::uint8_t>((dir + offset) % 8));
    const auto tx = attacker.x() + dx;
    const auto ty = attacker.y() + dy;
    GameObject* target = nullptr;
    for (auto& [actor_id, object] : objects) {
      if (actor_id == attacker.id() || !is_attackable_target(*object)) {
        continue;
      }
      if (object->x() == tx && object->y() == ty) {
        target = object.get();
        break;
      }
    }
    if (target == nullptr) {
      continue;
    }
    if (const auto* player_target = as_player(target); player_target != nullptr &&
        !resolve_pk_block_reason(map_config, attacker, *player_target, now_ms).empty()) {
      continue;
    }
    targets.push_back(target);
  }
  return targets;
}

bool magic_can_hit_target(const MagicConfig& magic, const GameObject& target) {
  if (as_player(&target) != nullptr) {
    return magic.affect_players;
  }
  if (as_monster(&target) != nullptr) {
    return magic.affect_monsters;
  }
  return false;
}

bool magic_is_harmful(const MagicConfig& magic) {
  return magic.power > 0 || magic.dot_damage > 0 || magic.slow_percent > 0;
}

bool magic_is_beneficial(const MagicConfig& magic) {
  return magic.instant_heal > 0 || magic.heal_per_tick > 0 || magic.dispel_negative ||
         magic.shield_amount > 0;
}

std::vector<std::uint64_t> collect_spell_target_ids(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects, const Player& attacker,
    const MagicConfig& magic, const MapConfig& map_config, const GameObject* primary_target,
    std::int32_t center_x, std::int32_t center_y, bool has_center, bool allow_self) {
  std::vector<std::uint64_t> target_ids;
  const auto try_add = [&](const GameObject& candidate) {
    if ((!allow_self && candidate.id() == attacker.id()) || !is_attackable_target(candidate) ||
        !magic_can_hit_target(magic, candidate)) {
      return;
    }
    if (const auto* player_target = as_player(&candidate); player_target != nullptr) {
      if (magic_is_harmful(magic) &&
          !resolve_pk_block_reason(map_config, attacker, *player_target).empty()) {
        return;
      }
    }
    if (magic.radius > 0) {
      if (!has_center) {
        return;
      }
      const auto distance =
          std::max(std::abs(candidate.x() - center_x), std::abs(candidate.y() - center_y));
      if (distance > magic.radius) {
        return;
      }
    } else if (primary_target == nullptr || primary_target->id() != candidate.id()) {
      return;
    }
    target_ids.push_back(candidate.id());
  };

  if (magic.radius > 0) {
    for (auto& [actor_id, object] : objects) {
      (void)actor_id;
      try_add(*object);
    }
  } else if (primary_target != nullptr) {
    try_add(*primary_target);
  }

  std::sort(target_ids.begin(), target_ids.end());
  target_ids.erase(std::unique(target_ids.begin(), target_ids.end()), target_ids.end());
  return target_ids;
}

Player* find_nearest_player(std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
                            const GameObject& monster, std::int32_t range) {
  Player* nearest = nullptr;
  auto best_distance = std::numeric_limits<std::int32_t>::max();
  for (auto& [actor_id, object] : objects) {
    auto* player = as_player(object.get());
    if (player == nullptr || player->is_dead()) {
      continue;
    }
    const auto distance =
        std::max(std::abs(player->x() - monster.x()), std::abs(player->y() - monster.y()));
    if (distance > range || distance >= best_distance) {
      continue;
    }
    nearest = player;
    best_distance = distance;
  }
  return nearest;
}

bool tile_occupied(const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
                   std::uint64_t actor_id, std::int32_t x, std::int32_t y) {
  for (const auto& [other_id, object] : objects) {
    if (other_id == actor_id || !is_alive(*object)) {
      continue;
    }
    if (object->x() == x && object->y() == y) {
      return true;
    }
  }
  return false;
}

std::pair<std::int32_t, std::int32_t> actor_physical_defense_range(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return {packed_min(player->character().ability.ac), packed_max(player->character().ability.ac)};
  }
  const auto defense = actor_physical_defense(object);
  return {defense, defense};
}

std::int32_t legacy_player_undead_power(
    const Player& attacker, const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  static_cast<void>(item_configs);
  return attacker.legacy_undead_power();
}

std::int32_t legacy_physical_struck_damage(const GameObject& target, std::int32_t damage,
                                           std::int32_t armor_roll,
                                           std::int32_t undead_power = 0) {
  const auto [ac_min, ac_max] = actor_physical_defense_range(target);
  const auto armor = ac_min + std::clamp(armor_roll, 0, std::max(0, ac_max - ac_min));
  auto result = std::max(0, damage - armor);
  if (actor_undead(target) && result > 0) {
    result += std::max(undead_power, 0);
  }
  return result;
}

std::int32_t compute_spell_damage(const Player& attacker, const GameObject& target,
                                  const MagicConfig& magic, std::int32_t magic_roll,
                                  std::int32_t defense_roll) {
  const auto mc_min = packed_min(attacker.character().ability.mc);
  const auto mc_max = std::max(mc_min, packed_max(attacker.character().ability.mc));
  const auto raw = std::max(magic.power, 1) + mc_min +
                   std::clamp(magic_roll, 0, std::max(0, mc_max - mc_min));
  const auto [mac_min, mac_max] = actor_magic_defense_range(target);
  const auto armor = mac_min + std::clamp(defense_roll, 0, std::max(0, mac_max - mac_min));
  return std::max(0, raw - armor);
}

bool legacy_spell_supported(std::int32_t magic_id, const MagicConfig& magic) {
  if (!magic.legacy.legacy_present || magic.legacy.is_sword_skill) {
    return false;
  }
  switch (magic_id) {
    case 1:
    case 2:
    case 5:
    case 6:
    case 8:
    case 37:
    case 9:
    case 10:
    case 11:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 35:
    case 36:
      return true;
    default:
      return false;
  }
}

std::uint8_t next_direction(std::int32_t sx, std::int32_t sy, std::int32_t tx,
                            std::int32_t ty) {
  const auto dx = tx == sx ? 0 : (tx > sx ? 1 : -1);
  const auto dy = ty == sy ? 0 : (ty > sy ? 1 : -1);
  if (dx == 0 && dy < 0) {
    return 0;
  }
  if (dx > 0 && dy < 0) {
    return 1;
  }
  if (dx > 0 && dy == 0) {
    return 2;
  }
  if (dx > 0 && dy > 0) {
    return 3;
  }
  if (dx == 0 && dy > 0) {
    return 4;
  }
  if (dx < 0 && dy > 0) {
    return 5;
  }
  if (dx < 0 && dy == 0) {
    return 6;
  }
  return 7;
}

bool legacy_mag_can_hit_target(std::int32_t sx, std::int32_t sy, const GameObject* target) {
  if (target == nullptr || !is_attackable_target(*target)) {
    return false;
  }
  for (std::int32_t step = 0; step <= 12; ++step) {
    if (sx == target->x() && sy == target->y()) {
      return true;
    }
    const auto [dx, dy] = direction_delta(next_direction(sx, sy, target->x(), target->y()));
    sx += dx;
    sy += dy;
  }
  return false;
}

std::pair<std::int32_t, std::int32_t> actor_magic_defense_range(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return {packed_min(player->character().ability.mac), packed_max(player->character().ability.mac)};
  }
  const auto defense = actor_magic_defense(object);
  return {defense, defense};
}

bool actor_undead(const GameObject& object) {
  const auto* monster = as_monster(&object);
  return monster != nullptr && monster->legacy_undead();
}

std::int32_t legacy_magic_defense_damage(GameObject& target, std::int32_t damage,
                                         LegacyRandom& random,
                                         std::uint64_t current_tick,
                                         std::uint32_t tick_ms,
                                         bool damage_magic_bubble = true,
                                         std::int32_t undead_power = 0) {
  const auto [mac_min, mac_max] = actor_magic_defense_range(target);
  const auto armor_random = random.random(std::max(0, mac_max - mac_min) + 1);
  auto* player_target = as_player(&target);
  const auto bubble_active =
      player_target != nullptr && player_target->legacy_magic_bubble_active(current_tick);
  const auto bubble_level = bubble_active ? player_target->legacy_magic_bubble_level() : 0;
  const auto result = legacy_mag_struck_damage(damage, mac_min, mac_max, armor_random,
                                               actor_undead(target), undead_power, bubble_active,
                                               bubble_level);
  if (result > 0 && bubble_active && damage_magic_bubble) {
    const auto bubble_damage_ticks = legacy_delay_ms_to_ticks(3000, tick_ms);
    player_target->damage_legacy_magic_bubble(current_tick, bubble_damage_ticks);
  }
  return result;
}

struct LegacyMagicDamageResult {
  std::int32_t applied_damage{0};
  bool target_died{false};
  std::uint64_t slain_monster_id{0};
};

bool try_legacy_revival_impl(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    const std::string& map_id,
    Player& player, RuntimeDispatch& dispatch,
    std::uint64_t current_tick, std::uint64_t now_ms) {
  if (!player.is_dead() || !player.legacy_revival_available(now_ms)) {
    return false;
  }

  std::size_t revival_slot = kMaxEquipSlots;
  for (std::size_t slot = 0; slot < player.character().equipped_items.size(); ++slot) {
    const auto* item = player.equipped_item(slot);
    if (item == nullptr || is_empty(*item) || item->dura == 0) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item->index);
    if (config != nullptr && config->shape == kLegacyRingRevivalItem) {
      revival_slot = slot;
      break;
    }
  }
  if (revival_slot == kMaxEquipSlots) {
    return false;
  }

  player.mark_legacy_revival(now_ms);
  const auto previous_status = player.character().status;
  if (auto* item = player.equipped_item_mutable(revival_slot); item != nullptr) {
    const auto before = *item;
    if (item->dura <= 1000) {
      *item = LegacyUserItem{};
      queue_packet(dispatch, player.session_id(),
                   make_del_item_packet(player.session_id(), player.id(), before,
                                        item_configs));
    } else {
      item->dura = static_cast<std::uint16_t>(item->dura - 1000);
      queue_packet(dispatch, player.session_id(),
                   make_update_item_packet(player.session_id(), player.id(), *item,
                                           item_configs));
      queue_packet(dispatch, player.session_id(),
                   make_dura_change_packet(player.session_id(), revival_slot, *item,
                                           item_configs));
    }
  }

  static_cast<void>(player.clear_negative_status_effects(current_tick));
  static_cast<void>(player.clear_negative_legacy_buffs(current_tick));
  player.refresh_derived_state(item_configs);
  static_cast<void>(player.apply_heal(player.character().ability.max_hp));
  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_sub_ability_packet(player.session_id(), player));
  if (player.character().status != previous_status) {
    queue_packet(dispatch, player.session_id(),
                 make_char_status_changed_packet(player.session_id(), player));
    for_each_player(objects, [&](std::uint64_t actor_id, const Player& watcher) {
      if (actor_id == player.id() || !is_legacy_visible_to(watcher, player)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_char_status_changed_packet(watcher.session_id(), player));
    });
  }
  queue_save_character(dispatch, player);

  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      "LegacyCombat",
      "revival_ring",
      map_id,
      {},
      player.id(),
      now_ms,
      current_tick,
      0,
      0,
      0,
      0,
      {},
      "RING_REVIVAL_ITEM",
      0,
      0,
      static_cast<std::int32_t>(revival_slot),
      static_cast<std::int32_t>(player.character().ability.hp),
      true});
  return true;
}

void queue_player_status_tick_result(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch,
    const Player& player,
    const StatusTickResult& result,
    bool include_health) {
  if (include_health) {
    queue_packet(dispatch, player.session_id(),
                 make_health_spell_changed_packet(player.session_id(), player));
  }
  if (result.legacy_status_changed) {
    queue_packet(dispatch, player.session_id(),
                 make_char_status_changed_packet(player.session_id(), player));
    for_each_player(objects, [&](std::uint64_t actor_id, const Player& watcher) {
      if (actor_id == player.id() || !is_legacy_visible_to(watcher, player)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_char_status_changed_packet(watcher.session_id(), player));
    });
  }
  if (result.ability_changed) {
    queue_packet(dispatch, player.session_id(),
                 make_ability_packet(player.session_id(), player.character()));
    queue_packet(dispatch, player.session_id(),
                 make_sub_ability_packet(player.session_id(), player));
  }
}

struct PendingLegacyPacket {
  std::uint64_t session_id{0};
  LegacyPacket packet{};
};

std::vector<PendingLegacyPacket> collect_legacy_death_packets(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GameObject& target) {
  std::vector<PendingLegacyPacket> packets;
  for_each_player(objects, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
      return;
    }
    packets.push_back(PendingLegacyPacket{
        watcher.session_id(),
        make_death_packet(watcher.session_id(), target, watcher.id() == target.id())});
  });
  return packets;
}

void queue_legacy_packets(RuntimeDispatch& dispatch,
                          std::vector<PendingLegacyPacket> packets) {
  for (auto& packet : packets) {
    queue_packet(dispatch, packet.session_id, std::move(packet.packet));
  }
}

void queue_legacy_death_packet(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const GameObject& target) {
  queue_legacy_packets(dispatch, collect_legacy_death_packets(objects, target));
}

LegacyMagicDamageResult apply_legacy_magic_damage(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    RuntimeDispatch& dispatch, Player& caster, GameObject& target, const MapConfig& map_config,
    std::int32_t damage, std::uint64_t current_tick, std::uint64_t now_ms) {
  LegacyMagicDamageResult result;
  if (damage <= 0 || !is_attackable_target(target)) {
    return result;
  }

  if (auto* player_target = as_player(&target); player_target != nullptr) {
    if (!map_config.fight_zone && !map_config.fight3_zone && player_target->pk_level() < 2) {
      player_target->record_pk_hiter(caster.id(), now_ms);
    }
    const auto damage_result = player_target->apply_damage(damage, current_tick);
    result.applied_damage = damage_result.hp_damage;
    result.target_died = player_target->is_dead();
    if (result.target_died &&
        try_legacy_revival_impl(objects, item_configs, map_config.id, *player_target, dispatch,
                                current_tick, now_ms)) {
      result.target_died = false;
    }
    if (result.target_died) {
      const auto death_clear = player_target->mark_dead(now_ms);
      queue_player_status_tick_result(objects, dispatch, *player_target, death_clear, false);
    }
    if (damage_result.absorbed_damage > 0) {
      queue_packet(dispatch, player_target->session_id(),
                   make_health_spell_changed_packet(player_target->session_id(), *player_target));
    }
  } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
    result.applied_damage = apply_legacy_monster_damage(
        objects, *monster_target, damage, caster.id(), map_config, current_tick, now_ms);
    result.target_died = monster_target->is_dead();
    result.slain_monster_id = result.target_died ? monster_target->id() : 0;
  }

  if (result.applied_damage <= 0) {
    return result;
  }

  if (!result.target_died) {
    for_each_player(objects, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_struck_packet(watcher.session_id(), target, caster.id(),
                                      result.applied_damage, true));
    });
  }
  return result;
}

std::vector<GameObject*> collect_legacy_area_targets(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const Player& caster, const MapConfig& map_config, std::int32_t center_x,
    std::int32_t center_y, std::int32_t wide, bool friends) {
  std::vector<GameObject*> targets;
  for (std::int32_t x = center_x - wide; x <= center_x + wide; ++x) {
    for (std::int32_t y = center_y - wide; y <= center_y + wide; ++y) {
      std::vector<std::uint64_t> ids_at_cell;
      for (const auto& [actor_id, object] : objects) {
        if (object->x() == x && object->y() == y) {
          ids_at_cell.push_back(actor_id);
        }
      }
      std::sort(ids_at_cell.begin(), ids_at_cell.end(), std::greater<>());
      for (const auto actor_id : ids_at_cell) {
        auto& object = *objects.at(actor_id);
        if (!is_attackable_target(object)) {
          continue;
        }
        if (friends) {
          if (as_player(&object) != nullptr) {
            targets.push_back(&object);
          } else if (const auto* monster = as_monster(&object);
                     monster != nullptr && monster->master_actor_id() == caster.id()) {
            targets.push_back(&object);
          }
          continue;
        }
        if (object.id() == caster.id()) {
          continue;
        }
        if (const auto* player_target = as_player(&object); player_target != nullptr) {
          if (!resolve_pk_block_reason(map_config, caster, *player_target).empty()) {
            continue;
          }
        }
        targets.push_back(&object);
      }
    }
  }
  return targets;
}

GameObject* find_legacy_line_target(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const Player& caster, std::int32_t x, std::int32_t y) {
  GameObject* best = nullptr;
  for (auto& [actor_id, object] : objects) {
    if (actor_id == caster.id() || object->x() != x || object->y() != y ||
        !is_attackable_target(*object)) {
      continue;
    }
    if (best == nullptr || actor_id > best->id()) {
      best = object.get();
    }
  }
  return best;
}

std::int32_t legacy_random_packed_power(std::uint16_t value, LegacyRandom& random) {
  const auto low = packed_min(value);
  const auto high = packed_max(value);
  if (high > low) {
    return low + random.random(high - low + 1);
  }
  return low;
}

std::int32_t legacy_fireball_power(const Player& caster, const LegacyMagicDefinition& magic,
                                   std::uint8_t level, LegacyRandom& random) {
  const auto mc_min = packed_min(caster.character().ability.mc);
  const auto mc_max = packed_max(caster.character().ability.mc);
  const auto base = legacy_power(magic, level, legacy_mpow(magic, random), random) + mc_min;
  return legacy_attack_power(base, static_cast<std::int8_t>(mc_max - mc_min) + 1, 0, random);
}

std::int32_t legacy_heal_power(const Player& caster, const LegacyMagicDefinition& magic,
                               std::uint8_t level, LegacyRandom& random) {
  const auto sc_min = packed_min(caster.character().ability.sc);
  const auto sc_max = packed_max(caster.character().ability.sc);
  const auto base = legacy_power(magic, level, legacy_mpow(magic, random), random) + sc_min * 2;
  return legacy_attack_power(base, static_cast<std::int8_t>(sc_max - sc_min) * 2 + 1, 0, random);
}

std::int32_t legacy_open_health_power(const Player& caster, const LegacyMagicDefinition& magic,
                                      std::uint8_t level, LegacyRandom& random) {
  const auto sc = legacy_random_packed_power(caster.character().ability.sc, random);
  return legacy_power13(magic, level, 30 + sc * 2,
                        random.random(magic.def_max_power - magic.def_min_power));
}

std::int32_t legacy_magic_bubble_seconds(const Player& caster,
                                         const LegacyMagicDefinition& magic,
                                         std::uint8_t level, LegacyRandom& random) {
  const auto mc = legacy_random_packed_power(caster.character().ability.mc, random);
  return legacy_power(magic, level, 15 + mc, random);
}

constexpr std::int32_t kLegacyPoisonDecHealth = 0;
constexpr std::int32_t kLegacyPoisonDamageArmor = 1;

struct LegacyBujukSlot {
  std::size_t slot{kMaxEquipSlots};
  LegacyUserItem* item{nullptr};
  const ItemConfig* config{nullptr};
};

bool item_is_bujuk_shape(const LegacyUserItem& item, const ItemConfig* config,
                         std::int32_t required_shape) {
  return !is_empty(item) && config != nullptr && config->std_mode == 25 &&
         config->shape == required_shape;
}

bool item_is_poison_powder(const LegacyUserItem& item, const ItemConfig* config) {
  return !is_empty(item) && config != nullptr && config->std_mode == 25 &&
         config->shape <= 2;
}

std::optional<LegacyBujukSlot> find_legacy_bujuk_slot(
    Player& player, const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    std::int32_t count) {
  const std::array<std::size_t, 2> slots{kEquipBujuk, kEquipArmRingLeft};
  for (const auto slot : slots) {
    auto* item = player.equipped_item_mutable(slot);
    if (item == nullptr) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item->index);
    if (!item_is_bujuk_shape(*item, config, 5)) {
      continue;
    }
    if (delphi_round(static_cast<double>(item->dura) / 100.0) >= std::max(count - 1, 0)) {
      return LegacyBujukSlot{slot, item, config};
    }
  }
  return std::nullopt;
}

std::optional<LegacyBujukSlot> find_legacy_poison_powder_slot(
    Player& player, const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const std::array<std::size_t, 2> slots{kEquipBujuk, kEquipArmRingLeft};
  for (const auto slot : slots) {
    auto* item = player.equipped_item_mutable(slot);
    if (item == nullptr) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item->index);
    if (item_is_poison_powder(*item, config) && item->dura >= 100) {
      return LegacyBujukSlot{slot, item, config};
    }
  }
  return std::nullopt;
}

void consume_legacy_bujuk_slot(LegacyBujukSlot& slot, std::int32_t count) {
  const auto cost = std::max(count, 0) * 100;
  if (slot.item == nullptr || cost <= 0) {
    return;
  }
  slot.item->dura = slot.item->dura >= cost
                        ? static_cast<std::uint16_t>(slot.item->dura - cost)
                        : static_cast<std::uint16_t>(0);
}

std::optional<LegacyUserItem> clear_legacy_bujuk_slot_if_spent(LegacyBujukSlot& slot) {
  if (slot.item == nullptr || slot.item->dura >= 100) {
    return std::nullopt;
  }
  auto removed = *slot.item;
  removed.dura = 0;
  *slot.item = LegacyUserItem{};
  return removed;
}

std::int32_t legacy_soul_fire_power(const Player& caster, const LegacyMagicDefinition& magic,
                                    std::uint8_t level, LegacyRandom& random) {
  const auto sc_min = packed_min(caster.character().ability.sc);
  const auto sc_max = packed_max(caster.character().ability.sc);
  const auto base = legacy_power(magic, level, legacy_mpow(magic, random), random) + sc_min;
  return legacy_attack_power(base, static_cast<std::int8_t>(sc_max - sc_min) + 1, 0, random);
}

std::int32_t legacy_defence_status_seconds(const Player& caster,
                                           const LegacyMagicDefinition& magic,
                                           std::uint8_t level, LegacyRandom& random) {
  const auto sc_min = packed_min(caster.character().ability.sc);
  const auto sc_max = packed_max(caster.character().ability.sc);
  const auto random_value = random.random(magic.def_max_power - magic.def_min_power);
  const auto base = legacy_power13(magic, level, 60, random_value) + 5 * sc_min;
  return legacy_attack_power(base, 5 * (static_cast<std::int8_t>(sc_max - sc_min) + 1), 0,
                             random);
}

std::int32_t legacy_poison_seconds(const Player& caster, const LegacyMagicDefinition& magic,
                                   std::uint8_t level, std::int32_t base_seconds,
                                   LegacyRandom& random) {
  const auto sc = legacy_random_packed_power(caster.character().ability.sc, random);
  const auto random_value = random.random(magic.def_max_power - magic.def_min_power);
  return legacy_power13(magic, level, base_seconds, random_value) + 2 * sc;
}

std::int32_t legacy_transparent_seconds(const Player& caster,
                                        const LegacyMagicDefinition& magic,
                                        std::uint8_t level, LegacyRandom& random) {
  const auto sc = legacy_random_packed_power(caster.character().ability.sc, random);
  const auto random_value = random.random(magic.def_max_power - magic.def_min_power);
  return legacy_power13(magic, level, 30, random_value) + 3 * sc;
}

std::int32_t legacy_fire_wall_seconds(const Player& caster,
                                      const LegacyMagicDefinition& magic,
                                      std::uint8_t level, LegacyRandom& random) {
  const auto mc = legacy_random_packed_power(caster.character().ability.mc, random);
  return legacy_power(magic, level, 10, random) + mc / 2;
}

std::int32_t legacy_holy_curtain_seconds(const Player& caster,
                                         const LegacyMagicDefinition& magic,
                                         std::uint8_t level, LegacyRandom& random) {
  const auto sc = legacy_random_packed_power(caster.character().ability.sc, random);
  const auto random_value = random.random(magic.def_max_power - magic.def_min_power);
  return legacy_power13(magic, level, 40, random_value) + 3 * sc;
}

std::int32_t calc_get_exp(std::int32_t attacker_level, std::int32_t target_level,
                          std::int32_t fight_exp) {
  const auto base = std::max(fight_exp, 1);
  if (attacker_level < target_level + 10) {
    return base;
  }
  const auto reduced =
      base - delphi_round((static_cast<double>(base) / 15.0) *
                          static_cast<double>(attacker_level - (target_level + 10)));
  return std::max(reduced, 1);
}

std::int32_t legacy_accuracy_point(const Player& attacker) {
  return std::max(attacker.accuracy_point(), 1);
}

std::int32_t legacy_speed_point(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return std::max(player->speed_point(), 1);
  }
  return 10;
}

std::uint64_t ms_to_logic_ticks(std::uint32_t value_ms, std::uint32_t tick_ms) {
  if (value_ms == 0 || tick_ms == 0) {
    return 0;
  }
  return std::max<std::uint64_t>(1, (static_cast<std::uint64_t>(value_ms) +
                                     static_cast<std::uint64_t>(tick_ms) - 1) /
                                        static_cast<std::uint64_t>(tick_ms));
}

}  // namespace

MapActor::MapActor(MapConfig config, LogicBudgetConfig budgets,
                   std::unordered_map<std::int32_t, ItemConfig> item_configs,
                   std::unordered_map<std::int32_t, MagicConfig> magic_configs,
                   std::vector<MapQuestConfig> map_quests,
                   CastleDialogContext castle_dialog_context,
                   std::unordered_map<std::string, MonsterDefConfig> monster_defs,
                   std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules,
                   MakeIndexAllocator* make_index_allocator,
                   std::string black_stone_name,
                   bool legacy_approval_mode,
                   std::shared_ptr<std::array<std::int32_t, 10>> script_global_params)
    : config_(std::move(config)),
      budgets_(std::move(budgets)),
      item_configs_(std::move(item_configs)),
      magic_configs_(std::move(magic_configs)),
      monster_defs_(std::move(monster_defs)),
      map_quests_(std::move(map_quests)),
      map_entry_rules_(std::move(map_entry_rules)),
      black_stone_name_(std::move(black_stone_name)),
      legacy_approval_mode_(legacy_approval_mode),
      castle_dialog_context_(std::move(castle_dialog_context)),
      make_index_allocator_(make_index_allocator),
      script_global_params_(std::move(script_global_params)) {
  if (script_global_params_ == nullptr) {
    script_global_params_ = std::make_shared<std::array<std::int32_t, 10>>();
  }
  movement_map_ = legacy::decode_map_file(config_.source_map);
  if (movement_map_ != nullptr) {
    if (config_.width <= 0) {
      config_.width = movement_map_->width;
    }
    if (config_.height <= 0) {
      config_.height = movement_map_->height;
    }
  }
  environment_.reset(config_.width, config_.height, movement_map_);
  for (std::size_t index = 0; index < config_.gates.size(); ++index) {
    const auto& gate = config_.gates[index];
    static_cast<void>(environment_.add_gate_object(
        gate.x, gate.y, kStaticGateObjectBase + index,
        LegacyMapGateState{gate.target_map_id, gate.target_x, gate.target_y,
                           gate.require_doors_open},
        0));
  }
  guild_castle_snapshot_.castle_dialog = castle_dialog_context_;
}

void MapActor::enqueue_mail(ActorMail mail) { mailbox_.push_back(std::move(mail)); }

void MapActor::set_legacy_random(LegacyRandom* legacy_random) { legacy_random_ = legacy_random; }

void MapActor::set_legacy_script_map_hooks(LegacyScriptMapHooks hooks) {
  legacy_script_map_hooks_ = std::move(hooks);
}

std::int32_t MapActor::allocate_make_index() {
  return make_index_allocator_ != nullptr ? make_index_allocator_->allocate()
                                          : fallback_make_index_allocator_.allocate();
}

bool MapActor::apply_merchant_state(const MerchantStateRecord& state) {
  if (!state.map_id.empty() && state.map_id != config_.id) {
    return false;
  }
  for (auto& [_, object] : objects_) {
    auto* merchant = as_npc(object.get());
    if (merchant == nullptr || merchant->merchant_key() != state.merchant_key) {
      continue;
    }
    merchant->apply_merchant_state(state);
    return true;
  }
  return false;
}

bool MapActor::legacy_add_event_object(std::uint64_t event_id, std::int32_t x, std::int32_t y,
                                       std::uint64_t now_ms, bool blocks_walk,
                                       RuntimeDispatch* dispatch,
                                       LegacyEventType type) {
  const auto placement_policy = type == LegacyEventType::stone_mine
                                    ? LegacyMapPlacementPolicy::blocked_only
                                    : LegacyMapPlacementPolicy::passable_only;
  const auto added = environment_.add_placeholder_object(x, y, LegacyMapObjectShape::event_object,
                                                        event_id, now_ms, placement_policy,
                                                        blocks_walk);
  if (added) {
    event_objects_[event_id] = {x, y};
    event_object_types_[event_id] = type;
    if (dispatch != nullptr) {
      sync_visibility_after_event_change(x, y, *dispatch);
    }
  }
  return added;
}

bool MapActor::legacy_add_event_object(std::uint64_t event_id, std::int32_t x, std::int32_t y,
                                       std::uint64_t now_ms, RuntimeDispatch* dispatch) {
  return legacy_add_event_object(event_id, x, y, now_ms, false, dispatch,
                                 LegacyEventType::pile_stones);
}

void MapActor::legacy_remove_event_object(std::uint64_t event_id, std::int32_t x,
                                          std::int32_t y, RuntimeDispatch* dispatch) {
  static_cast<void>(
      environment_.delete_from_map(x, y, LegacyMapObjectShape::event_object, event_id));
  event_objects_.erase(event_id);
  event_object_types_.erase(event_id);
  if (dispatch != nullptr) {
    for (auto& [_, visibility] : visibility_) {
      visibility.events.erase(event_id);
    }
    sync_visibility_after_event_change(x, y, *dispatch);
  }
}

RuntimeDispatch MapActor::legacy_apply_fire_burn_event(const LegacyEventRecord& event,
                                                       std::uint64_t current_tick,
                                                       std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  if (event.type != LegacyEventType::fire_burn || event.map_id != config_.id ||
      event.damage <= 0) {
    return dispatch;
  }
  auto* caster = find_player(event.owner_actor_id);
  if (caster == nullptr || caster->is_dead()) {
    return dispatch;
  }

  LegacyRandom fallback_random;
  auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
  std::vector<std::uint64_t> ids_at_cell;
  for (const auto& [actor_id, object] : objects_) {
    if (object->x() == event.x && object->y() == event.y) {
      ids_at_cell.push_back(actor_id);
    }
  }
  std::sort(ids_at_cell.begin(), ids_at_cell.end(), std::greater<>());
  for (const auto target_id : ids_at_cell) {
    const auto target_it = objects_.find(target_id);
    if (target_it == objects_.end()) {
      continue;
    }
    auto& target = *target_it->second;
    if (target.id() == caster->id() || target.x() != event.x || target.y() != event.y ||
        !is_attackable_target(target)) {
      continue;
    }
    if (auto* player_target = as_player(&target); player_target != nullptr) {
      if (!resolve_pk_block_reason(config_, *caster, *player_target, now_ms).empty()) {
        continue;
      }
    }

    const auto damage =
        legacy_magic_defense_damage(target, event.damage, random, current_tick, budgets_.tick_ms);
    const auto result = apply_legacy_magic_damage(objects_, item_configs_, dispatch, *caster,
                                                  target, config_, damage, current_tick, now_ms);
    if (result.applied_damage > 0 && as_monster(&target) != nullptr) {
      notify_owned_slaves_target(*caster, target.id(), now_ms);
    }
    if (result.target_died) {
      if (auto* player_target = as_player(&target); player_target != nullptr) {
        apply_bad_kill_penalty(*caster, *player_target, dispatch, current_tick, now_ms,
                               "LegacyFireBurn");
        static_cast<void>(settle_player_death(*player_target, dispatch, current_tick, now_ms));
        queue_legacy_death_packet(objects_, dispatch, target);
      }
    }
    if (result.slain_monster_id != 0) {
      auto pending_death_packets = collect_legacy_death_packets(objects_, target);
      finalize_monster_death(result.slain_monster_id, caster->id(), dispatch, current_tick);
      add_legacy_trace(dispatch, "LegacyEventManager", "fire_burn_exp", ActorMail{},
                       current_tick, now_ms, true, static_cast<std::int32_t>(event.id),
                       result.applied_damage, "WinExp");
      queue_legacy_packets(dispatch, std::move(pending_death_packets));
    }
    if (result.applied_damage > 0) {
      add_legacy_trace(dispatch, "LegacyEventManager",
                       result.target_died ? "fire_burn_death" : "fire_burn_struck",
                       ActorMail{}, current_tick, now_ms, true,
                       static_cast<std::int32_t>(target.id()), result.applied_damage,
                       "RM_MAGSTRUCK_MINE");
    }
  }
  return dispatch;
}

std::vector<std::uint64_t> MapActor::legacy_active_holy_seize_actor_ids(
    const std::vector<std::uint64_t>& actor_ids, std::uint64_t now_ms) const {
  std::vector<std::uint64_t> active;
  for (const auto actor_id : actor_ids) {
    const auto it = objects_.find(actor_id);
    const auto* monster = it != objects_.end() ? as_monster(it->second.get()) : nullptr;
    if (monster != nullptr && !monster->is_dead() &&
        monster->legacy_holy_seize_active(now_ms)) {
      active.push_back(actor_id);
    }
  }
  return active;
}

std::optional<std::pair<std::int32_t, std::int32_t>>
MapActor::legacy_random_space_move_target(LegacyRandom& random) const {
  const auto width = movement_width();
  const auto height = movement_height();
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  const auto edge_y = height < 150 ? (height < 30 ? 2 : 20) : 50;
  auto nx = edge_y + random.random(std::max(1, width - edge_y - 1));
  auto ny = edge_y + random.random(std::max(1, height - edge_y - 1));
  const auto step = width < 80 ? 3 : 10;
  const auto edge = height < 150 ? (height < 50 ? 2 : 15) : 50;
  for (std::int32_t attempt = 0; attempt <= 200; ++attempt) {
    if (environment_.can_walk(nx, ny, true)) {
      return std::pair{nx, ny};
    }
    if (nx < width - edge - 1) {
      nx += step;
    } else {
      nx = random.random(std::max(1, width));
      if (ny < height - edge - 1) {
        ny += step;
      } else {
        ny = random.random(std::max(1, height));
      }
    }
  }
  return std::nullopt;
}

RuntimeDispatch MapActor::legacy_space_move_player(
    std::uint64_t actor_id, const std::string& target_map_id, std::int32_t target_x,
    std::int32_t target_y, bool show2, std::uint64_t current_tick, std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    return dispatch;
  }

  auto snapshot = player->persistent_snapshot();
  snapshot.map_id = target_map_id.empty() ? config_.id : target_map_id;
  snapshot.x = target_x;
  snapshot.y = target_y;
  snapshot.dir = player->character().dir;
  snapshot.slaves = snapshot_owned_slaves(*player, now_ms);

  if (snapshot.map_id == config_.id) {
    if (!environment_.in_bounds(snapshot.x, snapshot.y) ||
        !environment_.can_walk(snapshot.x, snapshot.y, true)) {
      return dispatch;
    }
    const auto old_x = player->x();
    const auto old_y = player->y();
    if (environment_.move_to_moving_object(old_x, old_y, player->id(), snapshot.x, snapshot.y,
                                           true, now_ms, moving_state_for(*player)) != 1) {
      return dispatch;
    }
    ActorMail move_mail;
    move_mail.kind = ActorMailKind::move;
    move_mail.map_id = config_.id;
    move_mail.actor_id = player->id();
    move_mail.session_id = player->session_id();
    move_mail.x = snapshot.x;
    move_mail.y = snapshot.y;
    move_mail.dir = snapshot.dir;
    MapContext context;
    context.tick = current_tick;
    context.map_id = config_.id;
    context.dispatch = &dispatch;
    context.items = &item_configs_;
    context.magics = &magic_configs_;
    player->on_mail(move_mail, context);
    force_refresh_after_same_map_transfer(
        *player, old_x, old_y, dispatch, now_ms,
        show2 ? kSmSpaceMoveHide2 : kSmSpaceMoveHide,
        show2 ? kSmSpaceMoveShow2 : kSmSpaceMoveShow);
    recall_owned_slaves_to_master(*player, dispatch, current_tick, now_ms);
    dispatch.audit_events.push_back(AuditEvent{
        "world.space_move", snapshot.account_id + ":" + snapshot.character_name, config_.id});
    return dispatch;
  }

  const auto leave_clear = player->clear_legacy_buffs_on_leave_map(current_tick);
  dispatch_player_status_tick_result(*player, leave_clear, dispatch, false);

  ActorMail transfer;
  transfer.kind = ActorMailKind::spawn_player;
  transfer.map_id = snapshot.map_id;
  transfer.actor_id = player->id();
  transfer.session_id = player->session_id();
  transfer.name = snapshot.character_name;
  transfer.x = snapshot.x;
  transfer.y = snapshot.y;
  transfer.dir = snapshot.dir;
  transfer.character = snapshot;
  transfer.legacy_buffs = player->legacy_buffs_for_transfer(current_tick);
  transfer.legacy_name_color = player->legacy_name_color();
  transfer.legacy_space_move_show2 = show2;

  if (show2) {
    queue_actor_origin_packet(objects_, dispatch, *player, true,
                              [&](const Player& watcher) {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_hide2_packet(watcher.session_id(), *player));
    });
  } else {
    queue_actor_origin_packet(objects_, dispatch, *player, true,
                              [&](const Player& watcher) {
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_hide_packet(watcher.session_id(), *player));
    });
  }

  queue_packet(dispatch, player->session_id(), make_clear_objects_packet(player->session_id()));
  queue_packet(dispatch, player->session_id(),
               make_change_map_packet(player->session_id(), snapshot.map_id));
  queue_save_character(dispatch, snapshot);
  detach_owned_slaves(*player, dispatch, now_ms, true);
  remove_actor_from_visibility(player->id(), dispatch);
  static_cast<void>(environment_.delete_from_map(player->x(), player->y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player->id()));
  visibility_.erase(player->id());
  objects_.erase(actor_id);
  dispatch.cross_map_mails.push_back(std::move(transfer));
  dispatch.audit_events.push_back(AuditEvent{
      "world.space_move", snapshot.account_id + ":" + snapshot.character_name, snapshot.map_id});
  return dispatch;
}

void MapActor::set_castle_dialog_context(CastleDialogContext castle_dialog_context) {
  castle_dialog_context_ = std::move(castle_dialog_context);
  guild_castle_snapshot_.castle_dialog = castle_dialog_context_;
}

void MapActor::set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot) {
  guild_castle_snapshot_ = std::move(guild_castle_snapshot);
  castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
}

std::optional<CharacterRecord> MapActor::snapshot_player(std::uint64_t actor_id) const {
  const auto* player = find_player(actor_id);
  if (player == nullptr) {
    return std::nullopt;
  }
  return player->snapshot();
}

std::optional<CharacterRecord> MapActor::persistent_snapshot_player(std::uint64_t actor_id,
                                                                    std::uint64_t now_ms) {
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    return std::nullopt;
  }
  return snapshot_player_with_slaves(*player, now_ms);
}

RuntimeDispatch MapActor::legacy_spawn_player(const ActorMail& mail,
                                              std::uint64_t current_tick,
                                              std::uint64_t now_ms,
                                              bool fast_initialize) {
  RuntimeDispatch dispatch;
  if (mail.kind != ActorMailKind::spawn_player) {
    return dispatch;
  }
  handle_mail(mail, dispatch, current_tick, now_ms, true);
  auto* player = find_player(mail.actor_id);
  if (player == nullptr) {
    return dispatch;
  }
  if (fast_initialize) {
    dispatch_legacy_run_notice(*player, dispatch, now_ms);
    dispatch_legacy_initialize(*player, dispatch, now_ms);
  }
  if (mail.legacy_space_move_show2) {
    bool sent_self = false;
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!is_legacy_visible_to(watcher, *player)) {
        return;
      }
      if (watcher.session_id() == player->session_id()) {
        sent_self = true;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_space_move_show2_packet(watcher.session_id(), *player));
    });
    if (!sent_self) {
      queue_packet(dispatch, player->session_id(),
                   make_space_move_show2_packet(player->session_id(), *player));
    }
  }
  static_cast<void>(
      trigger_map_quest(*player, {}, {}, false, "enter", dispatch, current_tick, now_ms));
  return dispatch;
}

RuntimeDispatch MapActor::legacy_process_player(std::uint64_t actor_id,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms,
                                                bool persistence_overloaded,
                                                std::size_t player_input_budget_per_tick) {
  RuntimeDispatch dispatch;
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    return dispatch;
  }

  switch (player->legacy_state()) {
    case LegacyPlayerState::notice_pending:
      dispatch_legacy_run_notice(*player, dispatch, now_ms);
      break;
    case LegacyPlayerState::initialize_pending:
      dispatch_legacy_initialize(*player, dispatch, now_ms);
      break;
    case LegacyPlayerState::running: {
      legacy_operate_player_running(actor_id, *player, dispatch, current_tick, now_ms,
                                    persistence_overloaded, player_input_budget_per_tick);
      break;
    }
    case LegacyPlayerState::ghost:
      dispatch_legacy_close(*player, dispatch);
      break;
    default:
      break;
  }
  return dispatch;
}

RuntimeDispatch MapActor::legacy_process_monster(std::uint64_t actor_id,
                                                 std::uint64_t current_tick,
                                                 std::uint64_t now_ms,
                                                 std::size_t cursor,
                                                 std::size_t sub_cursor) {
  RuntimeDispatch dispatch;
  auto trace = LegacyRuntimeTrace{"ProcessMonsters", "missing", config_.id, {}, actor_id,
                                  now_ms, current_tick, cursor, sub_cursor, 0};
  const auto started = std::chrono::steady_clock::now();
  auto object_it = objects_.find(actor_id);
  if (object_it == objects_.end()) {
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  auto* monster = as_monster(object_it->second.get());
  if (monster == nullptr) {
    trace.action = "not_monster";
    trace.object_name = object_it->second->name();
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }

  trace.object_name = monster->name();
  if (!monster->is_dead() && handle_slave_lifecycle(*monster, dispatch, current_tick, now_ms)) {
    if (!monster->death_settled()) {
      finalize_monster_death(monster->id(), monster->last_hitter_id(), dispatch, current_tick);
    }
    trace.action = "slave_death";
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  if (!monster->is_dead()) {
    monster->expire_legacy_hitters(now_ms);
  }
  if (monster->is_dead()) {
    if (!monster->death_settled()) {
      finalize_monster_death(monster->id(), monster->last_hitter_id(), dispatch, current_tick);
    }
    if (monster->death_due_for_ghost(now_ms, kMonsterCorpseMs)) {
      trace.action = "ghost";
      finalize_monster_ghost(monster->id(), dispatch, current_tick, now_ms);
    } else {
      trace.action = "death_wait";
    }
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }

  const auto status_due = monster->next_status_tick() != 0 &&
                          monster->next_status_tick() <= current_tick;
  const auto run_due = monster->legacy_due(now_ms);
  if (!status_due && !run_due) {
    trace.action = "skip";
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }

  if (run_due) {
    monster->mark_legacy_run_time(now_ms);
    if (monster->legacy_search_due(now_ms)) {
      monster->mark_legacy_search_time(now_ms);
      legacy_refresh_monster_visible_actors(*monster);
    }
    handle_monster_ai(*monster, dispatch, current_tick, now_ms);
  }
  if (!monster->is_dead() &&
      handle_monster_status_effects(*monster, dispatch, current_tick, now_ms)) {
    trace.action = "status_death";
  } else {
    MapContext context;
    context.tick = current_tick;
    context.map_id = config_.id;
    context.dispatch = &dispatch;
    context.items = &item_configs_;
    context.magics = &magic_configs_;
    if (objects_.contains(actor_id)) {
      monster->on_tick(context);
    }
    trace.action = run_due ? "run" : "status";
  }
  trace.elapsed_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  dispatch.legacy_traces.push_back(std::move(trace));
  return dispatch;
}

RuntimeDispatch MapActor::legacy_process_merchant(std::uint64_t actor_id,
                                                  std::uint64_t current_tick,
                                                  std::uint64_t now_ms,
                                                  std::size_t cursor) {
  RuntimeDispatch dispatch;
  auto trace = LegacyRuntimeTrace{"ProcessMerchants", "missing", config_.id, {}, actor_id,
                                  now_ms, current_tick, cursor, 0, 0};
  const auto started = std::chrono::steady_clock::now();
  auto object_it = objects_.find(actor_id);
  if (object_it == objects_.end()) {
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  auto* merchant = as_npc(object_it->second.get());
  if (merchant == nullptr) {
    trace.action = "not_npc";
    trace.object_name = object_it->second->name();
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }

  trace.object_name = merchant->name();
  if (!merchant->legacy_due(now_ms)) {
    trace.action = "skip";
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  if (merchant->legacy_search_due(now_ms)) {
    merchant->mark_legacy_search_time(now_ms);
  }
  constexpr std::uint64_t kMerchantRefillMs = 5ULL * 60ULL * 1000ULL;
  constexpr std::uint64_t kMerchantVerifyMs = 10ULL * 60ULL * 1000ULL;
  if (merchant->legacy_refill_time_ms() == 0 ||
      now_ms > merchant->legacy_refill_time_ms() + kMerchantRefillMs) {
    bool changed = false;
    std::unordered_set<std::int32_t> product_item_ids;
    for (auto& product : merchant->merchant_products_mutable()) {
      if (product.item_id <= 0 || product.target_count < 0) {
        continue;
      }
      product_item_ids.insert(product.item_id);
      const auto refresh_ms = product.refresh_ms == 0 ? kMerchantRefillMs : product.refresh_ms;
      if (product.last_refill_ms != 0 && now_ms <= product.last_refill_ms + refresh_ms) {
        continue;
      }
      product.last_refill_ms = now_ms;
      const auto stock = merchant_stock_count(*merchant, product.item_id);
      if (stock < product.target_count) {
        const auto* config = find_item_config(item_configs_, product.item_id);
        if (config != nullptr) {
          static_cast<void>(price_up(*merchant, product.item_id, item_configs_));
          auto& goods = merchant->merchant_items_mutable();
          auto insert_at =
              std::find_if(goods.begin(), goods.end(), [&](const LegacyUserItem& existing) {
                return !is_empty(existing) && existing.index == product.item_id;
              });
          for (std::int32_t i = stock; i < product.target_count; ++i) {
            insert_at =
                goods.insert(insert_at, make_runtime_merchant_item(*config, allocate_make_index()));
            ++insert_at;
          }
          changed = true;
        }
      } else if (stock > product.target_count) {
        trim_merchant_item_tail(*merchant, product.item_id, stock - product.target_count);
        changed = true;
      }
    }

    std::unordered_map<std::int32_t, std::int32_t> counts;
    for (const auto& item : merchant->merchant_items()) {
      if (!is_empty(item)) {
        ++counts[item.index];
      }
    }
    for (const auto& [item_id, count] : counts) {
      const auto cap = product_item_ids.find(item_id) != product_item_ids.end() ? 5000 : 1000;
      if (count > cap) {
        trim_merchant_item_tail(*merchant, item_id, count - cap);
        changed = true;
      }
    }
    if (changed) {
      dispatch.persist_requests.push_back(make_save_merchant_state_request(*merchant));
    }
    merchant->mark_legacy_refill_time(now_ms);
  }
  if (merchant->legacy_verify_time_ms() == 0 ||
      now_ms > merchant->legacy_verify_time_ms() + kMerchantVerifyMs) {
    merchant->mark_legacy_verify_time(now_ms);
  }

  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;
  merchant->on_tick(context);
  merchant->mark_legacy_run_time(now_ms);
  trace.action = "run";
  trace.elapsed_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  dispatch.legacy_traces.push_back(std::move(trace));
  return dispatch;
}

RuntimeDispatch MapActor::legacy_process_npc(std::uint64_t actor_id,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms,
                                             std::size_t cursor) {
  RuntimeDispatch dispatch;
  auto trace = LegacyRuntimeTrace{"ProcessNpcs", "missing", config_.id, {}, actor_id,
                                  now_ms, current_tick, cursor, 0, 0};
  const auto started = std::chrono::steady_clock::now();
  auto object_it = objects_.find(actor_id);
  if (object_it == objects_.end()) {
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  auto* npc = as_npc(object_it->second.get());
  if (npc == nullptr) {
    trace.action = "not_npc";
    trace.object_name = object_it->second->name();
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }

  trace.object_name = npc->name();
  if (!npc->legacy_due(now_ms)) {
    trace.action = "skip";
    dispatch.legacy_traces.push_back(std::move(trace));
    return dispatch;
  }
  if (npc->legacy_search_due(now_ms)) {
    npc->mark_legacy_search_time(now_ms);
  }
  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;
  npc->on_tick(context);
  npc->mark_legacy_run_time(now_ms);
  trace.action = "run";
  trace.elapsed_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  dispatch.legacy_traces.push_back(std::move(trace));
  return dispatch;
}

bool MapActor::enqueue_legacy_player_command(const ActorMail& mail, std::uint64_t now_ms) {
  if (!is_legacy_player_command(mail.kind)) {
    return false;
  }
  auto* player = find_player(mail.actor_id);
  if (player == nullptr || player->legacy_ghost()) {
    return false;
  }
  player->enqueue_legacy_command(mail, now_ms);
  if (player->legacy_ready_run() && is_legacy_response_compensated_command(mail.kind)) {
    player->rewind_legacy_run_time(std::max<std::uint64_t>(
        100, player->legacy_run_next_tick_ms() + 1));
  }
  return true;
}

bool MapActor::mark_legacy_player_ghost(std::uint64_t actor_id, std::uint64_t now_ms) {
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    return false;
  }
  player->mark_legacy_ghost(now_ms);
  return true;
}

RuntimeDispatch MapActor::legacy_disconnect_player(std::uint64_t actor_id, std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    return dispatch;
  }
  const auto session_id = player->session_id();
  const auto logout_clear = player->clear_legacy_buffs_on_logout(0);
  dispatch_player_status_tick_result(*player, logout_clear, dispatch, false);
  cancel_trade_for(actor_id, dispatch, true);
  queue_save_player_character(dispatch, *player, now_ms);
  detach_owned_slaves(*player, dispatch, now_ms, true);
  queue_force_disconnect(dispatch, session_id, "legacy_player_disconnected");
  static_cast<void>(environment_.delete_from_map(player->x(), player->y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player->id()));
  remove_actor_from_visibility(player->id(), dispatch);
  visibility_.erase(player->id());
  objects_.erase(actor_id);
  sync_all_player_visibility(dispatch);
  return dispatch;
}

std::optional<LegacyPlayerState> MapActor::legacy_player_state(std::uint64_t actor_id) const {
  const auto* player = find_player(actor_id);
  if (player == nullptr) {
    return std::nullopt;
  }
  return player->legacy_state();
}

std::size_t MapActor::legacy_player_inbox_size(std::uint64_t actor_id) const {
  const auto* player = find_player(actor_id);
  return player != nullptr ? player->legacy_inbox_size() : 0;
}

std::vector<std::uint64_t> MapActor::legacy_player_inbox_session_sequences(
    std::uint64_t actor_id) const {
  const auto* player = find_player(actor_id);
  return player != nullptr ? player->legacy_inbox_session_sequences()
                           : std::vector<std::uint64_t>{};
}

std::int64_t MapActor::legacy_player_run_time_ms(std::uint64_t actor_id) const {
  const auto* player = find_player(actor_id);
  return player != nullptr ? player->legacy_run_time_ms() : 0;
}

RuntimeDispatch MapActor::tick(std::uint64_t current_tick) {
  const auto now_ms = current_tick * static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1));
  return tick(current_tick, now_ms);
}

RuntimeDispatch MapActor::drain_pending_mail(std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  for (auto& delayed_mail : delayed_mail_wheel_.pop_ready(current_tick)) {
    mailbox_.push_back(std::move(delayed_mail));
  }

  while (!mailbox_.empty()) {
    ActorMail mail = std::move(mailbox_.front());
    mailbox_.pop_front();
    LegacyRuntimeTrace trace;
    trace.stage = "MapMailbox";
    trace.action = "drain";
    trace.map_id = config_.id;
    trace.actor_id = mail.actor_id;
    trace.target_actor_id = mail.target_actor_id;
    trace.now_ms = now_ms;
    trace.current_tick = current_tick;
    trace.success = true;
    dispatch.legacy_traces.push_back(std::move(trace));
    handle_mail(mail, dispatch, current_tick, now_ms);
  }

  return dispatch;
}

RuntimeDispatch MapActor::run_maintenance_tick(std::uint64_t current_tick,
                                               std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;

  remove_expired_ground_items(dispatch, now_ms);

  std::unordered_map<GameObjectKind, std::uint64_t> consumed_budget{};
  for (const auto actor_id : object_wheel_.pop_ready(current_tick)) {
    auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }

    auto& object = *object_it->second;
    const auto limit_ms = budget_for(object.kind());
    if (consumed_budget[object.kind()] >= limit_ms) {
      object_wheel_.schedule(current_tick, 1, actor_id);
      continue;
    }

    const auto start = std::chrono::steady_clock::now();
    if (auto* player = as_player(&object); player != nullptr) {
      schedule_actor(current_tick, *player);
      continue;
    } else if (auto* monster = as_monster(&object); monster != nullptr) {
      static_cast<void>(monster);
      continue;
    } else if (object.kind() == GameObjectKind::npc) {
      continue;
    } else {
      object.on_tick(context);
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();
    consumed_budget[object.kind()] += static_cast<std::uint64_t>(elapsed);
    schedule_actor(current_tick, object);
  }

  return dispatch;
}

RuntimeDispatch MapActor::tick(std::uint64_t current_tick, std::uint64_t now_ms) {
  auto dispatch = drain_pending_mail(current_tick, now_ms);
  append_runtime_dispatch(dispatch, run_maintenance_tick(current_tick, now_ms));
  return dispatch;
}

void MapActor::refresh_ground_item_ownership(GroundItem& item, std::uint64_t now_ms) {
  if (item.owner_actor_id == 0) {
    return;
  }
  const auto expire_ms = item.ownership_expire_ms != 0
                             ? item.ownership_expire_ms
                             : item.drop_time_ms + kLegacyDropOwnerMs;
  if (expire_ms != 0 && now_ms > expire_ms) {
    item.owner_actor_id = 0;
    item.ownership_expire_ms = 0;
  }
}

void MapActor::remove_expired_ground_items(RuntimeDispatch& dispatch, std::uint64_t now_ms) {
  std::vector<std::uint64_t> expired_ids;
  for (auto& [item_id, item] : ground_items_) {
    refresh_ground_item_ownership(item, now_ms);
    if (item.expire_time_ms != 0 && now_ms > item.expire_time_ms) {
      expired_ids.push_back(item_id);
    }
  }

  for (const auto item_id : expired_ids) {
    const auto item_it = ground_items_.find(item_id);
    if (item_it == ground_items_.end()) {
      continue;
    }
    const auto item = item_it->second;
    static_cast<void>(environment_.delete_from_map(
        item.x, item.y, LegacyMapObjectShape::item_object, item.id));
    remove_item_from_visibility(item.id, dispatch);
    ground_items_.erase(item_it);
  }
}

RuntimeDispatch MapActor::close_expired_doors(std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  const auto closed_doors = environment_.close_expired_doors(now_ms, kDoorAutoCloseMs);
  if (!closed_doors.empty()) {
    broadcast_close_doors(closed_doors, dispatch);
  }
  return dispatch;
}

bool MapActor::legacy_monster_alive(std::uint64_t actor_id) const {
  const auto it = objects_.find(actor_id);
  if (it == objects_.end()) {
    return false;
  }
  const auto* monster = as_monster(it->second.get());
  return monster != nullptr && !monster->legacy_ghosted();
}

bool MapActor::legacy_monster_counts_for_spawn(std::uint64_t actor_id) const {
  const auto it = objects_.find(actor_id);
  if (it == objects_.end()) {
    return false;
  }
  const auto* monster = as_monster(it->second.get());
  return monster != nullptr && !monster->legacy_ghosted() && !monster->is_dead();
}

std::int32_t MapActor::legacy_live_monster_count() const {
  std::int32_t count = 0;
  for (const auto& [_, object] : objects_) {
    const auto* monster = as_monster(object.get());
    if (monster != nullptr && !monster->legacy_ghosted() && !monster->is_dead()) {
      ++count;
    }
  }
  return count;
}

std::int32_t MapActor::legacy_live_player_count() const {
  std::int32_t count = 0;
  for (const auto& [_, object] : objects_) {
    const auto* player = as_player(object.get());
    if (player != nullptr && !player->is_dead()) {
      ++count;
    }
  }
  return count;
}

std::int32_t MapActor::legacy_clear_monsters(RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  static_cast<void>(current_tick);
  static_cast<void>(now_ms);
  std::vector<std::uint64_t> remove_ids;
  for (const auto& [actor_id, object] : objects_) {
    if (as_monster(object.get()) != nullptr) {
      remove_ids.push_back(actor_id);
    }
  }
  for (const auto actor_id : remove_ids) {
    if (const auto it = objects_.find(actor_id); it != objects_.end()) {
      static_cast<void>(environment_.delete_from_map(
          it->second->x(), it->second->y(), LegacyMapObjectShape::moving_object,
          it->second->id()));
      remove_actor_from_visibility(actor_id, dispatch);
      objects_.erase(it);
    }
  }
  return static_cast<std::int32_t>(remove_ids.size());
}

std::optional<MonsterSnapshot> MapActor::legacy_monster_snapshot(std::uint64_t actor_id) const {
  const auto it = objects_.find(actor_id);
  if (it == objects_.end()) {
    return std::nullopt;
  }
  const auto* monster = as_monster(it->second.get());
  if (monster == nullptr) {
    return std::nullopt;
  }
  return monster->snapshot();
}

bool MapActor::legacy_set_player_slave_relax(std::uint64_t actor_id, bool value) {
  const auto it = objects_.find(actor_id);
  if (it == objects_.end()) {
    return false;
  }
  auto* player = as_player(it->second.get());
  if (player == nullptr) {
    return false;
  }
  player->set_legacy_slave_relax(value);
  return true;
}

bool MapActor::legacy_player_tracks_event(std::uint64_t actor_id, std::uint64_t event_id) const {
  const auto visibility_it = visibility_.find(actor_id);
  return visibility_it != visibility_.end() && visibility_it->second.events.contains(event_id);
}

bool MapActor::legacy_can_spawn_monster(std::int32_t x, std::int32_t y) const {
  return environment_.can_walk(x, y, true);
}

#include "world/map_actor_visibility.hpp"
#include "world/map_actor_movement.hpp"
void MapActor::notify_player_and_watchers(RuntimeDispatch& dispatch, const Player& player,
                                          const std::string& self_message,
                                          const std::string& watcher_message) const {
  if (!self_message.empty()) {
    queue_packet(dispatch, player.session_id(),
                 make_system_notice_packet(player.session_id(), self_message));
  }
  if (watcher_message.empty()) {
    return;
  }
  for_each_player(objects_, [&](std::uint64_t actor_id, const Player& watcher) {
    if (actor_id == player.id() || !in_interaction_range(player, watcher)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 make_system_notice_packet(watcher.session_id(), watcher_message));
  });
}

void MapActor::dispatch_player_status_tick_result(Player& player,
                                                  const StatusTickResult& result,
                                                  RuntimeDispatch& dispatch,
                                                  bool include_health) const {
  queue_player_status_tick_result(objects_, dispatch, player, result, include_health);
}

void MapActor::broadcast_legacy_char_status_changed(RuntimeDispatch& dispatch,
                                                    const Player& player) const {
  queue_packet(dispatch, player.session_id(),
               make_char_status_changed_packet(player.session_id(), player));
  for_each_player(objects_, [&](std::uint64_t actor_id, const Player& watcher) {
    if (actor_id == player.id() || !is_legacy_visible_to(watcher, player)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 make_char_status_changed_packet(watcher.session_id(), player));
  });
}

#include "world/map_actor_mail.hpp"
#include "world/map_actor_player.hpp"
#include "world/map_actor_monster.hpp"
void MapActor::schedule_actor(std::uint64_t current_tick, const GameObject& object) {
  const auto due_tick = object.next_due_tick() > current_tick ? object.next_due_tick() : current_tick + 1;
  object_wheel_.schedule(current_tick, due_tick - current_tick, object.id());
}

std::uint64_t MapActor::budget_for(GameObjectKind kind) const {
  switch (kind) {
    case GameObjectKind::player:
      return budgets_.player_budget_ms;
    case GameObjectKind::monster:
      return budgets_.monster_budget_ms;
    case GameObjectKind::npc:
      return budgets_.npc_budget_ms;
    case GameObjectKind::event_object:
      return budgets_.spawn_budget_ms;
  }
  return budgets_.spawn_budget_ms;
}

Player* MapActor::find_player(std::uint64_t actor_id) {
  const auto it = objects_.find(actor_id);
  return it != objects_.end() ? as_player(it->second.get()) : nullptr;
}

const Player* MapActor::find_player(std::uint64_t actor_id) const {
  const auto it = objects_.find(actor_id);
  return it != objects_.end() ? as_player(it->second.get()) : nullptr;
}

Player* MapActor::find_player_by_name(std::string_view character_name) {
  const auto key = util::lower_copy(std::string(character_name));
  if (key.empty()) {
    return nullptr;
  }
  for (auto& [_, object] : objects_) {
    auto* player = as_player(object.get());
    if (player != nullptr &&
        util::lower_copy(player->character().character_name) == key) {
      return player;
    }
  }
  return nullptr;
}

MapActor::TradeSession* MapActor::trade_session_for(std::uint64_t actor_id) {
  const auto by_actor_it = trade_session_by_actor_.find(actor_id);
  if (by_actor_it == trade_session_by_actor_.end()) {
    return nullptr;
  }
  const auto session_it = trade_sessions_.find(by_actor_it->second);
  return session_it != trade_sessions_.end() ? &session_it->second : nullptr;
}

MapActor::TradeOffer* MapActor::trade_offer_for(TradeSession& session,
                                                std::uint64_t actor_id) {
  if (session.first_actor_id == actor_id) {
    return &session.first;
  }
  if (session.second_actor_id == actor_id) {
    return &session.second;
  }
  return nullptr;
}

MapActor::TradeOffer* MapActor::trade_peer_offer_for(TradeSession& session,
                                                     std::uint64_t actor_id) {
  if (session.first_actor_id == actor_id) {
    return &session.second;
  }
  if (session.second_actor_id == actor_id) {
    return &session.first;
  }
  return nullptr;
}

bool MapActor::can_receive_trade_items(const Player& receiver,
                                       const std::vector<LegacyUserItem>& items) const {
  std::size_t free_slots = 0;
  std::int32_t total_weight = 0;
  std::unordered_set<std::int32_t> incoming_make_indices;
  for (const auto& bag_item : receiver.character().bag_items) {
    if (is_empty(bag_item)) {
      ++free_slots;
    } else {
      total_weight += item_weight(bag_item, item_configs_);
    }
  }
  if (items.size() > free_slots) {
    return false;
  }
  for (const auto& item : items) {
    if (is_empty(item) || !incoming_make_indices.insert(item.make_index).second) {
      return false;
    }
    const auto make_index_matches = [&](const LegacyUserItem& existing) {
      return !is_empty(existing) && existing.make_index == item.make_index;
    };
    if (std::any_of(receiver.character().bag_items.begin(),
                    receiver.character().bag_items.end(), make_index_matches) ||
        std::any_of(receiver.character().equipped_items.begin(),
                    receiver.character().equipped_items.end(), make_index_matches) ||
        std::any_of(receiver.character().storage_items.begin(),
                    receiver.character().storage_items.end(), make_index_matches)) {
      return false;
    }
    total_weight += item_weight(item, item_configs_);
  }
  return total_weight <= std::max<std::int32_t>(receiver.character().ability.max_weight, 0);
}

void MapActor::cancel_trade_for(std::uint64_t actor_id, RuntimeDispatch& dispatch, bool notify) {
  auto* session = trade_session_for(actor_id);
  if (session == nullptr) {
    return;
  }

  const auto session_id = session->id;
  auto* first = find_player(session->first_actor_id);
  auto* second = find_player(session->second_actor_id);
  if (notify) {
    if (first != nullptr) {
      queue_packet(dispatch, first->session_id(),
                   make_deal_simple_packet(first->session_id(), kSmDealCancel));
    }
    if (second != nullptr) {
      queue_packet(dispatch, second->session_id(),
                   make_deal_simple_packet(second->session_id(), kSmDealCancel));
    }
  }

  bool returned_all = true;
  auto return_offer = [&](Player* player, TradeOffer& offer) {
    if (player == nullptr) {
      return;
    }
    std::vector<LegacyUserItem> remaining;
    for (const auto& item : offer.items) {
      if (!player->add_bag_item(item)) {
        returned_all = false;
        remaining.push_back(item);
        continue;
      }
      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), player->id(), item, item_configs_));
    }
    if (offer.gold > 0) {
      player->add_gold(offer.gold);
      queue_packet(dispatch, player->session_id(),
                   make_gold_changed_packet(player->session_id(), player->character().gold));
      offer.gold = 0;
    }
    offer.items = std::move(remaining);
    offer.accepted = false;
    player->refresh_derived_state(item_configs_);
    queue_packet(dispatch, player->session_id(),
                 make_weight_changed_packet(player->session_id(), player->character()));
    queue_save_character(dispatch, *player);
    if (notify) {
      queue_system_notice(dispatch, *player, "Trade cancelled.");
    }
  };

  return_offer(first, session->first);
  return_offer(second, session->second);

  if (!returned_all) {
    if (first != nullptr) {
      queue_system_notice(dispatch, *first, "Trade cancel failed: bag is full.");
    }
    if (second != nullptr) {
      queue_system_notice(dispatch, *second, "Trade cancel failed: bag is full.");
    }
    return;
  }

  trade_session_by_actor_.erase(session->first_actor_id);
  trade_session_by_actor_.erase(session->second_actor_id);
  trade_sessions_.erase(session_id);
}

bool MapActor::commit_trade(TradeSession& session, RuntimeDispatch& dispatch) {
  auto* first = find_player(session.first_actor_id);
  auto* second = find_player(session.second_actor_id);
  if (first == nullptr || second == nullptr) {
    return false;
  }
  std::unordered_set<std::int32_t> trade_make_indices;
  auto add_trade_make_indices = [&](const std::vector<LegacyUserItem>& items) {
    for (const auto& item : items) {
      if (is_empty(item) || !trade_make_indices.insert(item.make_index).second) {
        return false;
      }
    }
    return true;
  };
  auto character_has_make_index = [](const CharacterRecord& character,
                                     std::int32_t make_index) {
    const auto make_index_matches = [&](const LegacyUserItem& item) {
      return !is_empty(item) && item.make_index == make_index;
    };
    return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                       make_index_matches) ||
           std::any_of(character.equipped_items.begin(), character.equipped_items.end(),
                       make_index_matches) ||
           std::any_of(character.storage_items.begin(), character.storage_items.end(),
                       make_index_matches);
  };
  auto offered_items_still_exist = [&](const CharacterRecord& character,
                                      const std::vector<LegacyUserItem>& items) {
    return std::any_of(items.begin(), items.end(), [&](const LegacyUserItem& item) {
      return !is_empty(item) && character_has_make_index(character, item.make_index);
    });
  };
  if (first->is_dead() || second->is_dead() || !in_interaction_range(*first, *second) ||
      (session.first.gold < 0 || session.second.gold < 0) ||
      static_cast<std::int64_t>(first->character().gold) + session.second.gold > kLegacyBagGold ||
      static_cast<std::int64_t>(second->character().gold) + session.first.gold > kLegacyBagGold ||
      !add_trade_make_indices(session.first.items) ||
      !add_trade_make_indices(session.second.items) ||
      offered_items_still_exist(first->character(), session.first.items) ||
      offered_items_still_exist(second->character(), session.second.items) ||
      !can_receive_trade_items(*first, session.second.items) ||
      !can_receive_trade_items(*second, session.first.items)) {
    cancel_trade_for(first->id(), dispatch, true);
    return false;
  }

  std::vector<LegacyUserItem> added_to_first;
  std::vector<LegacyUserItem> added_to_second;
  auto rollback_added = [&] {
    for (const auto& item : added_to_first) {
      static_cast<void>(first->remove_bag_item(item.make_index, item_name(item, item_configs_),
                                               item_configs_));
    }
    for (const auto& item : added_to_second) {
      static_cast<void>(second->remove_bag_item(item.make_index, item_name(item, item_configs_),
                                                item_configs_));
    }
    first->refresh_derived_state(item_configs_);
    second->refresh_derived_state(item_configs_);
  };

  for (const auto& item : session.second.items) {
    if (!first->add_bag_item(item)) {
      rollback_added();
      cancel_trade_for(first->id(), dispatch, true);
      return false;
    }
    added_to_first.push_back(item);
  }
  for (const auto& item : session.first.items) {
    if (!second->add_bag_item(item)) {
      rollback_added();
      cancel_trade_for(first->id(), dispatch, true);
      return false;
    }
    added_to_second.push_back(item);
  }

  if (session.first.gold > 0) {
    second->add_gold(session.first.gold);
  }
  if (session.second.gold > 0) {
    first->add_gold(session.second.gold);
  }

  first->refresh_derived_state(item_configs_);
  second->refresh_derived_state(item_configs_);
  for (const auto& item : added_to_first) {
    queue_packet(dispatch, first->session_id(),
                 make_add_item_packet(first->session_id(), first->id(), item, item_configs_));
  }
  for (const auto& item : added_to_second) {
    queue_packet(dispatch, second->session_id(),
                 make_add_item_packet(second->session_id(), second->id(), item, item_configs_));
  }
  if (session.first.gold > 0 || session.second.gold > 0) {
    queue_packet(dispatch, first->session_id(),
                 make_gold_changed_packet(first->session_id(), first->character().gold));
    queue_packet(dispatch, second->session_id(),
                 make_gold_changed_packet(second->session_id(), second->character().gold));
  }
  queue_packet(dispatch, first->session_id(),
               make_weight_changed_packet(first->session_id(), first->character()));
  queue_packet(dispatch, second->session_id(),
               make_weight_changed_packet(second->session_id(), second->character()));
  queue_save_character(dispatch, *first);
  queue_save_character(dispatch, *second);
  queue_packet(dispatch, second->session_id(),
               make_deal_simple_packet(second->session_id(), kSmDealSuccess));
  queue_packet(dispatch, first->session_id(),
               make_deal_simple_packet(first->session_id(), kSmDealSuccess));
  queue_system_notice(dispatch, *first, "Trade completed.");
  queue_system_notice(dispatch, *second, "Trade completed.");

  const auto session_id = session.id;
  trade_session_by_actor_.erase(session.first_actor_id);
  trade_session_by_actor_.erase(session.second_actor_id);
  trade_sessions_.erase(session_id);
  return true;
}

std::int32_t MapActor::movement_width() const {
  return movement_map_ != nullptr ? movement_map_->width : config_.width;
}

std::int32_t MapActor::movement_height() const {
  return movement_map_ != nullptr ? movement_map_->height : config_.height;
}

bool MapActor::can_walk_tile(std::int32_t x, std::int32_t y) const {
  return environment_.static_can_move(x, y);
}

LegacyMovingObjectState MapActor::moving_state_for(const GameObject& object) const {
  LegacyMovingObjectState state;
  if (const auto* player = as_player(&object); player != nullptr) {
    state.death = player->is_dead();
  } else if (const auto* monster = as_monster(&object); monster != nullptr) {
    state.death = monster->is_dead();
    state.hide_mode = monster->hide_mode();
  }
  return state;
}

std::vector<const MapActor::GroundItem*> MapActor::ordered_ground_items() const {
  std::vector<const GroundItem*> ordered;
  for (const auto item_id : environment_.item_object_ids_in_order()) {
    const auto item_it = ground_items_.find(item_id);
    if (item_it != ground_items_.end()) {
      ordered.push_back(&item_it->second);
    }
  }
  return ordered;
}

bool MapActor::apply_equipped_item_durability_loss(Player& player, std::size_t slot,
                                                   std::int32_t loss,
                                                   RuntimeDispatch& dispatch) {
  if (loss <= 0) {
    return false;
  }
  auto* item = player.equipped_item_mutable(slot);
  if (item == nullptr || is_empty(*item) || item->dura == 0) {
    return false;
  }

  const auto before = *item;
  item->dura = item->dura > loss ? static_cast<std::uint16_t>(item->dura - loss) : 0;
  queue_packet(dispatch, player.session_id(),
               make_update_item_packet(player.session_id(), player.id(), *item,
                                       item_configs_));
  if (display_dura_units(before.dura) != display_dura_units(item->dura) ||
      item->dura == 0) {
    queue_packet(dispatch, player.session_id(),
                 make_dura_change_packet(player.session_id(), slot, *item, item_configs_));
  }
  if (item->dura == 0) {
    const auto previous_status = player.character().status;
    player.refresh_derived_state(item_configs_);
    queue_packet(dispatch, player.session_id(),
                 make_ability_packet(player.session_id(), player.character()));
    queue_packet(dispatch, player.session_id(),
                 make_sub_ability_packet(player.session_id(), player));
    if (player.character().status != previous_status) {
      broadcast_legacy_char_status_changed(dispatch, player);
    }
  }
  return true;
}

std::int32_t MapActor::roll_legacy_weapon_durability_loss(const Player& attacker,
                                                          const GameObject& target,
                                                          RuntimeDispatch& dispatch,
                                                          std::uint64_t current_tick,
                                                          std::uint64_t now_ms) {
  const auto* weapon =
      attacker.equipped_item(static_cast<std::size_t>(kEquipWeapon));
  if (weapon == nullptr || is_empty(*weapon) || weapon->dura == 0) {
    return 0;
  }

  std::int32_t weapon_strong = 0;
  if (const auto* config = find_item_config(item_configs_, weapon->index);
      config != nullptr && config->special_pwr >= 1 && config->special_pwr <= 10) {
    weapon_strong = config->special_pwr;
  }
  return legacy_random_value(dispatch, "LegacyCombat", "weapon_dura_damage", 5,
                             attacker.id(), target.id(), "DoDamageWeapon",
                             now_ms, current_tick) +
         2 - weapon_strong;
}

bool MapActor::apply_legacy_weapon_durability_loss(Player& attacker,
                                                   std::int32_t loss,
                                                   RuntimeDispatch& dispatch) {
  const auto changed =
      apply_equipped_item_durability_loss(attacker, kEquipWeapon, loss, dispatch);
  if (changed) {
    queue_save_character(dispatch, attacker);
  }
  return changed;
}

bool MapActor::apply_legacy_struck_equipment_durability(Player& target,
                                                        std::uint64_t hitter_id,
                                                        RuntimeDispatch& dispatch,
                                                        std::uint64_t current_tick,
                                                        std::uint64_t now_ms,
                                                        std::string stage) {
  auto loss =
      legacy_random_value(dispatch, stage, "struck_dura_damage", 10,
                          hitter_id, target.id(), "StruckDamage", now_ms,
                          current_tick) +
      5;
  if (target.legacy_poison_damage_armor_active(current_tick)) {
    loss = (loss * 6 + 2) / 5;
  }

  auto changed = apply_equipped_item_durability_loss(target, kEquipDress, loss, dispatch);
  for (std::size_t slot = 1; slot <= kEquipBoots; ++slot) {
    if (slot == kEquipBujuk) {
      continue;
    }
    const auto* item = target.equipped_item(slot);
    if (item == nullptr || is_empty(*item) || item->dura == 0) {
      continue;
    }
    const auto chance =
        legacy_random_value(dispatch, stage, "struck_dura_gate", 8,
                            hitter_id, target.id(), "StruckDamage", now_ms,
                            current_tick);
    if (chance == 0) {
      changed = apply_equipped_item_durability_loss(target, slot, loss, dispatch) ||
                changed;
    }
  }
  if (changed) {
    queue_save_character(dispatch, target);
  }
  return changed;
}

std::int32_t MapActor::roll_legacy_player_attack_power(
    const Player& attacker, const GameObject& target, std::uint16_t ident,
    RuntimeDispatch& dispatch, std::string stage, std::string command,
    std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto dc_min = packed_min(attacker.character().ability.dc);
  const auto dc_max =
      std::max(dc_min, packed_max(attacker.character().ability.dc) +
                           attacker.legacy_dc_up_bonus());
  const auto range = std::max(0, dc_max - dc_min);
  const auto luck = attacker.legacy_luck();
  auto raw = dc_min;
  if (luck > 0) {
    const auto gate_range = std::max(1, 10 - std::min(9, luck));
    const auto gate = legacy_random_value(dispatch, stage, "attack_luck_gate",
                                          gate_range, attacker.id(), target.id(),
                                          command, now_ms, current_tick);
    if (gate == 0) {
      raw = dc_min + range;
    } else {
      const auto roll = legacy_random_value(dispatch, stage, "attack_power_roll",
                                            range + 1, attacker.id(), target.id(),
                                            command, now_ms, current_tick);
      raw = dc_min + std::clamp(roll, 0, range);
    }
  } else {
    const auto roll = legacy_random_value(dispatch, stage, "attack_power_roll",
                                          range + 1, attacker.id(), target.id(),
                                          command, now_ms, current_tick);
    raw = dc_min + std::clamp(roll, 0, range);
    if (luck < 0) {
      const auto gate_range = 10 - std::max(0, -luck);
      const auto gate = gate_range <= 0
                            ? 0
                            : legacy_random_value(dispatch, stage, "attack_luck_gate",
                                                  gate_range, attacker.id(), target.id(),
                                                  command, now_ms, current_tick);
      if (gate == 0) {
        raw = dc_min;
      }
    }
  }
  return std::max(0, static_cast<std::int32_t>(
                         std::lround(static_cast<double>(raw) *
                                     resolve_attack_multiplier(ident))));
}

bool MapActor::handle_legacy_rush_rush(Player& attacker, LegacyUseMagicInfo& user_magic,
                                       const MagicConfig& magic, const ActorMail& mail,
                                       RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                       std::uint64_t now_ms) {
  const auto dir = static_cast<std::uint8_t>(std::clamp(mail.x, 0, 7));
  const auto [dx, dy] = direction_delta(dir);
  auto moved = false;
  auto crash = false;
  auto damage_level = static_cast<std::int32_t>(user_magic.level) + 1;

  auto move_object = [&](GameObject& object, std::int32_t x, std::int32_t y,
                         std::uint8_t object_dir, std::uint16_t ident) {
    const auto old_x = object.x();
    const auto old_y = object.y();
    if (environment_.move_to_moving_object(old_x, old_y, object.id(), x, y, false,
                                           now_ms, moving_state_for(object)) != 1) {
      return false;
    }
    ActorMail move_mail;
    move_mail.kind = ActorMailKind::move;
    move_mail.map_id = config_.id;
    move_mail.actor_id = object.id();
    if (const auto* player = as_player(&object); player != nullptr) {
      move_mail.session_id = player->session_id();
    }
    move_mail.x = x;
    move_mail.y = y;
    move_mail.dir = object_dir;
    if (auto* monster = as_monster(&object); monster != nullptr) {
      monster->set_dir(object_dir);
    }
    MapContext context;
    context.tick = current_tick;
    context.map_id = config_.id;
    context.dispatch = &dispatch;
    context.items = &item_configs_;
    context.magics = &magic_configs_;
    object.on_mail(move_mail, context);
    sync_visibility_after_actor_move(object, old_x, old_y, x, y, dispatch);
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != object.id() && !is_legacy_visible_to(watcher, object)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   ident == kSmRush ? make_rush_packet(watcher.session_id(), object)
                                     : make_turn_like_packet(watcher.session_id(), ident,
                                                             object, false));
    });
    return true;
  };

  auto apply_rush_damage = [&](GameObject& target, std::int32_t raw_damage,
                               std::uint64_t hitter_id, std::string command) {
    const auto [ac_min, ac_max] = actor_physical_defense_range(target);
    const auto armor_roll =
        legacy_random_value(dispatch, "LegacyCombat", "armor_roll",
                            std::max(1, ac_max - ac_min + 1), attacker.id(),
                            target.id(), command, now_ms, current_tick);
    const auto damage = legacy_physical_struck_damage(target, raw_damage, armor_roll);
    std::int32_t applied_damage = 0;
    bool target_died = false;
    Monster* slain_monster = nullptr;
    if (auto* player_target = as_player(&target); player_target != nullptr) {
      const auto damage_result = player_target->apply_damage(damage, current_tick);
      applied_damage = damage_result.hp_damage;
      target_died = player_target->is_dead();
      if (target_died && try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
        target_died = false;
      }
      if (target_died) {
        const auto death_clear = player_target->mark_dead(now_ms);
        dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
        if (hitter_id == attacker.id() && player_target->id() != attacker.id()) {
          apply_bad_kill_penalty(attacker, *player_target, dispatch, current_tick,
                                 now_ms, "LegacyCombat");
        }
        static_cast<void>(settle_player_death(*player_target, dispatch, current_tick,
                                              now_ms));
      }
    } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
      applied_damage = apply_legacy_monster_damage(objects_, *monster_target, damage,
                                                   hitter_id, config_, current_tick, now_ms);
      if (applied_damage > 0 && hitter_id == attacker.id()) {
        notify_owned_slaves_target(attacker, monster_target->id(), now_ms);
      }
      target_died = monster_target->is_dead();
      slain_monster = target_died ? monster_target : nullptr;
    }
    if (applied_damage <= 0) {
      return 0;
    }
    auto pending_death_packets =
        target_died && slain_monster != nullptr
            ? collect_legacy_death_packets(objects_, target)
            : std::vector<PendingLegacyPacket>{};
    if (!target_died || slain_monster == nullptr) {
      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
          return;
        }
        queue_packet(dispatch, watcher.session_id(),
                     target_died ? make_death_packet(watcher.session_id(), target,
                                                     watcher.id() == target.id())
                                 : make_struck_packet(watcher.session_id(), target, hitter_id,
                                                      applied_damage, false));
      });
    }
    if (slain_monster != nullptr) {
      finalize_monster_death(slain_monster->id(), attacker.id(), dispatch, current_tick);
      add_legacy_trace(dispatch, "LegacyCombat", "exp", mail, current_tick, now_ms,
                       true, magic.id, applied_damage, "WinExp");
      queue_legacy_packets(dispatch, std::move(pending_death_packets));
    }
    add_legacy_trace(dispatch, "LegacyCombat", target_died ? "death" : "struck", mail,
                     current_tick, now_ms, true, magic.id, applied_damage,
                     target_died ? "SM_DEATH" : "SM_STRUCK");
    return applied_damage;
  };

  ActorMail dir_mail = mail;
  dir_mail.kind = ActorMailKind::spell;
  dir_mail.dir = dir;
  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;
  attacker.on_mail(dir_mail, context);

  auto* front_target = find_attack_target_by_position(objects_, attacker, attacker.x() + dx,
                                                       attacker.y() + dy, 1);
  if (front_target != nullptr) {
    auto can_push = actor_level(attacker) > actor_level(*front_target);
    if (const auto* monster_target = as_monster(front_target);
        monster_target != nullptr && monster_target->stick_mode()) {
      can_push = false;
    }
    if (const auto* player_target = as_player(front_target);
        player_target != nullptr &&
        !resolve_pk_block_reason(config_, attacker, *player_target, now_ms).empty()) {
      can_push = false;
    }
    const auto level_gap = actor_level(attacker) - actor_level(*front_target);
    const auto gate_roll = legacy_random_value(dispatch, "LegacySkill", "rush_gate", 20,
                                               attacker.id(), front_target->id(),
                                               "Random(20)", now_ms, current_tick);
    can_push = can_push && gate_roll < 6 + static_cast<std::int32_t>(user_magic.level) * 3 +
                                level_gap;
    add_legacy_trace(dispatch, "LegacySkill", "rush_gate", mail, current_tick, now_ms,
                     can_push, gate_roll, 0, "CanPush");
    if (can_push) {
      const auto target_old_x = front_target->x();
      const auto target_old_y = front_target->y();
      const auto target_new_x = front_target->x() + dx;
      const auto target_new_y = front_target->y() + dy;
      if (environment_.can_walk(target_new_x, target_new_y, false) &&
          move_object(*front_target, target_new_x, target_new_y,
                      static_cast<std::uint8_t>((dir + 4) % 8), kSmWalk) &&
          move_object(attacker, target_old_x, target_old_y, dir, kSmRush)) {
        moved = true;
        --damage_level;
        add_legacy_trace(dispatch, "LegacySkill", "rush_push", mail, current_tick, now_ms,
                         true, magic.id, 0, "RM_RUSH");
        const auto damage_seed = std::max(1, (1 + damage_level) * 5);
        const auto raw_damage =
            (1 + damage_level) * 4 +
            legacy_random_value(dispatch, "LegacyCombat", "rush_damage", damage_seed,
                                attacker.id(), front_target->id(), "Random", now_ms,
                                current_tick);
        static_cast<void>(apply_rush_damage(*front_target, raw_damage, attacker.id(),
                                            "rush_target"));
      } else {
        crash = true;
      }
    } else {
      crash = true;
    }
  } else {
    const auto step_limit = std::max<std::int32_t>(2, static_cast<std::int32_t>(user_magic.level) + 1);
    for (std::int32_t step = 0; step < step_limit; ++step) {
      const auto nx = attacker.x() + dx;
      const auto ny = attacker.y() + dy;
      if (!environment_.can_walk(nx, ny, false)) {
        crash = true;
        break;
      }
      if (!move_object(attacker, nx, ny, dir, kSmRush)) {
        crash = true;
        break;
      }
      moved = true;
    }
  }

  if (crash) {
    const auto crash_x = attacker.x() + dx;
    const auto crash_y = attacker.y() + dy;
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != attacker.id() && !is_legacy_visible_to(watcher, attacker)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_rush_kung_packet(watcher.session_id(), attacker, crash_x, crash_y));
    });
    add_legacy_trace(dispatch, "LegacySkill", "rush_crash", mail, current_tick, now_ms,
                     false, magic.id, 0, "RM_RUSHKUNG");
    if (damage_level > 0) {
      const auto damage_seed = std::max(1, (1 + damage_level) * 5);
      const auto raw_damage =
          (1 + damage_level) * 5 +
          legacy_random_value(dispatch, "LegacyCombat", "rush_self_damage", damage_seed,
                              attacker.id(), attacker.id(), "Random", now_ms, current_tick);
      static_cast<void>(apply_rush_damage(attacker, raw_damage, 0, "rush_self"));
    }
  }

  return moved;
}

bool MapActor::apply_legacy_physical_equipment_specials(Player& attacker,
                                                        GameObject& target,
                                                        std::int32_t hit_damage,
                                                        std::int32_t suck_damage,
                                                        RuntimeDispatch& dispatch,
                                                        std::string stage,
                                                        std::uint64_t current_tick,
                                                        std::uint64_t now_ms) {
  if (hit_damage <= 0) {
    return false;
  }
  auto changed = false;
  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::attack;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = attacker.id();
  trace_mail.target_actor_id = target.id();

  if (attacker.legacy_make_stone() && is_alive(target)) {
    const auto anti_poison = legacy_actor_anti_poison(target);
    const auto gate_range = std::max(1, 5 + anti_poison);
    const auto gate = legacy_random_value(dispatch, stage, "make_stone_gate",
                                          gate_range, attacker.id(), target.id(),
                                          "RING_MAKESTONE", now_ms, current_tick);
    auto applied = false;
    if (gate == 0) {
      const auto duration_ticks = legacy_delay_ms_to_ticks(5000, budgets_.tick_ms);
      const auto poison_tick_interval = legacy_delay_ms_to_ticks(2500, budgets_.tick_ms);
      if (auto* player_target = as_player(&target); player_target != nullptr) {
        applied = player_target->apply_legacy_poison(
            kLegacyPoisonStone, duration_ticks, 0, poison_tick_interval,
            attacker.id(), current_tick);
        if (applied) {
          broadcast_legacy_char_status_changed(dispatch, *player_target);
        }
      } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
        applied = monster_target->apply_legacy_poison(
            kLegacyPoisonStone, duration_ticks, 0, poison_tick_interval,
            attacker.id(), current_tick);
        monster_target->schedule_next_ai_tick(current_tick);
      }
    }
    add_legacy_trace(dispatch, stage, "make_stone", trace_mail, current_tick,
                     now_ms, applied, gate, anti_poison, "Random(5+AntiPoison)");
    changed = changed || applied;
  }

  const auto healed = suck_damage > 0 ? attacker.apply_legacy_suck_health(suck_damage) : 0;
  if (healed > 0) {
    queue_packet(dispatch, attacker.session_id(),
                 make_health_spell_changed_packet(attacker.session_id(), attacker));
    add_legacy_trace(dispatch, stage, "suck_health", trace_mail, current_tick,
                     now_ms, true, attacker.legacy_equipment_specials().suck_health_rate,
                     healed, "SuckupEnemyHealth");
    changed = true;
  }
  return changed;
}

bool MapActor::handle_weapon_upgrade_start(Player& player, Npc& npc,
                                           RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick,
                                           std::uint64_t now_ms) {
  if (!npc.supports_weapon_upgrade() || player.is_dead() ||
      trade_session_for(player.id()) != nullptr) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }
  const auto existing = std::find_if(npc.weapon_upgrades().begin(), npc.weapon_upgrades().end(),
                                    [&](const LegacyWeaponUpgradeRecord& record) {
                                      return record.character_name == player.character().character_name;
                                    });
  const auto* weapon = player.equipped_item(kEquipWeapon);
  const auto* weapon_config =
      weapon != nullptr ? find_item_config(item_configs_, weapon->index) : nullptr;
  if (existing != npc.weapon_upgrades().end() || weapon == nullptr || is_empty(*weapon) ||
      weapon_config == nullptr || (weapon_config->std_mode != 5 && weapon_config->std_mode != 6) ||
      !player.can_spend_gold(castle_dialog_context_.upgrade_weapon_fee)) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }

  auto preparation = prepare_weapon_upgrade(player, item_configs_, black_stone_name_);
  if (!preparation.has_black_stone) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }

  const auto previous_feature = player.character().feature;
  const auto previous_status = player.character().status;
  player.spend_gold(castle_dialog_context_.upgrade_weapon_fee);
  queue_packet(dispatch, player.session_id(),
               make_gold_changed_packet(player.session_id(), player.character().gold));

  for (const auto slot : preparation.consume_slots) {
    if (auto removed = player.remove_bag_item_at(slot); removed.has_value()) {
      queue_packet(dispatch, player.session_id(),
                   make_del_item_packet(player.session_id(), player.id(), *removed,
                                        item_configs_));
    }
  }

  auto removed_weapon = player.remove_equipped_item(kEquipWeapon, weapon->make_index,
                                                   item_name(*weapon, item_configs_),
                                                   item_configs_);
  if (!removed_weapon.has_value()) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }

  LegacyWeaponUpgradeRecord record;
  record.character_name = player.character().character_name;
  record.item = *removed_weapon;
  record.updc = preparation.updc;
  record.upsc = preparation.upsc;
  record.upmc = preparation.upmc;
  record.durapoint = preparation.durapoint;
  record.ready_time_ms = now_ms + 60ULL * 60ULL * 1000ULL;
  npc.weapon_upgrades_mutable().push_back(record);

  queue_packet(dispatch, player.session_id(),
               make_del_item_packet(player.session_id(), player.id(), *removed_weapon,
                                    item_configs_));
  player.refresh_derived_state(item_configs_);
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_sub_ability_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_use_items_packet(player.session_id(), player, item_configs_));
  queue_packet(dispatch, player.session_id(),
               make_weight_changed_packet(player.session_id(), player.character()));
  if (player.character().feature != previous_feature) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (is_legacy_visible_to(watcher, player)) {
        queue_packet(dispatch, watcher.session_id(),
                     make_feature_changed_packet(watcher.session_id(), player.id(),
                                                 player.character().feature));
      }
    });
  }
  if (player.character().status != previous_status) {
    broadcast_legacy_char_status_changed(dispatch, player);
  }
  queue_save_character(dispatch, player);
  dispatch.persist_requests.push_back(make_save_merchant_state_request(npc));
  queue_system_notice(dispatch, player, "Weapon upgrade started.");
  return true;
}

bool MapActor::handle_weapon_upgrade_get_back(Player& player, Npc& npc,
                                              RuntimeDispatch& dispatch,
                                              std::uint64_t current_tick,
                                              std::uint64_t now_ms) {
  if (!npc.supports_weapon_upgrade() || player.is_dead() || !player.has_free_bag_slot()) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }
  auto& records = npc.weapon_upgrades_mutable();
  const auto record_it = std::find_if(records.begin(), records.end(),
                                     [&](const LegacyWeaponUpgradeRecord& record) {
                                       return record.character_name ==
                                              player.character().character_name;
                                     });
  if (record_it == records.end()) {
    queue_system_notice(dispatch, player, "Weapon upgrade failed.");
    return false;
  }
  if (record_it->ready_time_ms != 0 && now_ms < record_it->ready_time_ms) {
    queue_system_notice(dispatch, player, "Weapon upgrade is not ready.");
    return false;
  }

  auto item = record_it->item;
  const auto durapoint = static_cast<std::int32_t>(record_it->durapoint);
  if (durapoint <= 8) {
    item.dura_max = item.dura_max > 3000
                        ? clamp_dura_value(static_cast<std::int32_t>(item.dura_max) - 3000)
                        : clamp_dura_value(static_cast<std::int32_t>(item.dura_max) / 2);
    if (item.dura > item.dura_max) {
      item.dura = item.dura_max;
    }
  } else if (durapoint <= 15) {
    const auto roll = legacy_random_value(dispatch, "LegacyWeaponUpgrade", "dura_down_gate",
                                          std::max(durapoint, 1), player.id(), npc.id(),
                                          "@getbackupgnow", now_ms, current_tick);
    if (roll < 6) {
      item.dura_max = clamp_dura_value(static_cast<std::int32_t>(item.dura_max) - 1000);
      if (item.dura > item.dura_max) {
        item.dura = item.dura_max;
      }
    }
  } else if (durapoint >= 18) {
    const auto roll = legacy_random_value(dispatch, "LegacyWeaponUpgrade", "dura_up_roll",
                                          std::max(durapoint - 18, 1), player.id(), npc.id(),
                                          "@getbackupgnow", now_ms, current_tick);
    if (roll >= 1 && roll <= 4) {
      item.dura_max = clamp_dura_value(static_cast<std::int32_t>(item.dura_max) + 1000);
    } else if (roll >= 5 && roll <= 7) {
      item.dura_max = clamp_dura_value(static_cast<std::int32_t>(item.dura_max) + 2000);
    } else if (roll >= 8) {
      item.dura_max = clamp_dura_value(static_cast<std::int32_t>(item.dura_max) + 4000);
    }
  }

  const auto equal_power =
      record_it->updc == record_it->upmc && record_it->upmc == record_it->upsc;
  const auto rand = equal_power ? legacy_random_value(dispatch, "LegacyWeaponUpgrade",
                                                       "equal_power_roll", 3, player.id(),
                                                       npc.id(), "@getbackupgnow",
                                                       now_ms, current_tick)
                                : -1;
  auto roll_upgrade = [&](std::uint8_t power, std::uint8_t success_code,
                          std::int32_t rand_value) {
    const auto per = std::min(85, 10 + std::min(11, static_cast<std::int32_t>(power)) * 7 +
                                      static_cast<std::int32_t>(item.desc[3]) -
                                      static_cast<std::int32_t>(item.desc[4]) +
                                      player.body_luck_level());
    const auto gate = legacy_random_value(dispatch, "LegacyWeaponUpgrade", "success_gate",
                                          100, player.id(), npc.id(), "@getbackupgnow",
                                          now_ms, current_tick);
    if (gate < per) {
      item.desc[10] = success_code;
      if (per > 63 &&
          legacy_random_value(dispatch, "LegacyWeaponUpgrade", "rare_gate",
                              30, player.id(), npc.id(), "@getbackupgnow",
                              now_ms, current_tick) == 0) {
        item.desc[10] = static_cast<std::uint8_t>(success_code + 1);
      }
      if (per > 79 &&
          legacy_random_value(dispatch, "LegacyWeaponUpgrade", "epic_gate",
                              200, player.id(), npc.id(), "@getbackupgnow",
                              now_ms, current_tick) == 0) {
        item.desc[10] = static_cast<std::uint8_t>(success_code + 2);
      }
    } else {
      item.desc[10] = 1;
    }
    static_cast<void>(rand_value);
  };
  if ((record_it->updc >= record_it->upmc && record_it->updc >= record_it->upsc) ||
      rand == 0) {
    roll_upgrade(record_it->updc, 10, rand);
  }
  if ((record_it->upmc >= record_it->updc && record_it->upmc >= record_it->upsc) ||
      rand == 1) {
    roll_upgrade(record_it->upmc, 20, rand);
  }
  if ((record_it->upsc >= record_it->upmc && record_it->upsc >= record_it->updc) ||
      rand == 2) {
    roll_upgrade(record_it->upsc, 30, rand);
  }

  records.erase(record_it);
  static_cast<void>(player.add_bag_item(item));
  queue_packet(dispatch, player.session_id(),
               make_add_item_packet(player.session_id(), player.id(), item, item_configs_));
  player.refresh_derived_state(item_configs_);
  queue_packet(dispatch, player.session_id(),
               make_weight_changed_packet(player.session_id(), player.character()));
  queue_save_character(dispatch, player);
  dispatch.persist_requests.push_back(make_save_merchant_state_request(npc));
  queue_system_notice(dispatch, player, "Weapon upgrade complete.");
  return true;
}

bool MapActor::apply_pending_weapon_upgrade_result(Player& attacker,
                                                   RuntimeDispatch& dispatch,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms) {
  auto* weapon = attacker.equipped_item_mutable(kEquipWeapon);
  if (weapon == nullptr || is_empty(*weapon) || weapon->desc[10] == 0) {
    return false;
  }

  const auto previous_feature = attacker.character().feature;
  const auto old_weapon = *weapon;
  if (weapon->desc[0] + weapon->desc[1] + weapon->desc[2] < 20) {
    const auto code = weapon->desc[10];
    if (code >= 10 && code <= 13) {
      weapon->desc[0] = clamp_desc_value(weapon->desc[0] + code - 9);
    } else if (code >= 20 && code <= 23) {
      weapon->desc[1] = clamp_desc_value(weapon->desc[1] + code - 19);
    } else if (code >= 30 && code <= 33) {
      weapon->desc[2] = clamp_desc_value(weapon->desc[2] + code - 29);
    } else if (code == 1) {
      *weapon = LegacyUserItem{};
    }
  } else {
    *weapon = LegacyUserItem{};
  }
  if (!is_empty(*weapon)) {
    weapon->desc[10] = 0;
    queue_packet(dispatch, attacker.session_id(),
                 make_update_item_packet(attacker.session_id(), attacker.id(), *weapon,
                                         item_configs_));
  } else {
    queue_packet(dispatch, attacker.session_id(),
                 make_del_item_packet(attacker.session_id(), attacker.id(), old_weapon,
                                      item_configs_));
    queue_packet(dispatch, attacker.session_id(),
                 make_break_weapon_packet(attacker.session_id(), attacker));
  }

  attacker.refresh_derived_state(item_configs_);
  queue_packet(dispatch, attacker.session_id(),
               make_ability_packet(attacker.session_id(), attacker.character()));
  queue_packet(dispatch, attacker.session_id(),
               make_sub_ability_packet(attacker.session_id(), attacker));
  queue_packet(dispatch, attacker.session_id(),
               make_use_items_packet(attacker.session_id(), attacker, item_configs_));
  if (attacker.character().feature != previous_feature) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (is_legacy_visible_to(watcher, attacker)) {
        queue_packet(dispatch, watcher.session_id(),
                     make_feature_changed_packet(watcher.session_id(), attacker.id(),
                                                 attacker.character().feature));
      }
    });
  }
  queue_save_character(dispatch, attacker);
  add_legacy_trace(dispatch, "LegacyWeaponUpgrade", "identify_result", ActorMail{},
                   current_tick, now_ms, !is_empty(*weapon), old_weapon.desc[10], 0,
                   "CheckWeaponUpgradeResult");
  return true;
}

bool MapActor::apply_legacy_weapon_unlock(Player& player, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms,
                                          std::string stage) {
  auto* weapon = player.equipped_item_mutable(kEquipWeapon);
  if (weapon == nullptr || is_empty(*weapon)) {
    return false;
  }
  if (weapon->desc[3] > 0) {
    --weapon->desc[3];
  } else if (weapon->desc[4] < 10) {
    ++weapon->desc[4];
  }
  player.refresh_derived_state(item_configs_);
  queue_packet(dispatch, player.session_id(),
               make_update_item_packet(player.session_id(), player.id(), *weapon,
                                       item_configs_));
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_sub_ability_packet(player.session_id(), player));
  add_legacy_trace(dispatch, std::move(stage), "weapon_unlock", ActorMail{},
                   current_tick, now_ms, true, weapon->desc[3], weapon->desc[4],
                   "MakeWeaponUnlock");
  return true;
}

bool MapActor::apply_legacy_weapon_good_luck(Player& player, RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  auto* weapon = player.equipped_item_mutable(kEquipWeapon);
  if (weapon == nullptr || is_empty(*weapon)) {
    return false;
  }
  std::int32_t difficulty = 0;
  if (const auto* config = find_item_config(item_configs_, weapon->index); config != nullptr) {
    difficulty = std::abs(packed_max(config->dc) - packed_min(config->dc)) / 5;
  }
  if (legacy_random_value(dispatch, "LegacyWeaponLuck", "curse_gate", 20,
                          player.id(), 0, "MakeWeaponGoodLock", now_ms,
                          current_tick) == 1) {
    static_cast<void>(apply_legacy_weapon_unlock(player, dispatch, current_tick, now_ms,
                                                 "LegacyWeaponLuck"));
    return true;
  }
  if (weapon->desc[4] > 0) {
    --weapon->desc[4];
  } else if (weapon->desc[3] < 1) {
    ++weapon->desc[3];
  } else if (weapon->desc[3] < 3 &&
             legacy_random_value(dispatch, "LegacyWeaponLuck", "luck_3_gate",
                                 6 + difficulty, player.id(), 0,
                                 "MakeWeaponGoodLock", now_ms, current_tick) == 1) {
    ++weapon->desc[3];
  } else if (weapon->desc[3] < 7 &&
             legacy_random_value(dispatch, "LegacyWeaponLuck", "luck_7_gate",
                                 30 + difficulty * 5, player.id(), 0,
                                 "MakeWeaponGoodLock", now_ms, current_tick) == 1) {
    ++weapon->desc[3];
  }
  player.refresh_derived_state(item_configs_);
  queue_packet(dispatch, player.session_id(),
               make_update_item_packet(player.session_id(), player.id(), *weapon,
                                       item_configs_));
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_sub_ability_packet(player.session_id(), player));
  add_legacy_trace(dispatch, "LegacyWeaponLuck", "weapon_good_luck", ActorMail{},
                   current_tick, now_ms, true, weapon->desc[3], weapon->desc[4],
                   "MakeWeaponGoodLock");
  return true;
}

void MapActor::apply_bad_kill_penalty(Player& killer, const Player& victim,
                                      RuntimeDispatch& dispatch,
                                      std::uint64_t current_tick,
                                      std::uint64_t now_ms,
                                      std::string stage) {
  if (config_.fight_zone || config_.fight3_zone || victim.pk_level() >= 2 ||
      !victim.has_recent_pk_hiter(killer.id(), now_ms)) {
    return;
  }
  killer.inc_pk_point(100);
  killer.add_body_luck(-500.0);
  auto weapon_changed = false;
  if (victim.pk_level() < 1 &&
      legacy_random_value(dispatch, stage, "weapon_unlock_gate", 5,
                          killer.id(), victim.id(), "MakeWeaponUnlock", now_ms,
                          current_tick) == 0) {
    weapon_changed = apply_legacy_weapon_unlock(killer, dispatch, current_tick, now_ms,
                                                std::move(stage));
  }
  queue_packet(dispatch, killer.session_id(),
               make_username_packet(killer.session_id(), killer.id(),
                                    killer.character().character_name,
                                    actor_name_color(killer)));
  if (!weapon_changed) {
    queue_packet(dispatch, killer.session_id(),
                 make_ability_packet(killer.session_id(), killer.character()));
    queue_packet(dispatch, killer.session_id(),
                 make_sub_ability_packet(killer.session_id(), killer));
  }
  queue_save_character(dispatch, killer);
}

bool MapActor::settle_player_death(Player& player, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick,
                                   std::uint64_t now_ms) {
  if (!player.is_dead() || player.legacy_death_drop_settled()) {
    return false;
  }
  player.mark_legacy_death_drop_settled();
  cancel_trade_for(player.id(), dispatch, true);
  player.add_body_luck(-static_cast<double>(player.character().ability.level) * 5.0);
  if (config_.fight_zone || config_.fight3_zone) {
    queue_save_character(dispatch, player);
    return false;
  }

  auto changed = false;
  const auto previous_feature = player.character().feature;
  const auto previous_status = player.character().status;
  auto drop_position = [&](std::int32_t wide) -> std::pair<std::int32_t, std::int32_t> {
    std::optional<std::pair<std::int32_t, std::int32_t>> best;
    std::size_t best_count = std::numeric_limits<std::size_t>::max();
    for (std::int32_t dy = -wide; dy <= wide; ++dy) {
      for (std::int32_t dx = -wide; dx <= wide; ++dx) {
        const auto try_x = player.x() + dx;
        const auto try_y = player.y() + dy;
        if (!environment_.in_bounds(try_x, try_y) ||
            !environment_.static_can_move(try_x, try_y)) {
          continue;
        }
        const auto item_count = environment_.item_object_count(try_x, try_y);
        if (!item_count.has_value()) {
          continue;
        }
        if (*item_count == 0) {
          return std::pair{try_x, try_y};
        }
        if (*item_count < best_count) {
          best_count = *item_count;
          best = std::pair{try_x, try_y};
        }
      }
    }
    if (best.has_value() && best_count < 8) {
      return *best;
    }
    return std::pair{player.x(), player.y()};
  };
  auto place_item = [&](const LegacyUserItem& item) -> bool {
    if (is_empty(item)) {
      return false;
    }
    GroundItem ground_item;
    ground_item.id = next_ground_item_id_;
    ground_item.item = item;
    ground_item.name = item_name(item, item_configs_);
    ground_item.count = 1;
    ground_item.looks = item_looks(item, item_configs_);
    if (const auto* config = find_item_config(item_configs_, item.index); config != nullptr) {
      ground_item.ani_count = config->ani_count;
      if (config->std_mode == 40) {
        ground_item.item.dura =
            clamp_dura_value(static_cast<std::int32_t>(ground_item.item.dura) - 2000);
      }
    }
    ground_item.drop_time_ms = now_ms;
    ground_item.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
    ground_item.dropper_actor_id = player.id();
    ground_item.dropper_name = player.name();
    ground_item.death_drop = true;

    const auto [drop_x, drop_y] = drop_position(2);
    ground_item.x = drop_x;
    ground_item.y = drop_y;
    const auto add_result =
        environment_.add_item_object(ground_item.x, ground_item.y, ground_item.id,
                                     LegacyMapItemState{}, now_ms);
    if (!add_result.ok || add_result.merged) {
      return false;
    }
    const auto item_id = ground_item.id;
    const auto item_x = ground_item.x;
    const auto item_y = ground_item.y;
    ++next_ground_item_id_;
    ground_items_[item_id] = std::move(ground_item);
    sync_visibility_after_item_change(item_x, item_y, dispatch, item_id);
    return true;
  };

  const auto equip_ran = player.pk_level() > 2 ? 15 : 30;
  for (std::size_t slot = 0; slot < kMaxEquipSlots; ++slot) {
    auto* item = player.equipped_item_mutable(slot);
    if (item == nullptr || is_empty(*item)) {
      continue;
    }
    const auto* config = find_item_config(item_configs_, item->index);
    if (config != nullptr && (config->item_desc & kLegacyItemDieAndBreak) != 0) {
      const auto old = *item;
      *item = LegacyUserItem{};
      queue_packet(dispatch, player.session_id(),
                   make_del_item_packet(player.session_id(), player.id(), old,
                                        item_configs_));
      changed = true;
      continue;
    }
    if (legacy_random_value(dispatch, "LegacyDeathDrop", "equip_drop_gate",
                            equip_ran, player.id(), slot, "DropUseItems",
                            now_ms, current_tick) != 0) {
      continue;
    }
    if (config != nullptr && (config->item_desc & kLegacyItemNeverLose) != 0) {
      continue;
    }
    const auto old = *item;
    if (place_item(old)) {
      *item = LegacyUserItem{};
      queue_packet(dispatch, player.session_id(),
                   make_del_item_packet(player.session_id(), player.id(), old,
                                        item_configs_));
      changed = true;
    }
  }

  const auto all_bag_drop = player.pk_level() > 1;
  for (std::size_t slot = player.character().bag_items.size(); slot > 0; --slot) {
    const auto index = slot - 1;
    const auto& item = player.character().bag_items[index];
    if (is_empty(item)) {
      continue;
    }
    const auto* config = find_item_config(item_configs_, item.index);
    if (config != nullptr && (config->item_desc & kLegacyItemDieAndBreak) != 0) {
      if (auto removed = player.remove_bag_item_at(index); removed.has_value()) {
        queue_packet(dispatch, player.session_id(),
                     make_del_item_packet(player.session_id(), player.id(), *removed,
                                          item_configs_));
        changed = true;
      }
      continue;
    }
    if (!all_bag_drop &&
        legacy_random_value(dispatch, "LegacyDeathDrop", "bag_drop_gate", 3,
                            player.id(), index, "DropBagItems", now_ms,
                            current_tick) != 0) {
      continue;
    }
    if (config != nullptr && (config->item_desc & kLegacyItemNeverLose) != 0) {
      continue;
    }
    const auto old = item;
    if (place_item(old)) {
      static_cast<void>(player.remove_bag_item_at(index));
      queue_packet(dispatch, player.session_id(),
                   make_del_item_packet(player.session_id(), player.id(), old,
                                        item_configs_));
      changed = true;
    }
  }

  if (changed) {
    player.refresh_derived_state(item_configs_);
    queue_packet(dispatch, player.session_id(),
                 make_use_items_packet(player.session_id(), player, item_configs_));
    queue_packet(dispatch, player.session_id(),
                 make_weight_changed_packet(player.session_id(), player.character()));
    queue_packet(dispatch, player.session_id(),
                 make_ability_packet(player.session_id(), player.character()));
    queue_packet(dispatch, player.session_id(),
                 make_sub_ability_packet(player.session_id(), player));
    if (player.character().feature != previous_feature) {
      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (is_legacy_visible_to(watcher, player)) {
          queue_packet(dispatch, watcher.session_id(),
                       make_feature_changed_packet(watcher.session_id(), player.id(),
                                                   player.character().feature));
        }
      });
    }
    if (player.character().status != previous_status) {
      broadcast_legacy_char_status_changed(dispatch, player);
    }
  }
  queue_save_character(dispatch, player);
  return changed;
}

bool MapActor::try_legacy_revival(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t current_tick, std::uint64_t now_ms) {
  return try_legacy_revival_impl(objects_, item_configs_, config_.id, player, dispatch,
                                 current_tick, now_ms);
}

#include "world/map_actor_gm.hpp"
#include "world/map_actor_npc.hpp"
void MapActor::add_legacy_trace(RuntimeDispatch& dispatch,
                                std::string stage,
                                std::string action,
                                const ActorMail& mail,
                                std::uint64_t current_tick,
                                std::uint64_t now_ms,
                                bool success,
                                std::int32_t value,
                                std::int32_t damage,
                                std::string label) const {
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      std::move(stage),
      std::move(action),
      config_.id,
      {},
      mail.actor_id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      mail.target_actor_id,
      {},
      std::move(label),
      legacy_random_ != nullptr ? legacy_random_->state() : 0,
      legacy_random_ != nullptr ? legacy_random_->state() : 0,
      value,
      damage,
      success});
}

std::int32_t MapActor::legacy_random_value(RuntimeDispatch& dispatch,
                                           std::string stage,
                                           std::string action,
                                           std::int32_t range,
                                           std::uint64_t actor_id,
                                           std::uint64_t target_actor_id,
                                           std::string command,
                                           std::uint64_t now_ms,
                                           std::uint64_t current_tick) {
  const auto before = legacy_random_ != nullptr ? legacy_random_->state() : 0;
  const auto value = legacy_random_ != nullptr ? legacy_random_->random(range) : 0;
  const auto after = legacy_random_ != nullptr ? legacy_random_->state() : before;
  dispatch.legacy_traces.push_back(LegacyRuntimeTrace{
      std::move(stage),
      std::move(action),
      config_.id,
      {},
      actor_id,
      now_ms,
      current_tick,
      0,
      0,
      0,
      target_actor_id,
      std::move(command),
      "range=" + std::to_string(range),
      before,
      after,
      value,
      0,
      true});
  return value;
}

}  // namespace mir2
