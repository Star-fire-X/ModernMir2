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
#include "world/legacy_magic_runtime.hpp"
#include "world/legacy_item_rules.hpp"
#include "world/legacy_skill_formula.hpp"

namespace mir2 {

#include "world/map_actor_helpers.hpp"
#include "world/map_actor_packets.hpp"

namespace {
std::int32_t compute_repair_cost(const LegacyUserItem& item,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* config = find_item_config(item_configs, item.index);
  const auto price = config != nullptr ? std::max(config->price, 0) : 0;
  const auto dura_max = static_cast<std::int32_t>(item_dura_max(item, item_configs));
  const auto dura = static_cast<std::int32_t>(item.dura);
  if (price <= 0 || dura_max <= 0 || dura >= dura_max) {
    return price > 0 && dura_max > 0 ? 0 : -1;
  }
  const auto price_div3 = price / 3;
  const auto wear = dura_max - dura;
  return static_cast<std::int32_t>(
      std::lround((static_cast<double>(price_div3) / static_cast<double>(dura_max)) *
                  static_cast<double>(wear)));
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

PersistRequest make_save_merchant_state_request(const Npc& merchant) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_merchant_state;
  request.merchant_state = merchant.snapshot_merchant_state();
  return request;
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

std::optional<LegacyUserItem> take_merchant_item(
    Npc& merchant, std::string_view expected_name, std::int32_t item_make_index,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  auto& goods = merchant.merchant_items_mutable();
  for (auto it = goods.begin(); it != goods.end(); ++it) {
    if (is_empty(*it) || item_name(*it, item_configs) != expected_name) {
      continue;
    }
    const auto* config = find_item_config(item_configs, it->index);
    const auto can_ignore_make_index =
        config != nullptr && !requires_detail_goods_list(*config);
    if (!can_ignore_make_index && it->make_index != item_make_index) {
      continue;
    }
    auto item = *it;
    goods.erase(it);
    return item;
  }
  return std::nullopt;
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
  std::vector<std::pair<std::uint64_t, GameObject*>> ordered;
  const auto dir = actor_dir(attacker);
  const auto [fx, fy] = direction_delta(dir);
  const auto [lx, ly] = direction_delta(static_cast<std::uint8_t>((dir + 7) % 8));
  const auto [rx, ry] = direction_delta(static_cast<std::uint8_t>((dir + 1) % 8));
  const std::array<std::pair<std::int32_t, std::int32_t>, 3> cells{{
      {attacker.x() + fx, attacker.y() + fy},
      {attacker.x() + lx, attacker.y() + ly},
      {attacker.x() + rx, attacker.y() + ry},
  }};

  for (auto& [actor_id, object] : objects) {
    if (actor_id == attacker.id() || !is_attackable_target(*object)) {
      continue;
    }
    const auto in_fan = std::any_of(cells.begin(), cells.end(), [&](const auto& cell) {
      return object->x() == cell.first && object->y() == cell.second;
    });
    if (!in_fan) {
      continue;
    }
    if (const auto* player_target = as_player(object.get()); player_target != nullptr &&
        !resolve_pk_block_reason(map_config, attacker, *player_target, now_ms).empty()) {
      continue;
    }
    ordered.emplace_back(actor_id, object.get());
  }

  std::sort(ordered.begin(), ordered.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  std::vector<GameObject*> targets;
  targets.reserve(ordered.size());
  for (const auto& entry : ordered) {
    targets.push_back(entry.second);
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

std::int32_t legacy_packed_attack_power(const Player& attacker, std::uint16_t ident,
                                        std::int32_t roll) {
  const auto low = packed_min(attacker.character().ability.dc);
  const auto high = std::max(low, packed_max(attacker.character().ability.dc));
  const auto raw = low + std::clamp(roll, 0, high - low);
  return std::max(0, static_cast<std::int32_t>(
                         std::lround(static_cast<double>(raw) * resolve_attack_multiplier(ident))));
}

std::int32_t legacy_player_undead_power(
    const Player& attacker, const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::int32_t power = 0;
  for (const auto& item : attacker.character().equipped_items) {
    if (is_empty(item) || item.dura == 0) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      continue;
    }
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    power += std::max(upgraded.undead, 0);
  }
  return power;
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
    case 9:
    case 10:
    case 11:
    case 13:
    case 14:
    case 15:
    case 17:
    case 18:
    case 19:
    case 20:
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
                                         bool damage_magic_bubble = true) {
  const auto [mac_min, mac_max] = actor_magic_defense_range(target);
  const auto armor_random = random.random(std::max(0, mac_max - mac_min) + 1);
  auto* player_target = as_player(&target);
  const auto bubble_active =
      player_target != nullptr && player_target->legacy_magic_bubble_active(current_tick);
  const auto bubble_level = bubble_active ? player_target->legacy_magic_bubble_level() : 0;
  const auto result = legacy_mag_struck_damage(damage, mac_min, mac_max, armor_random,
                                               actor_undead(target), 0, bubble_active,
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

LegacyMagicDamageResult apply_legacy_magic_damage(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
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
    if (result.target_died) {
      player_target->mark_dead(now_ms);
      if (!map_config.fight_zone && !map_config.fight3_zone && player_target->pk_level() < 2 &&
          player_target->has_recent_pk_hiter(caster.id(), now_ms)) {
        caster.inc_pk_point(100);
        queue_packet(dispatch, caster.session_id(),
                     make_username_packet(caster.session_id(), caster.id(),
                                          caster.character().character_name,
                                          actor_name_color(caster)));
      }
    }
    if (damage_result.absorbed_damage > 0) {
      queue_packet(dispatch, player_target->session_id(),
                   make_health_spell_changed_packet(player_target->session_id(), *player_target));
    }
  } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
    result.applied_damage = apply_legacy_monster_damage(
        objects, *monster_target, damage, caster.id(), now_ms);
    result.target_died = monster_target->is_dead();
    result.slain_monster_id = result.target_died ? monster_target->id() : 0;
  }

  if (result.applied_damage <= 0) {
    return result;
  }

  for_each_player(objects, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 result.target_died ? make_death_packet(watcher.session_id(), target,
                                                        watcher.id() == target.id())
                                    : make_struck_packet(watcher.session_id(), target, caster.id(),
                                                         result.applied_damage, true));
  });
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
    if (delphi_round(static_cast<double>(item->dura) / 100.0) >= count) {
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
                   MakeIndexAllocator* make_index_allocator)
    : config_(std::move(config)),
      budgets_(std::move(budgets)),
      item_configs_(std::move(item_configs)),
      magic_configs_(std::move(magic_configs)),
      monster_defs_(std::move(monster_defs)),
      map_quests_(std::move(map_quests)),
      castle_dialog_context_(std::move(castle_dialog_context)),
      make_index_allocator_(make_index_allocator) {
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
                                       std::uint64_t now_ms, RuntimeDispatch* dispatch) {
  const auto added = environment_.add_placeholder_object(x, y, LegacyMapObjectShape::event_object,
                                                        event_id, now_ms);
  if (added) {
    event_objects_[event_id] = {x, y};
    if (dispatch != nullptr) {
      sync_visibility_after_event_change(x, y, *dispatch);
    }
  }
  return added;
}

void MapActor::legacy_remove_event_object(std::uint64_t event_id, std::int32_t x,
                                          std::int32_t y, RuntimeDispatch* dispatch) {
  static_cast<void>(
      environment_.delete_from_map(x, y, LegacyMapObjectShape::event_object, event_id));
  event_objects_.erase(event_id);
  if (dispatch != nullptr) {
    for (auto& [_, visibility] : visibility_) {
      visibility.events.erase(event_id);
    }
    sync_visibility_after_event_change(x, y, *dispatch);
  }
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
  static_cast<void>(
      trigger_map_quest(*player, {}, {}, false, "enter", dispatch, current_tick, now_ms));
  return dispatch;
}

RuntimeDispatch MapActor::legacy_process_player(std::uint64_t actor_id,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms,
                                                bool persistence_overloaded) {
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
                                    persistence_overloaded);
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

  if (monster->legacy_search_due(now_ms)) {
    monster->mark_legacy_search_time(now_ms);
  }
  if (run_due) {
    handle_monster_ai(*monster, dispatch, current_tick, now_ms);
    monster->mark_legacy_run_time(now_ms);
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
  static_cast<void>(player->clear_legacy_buffs_on_logout(0));
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

RuntimeDispatch MapActor::tick(std::uint64_t current_tick, std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;

  for (auto& delayed_mail : delayed_mail_wheel_.pop_ready(current_tick)) {
    mailbox_.push_back(std::move(delayed_mail));
  }

  while (!mailbox_.empty()) {
    ActorMail mail = std::move(mailbox_.front());
    mailbox_.pop_front();
    handle_mail(mail, dispatch, current_tick, now_ms);
  }

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
  return environment_.can_walk(x, y, false);
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
