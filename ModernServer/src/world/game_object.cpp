#include "world/game_object.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "world/legacy_item_rules.hpp"

namespace mir2 {

namespace {

const ItemConfig* find_item_config(const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   std::int32_t item_index) {
  const auto it = item_configs.find(item_index);
  return it != item_configs.end() ? &it->second : nullptr;
}

std::string item_name(const LegacyUserItem& item,
                      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr &&
                                                               !config->name.empty()) {
    return config->name;
  }
  return "Item " + std::to_string(item.index);
}

std::int32_t item_weight(const LegacyUserItem& item,
                         const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return std::max(config->weight, 0);
  }
  return 0;
}

bool matches_item(const LegacyUserItem& item, std::int32_t make_index, std::string_view expected_name,
                  const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item) || item.make_index != make_index) {
    return false;
  }
  return expected_name.empty() || item_name(item, item_configs) == expected_name;
}

std::uint16_t clamp_u16(std::int32_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0, 65535));
}

std::uint8_t clamp_u8(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint8_t clamp_slave_level(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 6));
}

std::int32_t packed_min(std::uint16_t value) { return static_cast<std::int32_t>(value & 0xffu); }

std::int32_t packed_max(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

std::int32_t packed_average(std::uint16_t value) {
  return (packed_min(value) + std::max(packed_min(value), packed_max(value))) / 2;
}

std::uint16_t add_packed_range(std::uint16_t lhs, std::uint16_t rhs) {
  const auto low = packed_min(lhs) + packed_min(rhs);
  const auto high = std::max(packed_max(lhs), packed_min(lhs)) +
                    std::max(packed_max(rhs), packed_min(rhs));
  return static_cast<std::uint16_t>((std::clamp(high, 0, 255) << 8) |
                                    std::clamp(low, 0, 255));
}

std::uint32_t next_level_exp(std::uint8_t level) {
  static constexpr std::array<std::uint32_t, 61> kNeedExps{
      100,       200,       300,       400,       600,       900,       1200,
      1700,      2500,      6000,      8000,      10000,     15000,     30000,
      40000,     50000,     70000,     100000,    120000,    140000,    250000,
      300000,    350000,    400000,    500000,    700000,    1000000,   1400000,
      1800000,   2000000,   2400000,   2800000,   3200000,   3600000,   4000000,
      4800000,   5600000,   8200000,   9000000,   12000000,  16000000,  30000000,
      50000000,  80000000,  120000000, 160000000, 200000000, 250000000, 300000000,
      350000000, 400000000, 480000000, 560000000, 640000000, 740000000, 840000000,
      950000000, 1070000000, 1200000000, 1500000000, 1500000000};
  const auto index = std::clamp<std::size_t>(level == 0 ? 0 : static_cast<std::size_t>(level - 1),
                                             0, kNeedExps.size() - 1);
  return kNeedExps[index];
}

std::uint8_t resolve_shape_feature(std::uint8_t sex, const LegacyUserItem& item,
                                   const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item)) {
    return sex;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return static_cast<std::uint8_t>(std::clamp(config->shape * 2 + sex, 0, 255));
  }
  return sex;
}

std::string normalize_service(std::string service) {
  std::transform(service.begin(), service.end(), service.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return service;
}

bool is_negative_effect(const TimedStatusEffect& effect) {
  return effect.damage_per_tick > 0 || effect.slow_percent > 0;
}

bool effect_has_payload(const TimedStatusEffect& effect) {
  return effect.damage_per_tick > 0 || effect.heal_per_tick > 0 || effect.slow_percent > 0 ||
         effect.shield_points > 0;
}

std::string lower_ascii_copy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

bool legacy_special_slave_name(std::string_view name) {
  const auto lowered = lower_ascii_copy(name);
  return lowered == "__whiteskeleton" || lowered == "__elf" || lowered == "__elfwarrior";
}

std::uint64_t ticks_to_ms(std::uint64_t ticks, std::uint32_t tick_ms) {
  return ticks * static_cast<std::uint64_t>(std::max<std::uint32_t>(tick_ms, 1));
}

std::uint64_t ms_to_ticks(std::uint32_t value_ms, std::uint32_t tick_ms) {
  const auto tick = std::max<std::uint32_t>(tick_ms, 1);
  return std::max<std::uint64_t>(
      1, (static_cast<std::uint64_t>(value_ms) + static_cast<std::uint64_t>(tick) - 1) /
             static_cast<std::uint64_t>(tick));
}

constexpr std::int32_t kPoisonDecHealth = 0;
constexpr std::int32_t kPoisonDamageArmor = 1;
constexpr std::int32_t kPoisonDontMove = 4;
constexpr std::int32_t kPoisonStone = 5;
constexpr std::int32_t kStateTransparent = 8;
constexpr std::int32_t kStateDefenceUp = 9;
constexpr std::int32_t kStateMagicDefenceUp = 10;
constexpr std::int32_t kStateBubbleDefenceUp = 11;
constexpr std::int32_t kLegacyHealingCap = 300;
constexpr std::int32_t kLegacyHealingPerTick = 5;

constexpr std::int32_t kRcSpitSpider = 82;
constexpr std::int32_t kRcKillingHerb = 85;
constexpr std::int32_t kRcDualAxeSkeleton = 87;
constexpr std::int32_t kRcBigKudeki = 90;
constexpr std::int32_t kRcMagCowFaceMon = 91;
constexpr std::int32_t kRcThornDark = 93;
constexpr std::int32_t kRcToxicGhost = 127;
constexpr std::int32_t kRcBeeQueen = 103;
constexpr std::int32_t kRcArcherMon = 104;
constexpr std::int32_t kRcCentipedeKing = 107;
constexpr std::int32_t kRcArcherGuard = 112;
constexpr std::int32_t kRcSpiderHouse = 116;
constexpr std::int32_t kRcHighRiskSpider = 118;
constexpr std::int32_t kRcBigPoisonSpider = 119;
constexpr std::int32_t kRcScultureKing = 102;
constexpr std::int32_t kRcScultureKingNoFollower = 122;
constexpr std::int32_t kRcDoorGuard = 11;
constexpr std::int32_t kRcArcherPolice = 20;
constexpr std::int32_t kRcCastleDoor = 110;
constexpr std::int32_t kRcWall = 111;

std::int32_t round_damage_120(std::int32_t amount) {
  return amount > 0 ? (amount * 6 + 2) / 5 : amount;
}

void set_legacy_status_bit(std::int32_t& status, std::int32_t index, bool active) {
  const auto mask = 0x80000000u >> static_cast<std::uint32_t>(std::clamp(index, 0, 31));
  auto bits = static_cast<std::uint32_t>(status);
  if (active) {
    bits |= mask;
  } else {
    bits &= ~mask;
  }
  status = static_cast<std::int32_t>(bits);
}

void clear_transient_legacy_status_bits(std::int32_t& status) {
  set_legacy_status_bit(status, kPoisonDecHealth, false);
  set_legacy_status_bit(status, kPoisonDamageArmor, false);
  set_legacy_status_bit(status, kPoisonDontMove, false);
  set_legacy_status_bit(status, kPoisonStone, false);
  set_legacy_status_bit(status, kStateTransparent, false);
  set_legacy_status_bit(status, kStateDefenceUp, false);
  set_legacy_status_bit(status, kStateMagicDefenceUp, false);
  set_legacy_status_bit(status, kStateBubbleDefenceUp, false);
}

}  // namespace

void MapContext::send_packet(std::uint64_t session_id, LegacyPacket packet) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->session_events.push_back(SessionEvent{
      SessionEventKind::send_packet, "game_gateway", session_id, {}, std::move(packet), {}});
}

void MapContext::emit_audit(std::string category, std::string message) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->audit_events.push_back(AuditEvent{std::move(category), std::move(message), map_id});
}

void MapContext::request_persist(PersistRequest request) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->persist_requests.push_back(std::move(request));
}

void MapContext::post_cross_map_mail(ActorMail mail) const {
  if (dispatch == nullptr) {
    return;
  }
  dispatch->cross_map_mails.push_back(std::move(mail));
}

GameObject::GameObject(std::uint64_t id, GameObjectKind kind, std::string name, std::string map_id,
                       std::int32_t x, std::int32_t y)
    : id_(id), kind_(kind), name_(std::move(name)), map_id_(std::move(map_id)), x_(x), y_(y) {}

void GameObject::on_mail(const ActorMail& mail, MapContext&) {
  if (mail.kind == ActorMailKind::move || mail.kind == ActorMailKind::run) {
    set_position(mail.x, mail.y);
  }
}

void GameObject::on_tick(MapContext& context) { set_next_due_tick(context.tick + 10); }

void GameObject::set_position(std::int32_t x, std::int32_t y) {
  x_ = x;
  y_ = y;
}

void GameObject::set_next_due_tick(std::uint64_t next_due_tick) { next_due_tick_ = next_due_tick; }

Player::Player(std::uint64_t id, std::uint64_t session_id, CharacterRecord character)
    : GameObject(id, GameObjectKind::player, character.character_name, character.map_id, character.x,
                 character.y),
      session_id_(session_id),
      character_(std::move(character)),
      base_ability_(character_.ability) {
  run_time_ms_ = 0;
  clear_transient_legacy_status_bits(character_.status);
  if (character_.ability.max_exp == 0 || character_.ability.max_exp == 100) {
    character_.ability.max_exp = next_level_exp(character_.ability.level);
    base_ability_.max_exp = character_.ability.max_exp;
  }
}

CharacterRecord Player::snapshot() const {
  CharacterRecord snapshot = character_;
  snapshot.map_id = map_id();
  snapshot.x = x();
  snapshot.y = y();
  snapshot.dir = character_.dir;
  return snapshot;
}

bool Player::is_dead() const { return character_.ability.hp == 0; }

bool Player::has_free_bag_slot() const {
  return std::any_of(character_.bag_items.begin(), character_.bag_items.end(),
                     [](const LegacyUserItem& item) { return is_empty(item); });
}

bool Player::has_free_storage_slot() const {
  return std::any_of(character_.storage_items.begin(), character_.storage_items.end(),
                     [](const LegacyUserItem& item) { return is_empty(item); });
}

bool Player::can_add_bag_item(
    const LegacyUserItem& item, const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  if (!has_free_bag_slot()) {
    return false;
  }

  std::int32_t total_weight = item_weight(item, item_configs);
  for (const auto& bag_item : character_.bag_items) {
    total_weight += item_weight(bag_item, item_configs);
  }
  return total_weight <= std::max<std::int32_t>(character_.ability.max_weight, 0);
}

LegacyUserItem* Player::bag_item_mutable(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      return &item;
    }
  }
  return nullptr;
}

const LegacyUserItem* Player::bag_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const {
  for (const auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      return &item;
    }
  }
  return nullptr;
}

const LegacyUserItem* Player::equipped_item(std::size_t slot) const {
  if (slot >= character_.equipped_items.size()) {
    return nullptr;
  }
  return &character_.equipped_items[slot];
}

LegacyUserItem* Player::equipped_item_mutable(std::size_t slot) {
  if (slot >= character_.equipped_items.size()) {
    return nullptr;
  }
  return &character_.equipped_items[slot];
}

const LegacyUseMagicInfo* Player::learned_magic(std::int32_t magic_id) const {
  for (const auto& magic : character_.magics) {
    if (!is_empty(magic) && magic.magic_id == magic_id) {
      return &magic;
    }
  }
  return nullptr;
}

LegacyUseMagicInfo* Player::learned_magic_mutable(std::int32_t magic_id) {
  for (auto& magic : character_.magics) {
    if (!is_empty(magic) && magic.magic_id == magic_id) {
      return &magic;
    }
  }
  return nullptr;
}

bool Player::add_legacy_magic(std::int32_t magic_id, char key, std::uint8_t level,
                              std::int32_t cur_train) {
  if (magic_id <= 0 || magic_id > 65535 || learned_magic(magic_id) != nullptr) {
    return false;
  }
  for (auto& magic : character_.magics) {
    if (is_empty(magic)) {
      magic.magic_id = static_cast<std::uint16_t>(magic_id);
      magic.key = key;
      magic.level = level;
      magic.cur_train = std::max(cur_train, 0);
      return true;
    }
  }
  return false;
}

bool Player::remove_legacy_magic(std::int32_t magic_id) {
  for (std::size_t index = 0; index < character_.magics.size(); ++index) {
    if (is_empty(character_.magics[index]) || character_.magics[index].magic_id != magic_id) {
      continue;
    }
    for (std::size_t move_index = index; move_index + 1 < character_.magics.size(); ++move_index) {
      character_.magics[move_index] = character_.magics[move_index + 1];
    }
    character_.magics.back() = LegacyUseMagicInfo{};
    legacy_magic_lvexp_generations_.erase(magic_id);
    return true;
  }
  return false;
}

bool Player::can_spend_gold(std::int32_t amount) const {
  return amount > 0 && character_.gold >= amount;
}

std::int32_t Player::pk_level() const { return std::max(character_.pk_point, 0) / 100; }

std::uint8_t Player::quest_mark(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_marks.size()) {
    return 0;
  }
  return character_.quest_marks[static_cast<std::size_t>(index)];
}

std::uint8_t Player::quest_open_unit(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_open_units.size()) {
    return 0;
  }
  return character_.quest_open_units[static_cast<std::size_t>(index)];
}

std::uint8_t Player::quest_unit(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_units.size()) {
    return 0;
  }
  return character_.quest_units[static_cast<std::size_t>(index)];
}

std::int32_t Player::script_param(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.script_params.size()) {
    return 0;
  }
  return character_.script_params[static_cast<std::size_t>(index)];
}

void Player::add_slave_actor_id(std::uint64_t actor_id) {
  if (actor_id == 0 ||
      std::find(slave_actor_ids_.begin(), slave_actor_ids_.end(), actor_id) !=
          slave_actor_ids_.end()) {
    return;
  }
  slave_actor_ids_.push_back(actor_id);
}

void Player::remove_slave_actor_id(std::uint64_t actor_id) {
  slave_actor_ids_.erase(std::remove(slave_actor_ids_.begin(), slave_actor_ids_.end(), actor_id),
                         slave_actor_ids_.end());
}

void Player::prune_slave_actor_ids(const std::unordered_set<std::uint64_t>& live_slave_ids) {
  slave_actor_ids_.erase(
      std::remove_if(slave_actor_ids_.begin(), slave_actor_ids_.end(),
                     [&](std::uint64_t actor_id) { return !live_slave_ids.contains(actor_id); }),
      slave_actor_ids_.end());
}

std::optional<LegacyUserItem> Player::remove_bag_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.bag_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      const auto removed = item;
      item = LegacyUserItem{};
      return removed;
    }
  }
  return std::nullopt;
}

std::optional<LegacyUserItem> Player::remove_bag_item_at(std::size_t slot) {
  if (slot >= character_.bag_items.size() || is_empty(character_.bag_items[slot])) {
    return std::nullopt;
  }
  const auto removed = character_.bag_items[slot];
  character_.bag_items[slot] = LegacyUserItem{};
  return removed;
}

std::optional<LegacyUserItem> Player::remove_storage_item(
    std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  for (auto& item : character_.storage_items) {
    if (matches_item(item, make_index, expected_name, item_configs)) {
      const auto removed = item;
      item = LegacyUserItem{};
      return removed;
    }
  }
  return std::nullopt;
}

std::optional<LegacyUserItem> Player::remove_equipped_item(
    std::size_t slot, std::int32_t make_index, std::string_view expected_name,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (slot >= character_.equipped_items.size()) {
    return std::nullopt;
  }
  auto& item = character_.equipped_items[slot];
  if (!matches_item(item, make_index, expected_name, item_configs)) {
    return std::nullopt;
  }
  const auto removed = item;
  item = LegacyUserItem{};
  return removed;
}

bool Player::add_bag_item(const LegacyUserItem& item) {
  for (auto& bag_item : character_.bag_items) {
    if (is_empty(bag_item)) {
      bag_item = item;
      return true;
    }
  }
  return false;
}

bool Player::add_storage_item(const LegacyUserItem& item) {
  for (auto& storage_item : character_.storage_items) {
    if (is_empty(storage_item)) {
      storage_item = item;
      return true;
    }
  }
  return false;
}

std::int32_t Player::melee_power() const {
  return std::max(1, std::max(packed_min(character_.ability.dc), packed_max(character_.ability.dc)));
}

std::int32_t Player::spell_power(std::int32_t base_power) const {
  const auto magic_bonus =
      std::max({packed_average(character_.ability.mc), packed_average(character_.ability.sc), 0});
  return std::max(1, base_power + magic_bonus);
}

std::int32_t Player::physical_defense() const {
  auto defense = std::max(0, packed_average(character_.ability.ac));
  if (legacy_defence_up_expire_tick_ != 0) {
    defense += 2 + static_cast<std::int32_t>(character_.ability.level) / 7;
  }
  return defense;
}

std::int32_t Player::magic_defense() const {
  auto defense = std::max(0, packed_average(character_.ability.mac));
  if (legacy_magic_defence_up_expire_tick_ != 0) {
    defense += 2 + static_cast<std::int32_t>(character_.ability.level) / 7;
  }
  return defense;
}

std::int32_t Player::current_shield_points(std::uint64_t current_tick) const {
  std::int32_t total = 0;
  for (const auto& effect : status_effects_) {
    if (effect.shield_points > 0 && current_tick <= effect.expire_tick) {
      total += effect.shield_points;
    }
  }
  return total;
}

bool Player::has_active_shield(std::uint64_t current_tick) const {
  return current_shield_points(current_tick) > 0;
}

std::int32_t Player::current_slow_percent(std::uint64_t current_tick) const {
  std::int32_t slow_percent = 0;
  for (const auto& effect : status_effects_) {
    if (current_tick <= effect.expire_tick && is_negative_effect(effect)) {
      slow_percent = std::max(slow_percent, effect.slow_percent);
    }
  }
  return slow_percent;
}

bool Player::can_move_at(std::uint64_t current_tick) const { return current_tick >= next_move_tick_; }

LegacyMoveThrottleResult Player::begin_move_attempt(std::uint64_t current_tick,
                                                    std::uint32_t tick_ms) {
  if (latest_walk_tick_ != 0 &&
      ticks_to_ms(current_tick > latest_walk_tick_ ? current_tick - latest_walk_tick_ : 0, tick_ms) <
          600) {
    ++walk_time_over_count_;
    ++walk_time_over_sum_;
  } else {
    walk_time_over_count_ = 0;
    if (walk_time_over_sum_ > 0) {
      --walk_time_over_sum_;
    }
  }

  latest_walk_tick_ = current_tick;
  if (walk_time_over_count_ < 4 && walk_time_over_sum_ < 6) {
    return {};
  }

  ++speed_hack_timer_over_count_;
  return LegacyMoveThrottleResult{false, speed_hack_timer_over_count_ > 8};
}

LegacySpellThrottleResult Player::begin_spell_attempt(std::uint64_t now_ms,
                                                      std::int32_t delay_time_ms,
                                                      bool sword_skill) {
  if (now_ms - latest_spell_time_ms_ > static_cast<std::uint64_t>(latest_spell_delay_ms_)) {
    spell_time_over_count_ = 0;
  } else {
    ++spell_time_over_count_;
  }

  if (spell_time_over_count_ < 2) {
    latest_spell_delay_ms_ = sword_skill ? 0 : std::max(delay_time_ms, 0) + 800;
    latest_spell_time_ms_ = now_ms;
    return LegacySpellThrottleResult{true, false, spell_time_over_count_};
  }

  if (sword_skill) {
    spell_time_over_count_ = 0;
    return LegacySpellThrottleResult{false, false, spell_time_over_count_};
  }

  latest_spell_time_ms_ = now_ms;
  ++spell_speed_hack_timer_over_count_;
  return LegacySpellThrottleResult{false, spell_speed_hack_timer_over_count_ > 8,
                                   spell_time_over_count_};
}

void Player::reset_move_throttle() {
  walk_time_over_count_ = 0;
  walk_time_over_sum_ = 0;
}

DamageResult Player::apply_damage(std::int32_t amount, std::uint64_t current_tick) {
  DamageResult result;
  if (amount <= 0 || character_.ability.hp == 0) {
    return result;
  }

  if (legacy_poison_damage_armor_expire_tick_ != 0 &&
      current_tick <= legacy_poison_damage_armor_expire_tick_) {
    amount = round_damage_120(amount);
  }

  const auto had_active_shield = has_active_shield(current_tick);
  auto remaining = amount;
  for (auto& effect : status_effects_) {
    if (remaining <= 0) {
      break;
    }
    if (effect.shield_points <= 0 || current_tick > effect.expire_tick) {
      continue;
    }
    const auto absorbed = std::min(remaining, effect.shield_points);
    effect.shield_points -= absorbed;
    remaining -= absorbed;
    result.absorbed_damage += absorbed;
    if (result.shield_name.empty()) {
      result.shield_name = effect.effect_name;
    }
  }

  if (remaining > 0) {
    const auto before = static_cast<std::int32_t>(character_.ability.hp);
    character_.ability.hp = clamp_u16(before - remaining);
    result.hp_damage = before - static_cast<std::int32_t>(character_.ability.hp);
  }
  result.shield_broken =
      had_active_shield && result.absorbed_damage > 0 && !has_active_shield(current_tick);
  return result;
}

std::int32_t Player::apply_heal(std::int32_t amount) {
  if (amount <= 0 || character_.ability.hp >= character_.ability.max_hp) {
    return 0;
  }
  const auto before = static_cast<std::int32_t>(character_.ability.hp);
  character_.ability.hp = clamp_u16(std::min(before + amount,
                                             static_cast<std::int32_t>(character_.ability.max_hp)));
  return static_cast<std::int32_t>(character_.ability.hp) - before;
}

void Player::queue_legacy_healing(std::int32_t amount, std::uint64_t current_tick,
                                  std::uint64_t tick_interval) {
  if (amount <= 0 || character_.ability.hp == 0 ||
      character_.ability.hp >= character_.ability.max_hp) {
    return;
  }
  legacy_inc_healing_ =
      std::min(kLegacyHealingCap, legacy_inc_healing_ + std::max(amount, 0));
  legacy_healing_tick_interval_ = std::max<std::uint64_t>(tick_interval, 1);
  if (legacy_next_healing_tick_ == 0 || current_tick >= legacy_next_healing_tick_) {
    legacy_next_healing_tick_ = current_tick + legacy_healing_tick_interval_;
  }
}

bool Player::legacy_healing_pending() const { return legacy_inc_healing_ > 0; }

bool Player::spend_mp(std::int32_t amount) {
  if (amount < 0) {
    return false;
  }
  if (static_cast<std::int32_t>(character_.ability.mp) < amount) {
    return false;
  }
  character_.ability.mp = clamp_u16(static_cast<std::int32_t>(character_.ability.mp) - amount);
  return true;
}

ExperienceResult Player::gain_experience(std::int32_t amount) {
  ExperienceResult result;
  result.gained = std::clamp(amount, 0, 60000);
  if (result.gained <= 0) {
    result.display_exp = static_cast<std::int32_t>(character_.ability.exp);
    return result;
  }

  character_.ability.exp += static_cast<std::uint32_t>(result.gained);
  result.display_exp = static_cast<std::int32_t>(character_.ability.exp);

  if (character_.ability.max_exp > 0 && character_.ability.exp >= character_.ability.max_exp) {
    character_.ability.exp -= character_.ability.max_exp;
    character_.ability.level = clamp_u8(static_cast<std::int32_t>(character_.ability.level) + 1);
    base_ability_.level = character_.ability.level;
    base_ability_.max_hp = clamp_u16(static_cast<std::int32_t>(base_ability_.max_hp) + 5);
    base_ability_.max_mp = clamp_u16(static_cast<std::int32_t>(base_ability_.max_mp) + 3);
    base_ability_.max_exp = next_level_exp(character_.ability.level);
    character_.ability.max_hp = base_ability_.max_hp;
    character_.ability.max_mp = base_ability_.max_mp;
    character_.ability.max_exp = base_ability_.max_exp;
    result.leveled_up = true;
  }
  base_ability_.level = character_.ability.level;
  base_ability_.exp = character_.ability.exp;
  base_ability_.max_exp = character_.ability.max_exp;

  return result;
}

void Player::add_status_effect(TimedStatusEffect effect) {
  if (!effect_has_payload(effect) || effect.expire_tick == 0) {
    return;
  }
  effect.tick_interval = std::max<std::uint64_t>(effect.tick_interval, 1);
  effect.next_tick = std::max(effect.next_tick, effect.tick_interval);
  status_effects_.push_back(std::move(effect));
}

bool Player::apply_legacy_poison(std::int32_t poison_kind, std::uint64_t duration_ticks,
                                 std::int32_t poison_level,
                                 std::uint64_t poison_tick_interval,
                                 std::uint64_t source_actor_id,
                                 std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  auto* expire_tick = poison_kind == kPoisonDecHealth
                          ? &legacy_poison_dechealth_expire_tick_
                          : poison_kind == kPoisonDamageArmor
                                ? &legacy_poison_damage_armor_expire_tick_
                                : poison_kind == kPoisonStone
                                      ? &legacy_poison_stone_expire_tick_
                                      : nullptr;
  if (expire_tick == nullptr) {
    return false;
  }
  const auto new_expire_tick = current_tick + duration_ticks;
  const auto changed = *expire_tick == 0 || new_expire_tick > *expire_tick;
  *expire_tick = std::max(*expire_tick, new_expire_tick);
  legacy_poison_level_ = std::max(poison_level, 0);
  if (source_actor_id != 0) {
    legacy_poison_source_actor_id_ = source_actor_id;
  }
  legacy_poison_tick_interval_ = std::max<std::uint64_t>(poison_tick_interval, 1);
  if (poison_kind == kPoisonDecHealth && legacy_next_poison_tick_ == 0) {
    legacy_next_poison_tick_ = current_tick + legacy_poison_tick_interval_;
  }
  set_legacy_status_bit(character_.status, poison_kind, true);
  return changed;
}

bool Player::activate_legacy_defence_up(std::uint64_t duration_ticks,
                                        std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  const auto changed = legacy_defence_up_expire_tick_ == 0 ||
                       expire_tick > legacy_defence_up_expire_tick_;
  legacy_defence_up_expire_tick_ = std::max(legacy_defence_up_expire_tick_, expire_tick);
  set_legacy_status_bit(character_.status, kStateDefenceUp, true);
  return changed;
}

bool Player::activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                              std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  const auto expire_tick = current_tick + duration_ticks;
  const auto changed = legacy_magic_defence_up_expire_tick_ == 0 ||
                       expire_tick > legacy_magic_defence_up_expire_tick_;
  legacy_magic_defence_up_expire_tick_ =
      std::max(legacy_magic_defence_up_expire_tick_, expire_tick);
  set_legacy_status_bit(character_.status, kStateMagicDefenceUp, true);
  return changed;
}

bool Player::legacy_transparent_active(std::uint64_t current_tick) const {
  return legacy_transparent_expire_tick_ != 0 && current_tick <= legacy_transparent_expire_tick_;
}

bool Player::activate_legacy_transparent(std::uint64_t duration_ticks,
                                         std::uint64_t current_tick) {
  if (duration_ticks == 0 || legacy_transparent_active(current_tick)) {
    return false;
  }
  legacy_transparent_expire_tick_ = current_tick + duration_ticks;
  set_legacy_status_bit(character_.status, kStateTransparent, true);
  return true;
}

bool Player::clear_legacy_transparent(std::uint64_t current_tick) {
  if (!legacy_transparent_active(current_tick)) {
    return false;
  }
  legacy_transparent_expire_tick_ = 0;
  set_legacy_status_bit(character_.status, kStateTransparent, false);
  return true;
}

std::size_t Player::clear_negative_status_effects(std::uint64_t current_tick) {
  const auto before = status_effects_.size();
  status_effects_.erase(
      std::remove_if(status_effects_.begin(), status_effects_.end(),
                     [](const TimedStatusEffect& effect) { return is_negative_effect(effect); }),
      status_effects_.end());
  if (status_effects_.size() != before) {
    next_move_tick_ = std::min(next_move_tick_, current_tick);
  }
  return before - status_effects_.size();
}

StatusTickResult Player::tick_status_effects(std::uint64_t current_tick) {
  StatusTickResult result;
  for (auto it = status_effects_.begin(); it != status_effects_.end();) {
    while (current_tick >= it->next_tick && it->next_tick <= it->expire_tick) {
      if (it->damage_per_tick > 0) {
        const auto applied = apply_damage(it->damage_per_tick, current_tick);
        result.damage += applied.hp_damage;
        result.absorbed_damage += applied.absorbed_damage;
        result.shield_broken = result.shield_broken || applied.shield_broken;
        if (result.shield_name.empty() && !applied.shield_name.empty()) {
          result.shield_name = applied.shield_name;
        }
        if (applied.hp_damage > 0 && it->source_actor_id != 0) {
          result.source_actor_id = it->source_actor_id;
        }
      }
      if (it->heal_per_tick > 0) {
        result.heal += apply_heal(it->heal_per_tick);
      }
      it->next_tick += std::max<std::uint64_t>(it->tick_interval, 1);
      if (character_.ability.hp == 0) {
        break;
      }
    }

    if (current_tick > it->expire_tick && it->shield_points > 0) {
      result.shield_expired = true;
      if (result.shield_name.empty()) {
        result.shield_name = it->effect_name;
      }
    }

    if (current_tick > it->expire_tick &&
        ((!effect_has_payload(*it)) || (it->damage_per_tick <= 0 && it->heal_per_tick <= 0) ||
         it->next_tick > it->expire_tick)) {
      it = status_effects_.erase(it);
    } else {
      ++it;
    }
  }

  while (legacy_inc_healing_ > 0 && legacy_next_healing_tick_ != 0 &&
         current_tick >= legacy_next_healing_tick_ && character_.ability.hp > 0) {
    const auto healed = apply_heal(std::min(legacy_inc_healing_, kLegacyHealingPerTick));
    if (healed <= 0) {
      legacy_inc_healing_ = 0;
      legacy_next_healing_tick_ = 0;
      break;
    }
    result.heal += healed;
    legacy_inc_healing_ -= healed;
    if (legacy_inc_healing_ <= 0 ||
        character_.ability.hp >= character_.ability.max_hp) {
      legacy_inc_healing_ = 0;
      legacy_next_healing_tick_ = 0;
      break;
    }
    legacy_next_healing_tick_ += std::max<std::uint64_t>(legacy_healing_tick_interval_, 1);
  }

  if (legacy_poison_dechealth_expire_tick_ != 0 &&
      current_tick > legacy_poison_dechealth_expire_tick_) {
    legacy_poison_dechealth_expire_tick_ = 0;
    legacy_next_poison_tick_ = 0;
    set_legacy_status_bit(character_.status, kPoisonDecHealth, false);
    result.legacy_status_changed = true;
  }
  if (legacy_poison_damage_armor_expire_tick_ != 0 &&
      current_tick > legacy_poison_damage_armor_expire_tick_) {
    legacy_poison_damage_armor_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kPoisonDamageArmor, false);
    result.legacy_status_changed = true;
  }
  if (legacy_poison_stone_expire_tick_ != 0 &&
      current_tick > legacy_poison_stone_expire_tick_) {
    legacy_poison_stone_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kPoisonStone, false);
    result.legacy_status_changed = true;
  }
  if (legacy_defence_up_expire_tick_ != 0 && current_tick > legacy_defence_up_expire_tick_) {
    legacy_defence_up_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kStateDefenceUp, false);
    result.ability_changed = true;
    result.legacy_status_changed = true;
  }
  if (legacy_magic_defence_up_expire_tick_ != 0 &&
      current_tick > legacy_magic_defence_up_expire_tick_) {
    legacy_magic_defence_up_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kStateMagicDefenceUp, false);
    result.ability_changed = true;
    result.legacy_status_changed = true;
  }
  if (legacy_transparent_expire_tick_ != 0 && current_tick > legacy_transparent_expire_tick_) {
    legacy_transparent_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kStateTransparent, false);
    result.legacy_status_changed = true;
  }
  if (legacy_magic_bubble_expire_tick_ != 0 && current_tick > legacy_magic_bubble_expire_tick_) {
    legacy_magic_bubble_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kStateBubbleDefenceUp, false);
    result.legacy_status_changed = true;
  }
  if (legacy_prepared_sword_expire_tick_ != 0 && current_tick > legacy_prepared_sword_expire_tick_) {
    clear_legacy_sword_skill();
  }
  while (legacy_poison_dechealth_expire_tick_ != 0 && legacy_next_poison_tick_ != 0 &&
         current_tick >= legacy_next_poison_tick_ &&
         legacy_next_poison_tick_ <= legacy_poison_dechealth_expire_tick_ &&
         character_.ability.hp > 0) {
    const auto applied = apply_damage(1 + legacy_poison_level_, current_tick);
    result.damage += applied.hp_damage;
    result.absorbed_damage += applied.absorbed_damage;
    result.shield_broken = result.shield_broken || applied.shield_broken;
    if (result.shield_name.empty() && !applied.shield_name.empty()) {
      result.shield_name = applied.shield_name;
    }
    if (applied.hp_damage > 0 && legacy_poison_source_actor_id_ != 0) {
      result.source_actor_id = legacy_poison_source_actor_id_;
    }
    legacy_next_poison_tick_ += std::max<std::uint64_t>(legacy_poison_tick_interval_, 1);
    if (character_.ability.hp == 0) {
      break;
    }
  }
  return result;
}

void Player::consume_move_action(std::uint64_t current_tick, bool running, std::uint32_t tick_ms) {
  const auto slow_percent = std::max(current_slow_percent(current_tick), 0);
  const auto base_interval = ms_to_ticks(running ? 250U : 250U, tick_ms);
  const auto interval = std::max<std::uint64_t>(
      1, (base_interval * static_cast<std::uint64_t>(100 + slow_percent) + 99) / 100);
  next_move_tick_ = current_tick + interval;
}

void Player::restore_full_vitals() {
  character_.ability.hp = character_.ability.max_hp;
  character_.ability.mp = character_.ability.max_mp;
}

void Player::equip_item(std::size_t slot, const LegacyUserItem& item) {
  if (slot >= character_.equipped_items.size()) {
    return;
  }
  character_.equipped_items[slot] = item;
}

void Player::apply_consumable(const ItemConfig& item_config) {
  character_.ability.hp =
      clamp_u16(static_cast<std::int32_t>(character_.ability.hp) + item_config.hp_add);
  character_.ability.mp =
      clamp_u16(static_cast<std::int32_t>(character_.ability.mp) + item_config.mp_add);
  character_.ability.hp = std::min(character_.ability.hp, character_.ability.max_hp);
  character_.ability.mp = std::min(character_.ability.mp, character_.ability.max_mp);
}

void Player::add_gold(std::int32_t amount) {
  character_.gold = std::max(0, character_.gold + amount);
}

void Player::spend_gold(std::int32_t amount) {
  if (amount > 0) {
    character_.gold = std::max(0, character_.gold - amount);
  }
}

void Player::set_guild_membership(std::string guild_name, std::string guild_title) {
  character_.guild_name = std::move(guild_name);
  character_.guild_title = std::move(guild_title);
}

void Player::clear_guild_membership() {
  character_.guild_name.clear();
  character_.guild_title.clear();
}

bool Player::set_quest_mark(std::int32_t index, std::uint8_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_marks.size()) {
    return false;
  }
  character_.quest_marks[static_cast<std::size_t>(index)] = value;
  return true;
}

bool Player::set_quest_open_unit(std::int32_t index, std::uint8_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_open_units.size()) {
    return false;
  }
  character_.quest_open_units[static_cast<std::size_t>(index)] = value;
  return true;
}

bool Player::set_quest_unit(std::int32_t index, std::uint8_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.quest_units.size()) {
    return false;
  }
  character_.quest_units[static_cast<std::size_t>(index)] = value;
  return true;
}

bool Player::set_script_param(std::int32_t index, std::int32_t value) {
  if (index < 0 || static_cast<std::size_t>(index) >= character_.script_params.size()) {
    return false;
  }
  character_.script_params[static_cast<std::size_t>(index)] = value;
  return true;
}

void Player::set_daily_quest(std::uint32_t value) {
  character_.daily_quest = value;
}

void Player::refresh_derived_state(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  base_ability_.level = character_.ability.level;
  base_ability_.exp = character_.ability.exp;
  base_ability_.max_exp = character_.ability.max_exp > 0
                              ? character_.ability.max_exp
                              : next_level_exp(character_.ability.level);
  const auto current_hp = character_.ability.hp;
  const auto current_mp = character_.ability.mp;
  auto derived = base_ability_;

  auto add_equipment_stats = [&](const LegacyUserItem& item) {
    if (is_empty(item) || item.dura == 0) {
      return;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      return;
    }
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    derived.ac = add_packed_range(derived.ac, upgraded.ac);
    derived.mac = add_packed_range(derived.mac, upgraded.mac);
    derived.dc = add_packed_range(derived.dc, upgraded.dc);
    derived.mc = add_packed_range(derived.mc, upgraded.mc);
    derived.sc = add_packed_range(derived.sc, upgraded.sc);
    derived.max_hp = clamp_u16(static_cast<std::int32_t>(derived.max_hp) + upgraded.hp_add);
    derived.max_mp = clamp_u16(static_cast<std::int32_t>(derived.max_mp) + upgraded.mp_add);
    accuracy_point_ += upgraded.accurate;
    speed_point_ += upgraded.agility;
  };

  accuracy_point_ = base_ability_.reserved1 > 0 ? base_ability_.reserved1 : 10;
  speed_point_ = base_ability_.exp_count > 0 ? base_ability_.exp_count : 10;
  for (const auto& item : character_.equipped_items) {
    add_equipment_stats(item);
  }

  std::int32_t bag_weight = 0;
  for (const auto& item : character_.bag_items) {
    bag_weight += item_weight(item, item_configs);
  }

  std::int32_t wear_weight = 0;
  std::int32_t hand_weight = 0;
  for (std::size_t slot = 0; slot < character_.equipped_items.size(); ++slot) {
    const auto weight = item_weight(character_.equipped_items[slot], item_configs);
    if (legacy_slot_uses_hand_weight(slot)) {
      hand_weight += weight;
    } else {
      wear_weight += weight;
    }
  }

  derived.hp = std::min(current_hp, derived.max_hp);
  derived.mp = std::min(current_mp, derived.max_mp);
  derived.weight = clamp_u16(bag_weight);
  derived.wear_weight = clamp_u8(wear_weight);
  derived.hand_weight = clamp_u8(hand_weight);
  derived.reserved1 = clamp_u8(accuracy_point_);
  derived.exp_count = clamp_u8(speed_point_);
  character_.ability = derived;

  const auto dress_feature =
      resolve_shape_feature(character_.sex, character_.equipped_items[kEquipDress], item_configs);
  const auto weapon_feature = resolve_shape_feature(character_.sex,
                                                    character_.equipped_items[kEquipWeapon],
                                                    item_configs);
  const auto face_feature =
      static_cast<std::uint8_t>(std::clamp(character_.hair * 2 + character_.sex, 0, 255));
  character_.feature = make_feature(0, dress_feature, weapon_feature, face_feature);
}

void Player::mark_dead(std::uint64_t now_ms) {
  if (character_.ability.hp != 0) {
    character_.ability.hp = 0;
  }
  if (character_.death_time_ms == 0) {
    character_.death_time_ms = now_ms;
  }
  next_move_tick_ = std::numeric_limits<std::uint64_t>::max();
}

void Player::revive_at(std::string map_id, std::int32_t x, std::int32_t y,
                       std::uint16_t hp, std::uint16_t mp) {
  character_.map_id = std::move(map_id);
  character_.x = x;
  character_.y = y;
  character_.ability.hp = std::min<std::uint16_t>(std::max<std::uint16_t>(hp, 1),
                                                  character_.ability.max_hp);
  character_.ability.mp = std::min<std::uint16_t>(mp, character_.ability.max_mp);
  character_.death_time_ms = 0;
  ghost_ = false;
  ghost_time_ms_ = 0;
  legacy_state_ = LegacyPlayerState::running;
  next_move_tick_ = 0;
  set_position(x, y);
}

void Player::inc_pk_point(std::int32_t amount) {
  character_.pk_point = std::max(0, character_.pk_point + amount);
}

void Player::record_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) {
  if (actor_id == 0 || actor_id == id()) {
    return;
  }
  const auto expire_before = now_ms >= 60000 ? now_ms - 60000 : 0;
  pk_hiters_.erase(std::remove_if(pk_hiters_.begin(), pk_hiters_.end(),
                                  [&](const PkHiterInfo& entry) {
                                    return entry.hit_time_ms < expire_before ||
                                           entry.actor_id == actor_id;
                                  }),
                   pk_hiters_.end());
  pk_hiters_.push_back(PkHiterInfo{actor_id, now_ms});
}

bool Player::has_recent_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) const {
  if (actor_id == 0) {
    return false;
  }
  const auto expire_before = now_ms >= 60000 ? now_ms - 60000 : 0;
  return std::any_of(pk_hiters_.begin(), pk_hiters_.end(), [&](const PkHiterInfo& entry) {
    return entry.actor_id == actor_id && entry.hit_time_ms >= expire_before;
  });
}

bool Player::legacy_due(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - run_time_ms_ >
         static_cast<std::int64_t>(run_next_tick_ms_);
}

bool Player::legacy_magic_bubble_active(std::uint64_t current_tick) const {
  return legacy_magic_bubble_expire_tick_ != 0 && legacy_magic_bubble_level_ >= 0 &&
         current_tick <= legacy_magic_bubble_expire_tick_;
}

bool Player::activate_legacy_magic_bubble(std::int32_t level, std::uint64_t current_tick,
                                          std::uint64_t expire_tick) {
  if (legacy_magic_bubble_active(current_tick)) {
    return false;
  }
  legacy_magic_bubble_level_ = std::max(level, 0);
  legacy_magic_bubble_expire_tick_ = expire_tick;
  set_legacy_status_bit(character_.status, kStateBubbleDefenceUp, true);
  return true;
}

void Player::damage_legacy_magic_bubble(std::uint64_t current_tick, std::uint64_t ticks) {
  if (!legacy_magic_bubble_active(current_tick)) {
    legacy_magic_bubble_expire_tick_ = 0;
    set_legacy_status_bit(character_.status, kStateBubbleDefenceUp, false);
    return;
  }
  const auto minimum_expire = current_tick + 1;
  if (legacy_magic_bubble_expire_tick_ > minimum_expire + ticks) {
    legacy_magic_bubble_expire_tick_ -= ticks;
  } else {
    legacy_magic_bubble_expire_tick_ = minimum_expire;
  }
}

void Player::prepare_legacy_sword_skill(std::int32_t magic_id, std::uint64_t expire_tick) {
  legacy_prepared_sword_magic_id_ = magic_id;
  legacy_prepared_sword_expire_tick_ = expire_tick;
}

std::int32_t Player::pending_legacy_sword_skill(std::uint64_t current_tick) const {
  if (legacy_prepared_sword_magic_id_ == 0 ||
      (legacy_prepared_sword_expire_tick_ != 0 &&
       current_tick > legacy_prepared_sword_expire_tick_)) {
    return 0;
  }
  return legacy_prepared_sword_magic_id_;
}

std::int32_t Player::consume_legacy_sword_skill(std::uint64_t current_tick) {
  if (legacy_prepared_sword_magic_id_ == 0 ||
      (legacy_prepared_sword_expire_tick_ != 0 && current_tick > legacy_prepared_sword_expire_tick_)) {
    clear_legacy_sword_skill();
    return 0;
  }
  const auto magic_id = legacy_prepared_sword_magic_id_;
  clear_legacy_sword_skill();
  return magic_id;
}

void Player::clear_legacy_sword_skill() {
  legacy_prepared_sword_magic_id_ = 0;
  legacy_prepared_sword_expire_tick_ = 0;
}

bool Player::legacy_open_health_active(std::uint64_t current_tick) const {
  return legacy_open_health_expire_tick_ != 0 && current_tick <= legacy_open_health_expire_tick_;
}

void Player::activate_legacy_open_health(std::uint64_t expire_tick) {
  legacy_open_health_expire_tick_ = std::max(legacy_open_health_expire_tick_, expire_tick);
}

std::uint32_t Player::legacy_magic_lvexp_generation(std::int32_t magic_id) const {
  const auto it = legacy_magic_lvexp_generations_.find(magic_id);
  return it == legacy_magic_lvexp_generations_.end() ? 0 : it->second;
}

std::uint32_t Player::advance_legacy_magic_lvexp_generation(std::int32_t magic_id) {
  auto& generation = legacy_magic_lvexp_generations_[magic_id];
  ++generation;
  if (generation == 0) {
    generation = 1;
  }
  return generation;
}

void Player::set_legacy_state(LegacyPlayerState state) { legacy_state_ = state; }

void Player::mark_legacy_notice_done(std::uint64_t now_ms) {
  login_sign_ = true;
  legacy_state_ = LegacyPlayerState::initialize_pending;
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

void Player::mark_legacy_initialize_done(std::uint64_t now_ms) {
  login_sign_ = true;
  ready_run_ = true;
  legacy_state_ = LegacyPlayerState::running;
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
  if (last_save_time_ms_ == 0) {
    last_save_time_ms_ = now_ms;
  }
}

void Player::mark_legacy_running_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

void Player::mark_legacy_autosaved(std::uint64_t now_ms) { last_save_time_ms_ = now_ms; }

void Player::mark_legacy_ghost(std::uint64_t now_ms) {
  ghost_ = true;
  ghost_time_ms_ = now_ms;
  legacy_state_ = LegacyPlayerState::ghost;
}

void Player::mark_legacy_closed() {
  ghost_ = true;
  legacy_state_ = LegacyPlayerState::closed;
}

void Player::rewind_legacy_run_time(std::uint64_t delta_ms) {
  run_time_ms_ -= static_cast<std::int64_t>(delta_ms);
}

void Player::enqueue_legacy_command(ActorMail mail, std::uint64_t now_ms) {
  legacy_inbox_.push_back(LegacyQueuedCommand{std::move(mail), now_ms, ++legacy_command_sequence_});
}

std::optional<LegacyQueuedCommand> Player::pop_legacy_command() {
  if (legacy_inbox_.empty()) {
    return std::nullopt;
  }
  auto command = std::move(legacy_inbox_.front());
  legacy_inbox_.pop_front();
  return command;
}

void Player::on_mail(const ActorMail& mail, MapContext& context) {
  GameObject::on_mail(mail, context);
  switch (mail.kind) {
    case ActorMailKind::turn:
      character_.dir = mail.dir;
      break;
    case ActorMailKind::move:
    case ActorMailKind::run:
      character_.x = mail.x;
      character_.y = mail.y;
      character_.dir = mail.dir;
      static_cast<void>(clear_legacy_transparent(context.tick));
      break;
    case ActorMailKind::attack:
    case ActorMailKind::spell:
      character_.dir = mail.dir;
      static_cast<void>(clear_legacy_transparent(context.tick));
      break;
    default:
      break;
  }
}

void Player::on_tick(MapContext& context) {
  if (context.tick % 500 == 0) {
    PersistRequest request;
    request.kind = PersistRequestKind::save_character;
    request.account_id = character_.account_id;
    request.character_name = character_.character_name;
    request.character = snapshot();
    context.request_persist(std::move(request));
  }
  set_next_due_tick(context.tick + 1);
}

Monster::Monster(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
                 std::int32_t level, std::int32_t max_hp, std::int32_t attack_power,
                 std::int32_t dc_min, std::int32_t dc_max, std::int32_t defense,
                 std::int32_t magic_defense, std::int32_t mc, std::int32_t sc,
                 std::int32_t exp_reward,
                 std::int32_t life_attrib, std::int32_t max_mp,
                 std::int32_t race_server, std::int32_t race_image,
                 std::int32_t appearance, std::int32_t cool_eye,
                 std::int32_t speed, std::int32_t accuracy,
                 std::int32_t walk_speed_ms, std::int32_t walk_step,
                 std::int32_t walk_wait_ms, std::int32_t attack_speed_ms,
                 MonsterAiProfile ai_profile, std::uint64_t search_rate_ms,
                 std::int32_t home_x, std::int32_t home_y,
                 std::int32_t home_area, bool legacy_spawn_group,
                 std::uint64_t master_actor_id, bool is_slave,
                 std::int32_t slave_exp, std::int32_t slave_make_level,
                 std::int32_t slave_exp_level,
                 std::uint64_t master_royalty_time_ms,
                 std::uint64_t slave_life_time_ms,
                 bool no_item,
                 bool tameable,
                 std::vector<LegacyUserItem> drop_items, std::int32_t drop_gold)
    : GameObject(id, GameObjectKind::monster, std::move(name), std::move(map_id), x, y),
      level_(std::max(level, 1)),
      hp_(std::max(max_hp, 1)),
      max_hp_(std::max(max_hp, 1)),
      mp_(std::max(max_mp, 0)),
      max_mp_(std::max(max_mp, 0)),
      attack_power_(std::max(dc_max > 0 ? dc_max : attack_power, 1)),
      dc_min_(std::max(dc_min, 0)),
      dc_max_(std::max({dc_max, dc_min_, attack_power_, 1})),
      defense_(std::max(defense, 0)),
      magic_defense_(std::max(magic_defense, 0)),
      mc_(std::max(mc, 0)),
      sc_(std::max(sc, 0)),
      exp_reward_(std::max(exp_reward, 1)),
      life_attrib_(std::max(life_attrib, 0)),
      race_server_(race_server),
      race_image_(race_image),
      appearance_(appearance),
      cool_eye_(cool_eye),
      speed_point_(speed),
      accuracy_point_(accuracy),
      walk_speed_ms_(std::max(walk_speed_ms, 1)),
      walk_step_(std::max(walk_step, 1)),
      walk_wait_ms_(std::max(walk_wait_ms, 0)),
      attack_speed_ms_(std::max(attack_speed_ms, 1)),
      ai_profile_(ai_profile),
      search_rate_ms_(search_rate_ms != 0
                          ? search_rate_ms
                          : (ai_profile == MonsterAiProfile::aggressive ? 1500 : 3000)),
      home_x_(home_x),
      home_y_(home_y),
      home_area_(std::max(home_area, 0)),
      legacy_spawn_group_(legacy_spawn_group),
      master_actor_id_(master_actor_id),
      is_slave_(is_slave || master_actor_id != 0),
      slave_exp_(std::max(slave_exp, 0)),
      slave_make_level_(std::max(slave_make_level, 0)),
      slave_exp_level_(std::clamp(slave_exp_level, 0, 6)),
      master_royalty_time_ms_(master_royalty_time_ms),
      slave_life_time_ms_(slave_life_time_ms),
      no_item_(no_item || is_slave || master_actor_id != 0),
      tameable_(tameable),
      base_max_hp_(std::max(max_hp, 1)),
      base_dc_max_(std::max({dc_max, dc_min, attack_power, 1})),
      base_magic_defense_(std::max(magic_defense, 0)),
      drop_items_(std::move(drop_items)),
      drop_gold_(std::max(drop_gold, 0)) {
  switch (race_server_) {
    case kRcDualAxeSkeleton:
      chain_shot_count_ = 2;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcThornDark:
      chain_shot_count_ = 3;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcArcherMon:
      chain_shot_count_ = 6;
      run_next_tick_ms_ = 250;
      search_rate_ms_ = 3000;
      break;
    case kRcKillingHerb:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 4;
      dig_down_range_ = 4;
      run_next_tick_ms_ = 250;
      break;
    case kRcCentipedeKing:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 4;
      dig_down_range_ = 6;
      run_next_tick_ms_ = 250;
      break;
    case kRcBeeQueen:
      stick_mode_ = true;
      summon_monster_name_ = "__Bee";
      summon_limit_ = 15;
      summon_delay_ms_ = 500;
      run_next_tick_ms_ = 250;
      break;
    case kRcSpiderHouse:
      stick_mode_ = true;
      summon_monster_name_ = "__Spider";
      summon_limit_ = 15;
      summon_delay_ms_ = 500;
      run_next_tick_ms_ = 250;
      break;
    case kRcScultureKing:
    case kRcScultureKingNoFollower:
      hide_mode_ = true;
      stick_mode_ = true;
      dig_up_range_ = 2;
      dig_down_range_ = 8;
      run_next_tick_ms_ = 250;
      break;
    case kRcDoorGuard:
    case kRcArcherGuard:
    case kRcArcherPolice:
    case kRcCastleDoor:
    case kRcWall:
      stick_mode_ = true;
      run_next_tick_ms_ = 250;
      break;
    case kRcSpitSpider:
    case kRcHighRiskSpider:
    case kRcBigPoisonSpider:
    case kRcBigKudeki:
    case kRcToxicGhost:
    case kRcMagCowFaceMon:
      search_rate_ms_ = search_rate_ms != 0 ? search_rate_ms : 1500;
      break;
    default:
      break;
  }
  if (is_slave_) {
    no_item_ = true;
    apply_slave_level_abilities();
  }
}

bool Monster::is_dead() const { return hp_ <= 0; }

MonsterSnapshot Monster::snapshot() const {
  return MonsterSnapshot{.id = id(),
                         .name = name(),
                         .map_id = map_id(),
                         .x = x(),
                         .y = y(),
                         .level = level_,
                         .hp = hp_,
                         .max_hp = max_hp_,
                         .mp = mp_,
                         .max_mp = max_mp_,
                         .dc_min = dc_min_,
                         .dc_max = dc_max_,
                         .attack_power = attack_power_,
                         .defense = defense_,
                         .magic_defense = magic_defense_,
                         .mc = mc_,
                         .sc = sc_,
                         .exp_reward = exp_reward_,
                         .life_attrib = life_attrib_,
                         .race_server = race_server_,
                         .race_image = race_image_,
                         .appearance = appearance_,
                         .cool_eye = cool_eye_,
                         .speed_point = speed_point_,
                         .accuracy_point = accuracy_point_,
                         .walk_speed_ms = walk_speed_ms_,
                         .walk_step = walk_step_,
                         .walk_wait_ms = walk_wait_ms_,
                         .attack_speed_ms = attack_speed_ms_,
                         .target_actor_id = aggro_target_id_,
                         .target_focus_time_ms = target_focus_time_ms_,
                         .target_x = target_x_,
                         .target_y = target_y_,
                         .walk_time_ms = walk_time_ms_,
                          .hit_time_ms = hit_time_ms_,
                          .search_enemy_time_ms = search_enemy_time_ms_,
                          .think_time_ms = think_time_ms_,
                          .last_hitter_id = last_hitter_id_,
                          .last_hit_time_ms = last_hit_time_ms_,
                          .exp_hitter_id = exp_hitter_id_,
                          .exp_hit_time_ms = exp_hit_time_ms_,
                          .death_time_ms = death_time_ms_,
                          .ghost_time_ms = ghost_time_ms_,
                          .walk_wait_mode = walk_wait_mode_,
                          .dup_mode = dup_mode_,
                          .ghosted = ghosted_,
                          .death_settled = death_settled_,
                          .chain_shot = chain_shot_,
                          .chain_shot_count = chain_shot_count_,
                          .hide_mode = hide_mode_,
                          .stick_mode = stick_mode_,
                          .dig_up_range = dig_up_range_,
                          .dig_down_range = dig_down_range_,
                          .appear_time_ms = appear_time_ms_,
                          .child_actor_count = child_actor_ids_.size(),
                          .summon_limit = summon_limit_,
                          .master_actor_id = master_actor_id_,
                          .is_slave = is_slave_,
                          .slave_exp = slave_exp_,
                          .slave_make_level = slave_make_level_,
                          .slave_exp_level = slave_exp_level_,
                          .master_royalty_time_ms = master_royalty_time_ms_,
                          .slave_life_time_ms = slave_life_time_ms_,
                          .no_item = no_item_,
                          .tameable = tameable_};
}

bool Monster::legacy_due(std::uint64_t now_ms) const {
  return run_next_tick_ms_ == 0 ||
         static_cast<std::int64_t>(now_ms) - run_time_ms_ >
             static_cast<std::int64_t>(run_next_tick_ms_);
}

bool Monster::legacy_search_due(std::uint64_t now_ms) const {
  return now_ms > search_time_ms_ + search_rate_ms_;
}

bool Monster::legacy_walk_due(std::uint64_t now_ms) const {
  return legacy_walk_due_by_walk_time(now_ms);
}

bool Monster::legacy_attack_due(std::uint64_t now_ms) const {
  return legacy_attack_due_by_hit_time(now_ms);
}

bool Monster::legacy_walk_due_by_walk_time(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(walk_time_ms_) >
         static_cast<std::int64_t>(std::max(walk_speed_ms_, 1));
}

bool Monster::legacy_attack_due_by_hit_time(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(hit_time_ms_) >
         static_cast<std::int64_t>(std::max(attack_speed_ms_, 1));
}

bool Monster::legacy_walk_wait_elapsed(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) -
             static_cast<std::int64_t>(walk_wait_cur_time_ms_) >
         static_cast<std::int64_t>(std::max(walk_wait_ms_, 0));
}

std::int32_t Monster::apply_damage(std::int32_t amount, std::uint64_t attacker_id) {
  return apply_damage(amount, attacker_id, 0);
}

std::int32_t Monster::apply_damage(std::int32_t amount, std::uint64_t attacker_id,
                                   std::uint64_t now_ms) {
  if (amount <= 0 || hp_ <= 0) {
    return 0;
  }
  if (legacy_poison_damage_armor_expire_tick_ != 0) {
    amount = round_damage_120(amount);
  }
  const auto before = hp_;
  hp_ = std::max(0, hp_ - amount);
  if (attacker_id != 0) {
    record_legacy_hitter(attacker_id, now_ms);
  }
  if (hp_ == 0 && now_ms != 0) {
    mark_legacy_death(now_ms);
  }
  return before - hp_;
}

void Monster::add_status_effect(TimedStatusEffect effect) {
  if ((effect.damage_per_tick <= 0 && effect.slow_percent <= 0) || effect.expire_tick == 0) {
    return;
  }
  effect.tick_interval = std::max<std::uint64_t>(effect.tick_interval, 1);
  effect.next_tick = std::max<std::uint64_t>(effect.next_tick, effect.tick_interval);
  status_effects_.push_back(std::move(effect));
}

bool Monster::apply_legacy_poison(std::int32_t poison_kind, std::uint64_t duration_ticks,
                                  std::int32_t poison_level,
                                  std::uint64_t poison_tick_interval,
                                  std::uint64_t source_actor_id,
                                  std::uint64_t current_tick) {
  if (duration_ticks == 0) {
    return false;
  }
  auto* expire_tick = poison_kind == kPoisonDecHealth
                          ? &legacy_poison_dechealth_expire_tick_
                          : poison_kind == kPoisonDamageArmor
                                ? &legacy_poison_damage_armor_expire_tick_
                                : poison_kind == kPoisonStone
                                      ? &legacy_poison_stone_expire_tick_
                                      : nullptr;
  if (expire_tick == nullptr) {
    return false;
  }
  const auto new_expire_tick = current_tick + duration_ticks;
  const auto changed = *expire_tick == 0 || new_expire_tick > *expire_tick;
  *expire_tick = std::max(*expire_tick, new_expire_tick);
  legacy_poison_level_ = std::max(poison_level, 0);
  if (source_actor_id != 0) {
    legacy_poison_source_actor_id_ = source_actor_id;
    record_legacy_hitter(source_actor_id, 0);
  }
  legacy_poison_tick_interval_ = std::max<std::uint64_t>(poison_tick_interval, 1);
  if (poison_kind == kPoisonDecHealth && legacy_next_poison_tick_ == 0) {
    legacy_next_poison_tick_ = current_tick + legacy_poison_tick_interval_;
  }
  return changed;
}

StatusTickResult Monster::tick_status_effects(std::uint64_t current_tick) {
  StatusTickResult result;
  for (auto it = status_effects_.begin(); it != status_effects_.end();) {
    while (it->damage_per_tick > 0 && hp_ > 0 && current_tick >= it->next_tick &&
           it->next_tick <= it->expire_tick) {
      const auto applied = apply_damage(it->damage_per_tick, it->source_actor_id);
      result.damage += applied;
      if (applied > 0 && it->source_actor_id != 0) {
        result.source_actor_id = it->source_actor_id;
      }
      it->next_tick += std::max<std::uint64_t>(it->tick_interval, 1);
    }

    if (current_tick > it->expire_tick &&
        (it->damage_per_tick <= 0 || it->next_tick > it->expire_tick)) {
      it = status_effects_.erase(it);
    } else {
      ++it;
    }
  }
  if (legacy_poison_dechealth_expire_tick_ != 0 &&
      current_tick > legacy_poison_dechealth_expire_tick_) {
    legacy_poison_dechealth_expire_tick_ = 0;
    legacy_next_poison_tick_ = 0;
    result.legacy_status_changed = true;
  }
  if (legacy_poison_damage_armor_expire_tick_ != 0 &&
      current_tick > legacy_poison_damage_armor_expire_tick_) {
    legacy_poison_damage_armor_expire_tick_ = 0;
    result.legacy_status_changed = true;
  }
  if (legacy_poison_stone_expire_tick_ != 0 &&
      current_tick > legacy_poison_stone_expire_tick_) {
    legacy_poison_stone_expire_tick_ = 0;
    result.legacy_status_changed = true;
  }
  while (legacy_poison_dechealth_expire_tick_ != 0 && legacy_next_poison_tick_ != 0 &&
         current_tick >= legacy_next_poison_tick_ &&
         legacy_next_poison_tick_ <= legacy_poison_dechealth_expire_tick_ && hp_ > 0) {
    const auto applied = apply_damage(1 + legacy_poison_level_, 0);
    result.damage += applied;
    if (applied > 0 && legacy_poison_source_actor_id_ != 0) {
      result.source_actor_id = legacy_poison_source_actor_id_;
    }
    legacy_next_poison_tick_ += std::max<std::uint64_t>(legacy_poison_tick_interval_, 1);
    if (hp_ == 0) {
      break;
    }
  }
  return result;
}

std::uint64_t Monster::next_status_tick() const {
  std::uint64_t next_tick = 0;
  for (const auto& effect : status_effects_) {
    if (effect.damage_per_tick <= 0 || effect.next_tick > effect.expire_tick) {
      continue;
    }
    if (next_tick == 0 || effect.next_tick < next_tick) {
      next_tick = effect.next_tick;
    }
  }
  if (legacy_next_poison_tick_ != 0 && legacy_poison_dechealth_expire_tick_ != 0 &&
      legacy_next_poison_tick_ <= legacy_poison_dechealth_expire_tick_ &&
      (next_tick == 0 || legacy_next_poison_tick_ < next_tick)) {
    next_tick = legacy_next_poison_tick_;
  }
  return next_tick;
}

std::int32_t Monster::current_slow_percent(std::uint64_t current_tick) const {
  std::int32_t slow_percent = 0;
  for (const auto& effect : status_effects_) {
    if (current_tick <= effect.expire_tick) {
      slow_percent = std::max(slow_percent, effect.slow_percent);
    }
  }
  return slow_percent;
}

void Monster::mark_legacy_run_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

void Monster::mark_legacy_search_time(std::uint64_t now_ms) { search_time_ms_ = now_ms; }

void Monster::mark_legacy_attack_time(std::uint64_t now_ms) {
  attack_time_ms_ = now_ms;
  hit_time_ms_ = now_ms;
}

void Monster::mark_legacy_walk_time(std::uint64_t now_ms) { walk_time_ms_ = now_ms; }

void Monster::mark_legacy_hit_time(std::uint64_t now_ms) {
  attack_time_ms_ = now_ms;
  hit_time_ms_ = now_ms;
}

void Monster::mark_search_enemy_time(std::uint64_t now_ms) { search_enemy_time_ms_ = now_ms; }

void Monster::mark_think_time(std::uint64_t now_ms) { think_time_ms_ = now_ms; }

void Monster::mark_legacy_ghost_time(std::uint64_t now_ms) { ghost_time_ms_ = now_ms; }

void Monster::record_legacy_hitter(std::uint64_t attacker_id, std::uint64_t now_ms,
                                   bool exp_hitter) {
  if (attacker_id == 0) {
    return;
  }
  if (now_ms != 0 && exp_hitter_id_ != 0 && exp_hit_time_ms_ != 0 &&
      now_ms > exp_hit_time_ms_ + 6000ULL) {
    clear_exp_hitter();
  }
  if (now_ms != 0 && last_hitter_id_ != 0 && last_hit_time_ms_ != 0 &&
      now_ms > last_hit_time_ms_ + 30000ULL) {
    clear_last_hitter();
  }
  last_hitter_id_ = attacker_id;
  last_hit_time_ms_ = now_ms;
  if (exp_hitter && (exp_hitter_id_ == 0 || exp_hitter_id_ == attacker_id)) {
    exp_hitter_id_ = attacker_id;
    exp_hit_time_ms_ = now_ms;
  }
}

void Monster::clear_last_hitter() {
  last_hitter_id_ = 0;
  last_hit_time_ms_ = 0;
}

void Monster::clear_exp_hitter() {
  exp_hitter_id_ = 0;
  exp_hit_time_ms_ = 0;
}

void Monster::clear_legacy_hitters() {
  clear_last_hitter();
  clear_exp_hitter();
}

void Monster::expire_legacy_hitters(std::uint64_t now_ms) {
  if (is_dead() || legacy_ghosted()) {
    clear_legacy_hitters();
    return;
  }
  if (exp_hitter_id_ != 0 && exp_hit_time_ms_ != 0 && now_ms > exp_hit_time_ms_ + 6000ULL) {
    clear_exp_hitter();
  }
  if (last_hitter_id_ != 0 && last_hit_time_ms_ != 0 && now_ms > last_hit_time_ms_ + 30000ULL) {
    clear_last_hitter();
  }
}

void Monster::mark_legacy_death(std::uint64_t now_ms) {
  hp_ = 0;
  if (death_time_ms_ == 0) {
    death_time_ms_ = now_ms;
  }
  aggro_target_id_ = 0;
  target_focus_time_ms_ = 0;
  clear_target_xy();
  walk_wait_mode_ = false;
  walk_wait_cur_time_ms_ = 0;
}

void Monster::mark_legacy_ghost(std::uint64_t now_ms) {
  ghosted_ = true;
  ghost_time_ms_ = now_ms;
}

bool Monster::death_due_for_ghost(std::uint64_t now_ms, std::uint64_t corpse_ms) const {
  return is_dead() && !ghosted_ && death_time_ms_ != 0 &&
         static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(death_time_ms_) >
             static_cast<std::int64_t>(corpse_ms);
}

void Monster::mark_death_settled() { death_settled_ = true; }

void Monster::set_chain_shot(std::int32_t value) {
  chain_shot_ = std::max(value, 0);
}

void Monster::increment_chain_shot() { ++chain_shot_; }

void Monster::set_chain_shot_count(std::int32_t value) {
  chain_shot_count_ = std::max(value, 0);
}

void Monster::set_hide_mode(bool value) { hide_mode_ = value; }

void Monster::set_stick_mode(bool value) { stick_mode_ = value; }

void Monster::set_dig_ranges(std::int32_t up_range, std::int32_t down_range) {
  dig_up_range_ = std::max(up_range, 0);
  dig_down_range_ = std::max(down_range, 0);
}

void Monster::set_appear_time_ms(std::uint64_t now_ms) { appear_time_ms_ = now_ms; }

void Monster::add_child_actor_id(std::uint64_t actor_id) {
  if (actor_id != 0) {
    child_actor_ids_.push_back(actor_id);
  }
}

void Monster::prune_child_actor_ids(const std::unordered_set<std::uint64_t>& live_child_ids) {
  child_actor_ids_.erase(
      std::remove_if(child_actor_ids_.begin(), child_actor_ids_.end(),
                     [&](std::uint64_t actor_id) {
                       return live_child_ids.find(actor_id) == live_child_ids.end();
                     }),
      child_actor_ids_.end());
}

void Monster::set_summon(std::string monster_name, std::int32_t limit,
                         std::uint64_t delay_ms) {
  summon_monster_name_ = std::move(monster_name);
  summon_limit_ = std::max(limit, 0);
  summon_delay_ms_ = delay_ms;
}

void Monster::set_master_actor_id(std::uint64_t actor_id) {
  master_actor_id_ = actor_id;
  is_slave_ = actor_id != 0;
  if (is_slave_) {
    no_item_ = true;
  }
}

void Monster::configure_slave(std::uint64_t master_actor_id, std::int32_t slave_exp,
                              std::int32_t slave_make_level, std::int32_t slave_exp_level,
                              std::uint64_t master_royalty_time_ms,
                              std::uint64_t slave_life_time_ms, bool no_item) {
  master_actor_id_ = master_actor_id;
  is_slave_ = master_actor_id != 0;
  slave_exp_ = std::max(slave_exp, 0);
  slave_make_level_ = std::max(slave_make_level, 0);
  slave_exp_level_ = std::clamp(slave_exp_level, 0, 6);
  master_royalty_time_ms_ = master_royalty_time_ms;
  slave_life_time_ms_ = slave_life_time_ms;
  no_item_ = no_item || is_slave_;
  apply_slave_level_abilities();
}

void Monster::set_hp_mp(std::int32_t hp, std::int32_t mp) {
  hp_ = std::clamp(hp, 0, max_hp_);
  mp_ = std::clamp(mp, 0, max_mp_);
  if (hp_ == 0) {
    death_time_ms_ = death_time_ms_ == 0 ? 1 : death_time_ms_;
  }
}

void Monster::reduce_hp_to_loyalty_break_floor() {
  hp_ = std::max(1, hp_ / 10);
}

void Monster::apply_slave_level_abilities() {
  max_hp_ = std::max(base_max_hp_, 1);
  dc_max_ = std::max(base_dc_max_, dc_min_);
  attack_power_ = std::max(dc_max_, 1);
  magic_defense_ = base_magic_defense_;
  accuracy_point_ = 15;

  const auto exp_level = std::clamp(slave_exp_level_, 0, 6);
  if (exp_level <= 0) {
    hp_ = std::clamp(hp_, 0, max_hp_);
    return;
  }

  if (legacy_special_slave_name(name())) {
    const auto factor = 0.3 + static_cast<double>(exp_level) * 0.1;
    dc_max_ += static_cast<std::int32_t>(std::lround(3.0 * factor * exp_level));
    max_hp_ += static_cast<std::int32_t>(
        std::lround(static_cast<double>(base_max_hp_) * factor)) *
               exp_level;
  } else {
    dc_max_ += 2 * exp_level;
    max_hp_ = std::min(base_max_hp_ + 60 * exp_level,
                       base_max_hp_ +
                           static_cast<std::int32_t>(
                               std::lround(static_cast<double>(base_max_hp_) * 0.15)) *
                               exp_level);
    magic_defense_ = 0;
  }
  dc_max_ = std::max(dc_max_, dc_min_);
  attack_power_ = std::max(dc_max_, 1);
  hp_ = std::clamp(hp_, 0, max_hp_);
}

bool Monster::gain_slave_exp(std::int32_t slain_level) {
  if (!is_slave_) {
    return false;
  }
  static constexpr std::array<std::int32_t, 7> kSlaveExpMore{0, 0, 50, 100, 200, 300, 600};
  const auto exp_level = std::clamp(slave_exp_level_, 0, 6);
  const auto next = 100 + std::max(level_, 1) * 15 + kSlaveExpMore[exp_level];
  slave_exp_ += std::max(slain_level, 0);
  if (slave_exp_ <= next) {
    return false;
  }
  slave_exp_ -= next;
  const auto cap = std::max(slave_make_level_, 0) * 2 + 1;
  if (slave_exp_level_ >= cap) {
    return false;
  }
  slave_exp_level_ = std::min(slave_exp_level_ + 1, 6);
  apply_slave_level_abilities();
  return true;
}

void Monster::schedule_next_ai_tick(std::uint64_t current_tick) {
  const auto slow_percent = std::max(current_slow_percent(current_tick), 0);
  const auto interval =
      static_cast<std::uint64_t>(std::max<std::int32_t>(1, (100 + slow_percent + 99) / 100));
  next_ai_tick_ = current_tick + interval;
}

bool Monster::inside_home_area() const {
  return std::abs(x() - home_x_) <= home_area_ && std::abs(y() - home_y_) <= home_area_;
}

void Monster::select_target(std::uint64_t actor_id, std::uint64_t now_ms) {
  aggro_target_id_ = actor_id;
  target_focus_time_ms_ = now_ms;
}

void Monster::lose_target() {
  aggro_target_id_ = 0;
  target_focus_time_ms_ = 0;
  clear_target_xy();
}

void Monster::set_target_xy(std::int32_t x, std::int32_t y) {
  target_x_ = x;
  target_y_ = y;
}

void Monster::clear_target_xy() {
  target_x_ = -1;
  target_y_ = -1;
}

void Monster::begin_walk_wait(std::uint64_t now_ms) {
  walk_wait_mode_ = true;
  walk_wait_cur_time_ms_ = now_ms;
}

void Monster::set_walk_wait_mode(bool value) {
  walk_wait_mode_ = value;
  if (!value) {
    walk_wait_cur_time_ms_ = 0;
  }
}

void Monster::set_dup_mode(bool value) { dup_mode_ = value; }

void Monster::reset_walk_cur_step() { walk_cur_step_ = 0; }

void Monster::increment_walk_cur_step() { ++walk_cur_step_; }

void Monster::initialize_legacy_ai_timers(std::uint64_t now_ms,
                                          std::uint64_t walk_offset_ms,
                                          std::uint64_t hit_offset_ms) {
  walk_time_ms_ = now_ms >= walk_offset_ms ? now_ms - walk_offset_ms : 0;
  hit_time_ms_ = now_ms >= hit_offset_ms ? now_ms - hit_offset_ms : 0;
  attack_time_ms_ = hit_time_ms_;
  search_enemy_time_ms_ = now_ms;
}

void Monster::on_tick(MapContext& context) {
  if (hp_ <= 0) {
    set_next_due_tick(context.tick + 50);
    return;
  }
  auto next_due_tick = next_ai_tick_ > context.tick ? next_ai_tick_ : context.tick + 1;
  if (const auto status_tick = next_status_tick(); status_tick != 0) {
    next_due_tick = std::min(next_due_tick, status_tick > context.tick ? status_tick : context.tick + 1);
  }
  set_next_due_tick(next_due_tick);
}

Npc::Npc(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
         std::string service, std::vector<LegacyUserItem> merchant_items,
         std::vector<NpcDialogSectionConfig> dialog_sections,
         std::int32_t price_rate_percent, std::string merchant_key,
         std::vector<MerchantProductRuntimeConfig> merchant_products,
         std::unordered_map<std::int32_t, std::int32_t> merchant_prices,
         std::vector<std::int32_t> deal_std_modes)
    : GameObject(id, GameObjectKind::npc, std::move(name), std::move(map_id), x, y),
      service_(normalize_service(std::move(service))),
      merchant_key_(std::move(merchant_key)),
      merchant_items_(std::move(merchant_items)),
      merchant_products_(std::move(merchant_products)),
      merchant_prices_(std::move(merchant_prices)),
      deal_std_modes_(std::move(deal_std_modes)),
      dialog_sections_(std::move(dialog_sections)),
      price_rate_percent_(std::max(price_rate_percent, 0)),
      buy_enabled_(!merchant_items_.empty() || !merchant_products_.empty()) {}

bool Npc::supports_buy() const { return buy_enabled_; }

bool Npc::supports_sell() const { return service_.find("sell") != std::string::npos; }

bool Npc::supports_repair() const { return service_.find("repair") != std::string::npos; }

bool Npc::supports_storage() const { return service_.find("storage") != std::string::npos; }

bool Npc::supports_guild() const { return service_.find("guild") != std::string::npos; }

bool Npc::supports_castle() const { return service_.find("castle") != std::string::npos; }

bool Npc::legacy_due(std::uint64_t now_ms) const {
  return static_cast<std::int64_t>(now_ms) - run_time_ms_ >
         static_cast<std::int64_t>(run_next_tick_ms_);
}

bool Npc::legacy_search_due(std::uint64_t now_ms) const {
  return now_ms > search_time_ms_ + search_rate_ms_;
}

void Npc::mark_legacy_run_time(std::uint64_t now_ms) {
  run_time_ms_ = static_cast<std::int64_t>(now_ms);
}

void Npc::mark_legacy_search_time(std::uint64_t now_ms) { search_time_ms_ = now_ms; }

void Npc::mark_legacy_ghost_time(std::uint64_t now_ms) { ghost_time_ms_ = now_ms; }

void Npc::mark_legacy_refill_time(std::uint64_t now_ms) { refill_time_ms_ = now_ms; }

void Npc::mark_legacy_verify_time(std::uint64_t now_ms) { verify_time_ms_ = now_ms; }

std::optional<std::int32_t> Npc::merchant_price(std::int32_t item_id) const {
  const auto it = merchant_prices_.find(item_id);
  if (it == merchant_prices_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Npc::set_merchant_price(std::int32_t item_id, std::int32_t price) {
  if (item_id <= 0 || price <= 0) {
    return;
  }
  merchant_prices_[item_id] = price;
}

bool Npc::deals_std_mode(std::int32_t std_mode) const {
  return std::find(deal_std_modes_.begin(), deal_std_modes_.end(), std_mode) !=
         deal_std_modes_.end();
}

void Npc::apply_merchant_state(const MerchantStateRecord& state) {
  if (!state.merchant_key.empty() && state.merchant_key != merchant_key_) {
    return;
  }
  merchant_items_ = state.goods;
  merchant_prices_ = state.prices;
  buy_enabled_ = !merchant_items_.empty() || !merchant_products_.empty();
}

MerchantStateRecord Npc::snapshot_merchant_state() const {
  MerchantStateRecord state;
  state.merchant_key = merchant_key_;
  state.npc_id = std::to_string(id());
  state.map_id = map_id();
  state.goods = merchant_items_;
  state.prices = merchant_prices_;
  return state;
}

void Npc::on_tick(MapContext& context) { set_next_due_tick(context.tick + 100); }

EventObject::EventObject(std::uint64_t id, std::string name, std::string map_id, std::int32_t x,
                         std::int32_t y)
    : GameObject(id, GameObjectKind::event_object, std::move(name), std::move(map_id), x, y) {}

void EventObject::on_tick(MapContext& context) { set_next_due_tick(context.tick + 50); }

}  // namespace mir2
