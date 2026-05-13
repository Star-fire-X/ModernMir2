#include "services/client_v1_game_gateway_service.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <utility>

#include "protocol/canonical_login_error.hpp"
#include "protocol/client_v1_legacy_command_decoder.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "shared/legacy/action_ids.hpp"
#include "shared/legacy/map_document.hpp"
#include "shared/legacy/movement_rules.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

constexpr std::int32_t kClientV1VisibleBagFirstSlot = 6;

std::uint8_t resolve_direction(const int sx, const int sy, const int dx, const int dy,
                               const std::uint8_t fallback) {
  if (sx == dx && sy == dy) {
    return fallback;
  }
  return legacy::next_direction(sx, sy, dx, dy);
}

std::int32_t default_player_feature(const CharacterRecord& character) {
  return make_feature(
      0, character.sex, character.sex,
      static_cast<std::uint8_t>(std::clamp(static_cast<int>(character.hair) * 2 +
                                               static_cast<int>(character.sex),
                                           0, 255)));
}

std::uint8_t normalize_magic_key(const char key) {
  const auto raw = static_cast<unsigned char>(key);
  if (raw >= 1U && raw <= 8U) {
    return static_cast<std::uint8_t>(raw);
  }
  if (raw >= static_cast<unsigned char>('1') && raw <= static_cast<unsigned char>('8')) {
    return static_cast<std::uint8_t>(raw - static_cast<unsigned char>('0'));
  }
  return 0;
}

CanonicalClientV1LoginErrorResponse client_error(CanonicalLoginErrorKind kind) {
  return canonical_login_error_mapping(kind).client_v1;
}

client_v1::SelfAbility self_ability_from_character(const CharacterRecord& character) {
  client_v1::SelfAbility ability;
  ability.level = character.ability.level;
  ability.job = character.job;
  ability.exp = character.ability.exp;
  ability.max_exp = character.ability.max_exp;
  ability.weight = character.ability.weight;
  ability.max_weight = character.ability.max_weight;
  ability.gold = character.gold;
  ability.hunger_state = 0;
  return ability;
}

client_v1::SelfAbilityDetail self_ability_detail_from_character(const CharacterRecord& character) {
  client_v1::SelfAbilityDetail detail;
  detail.level = character.ability.level;
  detail.job = character.job;
  detail.sex = character.sex;
  detail.hair = character.hair;
  detail.hp = character.ability.hp;
  detail.max_hp = character.ability.max_hp;
  detail.mp = character.ability.mp;
  detail.max_mp = character.ability.max_mp;
  detail.ac = character.ability.ac;
  detail.mac = character.ability.mac;
  detail.dc = character.ability.dc;
  detail.mc = character.ability.mc;
  detail.sc = character.ability.sc;
  detail.exp = character.ability.exp;
  detail.max_exp = character.ability.max_exp;
  detail.weight = character.ability.weight;
  detail.max_weight = character.ability.max_weight;
  detail.wear_weight = character.ability.wear_weight;
  detail.max_wear_weight = character.ability.max_wear_weight;
  detail.hand_weight = character.ability.hand_weight;
  detail.max_hand_weight = character.ability.max_hand_weight;
  detail.guild_name = character.guild_name;
  detail.guild_rank_name = character.guild_title;
  detail.name_color = 0xFFFFFFFFU;
  return detail;
}

std::uint16_t action_legacy_ident(client_v1::WorldActionKind kind, std::uint16_t requested_ident) {
  switch (kind) {
    case client_v1::WorldActionKind::turn:
      return kCmTurn;
    case client_v1::WorldActionKind::walk:
      return kCmWalk;
    case client_v1::WorldActionKind::run:
      return kCmRun;
    case client_v1::WorldActionKind::attack:
      return legacy::sm_attack_ident_to_cm(
          legacy::normalize_attack_ident_to_sm(requested_ident));
  }
  return kCmHit;
}

std::uint16_t client_action_legacy_ident(client_v1::WorldActionKind kind,
                                         std::uint16_t requested_ident) {
  if (kind == client_v1::WorldActionKind::attack) {
    return legacy::normalize_attack_ident_to_sm(requested_ident);
  }
  return action_legacy_ident(kind, requested_ident);
}

client_v1::ActorActionKind actor_action_kind_for_sm(std::uint16_t ident) {
  switch (ident) {
    case kSmTurn:
      return client_v1::ActorActionKind::turn;
    case kSmWalk:
      return client_v1::ActorActionKind::walk;
    case kSmRun:
      return client_v1::ActorActionKind::run;
    case kSmRush:
      return client_v1::ActorActionKind::rush;
    case kSmRushKung:
      return client_v1::ActorActionKind::rush_kung;
    case kSmBackStep:
      return client_v1::ActorActionKind::backstep;
    case kSmSpell:
      return client_v1::ActorActionKind::spell;
    case kSmStruck:
      return client_v1::ActorActionKind::struck;
    case legacy::kSmFireHit:
    case kSmHit:
    case legacy::kSmHeavyHit:
    case legacy::kSmBigHit:
    case legacy::kSmPowerHit:
    case legacy::kSmLongHit:
    case legacy::kSmWideHit:
    case legacy::kSmCrossHit:
    default:
      return client_v1::ActorActionKind::hit;
  }
}

template <typename T>
std::size_t legacy_encoded_size_for() {
  const T value{};
  return legacy_encode_buffer(&value, sizeof(value)).size();
}

std::optional<LegacyCharDesc> decode_char_desc_prefix(std::string_view encoded) {
  const auto size = legacy_encoded_size_for<LegacyCharDesc>();
  if (encoded.size() < size) {
    return std::nullopt;
  }
  LegacyCharDesc desc;
  if (!legacy_decode_buffer(encoded.substr(0, size), &desc, sizeof(desc))) {
    return std::nullopt;
  }
  return desc;
}

std::optional<LegacyMessageBodyWL> decode_body_wl_prefix(std::string_view encoded) {
  const auto size = legacy_encoded_size_for<LegacyMessageBodyWL>();
  if (encoded.size() < size) {
    return std::nullopt;
  }
  LegacyMessageBodyWL body;
  if (!legacy_decode_buffer(encoded.substr(0, size), &body, sizeof(body))) {
    return std::nullopt;
  }
  return body;
}

std::optional<LegacyAbility> decode_ability(std::string_view encoded) {
  LegacyAbility ability;
  if (!legacy_decode_buffer(encoded, &ability, sizeof(ability))) {
    return std::nullopt;
  }
  return ability;
}

std::optional<LegacyClientMagic> decode_client_magic(std::string_view encoded) {
  LegacyClientMagic magic;
  if (!legacy_decode_buffer(encoded, &magic, sizeof(magic))) {
    return std::nullopt;
  }
  return magic;
}

client_v1::MagicEntry magic_entry_from_legacy(const LegacyClientMagic& legacy_magic) {
  client_v1::MagicEntry entry;
  entry.magic_id = legacy_magic.def.magic_id;
  entry.key = normalize_magic_key(legacy_magic.key);
  entry.level = legacy_magic.level;
  entry.train = legacy_magic.cur_train;
  entry.delay_ms = legacy_magic.def.delay_time;
  entry.name = to_string(legacy_magic.def.magic_name);
  entry.effect = legacy_magic.def.effect;
  entry.effect_type = legacy_magic.def.effect_type;
  const auto level = std::clamp<int>(entry.level, 0, 3);
  entry.max_train = legacy_magic.def.max_train[static_cast<std::size_t>(level)];
  return entry;
}

std::vector<client_v1::MagicEntry> decode_client_magic_entries(std::string_view encoded) {
  std::vector<client_v1::MagicEntry> magics;
  std::size_t start = 0;
  while (start <= encoded.size()) {
    const auto end = encoded.find('/', start);
    const auto part =
        end == std::string_view::npos ? encoded.substr(start) : encoded.substr(start, end - start);
    if (!part.empty()) {
      if (const auto legacy_magic = decode_client_magic(part); legacy_magic.has_value()) {
        magics.push_back(magic_entry_from_legacy(*legacy_magic));
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return magics;
}

void upsert_magic_entry(std::vector<client_v1::MagicEntry>& magics,
                        client_v1::MagicEntry entry) {
  if (entry.magic_id == 0) {
    return;
  }
  const auto it = std::find_if(magics.begin(), magics.end(), [&](const client_v1::MagicEntry& magic) {
    return magic.magic_id == entry.magic_id;
  });
  if (it != magics.end()) {
    *it = std::move(entry);
    return;
  }
  magics.push_back(std::move(entry));
}

void remove_magic_entry(std::vector<client_v1::MagicEntry>& magics, std::int32_t magic_id) {
  magics.erase(std::remove_if(magics.begin(), magics.end(),
                              [&](const client_v1::MagicEntry& magic) {
                                return magic.magic_id == magic_id;
                              }),
               magics.end());
}

void upsert_character_magic(CharacterRecord& character, const client_v1::MagicEntry& entry) {
  if (entry.magic_id == 0) {
    return;
  }
  auto assign = [&](LegacyUseMagicInfo& magic) {
    magic.magic_id = entry.magic_id;
    magic.key = static_cast<char>(entry.key);
    magic.level = entry.level;
    magic.cur_train = entry.train;
  };
  for (auto& magic : character.magics) {
    if (!is_empty(magic) && magic.magic_id == entry.magic_id) {
      assign(magic);
      return;
    }
  }
  for (auto& magic : character.magics) {
    if (is_empty(magic)) {
      assign(magic);
      return;
    }
  }
}

void remove_character_magic(CharacterRecord& character, std::int32_t magic_id) {
  for (std::size_t index = 0; index < character.magics.size(); ++index) {
    if (is_empty(character.magics[index]) || character.magics[index].magic_id != magic_id) {
      continue;
    }
    for (std::size_t move_index = index; move_index + 1 < character.magics.size(); ++move_index) {
      character.magics[move_index] = character.magics[move_index + 1];
    }
    character.magics.back() = LegacyUseMagicInfo{};
    return;
  }
}

std::optional<LegacyClientItem> decode_client_item(std::string_view encoded) {
  LegacyClientItem item;
  if (!legacy_decode_buffer(encoded, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

std::optional<std::int32_t> parse_i32(std::string_view text) {
  std::int32_t value = 0;
  const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::string name_from_turn_body(std::string_view encoded) {
  const auto desc_size = legacy_encoded_size_for<LegacyCharDesc>();
  if (encoded.size() <= desc_size) {
    return {};
  }
  auto decoded = legacy_decode_string(encoded.substr(desc_size));
  const auto slash = decoded.find('/');
  if (slash != std::string::npos) {
    decoded.resize(slash);
  }
  return decoded;
}

client_v1::ActorType actor_type_for(std::uint64_t actor_id, std::uint64_t self_actor_id) {
  return actor_id == self_actor_id ? client_v1::ActorType::player : client_v1::ActorType::monster;
}

client_v1::ItemState item_state_from_legacy(const LegacyClientItem& item) {
  client_v1::ItemState state;
  state.name = to_string(item.item.name);
  state.make_index = item.make_index;
  state.looks = item.item.looks;
  state.std_mode = item.item.std_mode;
  state.dura = item.dura;
  state.dura_max = item.dura_max;
  return state;
}

bool empty_item_state(const client_v1::ItemState& item) {
  return item.make_index == 0 && item.name.empty();
}

template <std::size_t N>
std::vector<client_v1::ItemSlotState> item_slot_snapshot(
    const std::array<client_v1::ItemState, N>& items) {
  std::vector<client_v1::ItemSlotState> snapshot;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (!empty_item_state(items[index])) {
      snapshot.push_back(client_v1::ItemSlotState{static_cast<std::int32_t>(index), items[index]});
    }
  }
  return snapshot;
}

template <std::size_t N>
std::optional<std::int32_t> find_item_slot(const std::array<client_v1::ItemState, N>& items,
                                           std::int32_t make_index) {
  if (make_index == 0) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (!empty_item_state(items[index]) && items[index].make_index == make_index) {
      return static_cast<std::int32_t>(index);
    }
  }
  return std::nullopt;
}

template <std::size_t N>
std::optional<std::int32_t> first_empty_slot(const std::array<client_v1::ItemState, N>& items,
                                             std::size_t first_slot = 0) {
  for (std::size_t index = std::min(first_slot, items.size()); index < items.size(); ++index) {
    if (empty_item_state(items[index])) {
      return static_cast<std::int32_t>(index);
    }
  }
  return std::nullopt;
}

std::vector<LegacyClientItem> decode_client_item_list(std::string_view encoded) {
  std::vector<LegacyClientItem> items;
  std::size_t start = 0;
  while (start <= encoded.size()) {
    const auto end = encoded.find('/', start);
    const auto part = end == std::string_view::npos ? encoded.substr(start)
                                                    : encoded.substr(start, end - start);
    if (!part.empty()) {
      if (auto item = decode_client_item(part); item.has_value()) {
        items.push_back(*item);
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return items;
}

std::vector<std::pair<std::int32_t, LegacyClientItem>> decode_equipment_item_list(
    std::string_view encoded) {
  std::vector<std::pair<std::int32_t, LegacyClientItem>> items;
  std::size_t start = 0;
  while (start <= encoded.size()) {
    const auto slot_end = encoded.find('/', start);
    if (slot_end == std::string_view::npos) {
      break;
    }
    const auto slot = parse_i32(encoded.substr(start, slot_end - start));
    const auto item_start = slot_end + 1;
    const auto item_end = encoded.find('/', item_start);
    const auto item_part = item_end == std::string_view::npos
                               ? encoded.substr(item_start)
                               : encoded.substr(item_start, item_end - item_start);
    if (slot.has_value() && !item_part.empty()) {
      if (auto item = decode_client_item(item_part); item.has_value()) {
        items.emplace_back(*slot, *item);
      }
    }
    if (item_end == std::string_view::npos) {
      break;
    }
    start = item_end + 1;
  }
  return items;
}

std::string merchant_dialog_text(std::string_view body) {
  return legacy_decode_string(body);
}

std::vector<client_v1::MerchantGoodsItem> merchant_goods_from_legacy_body(
    std::string_view body) {
  const auto decoded = legacy_decode_string(body);
  const auto tokens = util::split(decoded, '/');
  std::vector<client_v1::MerchantGoodsItem> goods;
  for (std::size_t index = 0; index + 3U < tokens.size(); index += 4U) {
    client_v1::MerchantGoodsItem item;
    item.name = tokens[index];
    item.server_index = static_cast<std::int32_t>(goods.size());
    item.price = parse_i32(tokens[index + 2U]).value_or(0);
    if (!item.name.empty()) {
      goods.push_back(std::move(item));
    }
  }
  return goods;
}

client_v1::MiniMapData build_minimap_data(const MapConfig& map) {
  constexpr std::uint16_t kMiniMapWidth = 160;
  constexpr std::uint16_t kMiniMapHeight = 120;

  client_v1::MiniMapData data;
  data.map_id = map.id;
  data.width = kMiniMapWidth;
  data.height = kMiniMapHeight;
  data.pixels.resize(static_cast<std::size_t>(kMiniMapWidth) * kMiniMapHeight);

  const auto document = legacy::decode_map_file(map.source_map);
  if (document == nullptr || document->width <= 0 || document->height <= 0) {
    data.success = false;
    data.width = 0;
    data.height = 0;
    data.pixels.clear();
    data.error_message = "No minimap data for " + map.id + ".";
    return data;
  }

  data.success = true;
  for (int y = 0; y < kMiniMapHeight; ++y) {
    for (int x = 0; x < kMiniMapWidth; ++x) {
      const auto source_x = std::clamp((x * document->width) / kMiniMapWidth, 0,
                                       document->width - 1);
      const auto source_y = std::clamp((y * document->height) / kMiniMapHeight, 0,
                                       document->height - 1);
      data.pixels[static_cast<std::size_t>(y) * kMiniMapWidth + x] =
          document->can_move(source_x, source_y) ? 1U : 0U;
    }
  }
  return data;
}

}  // namespace

ClientV1GameGatewayService::ClientV1GameGatewayService(
    std::shared_ptr<ClientV1AdmissionRegistry> admissions)
    : ClientV1GatewayServiceBase("client_v1_game_gateway"), admissions_(std::move(admissions)) {}

#ifdef MIR2_ENABLE_TEST_HOOKS
void ClientV1GameGatewayService::seed_session_for_test(std::uint64_t session_id) {
  std::scoped_lock lock(mutex_);
  sessions_[session_id] = SessionState{};
}

void ClientV1GameGatewayService::translate_legacy_packet_for_test(
    std::uint64_t session_id, const LegacyPacket& packet,
    std::vector<client_v1::Message>& messages) {
  translate_legacy_packet(session_id, packet, messages);
}

std::optional<CharacterRecord> ClientV1GameGatewayService::session_character_for_test(
    std::uint64_t session_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  return it->second.character;
}
#endif

void ClientV1GameGatewayService::start(HostContext& context) {
  repository_ = std::make_unique<Repository>(context.root_dir / context.config.runtime.data_dir / "mir2.sqlite");
  repository_->ensure_schema(context.root_dir / "schema" / "mir2.sql");
  repository_->seed_runtime();
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  bus_running_.store(true, std::memory_order_relaxed);
  bus_thread_ = std::thread([this] { bus_loop(); });
  ClientV1GatewayServiceBase::start(context);
}

void ClientV1GameGatewayService::stop() {
  bus_running_.store(false, std::memory_order_relaxed);
  ClientV1GatewayServiceBase::stop();
}

void ClientV1GameGatewayService::join() {
  if (bus_thread_.joinable()) {
    bus_thread_.join();
  }
  ClientV1GatewayServiceBase::join();
}

PortBinding ClientV1GameGatewayService::binding(const HostContext& context) const {
  return context.config.ports.client_v1_game_gateway;
}

void ClientV1GameGatewayService::handle_message(std::uint64_t session_id,
                                                const std::string& /*peer_address*/,
                                                const client_v1::Message& message) {
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, client_v1::ClientHello>) {
          handle_client_hello(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::EnterWorldRequest>) {
          handle_enter_world_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::LoginNoticeOk>) {
          handle_login_notice_ok(session_id);
        } else if constexpr (std::is_same_v<T, client_v1::MoveIntent>) {
          handle_move_intent(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::ActionIntent>) {
          handle_action_intent(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::SpellIntent>) {
          handle_spell_intent(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::PickupIntent>) {
          handle_pickup_intent(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::UseItemIntent>) {
          handle_use_item_intent(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::EquipItemRequest>) {
          handle_equip_item_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::UnequipItemRequest>) {
          handle_unequip_item_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::DropItemRequest>) {
          handle_drop_item_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::DropGoldRequest>) {
          handle_drop_gold_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::ReviveRequest>) {
          handle_revive_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MagicKeyChangeRequest>) {
          handle_magic_key_change_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::NpcClickRequest>) {
          handle_npc_click_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::NpcDialogSelectRequest>) {
          handle_npc_dialog_select_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MerchantBuyRequest>) {
          handle_merchant_buy_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MerchantSellRequest>) {
          handle_merchant_sell_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MerchantSellPriceRequest>) {
          handle_merchant_sell_price_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MerchantRepairPriceRequest>) {
          handle_merchant_repair_price_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MerchantRepairRequest>) {
          handle_merchant_repair_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::StorageDepositRequest>) {
          handle_storage_deposit_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::StorageWithdrawRequest>) {
          handle_storage_withdraw_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GroupModeRequest>) {
          handle_group_mode_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GroupCreateRequest>) {
          handle_group_create_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GroupAddMemberRequest>) {
          handle_group_add_member_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GroupRemoveMemberRequest>) {
          handle_group_remove_member_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeTryRequest>) {
          handle_trade_try_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeCancelRequest>) {
          handle_trade_cancel_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeAddItemRequest>) {
          handle_trade_add_item_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeRemoveItemRequest>) {
          handle_trade_remove_item_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeSetGoldRequest>) {
          handle_trade_set_gold_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::TradeAcceptRequest>) {
          handle_trade_accept_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildOpenRequest>) {
          handle_guild_open_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildHomeRequest>) {
          handle_guild_home_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildMemberListRequest>) {
          handle_guild_member_list_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildAddMemberRequest>) {
          handle_guild_add_member_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildRemoveMemberRequest>) {
          handle_guild_remove_member_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildUpdateNoticeRequest>) {
          handle_guild_update_notice_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::GuildUpdateGradeRequest>) {
          handle_guild_update_grade_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::MiniMapRequest>) {
          handle_minimap_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::ChatSend>) {
          handle_chat_send(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::Ping>) {
          handle_ping(session_id, value);
        } else {
          const auto error = client_error(CanonicalLoginErrorKind::unsupported_game_message);
          disconnect(session_id, error.code, std::string(error.text));
        }
      },
      message);
}

void ClientV1GameGatewayService::handle_connected(std::uint64_t session_id,
                                                  const std::string& peer_address) {
  std::scoped_lock lock(mutex_);
  sessions_[session_id] = SessionState{};
  if (context().bus != nullptr) {
    context().bus->post("world_service",
                        SessionEvent{SessionEventKind::connected, name(), session_id, peer_address, {}, {}});
  }
}

void ClientV1GameGatewayService::handle_disconnected(std::uint64_t session_id,
                                                     const std::string& peer_address,
                                                     const std::string& reason) {
  std::optional<SessionState> state;
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> group_states;
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> trade_states;
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> guild_states;
  {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      state = it->second;
      if (it->second.group_id != 0) {
        const auto group_id = it->second.group_id;
        if (auto group_it = groups_.find(group_id); group_it != groups_.end()) {
          group_it->second.members.erase(
              std::remove(group_it->second.members.begin(), group_it->second.members.end(),
                          session_id),
              group_it->second.members.end());
          if (group_it->second.members.size() < 2) {
            const auto remaining = group_it->second.members;
            for (const auto member_session_id : remaining) {
              if (auto member_it = sessions_.find(member_session_id);
                  member_it != sessions_.end()) {
                member_it->second.group_id = 0;
                member_it->second.group_visible = false;
                group_states.emplace_back(member_session_id, group_state_locked(member_session_id));
              }
            }
            groups_.erase(group_it);
          } else {
            group_states = group_broadcast_locked(group_id);
          }
        }
      }
      if (it->second.trade_peer_session_id != 0) {
        const auto peer_id = it->second.trade_peer_session_id;
        if (auto peer_it = sessions_.find(peer_id); peer_it != sessions_.end()) {
          clear_trade_locked(peer_it->second);
          trade_states.emplace_back(peer_id, client_v1::TradeState{});
        }
      }
      const auto guild_name = it->second.character.guild_name;
      sessions_.erase(it);
      if (!guild_name.empty()) {
        if (auto guild_it = guilds_.find(guild_name); guild_it != guilds_.end()) {
          for (auto& member : guild_it->second.members) {
            if (state.has_value() && member.name == state->character_name) {
              member.online = false;
            }
          }
        }
        guild_states = guild_broadcast_locked(guild_name);
      }
    }
  }
  for (const auto& [target_session_id, group_state] : group_states) {
    send_message(target_session_id, group_state);
  }
  for (const auto& [target_session_id, trade_state] : trade_states) {
    send_message(target_session_id, trade_state);
  }
  for (const auto& [target_session_id, guild_state] : guild_states) {
    send_message(target_session_id, guild_state);
  }
  if (state.has_value() && state->entered_world) {
    context().bus->post(
        "world_service",
        SessionEvent{SessionEventKind::disconnected, name(), session_id, peer_address, {}, reason});
  }
}

void ClientV1GameGatewayService::handle_client_hello(std::uint64_t session_id,
                                                     const client_v1::ClientHello& hello) {
  if (hello.protocol_version != client_v1::kProtocolVersion) {
    const auto error = client_error(CanonicalLoginErrorKind::protocol_version_mismatch);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }
  std::scoped_lock lock(mutex_);
  sessions_[session_id].greeted = true;
}

void ClientV1GameGatewayService::handle_enter_world_request(
    std::uint64_t session_id, const client_v1::EnterWorldRequest& request) {
  auto session_state = session(session_id);
  if (!session_state.has_value() || !session_state->greeted) {
    const auto error = client_error(CanonicalLoginErrorKind::missing_client_hello);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }
  if (session_state->entered_world) {
    const auto error = client_error(CanonicalLoginErrorKind::already_entered_world);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  const auto admission = admissions_->consume(request.token);
  if (!admission.has_value()) {
    const auto error = client_error(CanonicalLoginErrorKind::invalid_enter_world_token);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  const auto character =
      repository_->load_character(admission->account_id, admission->character_name);
  if (!character.has_value()) {
    const auto error = client_error(CanonicalLoginErrorKind::character_not_found);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  SessionState updated;
  updated.greeted = true;
  updated.entered_world = true;
  updated.pending_login_notice = !context().config.runtime.login_notice_text.empty();
  updated.stage = advance(CanonicalLoginStage::character_selected,
                          CanonicalLoginTransition::enter_game);
  updated.account_id = admission->account_id;
  updated.character_name = admission->character_name;
  updated.character = *character;
  if (updated.character.feature == 0) {
    updated.character.feature = default_player_feature(updated.character);
  }
  {
    std::scoped_lock lock(mutex_);
    sessions_[session_id] = updated;
  }

  if (updated.pending_login_notice) {
    send_message(session_id, client_v1::LoginNotice{context().config.runtime.login_notice_title,
                                                    context().config.runtime.login_notice_text});
    return;
  }

  {
    std::scoped_lock lock(mutex_);
    sessions_[session_id].stage =
        advance(sessions_[session_id].stage, CanonicalLoginTransition::enter_game_complete);
    updated = sessions_[session_id];
  }
  post_enter_world(session_id, updated);
}

void ClientV1GameGatewayService::handle_login_notice_ok(std::uint64_t session_id) {
  std::optional<SessionState> state;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.entered_world || !it->second.pending_login_notice ||
        !can_accept(it->second.stage, CanonicalLoginRequest::finish_enter_game)) {
      return;
    }
    it->second.pending_login_notice = false;
    it->second.stage =
        advance(it->second.stage, CanonicalLoginTransition::enter_game_complete);
    state = it->second;
  }

  if (state.has_value()) {
    post_enter_world(session_id, *state);
  }
}

void ClientV1GameGatewayService::post_enter_world(std::uint64_t session_id,
                                                  const SessionState& state) {
  LogicCommand command;
  command.kind = LogicCommandKind::enter_world;
  command.gateway = name();
  command.session_id = session_id;
  command.account_id = state.account_id;
  command.character_name = state.character_name;
  command.map_id = state.character.map_id;
  command.x = state.character.x;
  command.y = state.character.y;
  command.character = state.character;
  post_logic_command(std::move(command));
}

void ClientV1GameGatewayService::handle_move_intent(std::uint64_t session_id,
                                                    const client_v1::MoveIntent& intent) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  client_v1::ActionIntent action;
  action.kind = intent.mode == client_v1::MoveMode::run ? client_v1::WorldActionKind::run
                                                        : client_v1::WorldActionKind::walk;
  action.x = intent.x;
  action.y = intent.y;
  action.dir =
      resolve_direction(state->character.x, state->character.y, intent.x, intent.y, state->character.dir);
  handle_action_intent(session_id, action);
}

void ClientV1GameGatewayService::handle_action_intent(
    std::uint64_t session_id, const client_v1::ActionIntent& intent) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  // Rate limit: at most one action (move/attack) in flight per session.
  // This matches legacy client behavior where a new CM_ packet is not sent
  // until the previous action completes.  The pending_action is cleared by
  // the world service when it processes the action.
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      if (it->second.pending_action.has_value()) {
        return;
      }
    }
  }

  auto effective = intent;
  if (effective.kind == client_v1::WorldActionKind::walk ||
      effective.kind == client_v1::WorldActionKind::run) {
    effective.dir = resolve_direction(state->character.x, state->character.y, effective.x,
                                      effective.y, state->character.dir);
  }

  const auto client_ident = client_action_legacy_ident(effective.kind, effective.legacy_ident);
  effective.legacy_ident = client_ident;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.pending_action = effective;
    }
  }
  post_canonical_command(decode_client_v1_action_command(session_id, effective));
}

void ClientV1GameGatewayService::handle_spell_intent(
    std::uint64_t session_id, const client_v1::SpellIntent& intent) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_spell_command(session_id, intent));
}

void ClientV1GameGatewayService::handle_pickup_intent(
    std::uint64_t session_id, const client_v1::PickupIntent& intent) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_pickup_command(session_id, intent));
}

void ClientV1GameGatewayService::handle_use_item_intent(
    std::uint64_t session_id, const client_v1::UseItemIntent& intent) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_use_item_command(session_id, intent));
}

void ClientV1GameGatewayService::handle_equip_item_request(
    std::uint64_t session_id, const client_v1::EquipItemRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_equip_item_command(session_id, request));
}

void ClientV1GameGatewayService::handle_unequip_item_request(
    std::uint64_t session_id, const client_v1::UnequipItemRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_unequip_item_command(session_id, request));
}

void ClientV1GameGatewayService::handle_drop_item_request(
    std::uint64_t session_id, const client_v1::DropItemRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_drop_item_command(session_id, request));
}

void ClientV1GameGatewayService::handle_drop_gold_request(
    std::uint64_t session_id, const client_v1::DropGoldRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() || request.amount <= 0) {
    return;
  }

  post_canonical_command(decode_client_v1_drop_gold_command(session_id, request));
}

void ClientV1GameGatewayService::handle_revive_request(
    std::uint64_t session_id, const client_v1::ReviveRequest& /*request*/) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_revive_command(session_id));
}

void ClientV1GameGatewayService::handle_magic_key_change_request(
    std::uint64_t session_id, const client_v1::MagicKeyChangeRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.magic_id == 0 || request.key > 8) {
    return;
  }

  client_v1::MagicList list;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      return;
    }
    auto target = std::find_if(it->second.magics.begin(), it->second.magics.end(),
                               [&](const client_v1::MagicEntry& magic) {
                                 return magic.magic_id == request.magic_id;
                               });
    if (target == it->second.magics.end()) {
      return;
    }
    if (request.key != 0) {
      for (auto& magic : it->second.magics) {
        if (magic.magic_id != request.magic_id && magic.key == request.key) {
          magic.key = 0;
          upsert_character_magic(it->second.character, magic);
        }
      }
    }
    target->key = request.key;
    upsert_character_magic(it->second.character, *target);
    list.magics = it->second.magics;
  }
  send_message(session_id, std::move(list));
}

void ClientV1GameGatewayService::handle_merchant_buy_request(
    std::uint64_t session_id, const client_v1::MerchantBuyRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
    }
  }

  post_canonical_command(decode_client_v1_merchant_buy_command(session_id, merchant_id,
                                                               request));
}

void ClientV1GameGatewayService::handle_merchant_sell_request(
    std::uint64_t session_id, const client_v1::MerchantSellRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
      it->second.pending_sell_item_make_index = request.item_make_index;
      it->second.pending_sell_item_name = request.name;
    }
  }

  post_canonical_command(decode_client_v1_merchant_sell_command(session_id, merchant_id,
                                                                request));
}

void ClientV1GameGatewayService::handle_merchant_sell_price_request(
    std::uint64_t session_id, const client_v1::MerchantSellPriceRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }

  post_canonical_command(decode_client_v1_merchant_sell_price_command(session_id, merchant_id,
                                                                      request));
}

void ClientV1GameGatewayService::handle_merchant_repair_price_request(
    std::uint64_t session_id, const client_v1::MerchantRepairPriceRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
      it->second.pending_repair_item_make_index = request.item_make_index;
      it->second.pending_repair_item_name = request.name;
    }
  }

  post_canonical_command(decode_client_v1_merchant_repair_price_command(
      session_id, merchant_id, request));
}

void ClientV1GameGatewayService::handle_merchant_repair_request(
    std::uint64_t session_id, const client_v1::MerchantRepairRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
      it->second.pending_repair_item_make_index = request.item_make_index;
      it->second.pending_repair_item_name = request.name;
    }
  }

  post_canonical_command(decode_client_v1_merchant_repair_command(session_id, merchant_id,
                                                                  request));
}

void ClientV1GameGatewayService::handle_storage_deposit_request(
    std::uint64_t session_id, const client_v1::StorageDepositRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
    }
  }

  post_canonical_command(decode_client_v1_storage_deposit_command(session_id, merchant_id,
                                                                  request));
}

void ClientV1GameGatewayService::handle_storage_withdraw_request(
    std::uint64_t session_id, const client_v1::StorageWithdrawRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game() ||
      request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = merchant_id;
    }
  }

  post_canonical_command(decode_client_v1_storage_withdraw_command(session_id, merchant_id,
                                                                   request));
}

std::optional<std::uint64_t> ClientV1GameGatewayService::find_session_by_character_locked(
    const std::string_view name) const {
  for (const auto& [id, state] : sessions_) {
    if (state.in_game() && state.character_name == name) {
      return id;
    }
  }
  return std::nullopt;
}

client_v1::GroupState ClientV1GameGatewayService::group_state_locked(
    const std::uint64_t session_id) const {
  client_v1::GroupState result;
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return result;
  }
  result.visible = it->second.group_visible || it->second.group_id != 0;
  result.allow_group = it->second.allow_group;
  if (it->second.group_id == 0) {
    if (result.visible && !it->second.character_name.empty()) {
      result.members.push_back(it->second.character_name);
    }
    return result;
  }
  const auto group_it = groups_.find(it->second.group_id);
  if (group_it == groups_.end()) {
    return result;
  }
  for (const auto member_session_id : group_it->second.members) {
    const auto member_it = sessions_.find(member_session_id);
    if (member_it != sessions_.end() && member_it->second.in_game()) {
      result.members.push_back(member_it->second.character_name);
    }
  }
  return result;
}

std::vector<std::pair<std::uint64_t, client_v1::GroupState>>
ClientV1GameGatewayService::group_broadcast_locked(const std::uint64_t group_id) const {
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> states;
  const auto group_it = groups_.find(group_id);
  if (group_it == groups_.end()) {
    return states;
  }
  for (const auto member_session_id : group_it->second.members) {
    if (sessions_.contains(member_session_id)) {
      states.emplace_back(member_session_id, group_state_locked(member_session_id));
    }
  }
  return states;
}

client_v1::TradeState ClientV1GameGatewayService::trade_state_locked(
    const std::uint64_t session_id) const {
  client_v1::TradeState result;
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return result;
  }
  const auto& state = it->second;
  result.visible = state.trade_visible;
  result.remote_name = state.trade_remote_name;
  result.local_items = state.trade_local_items;
  result.local_gold = state.trade_local_gold;
  result.local_accept = state.trade_local_accept;
  const auto peer_it = sessions_.find(state.trade_peer_session_id);
  if (peer_it != sessions_.end() && peer_it->second.trade_peer_session_id == session_id) {
    result.remote_items = peer_it->second.trade_local_items;
    result.remote_gold = peer_it->second.trade_local_gold;
    result.remote_accept = peer_it->second.trade_local_accept;
    if (result.remote_name.empty()) {
      result.remote_name = peer_it->second.character_name;
    }
  }
  return result;
}

std::vector<std::pair<std::uint64_t, client_v1::TradeState>>
ClientV1GameGatewayService::trade_pair_states_locked(const std::uint64_t session_id) const {
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return states;
  }
  states.emplace_back(session_id, trade_state_locked(session_id));
  const auto peer_id = it->second.trade_peer_session_id;
  if (peer_id != 0 && sessions_.contains(peer_id)) {
    states.emplace_back(peer_id, trade_state_locked(peer_id));
  }
  return states;
}

void ClientV1GameGatewayService::clear_trade_locked(SessionState& state) {
  state.trade_visible = false;
  state.trade_peer_session_id = 0;
  state.trade_remote_name.clear();
  state.trade_local_items.clear();
  state.trade_local_gold = 0;
  state.trade_local_accept = false;
}

std::optional<client_v1::ItemSlotState> ClientV1GameGatewayService::trade_item_from_bag_locked(
    const SessionState& state, const std::int32_t make_index, const std::string_view name) const {
  if (std::any_of(state.trade_local_items.begin(), state.trade_local_items.end(),
                  [&](const client_v1::ItemSlotState& entry) {
                    return entry.item.make_index == make_index;
                  })) {
    return std::nullopt;
  }
  for (const auto& item : state.bag_items) {
    if (!item.empty() && item.make_index == make_index &&
        (name.empty() || item.name == name)) {
      return client_v1::ItemSlotState{
          static_cast<std::int32_t>(state.trade_local_items.size()), item};
    }
  }
  return std::nullopt;
}

void ClientV1GameGatewayService::ensure_guild_member_locked(SessionState& state) {
  if (state.character.guild_name.empty()) {
    return;
  }
  auto& guild = guilds_[state.character.guild_name];
  if (guild.name.empty()) {
    guild.name = state.character.guild_name;
    guild.notice = "Guild notice";
  }
  const auto rank = state.character.guild_title.empty() ? std::string{"Member"}
                                                        : state.character.guild_title;
  if (std::find(guild.ranks.begin(), guild.ranks.end(), rank) == guild.ranks.end()) {
    guild.ranks.push_back(rank);
  }
  auto member_it = std::find_if(guild.members.begin(), guild.members.end(),
                                [&](const client_v1::GuildMemberState& member) {
                                  return member.name == state.character_name;
                                });
  if (member_it == guild.members.end()) {
    guild.members.push_back(client_v1::GuildMemberState{state.character_name, rank, true});
  } else {
    member_it->rank = rank;
    member_it->online = true;
  }
  for (auto& member : guild.members) {
    member.online = false;
    for (const auto& [session_id, session_state] : sessions_) {
      (void)session_id;
      if (session_state.in_game() &&
          session_state.character.guild_name == guild.name &&
          session_state.character_name == member.name) {
        member.online = true;
        if (!session_state.character.guild_title.empty()) {
          member.rank = session_state.character.guild_title;
        }
      }
    }
  }
}

client_v1::GuildState ClientV1GameGatewayService::guild_state_locked(
    const std::uint64_t session_id) {
  client_v1::GuildState result;
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return result;
  }
  result.visible = it->second.guild_visible;
  if (it->second.character.guild_name.empty()) {
    return result;
  }
  ensure_guild_member_locked(it->second);
  const auto guild_it = guilds_.find(it->second.character.guild_name);
  if (guild_it == guilds_.end()) {
    return result;
  }
  result.visible = true;
  result.guild_name = guild_it->second.name;
  result.rank_name = it->second.character.guild_title.empty() ? "Member"
                                                              : it->second.character.guild_title;
  result.notice = guild_it->second.notice;
  result.members = guild_it->second.members;
  result.ranks = guild_it->second.ranks;
  result.can_admin = !result.guild_name.empty();
  return result;
}

std::vector<std::pair<std::uint64_t, client_v1::GuildState>>
ClientV1GameGatewayService::guild_broadcast_locked(const std::string_view guild_name) {
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> states;
  for (auto& [session_id, session_state] : sessions_) {
    if (session_state.in_game() && session_state.guild_visible &&
        session_state.character.guild_name == guild_name) {
      states.emplace_back(session_id, guild_state_locked(session_id));
    }
  }
  return states;
}

void ClientV1GameGatewayService::handle_group_mode_request(
    std::uint64_t session_id, const client_v1::GroupModeRequest& request) {
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> states;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.allow_group = request.allow;
    it->second.group_visible = true;
    states = it->second.group_id != 0 ? group_broadcast_locked(it->second.group_id)
                                      : std::vector<std::pair<std::uint64_t, client_v1::GroupState>>{
                                            {session_id, group_state_locked(session_id)}};
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  send_message(session_id, client_v1::SysMessage{
                               request.allow ? "Group invitations enabled."
                                             : "Group invitations disabled.",
                               0});
}

void ClientV1GameGatewayService::handle_group_create_request(
    std::uint64_t session_id, const client_v1::GroupCreateRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    const auto target_id = find_session_by_character_locked(request.target_name);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (!target_id.has_value() || *target_id == session_id ||
        !sessions_[*target_id].allow_group || it->second.group_id != 0 ||
        sessions_[*target_id].group_id != 0) {
      failure = client_v1::SysMessage{"Group create failed.", 1};
    } else {
      const auto group_id = next_group_id_++;
      groups_[group_id].members = {session_id, *target_id};
      it->second.group_id = group_id;
      it->second.group_visible = true;
      sessions_[*target_id].group_id = group_id;
      sessions_[*target_id].group_visible = true;
      states = group_broadcast_locked(group_id);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_group_add_member_request(
    std::uint64_t session_id, const client_v1::GroupAddMemberRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    const auto target_id = find_session_by_character_locked(request.target_name);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (it->second.group_id == 0 || !target_id.has_value() || *target_id == session_id ||
        !sessions_[*target_id].allow_group || sessions_[*target_id].group_id != 0) {
      failure = client_v1::SysMessage{"Group add failed.", 1};
    } else {
      auto& group = groups_[it->second.group_id];
      group.members.push_back(*target_id);
      sessions_[*target_id].group_id = it->second.group_id;
      sessions_[*target_id].group_visible = true;
      states = group_broadcast_locked(it->second.group_id);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_group_remove_member_request(
    std::uint64_t session_id, const client_v1::GroupRemoveMemberRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GroupState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    const auto target_id = find_session_by_character_locked(request.target_name);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (it->second.group_id == 0 || !target_id.has_value() ||
        sessions_[*target_id].group_id != it->second.group_id) {
      failure = client_v1::SysMessage{"Group remove failed.", 1};
    } else {
      const auto group_id = it->second.group_id;
      auto group_it = groups_.find(group_id);
      if (group_it == groups_.end()) {
        failure = client_v1::SysMessage{"Group remove failed.", 1};
      } else {
        auto members = group_it->second.members;
        group_it->second.members.erase(
            std::remove(group_it->second.members.begin(), group_it->second.members.end(),
                        *target_id),
            group_it->second.members.end());
        sessions_[*target_id].group_id = 0;
        sessions_[*target_id].group_visible = false;
        if (group_it->second.members.size() < 2) {
          for (const auto member_session_id : members) {
            if (auto member_it = sessions_.find(member_session_id);
                member_it != sessions_.end()) {
              member_it->second.group_id = 0;
              member_it->second.group_visible = false;
            }
          }
          groups_.erase(group_it);
          for (const auto member_session_id : members) {
            if (sessions_.contains(member_session_id)) {
              states.emplace_back(member_session_id, group_state_locked(member_session_id));
            }
          }
        } else {
          states = group_broadcast_locked(group_id);
          states.emplace_back(*target_id, group_state_locked(*target_id));
        }
      }
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_trade_try_request(
    std::uint64_t session_id, const client_v1::TradeTryRequest& request) {
  if (request.target_name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    const auto target_id = find_session_by_character_locked(request.target_name);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (!target_id.has_value() || *target_id == session_id || it->second.trade_visible ||
        sessions_[*target_id].trade_visible) {
      failure = client_v1::SysMessage{"Trade request failed.", 1};
    } else {
      it->second.trade_visible = true;
      it->second.trade_peer_session_id = *target_id;
      it->second.trade_remote_name = sessions_[*target_id].character_name;
      it->second.trade_local_items.clear();
      it->second.trade_local_gold = 0;
      it->second.trade_local_accept = false;
      auto& peer = sessions_[*target_id];
      peer.trade_visible = true;
      peer.trade_peer_session_id = session_id;
      peer.trade_remote_name = it->second.character_name;
      peer.trade_local_items.clear();
      peer.trade_local_gold = 0;
      peer.trade_local_accept = false;
      states = trade_pair_states_locked(session_id);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
  post_canonical_command(decode_client_v1_trade_try_command(session_id, request));
}

void ClientV1GameGatewayService::handle_trade_cancel_request(
    std::uint64_t session_id, const client_v1::TradeCancelRequest& /*request*/) {
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    const auto peer_id = it->second.trade_peer_session_id;
    clear_trade_locked(it->second);
    states.emplace_back(session_id, client_v1::TradeState{});
    if (auto peer_it = sessions_.find(peer_id); peer_it != sessions_.end()) {
      clear_trade_locked(peer_it->second);
      states.emplace_back(peer_id, client_v1::TradeState{});
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  post_canonical_command(decode_client_v1_trade_cancel_command(session_id));
}

void ClientV1GameGatewayService::handle_trade_add_item_request(
    std::uint64_t session_id, const client_v1::TradeAddItemRequest& request) {
  if (request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  std::optional<client_v1::SysMessage> failure;
  bool should_post = false;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    should_post = true;
    if (!it->second.trade_visible) {
      states.clear();
    } else {
      auto item = trade_item_from_bag_locked(it->second, request.item_make_index, request.name);
      if (!item.has_value()) {
        failure = client_v1::SysMessage{"Trade item failed.", 1};
      } else {
        it->second.trade_local_items.push_back(*item);
        it->second.trade_local_accept = false;
        if (auto peer_it = sessions_.find(it->second.trade_peer_session_id);
            peer_it != sessions_.end()) {
          peer_it->second.trade_local_accept = false;
        }
        states = trade_pair_states_locked(session_id);
      }
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
  if (should_post) {
    post_canonical_command(decode_client_v1_trade_add_item_command(session_id, request));
  }
}

void ClientV1GameGatewayService::handle_trade_remove_item_request(
    std::uint64_t session_id, const client_v1::TradeRemoveItemRequest& request) {
  if (request.item_make_index == 0 || request.name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  std::optional<client_v1::SysMessage> failure;
  bool should_post = false;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    should_post = true;
    if (!it->second.trade_visible) {
      states.clear();
    } else {
      auto& items = it->second.trade_local_items;
      const auto item_it = std::find_if(items.begin(), items.end(),
                                        [&](const client_v1::ItemSlotState& entry) {
                                          return entry.item.make_index == request.item_make_index &&
                                                 entry.item.name == request.name;
                                        });
      if (item_it == items.end()) {
        failure = client_v1::SysMessage{"Trade remove failed.", 1};
      } else {
        items.erase(item_it);
        for (std::size_t index = 0; index < items.size(); ++index) {
          items[index].slot = static_cast<std::int32_t>(index);
        }
        it->second.trade_local_accept = false;
        if (auto peer_it = sessions_.find(it->second.trade_peer_session_id);
            peer_it != sessions_.end()) {
          peer_it->second.trade_local_accept = false;
        }
        states = trade_pair_states_locked(session_id);
      }
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
  if (should_post) {
    post_canonical_command(decode_client_v1_trade_remove_item_command(session_id, request));
  }
}

void ClientV1GameGatewayService::handle_trade_set_gold_request(
    std::uint64_t session_id, const client_v1::TradeSetGoldRequest& request) {
  if (request.gold < 0) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  std::int32_t gold = request.gold;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (it->second.trade_visible) {
      gold = std::min<std::int32_t>(request.gold, it->second.character.gold);
      it->second.trade_local_gold = gold;
      it->second.trade_local_accept = false;
      if (auto peer_it = sessions_.find(it->second.trade_peer_session_id);
          peer_it != sessions_.end()) {
        peer_it->second.trade_local_accept = false;
      }
      states = trade_pair_states_locked(session_id);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  post_canonical_command(decode_client_v1_trade_set_gold_command(session_id, gold));
}

void ClientV1GameGatewayService::handle_trade_accept_request(
    std::uint64_t session_id, const client_v1::TradeAcceptRequest& /*request*/) {
  std::vector<std::pair<std::uint64_t, client_v1::TradeState>> states;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    if (it->second.trade_visible) {
      it->second.trade_local_accept = true;
      states = trade_pair_states_locked(session_id);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  post_canonical_command(decode_client_v1_trade_accept_command(session_id));
}

void ClientV1GameGatewayService::handle_guild_open_request(
    std::uint64_t session_id, const client_v1::GuildOpenRequest& /*request*/) {
  client_v1::GuildState guild;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.guild_visible = true;
    guild = guild_state_locked(session_id);
  }
  send_message(session_id, std::move(guild));
}

void ClientV1GameGatewayService::handle_guild_home_request(
    std::uint64_t session_id, const client_v1::GuildHomeRequest& request) {
  handle_guild_open_request(session_id, client_v1::GuildOpenRequest{});
  (void)request;
}

void ClientV1GameGatewayService::handle_guild_member_list_request(
    std::uint64_t session_id, const client_v1::GuildMemberListRequest& request) {
  handle_guild_open_request(session_id, client_v1::GuildOpenRequest{});
  (void)request;
}

void ClientV1GameGatewayService::handle_guild_add_member_request(
    std::uint64_t session_id, const client_v1::GuildAddMemberRequest& request) {
  if (request.name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.guild_visible = true;
    auto current = guild_state_locked(session_id);
    if (!current.can_admin) {
      failure = client_v1::SysMessage{"Guild add failed.", 1};
    } else {
      auto& guild = guilds_[current.guild_name];
      const auto target_id = find_session_by_character_locked(request.name);
      const auto rank = std::string{"Member"};
      auto member_it = std::find_if(guild.members.begin(), guild.members.end(),
                                    [&](const client_v1::GuildMemberState& member) {
                                      return member.name == request.name;
                                    });
      if (member_it == guild.members.end()) {
        guild.members.push_back(client_v1::GuildMemberState{
            request.name, rank, target_id.has_value()});
      } else {
        member_it->rank = rank;
        member_it->online = target_id.has_value();
      }
      if (target_id.has_value()) {
        auto& target = sessions_[*target_id];
        target.character.guild_name = current.guild_name;
        target.character.guild_title = rank;
        if (target.guild_visible) {
          states.emplace_back(*target_id, guild_state_locked(*target_id));
        }
      }
      states = guild_broadcast_locked(current.guild_name);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_guild_remove_member_request(
    std::uint64_t session_id, const client_v1::GuildRemoveMemberRequest& request) {
  if (request.name.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.guild_visible = true;
    auto current = guild_state_locked(session_id);
    if (!current.can_admin) {
      failure = client_v1::SysMessage{"Guild remove failed.", 1};
    } else {
      auto& guild = guilds_[current.guild_name];
      guild.members.erase(std::remove_if(guild.members.begin(), guild.members.end(),
                                         [&](const client_v1::GuildMemberState& member) {
                                           return member.name == request.name;
                                         }),
                          guild.members.end());
      const auto target_id = find_session_by_character_locked(request.name);
      if (target_id.has_value()) {
        auto& target = sessions_[*target_id];
        target.character.guild_name.clear();
        target.character.guild_title.clear();
        if (target.guild_visible) {
          target.guild_visible = false;
          states.emplace_back(*target_id, client_v1::GuildState{});
        }
      }
      auto broadcast = guild_broadcast_locked(current.guild_name);
      states.insert(states.end(), broadcast.begin(), broadcast.end());
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_guild_update_notice_request(
    std::uint64_t session_id, const client_v1::GuildUpdateNoticeRequest& request) {
  if (request.text.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.guild_visible = true;
    auto current = guild_state_locked(session_id);
    if (!current.can_admin) {
      failure = client_v1::SysMessage{"Guild notice update failed.", 1};
    } else {
      guilds_[current.guild_name].notice = request.text;
      states = guild_broadcast_locked(current.guild_name);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_guild_update_grade_request(
    std::uint64_t session_id, const client_v1::GuildUpdateGradeRequest& request) {
  if (request.text.empty()) {
    return;
  }
  std::vector<std::pair<std::uint64_t, client_v1::GuildState>> states;
  std::optional<client_v1::SysMessage> failure;
  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.in_game()) {
      return;
    }
    it->second.guild_visible = true;
    auto current = guild_state_locked(session_id);
    if (!current.can_admin) {
      failure = client_v1::SysMessage{"Guild rank update failed.", 1};
    } else {
      auto& ranks = guilds_[current.guild_name].ranks;
      ranks.clear();
      std::string rank;
      auto flush_rank = [&] {
        if (!rank.empty()) {
          ranks.push_back(rank);
          rank.clear();
        }
      };
      for (const auto ch : request.text) {
        if (ch == '/' || ch == '\n' || ch == '\r') {
          flush_rank();
        } else {
          rank.push_back(ch);
        }
      }
      flush_rank();
      if (ranks.empty()) {
        ranks.push_back("Member");
      }
      states = guild_broadcast_locked(current.guild_name);
    }
  }
  for (const auto& [target_session_id, state] : states) {
    send_message(target_session_id, state);
  }
  if (failure.has_value()) {
    send_message(session_id, *failure);
  }
}

void ClientV1GameGatewayService::handle_minimap_request(
    std::uint64_t session_id, const client_v1::MiniMapRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }
  const auto map_id = request.map_id.empty() ? state->character.map_id : request.map_id;
  const auto map = find_map(map_id);
  if (!map.has_value() || map->source_map.empty()) {
    client_v1::MiniMapData failure;
    failure.success = false;
    failure.map_id = map_id;
    failure.error_message = "No minimap data for " + map_id + ".";
    send_message(session_id, std::move(failure));
    return;
  }
  send_message(session_id, build_minimap_data(*map));
}

void ClientV1GameGatewayService::handle_npc_click_request(
    std::uint64_t session_id, const client_v1::NpcClickRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  {
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
      it->second.current_merchant_id = request.actor_id;
    }
  }

  post_canonical_command(decode_client_v1_npc_click_command(session_id, request));
}

void ClientV1GameGatewayService::handle_npc_dialog_select_request(
    std::uint64_t session_id, const client_v1::NpcDialogSelectRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  const auto merchant_id =
      request.merchant_id != 0 ? request.merchant_id : state->current_merchant_id;
  if (merchant_id == 0) {
    return;
  }

  post_canonical_command(decode_client_v1_npc_dialog_select_command(
      session_id, merchant_id, request.selection));
}

void ClientV1GameGatewayService::handle_chat_send(std::uint64_t session_id,
                                                  const client_v1::ChatSend& chat) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->in_game()) {
    return;
  }

  post_canonical_command(decode_client_v1_chat_command(session_id, chat));
}

void ClientV1GameGatewayService::handle_ping(std::uint64_t session_id,
                                             const client_v1::Ping& ping) {
  const auto now =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  send_message(session_id, client_v1::Pong{ping.client_time_ms, static_cast<std::uint64_t>(now)});
}

void ClientV1GameGatewayService::post_canonical_command(CanonicalLegacyCommand command) {
  auto logic = to_logic_command(command);
  logic.gateway = name();
  post_logic_command(std::move(logic));
}

void ClientV1GameGatewayService::post_logic_command(LogicCommand command) {
  if (command.gateway.empty()) {
    command.gateway = name();
  }
  if (command.session_id != 0 && command.session_seq == 0) {
    std::scoped_lock lock(mutex_);
    if (auto it = sessions_.find(command.session_id); it != sessions_.end()) {
      command.session_seq = ++it->second.next_session_seq;
    }
  }
  context().bus->post("world_service", std::move(command));
}

void ClientV1GameGatewayService::bus_loop() {
  while (bus_running_.load(std::memory_order_relaxed)) {
    if (endpoint_ == nullptr) {
      break;
    }
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }

    if (auto session_event = std::get_if<SessionEvent>(&*message)) {
      handle_session_event(*session_event);
    }
  }
}

void ClientV1GameGatewayService::handle_session_event(const SessionEvent& event) {
  if (!event.gateway.empty() && event.gateway != name()) {
    return;
  }

  if (event.kind == SessionEventKind::send_packet ||
      event.kind == SessionEventKind::send_packet_and_close) {
    std::vector<client_v1::Message> messages;
    translate_legacy_packet(event.session_id, event.packet, messages);
    for (const auto& message : messages) {
      send_message(event.session_id, message);
    }
    if (event.kind == SessionEventKind::send_packet_and_close) {
      disconnect(event.session_id, 409, event.reason.empty() ? "server_closed" : event.reason);
    }
    return;
  }

  if (event.kind == SessionEventKind::force_disconnect) {
    disconnect(event.session_id, 400, event.reason.empty() ? "forced_disconnect" : event.reason);
  }
}

void ClientV1GameGatewayService::translate_legacy_packet(
    std::uint64_t session_id, const LegacyPacket& packet,
    std::vector<client_v1::Message>& messages) {
  if (packet.header.length < 0) {
    const std::string body(packet.body.begin(), packet.body.end());
    if (body.rfind("+GOOD/", 0) == 0 || body.rfind("+FAIL/", 0) == 0) {
      const auto ok = body.rfind("+GOOD/", 0) == 0;
      const auto slash = body.find('/');
      const auto time_text = slash == std::string::npos ? std::string_view{} : std::string_view(body).substr(slash + 1);
      const auto time = parse_i32(time_text).value_or(0);
      messages.push_back(client_v1::ActionAck{ok, static_cast<std::uint32_t>(std::max(time, 0))});
      std::optional<client_v1::ActionIntent> pending;
      SessionState current;
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          pending = it->second.pending_action;
          it->second.pending_action.reset();
          current = it->second;
        }
      }
      if (ok && pending.has_value() && current.actor_id != 0) {
        const auto kind = pending->kind == client_v1::WorldActionKind::run
                              ? client_v1::ActorActionKind::run
                              : (pending->kind == client_v1::WorldActionKind::walk
                                     ? client_v1::ActorActionKind::walk
                                     : (pending->kind == client_v1::WorldActionKind::attack
                                            ? client_v1::ActorActionKind::hit
                                            : client_v1::ActorActionKind::turn));
        if (pending->kind == client_v1::WorldActionKind::walk ||
            pending->kind == client_v1::WorldActionKind::run ||
            pending->kind == client_v1::WorldActionKind::turn) {
          const auto ack_x = pending->kind == client_v1::WorldActionKind::turn
                                 ? current.character.x
                                 : pending->x;
          const auto ack_y = pending->kind == client_v1::WorldActionKind::turn
                                 ? current.character.y
                                 : pending->y;
          messages.push_back(client_v1::ActorStateDelta{current.actor_id, ack_x, ack_y, pending->dir});
          std::scoped_lock lock(mutex_);
          auto it = sessions_.find(session_id);
          if (it != sessions_.end()) {
            it->second.character.x = ack_x;
            it->second.character.y = ack_y;
            it->second.character.dir = pending->dir;
          }
          pending->x = ack_x;
          pending->y = ack_y;
        }
        messages.push_back(client_v1::ActorAction{current.actor_id, kind, pending->x, pending->y,
                                                  pending->dir, pending->target_actor_id, 0,
                                                  pending->legacy_ident, 0, false});
      }
    }
    return;
  }

  const auto decoded = decode_legacy_game_packet(packet);
  if (!decoded.has_value()) {
    return;
  }

  auto state = session(session_id).value_or(SessionState{});
  const auto actor_id = static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
  auto make_actor = [&](std::uint64_t id, std::string name, std::int32_t x, std::int32_t y,
                        std::uint8_t dir, std::int32_t feature, std::int32_t status) {
    if (name.empty() && id == state.actor_id) {
      name = state.character_name;
    }
    return client_v1::WorldActor{id, std::move(name), x, y, dir, feature, status,
                                 actor_type_for(id, state.actor_id)};
  };
  bool request_bag_items = false;
  bool request_storage_items = false;

  switch (decoded->message.ident) {
    case kSmClearObjects:
      messages.push_back(client_v1::WorldClearObjects{});
      break;
    case kSmChangeMap:
      messages.push_back(client_v1::MapChange{legacy_decode_string(decoded->body)});
      break;
    case kSmOpenDoorOk:
      messages.push_back(client_v1::MapDoorState{decoded->message.param,
                                                 decoded->message.tag, true});
      break;
    case kSmCloseDoor:
      messages.push_back(client_v1::MapDoorState{decoded->message.param,
                                                 decoded->message.tag, false});
      break;
    case kSmNewMap: {
      const auto map_id = legacy_decode_string(decoded->body);
      std::scoped_lock lock(mutex_);
      auto& current = sessions_[session_id];
      current.actor_id = actor_id;
      current.character.map_id = map_id;
      current.character.x = decoded->message.param;
      current.character.y = decoded->message.tag;
      state = current;
      break;
    }
    case kSmLogon: {
      auto desc = decode_body_wl_prefix(decoded->body);
      client_v1::WorldSnapshot snapshot;
      client_v1::EnterWorldResult enter_result;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.actor_id = actor_id;
        current.character.x = decoded->message.param;
        current.character.y = decoded->message.tag;
        current.character.dir = static_cast<std::uint8_t>(decoded->message.series & 0xFFU);
        if (desc.has_value()) {
          current.character.feature = desc->lparam1;
          current.character.status = desc->lparam2;
        }
        state = current;
        if (!current.world_result_sent) {
          enter_result.success = true;
          enter_result.self_actor_id = current.actor_id;
          enter_result.character_name = current.character_name;
          enter_result.map_id = current.character.map_id;
          enter_result.x = current.character.x;
          enter_result.y = current.character.y;
          current.world_result_sent = true;
        }
        snapshot.map_id = current.character.map_id;
        if (const auto map = find_map(snapshot.map_id); map.has_value()) {
          snapshot.width = map->width;
          snapshot.height = map->height;
        }
        snapshot.self_actor_id = current.actor_id;
        snapshot.actors.push_back(make_actor(current.actor_id, current.character_name,
                                             current.character.x, current.character.y,
                                             current.character.dir, current.character.feature,
                                             current.character.status));
      }
      if (enter_result.success) {
        messages.push_back(enter_result);
        request_bag_items = true;
      }
      messages.push_back(snapshot);
      messages.push_back(self_ability_from_character(state.character));
      messages.push_back(self_ability_detail_from_character(state.character));
      break;
    }
    case kSmTurn:
    case kSmWalk:
    case kSmRun:
    case kSmRush:
    case kSmBackStep: {
      auto desc = decode_char_desc_prefix(decoded->body);
      const auto name = name_from_turn_body(decoded->body);
      const auto dir = static_cast<std::uint8_t>(decoded->message.series & 0xFFU);
      const auto feature = desc.has_value() ? desc->feature : 0;
      const auto status = desc.has_value() ? desc->status : 0;
      messages.push_back(client_v1::ActorUpsert{
          make_actor(actor_id, name, decoded->message.param, decoded->message.tag, dir, feature, status)});
      if (decoded->message.ident == kSmWalk || decoded->message.ident == kSmRun ||
          decoded->message.ident == kSmRush || decoded->message.ident == kSmBackStep) {
        messages.push_back(client_v1::ActorStateDelta{
            actor_id, decoded->message.param, decoded->message.tag, dir});
      }
      messages.push_back(client_v1::ActorAction{
          actor_id, actor_action_kind_for_sm(decoded->message.ident), decoded->message.param,
          decoded->message.tag, dir, 0, 0, decoded->message.ident, 0, false});
      if (actor_id == state.actor_id) {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.x = decoded->message.param;
        current.character.y = decoded->message.tag;
        current.character.dir = dir;
      }
      break;
    }
    case kSmHit:
    case legacy::kSmFireHit:
    case legacy::kSmHeavyHit:
    case legacy::kSmBigHit:
    case legacy::kSmPowerHit:
    case legacy::kSmLongHit:
    case legacy::kSmWideHit:
    case legacy::kSmCrossHit:
    case kSmRushKung:
      messages.push_back(client_v1::ActorAction{
          actor_id,
          decoded->message.ident == kSmRushKung ? client_v1::ActorActionKind::rush_kung
                                                : client_v1::ActorActionKind::hit,
          decoded->message.param, decoded->message.tag,
          static_cast<std::uint8_t>(decoded->message.series), 0, 0,
          decoded->message.ident == kSmRushKung ? decoded->message.ident
                                                : legacy::normalize_attack_ident_to_sm(decoded->message.ident),
          0, false});
      break;
    case kSmSpell: {
      const auto magic_id = parse_i32(decoded->body).value_or(decoded->message.series);
      messages.push_back(client_v1::ActorAction{
          actor_id, client_v1::ActorActionKind::spell, decoded->message.param, decoded->message.tag,
          0, 0, 0, decoded->message.ident, static_cast<std::uint16_t>(magic_id), true,
          decoded->message.series});
      break;
    }
    case kSmMagicFire: {
      std::int32_t target = 0;
      static_cast<void>(legacy_decode_buffer(decoded->body, &target, sizeof(target)));
      messages.push_back(client_v1::ActorMagicFire{
          actor_id,
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(target)),
          decoded->message.param,
          decoded->message.tag,
          static_cast<std::uint8_t>(decoded->message.series & 0xFFU),
          static_cast<std::uint8_t>((decoded->message.series >> 8U) & 0xFFU),
          decoded->message.ident});
      break;
    }
    case kSmMagicFireFail: {
      messages.push_back(client_v1::ActorMagicFireFail{actor_id, decoded->message.ident});
      break;
    }
    case kSmStruck: {
      const auto body = decode_body_wl_prefix(decoded->body);
      const auto source = body.has_value() ? static_cast<std::uint64_t>(static_cast<std::uint32_t>(body->ltag1)) : 0;
      const auto magic = body.has_value() && body->ltag2 != 0;
      messages.push_back(client_v1::ActorVitals{
          actor_id, decoded->message.param, decoded->message.tag, -1, -1,
          decoded->message.series, source, magic, decoded->message.ident});
      messages.push_back(client_v1::ActorAction{
          actor_id, client_v1::ActorActionKind::struck, 0, 0, 0, source,
          decoded->message.series, decoded->message.ident, 0, magic});
      break;
    }
    case kSmDeath:
    case kSmNowDeath:
      messages.push_back(client_v1::ActorDeath{
          actor_id, decoded->message.param, decoded->message.tag,
          static_cast<std::uint8_t>(decoded->message.series), decoded->message.ident});
      if (decoded->message.ident == kSmNowDeath) {
        messages.push_back(client_v1::SysMessage{"You died.", 1});
      }
      break;
    case kSmDisappear:
    case kSmSpaceMoveHide:
    case kSmSpaceMoveHide2:
      messages.push_back(client_v1::ActorRemove{actor_id, decoded->message.ident});
      break;
    case kSmAlive: {
      client_v1::ActorUpsert upsert;
      client_v1::ActorVitals vitals;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.x = decoded->message.param;
        current.character.y = decoded->message.tag;
        current.character.dir = static_cast<std::uint8_t>(decoded->message.series);
        state = current;
        upsert.actor = make_actor(actor_id, current.character_name,
                                  current.character.x, current.character.y,
                                  current.character.dir, current.character.feature,
                                  current.character.status);
        vitals = client_v1::ActorVitals{
            actor_id,
            static_cast<std::int32_t>(current.character.ability.hp),
            static_cast<std::int32_t>(current.character.ability.max_hp),
            static_cast<std::int32_t>(current.character.ability.mp),
            static_cast<std::int32_t>(current.character.ability.max_mp),
            0, 0, false};
      }
      messages.push_back(upsert);
      messages.push_back(vitals);
      messages.push_back(client_v1::ActorStateDelta{
          actor_id, decoded->message.param, decoded->message.tag,
          static_cast<std::uint8_t>(decoded->message.series)});
      messages.push_back(client_v1::SysMessage{"Revived.", 0});
      break;
    }
    case kSmAbility:
      if (const auto ability = decode_ability(decoded->body); ability.has_value()) {
        client_v1::SelfAbility self_ability;
        client_v1::SelfAbilityDetail self_ability_detail;
        client_v1::ActorVitals vitals;
        {
          std::scoped_lock lock(mutex_);
          auto& current = sessions_[session_id];
          current.character.gold = decoded->message.recog;
          current.character.job = static_cast<std::uint8_t>(
              std::clamp<std::int32_t>(decoded->message.param, 0, 255));
          current.character.ability = *ability;
          self_ability = self_ability_from_character(current.character);
          self_ability_detail = self_ability_detail_from_character(current.character);
          vitals = client_v1::ActorVitals{
              current.actor_id,
              static_cast<std::int32_t>(current.character.ability.hp),
              static_cast<std::int32_t>(current.character.ability.max_hp),
              static_cast<std::int32_t>(current.character.ability.mp),
              static_cast<std::int32_t>(current.character.ability.max_mp),
              0, 0, false};
        }
        messages.push_back(vitals);
        messages.push_back(self_ability);
        messages.push_back(self_ability_detail);
      }
      break;
    case kSmHealthSpellChanged:
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end() && actor_id == it->second.actor_id) {
          it->second.character.ability.hp =
              static_cast<std::uint16_t>(std::clamp<std::int32_t>(decoded->message.param, 0, 65535));
          it->second.character.ability.mp =
              static_cast<std::uint16_t>(std::clamp<std::int32_t>(decoded->message.tag, 0, 65535));
          it->second.character.ability.max_hp =
              static_cast<std::uint16_t>(std::clamp<std::int32_t>(decoded->message.series, 0, 65535));
        }
      }
      messages.push_back(client_v1::ActorVitals{
          actor_id, decoded->message.param, decoded->message.series, decoded->message.tag,
          -1, 0, 0, false});
      break;
    case kSmItemShow:
      messages.push_back(client_v1::GroundItemAdd{client_v1::GroundItemState{
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog)),
          decoded->message.param, decoded->message.tag, decoded->message.series,
          legacy_decode_string(decoded->body)}});
      break;
    case kSmItemHide:
      messages.push_back(client_v1::GroundItemRemove{
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog)),
          decoded->message.param, decoded->message.tag});
      break;
    case kSmEatOk:
      messages.push_back(client_v1::UseItemResult{true});
      break;
    case kSmEatFail:
      messages.push_back(client_v1::UseItemResult{false});
      break;
    case kSmBagItems: {
      const auto legacy_items = decode_client_item_list(decoded->body);
      client_v1::BagSnapshot snapshot;
      {
        std::scoped_lock lock(mutex_);
        auto& bag = sessions_[session_id].bag_items;
        bag.fill(client_v1::ItemState{});
        auto slot = static_cast<std::size_t>(kClientV1VisibleBagFirstSlot);
        for (const auto& item : legacy_items) {
          if (slot >= bag.size()) {
            break;
          }
          bag[slot++] = item_state_from_legacy(item);
        }
        snapshot.items = item_slot_snapshot(bag);
      }
      messages.push_back(std::move(snapshot));
      break;
    }
    case kSmSendUseItems: {
      const auto legacy_items = decode_equipment_item_list(decoded->body);
      client_v1::EquipmentSnapshot snapshot;
      {
        std::scoped_lock lock(mutex_);
        auto& equipment = sessions_[session_id].equipment_items;
        equipment.fill(client_v1::ItemState{});
        for (const auto& [slot, item] : legacy_items) {
          if (slot >= 0 && slot < static_cast<std::int32_t>(equipment.size())) {
            equipment[static_cast<std::size_t>(slot)] = item_state_from_legacy(item);
          }
        }
        snapshot.items = item_slot_snapshot(equipment);
      }
      messages.push_back(std::move(snapshot));
      messages.push_back(self_ability_detail_from_character(state.character));
      break;
    }
    case kSmAddItem:
      if (const auto item = decode_client_item(decoded->body); item.has_value()) {
        const auto state = item_state_from_legacy(*item);
        std::optional<std::int32_t> slot;
        {
          std::scoped_lock lock(mutex_);
          auto& bag = sessions_[session_id].bag_items;
          slot = find_item_slot(bag, state.make_index);
          if (!slot.has_value()) {
            slot = first_empty_slot(bag, static_cast<std::size_t>(kClientV1VisibleBagFirstSlot));
          }
          if (slot.has_value()) {
            bag[static_cast<std::size_t>(*slot)] = state;
          }
        }
        if (slot.has_value()) {
          messages.push_back(client_v1::InventoryAdd{
              client_v1::ItemSlotState{*slot, state}});
        }
        messages.push_back(client_v1::SysMessage{"Picked up " + to_string(item->item.name), 0});
      }
      break;
    case kSmDelItem:
      if (const auto item = decode_client_item(decoded->body); item.has_value()) {
        std::optional<std::int32_t> slot;
        {
          std::scoped_lock lock(mutex_);
          auto& bag = sessions_[session_id].bag_items;
          slot = find_item_slot(bag, item->make_index);
          if (slot.has_value()) {
            bag[static_cast<std::size_t>(*slot)] = client_v1::ItemState{};
          }
        }
        if (slot.has_value()) {
          messages.push_back(client_v1::InventoryRemove{*slot});
        }
      }
      break;
    case kSmUpdateItem:
      if (const auto item = decode_client_item(decoded->body); item.has_value()) {
        const auto updated = item_state_from_legacy(*item);
        std::optional<std::int32_t> bag_slot;
        std::optional<std::int32_t> equipment_slot;
        client_v1::EquipmentSnapshot equipment_snapshot;
        {
          std::scoped_lock lock(mutex_);
          auto& current = sessions_[session_id];
          bag_slot = find_item_slot(current.bag_items, updated.make_index);
          if (bag_slot.has_value()) {
            current.bag_items[static_cast<std::size_t>(*bag_slot)] = updated;
          } else {
            equipment_slot = find_item_slot(current.equipment_items, updated.make_index);
            if (equipment_slot.has_value()) {
              current.equipment_items[static_cast<std::size_t>(*equipment_slot)] = updated;
              equipment_snapshot.items = item_slot_snapshot(current.equipment_items);
            }
          }
        }
        if (bag_slot.has_value()) {
          messages.push_back(client_v1::InventoryUpdate{
              client_v1::ItemSlotState{*bag_slot, updated}});
        } else if (equipment_slot.has_value()) {
          messages.push_back(std::move(equipment_snapshot));
          messages.push_back(self_ability_detail_from_character(state.character));
        }
      }
      break;
    case kSmDuraChange:
      {
        const auto slot = decoded->message.param;
        const auto dura = static_cast<std::uint16_t>(
            std::clamp<std::int32_t>(decoded->message.recog, 0, 65535));
        const auto dura_max = static_cast<std::uint16_t>(
            std::clamp<std::int32_t>(
                decoded->message.tag | (decoded->message.series << 16), 0, 65535));
        std::optional<client_v1::EquipmentSnapshot> equipment_snapshot;
        std::optional<client_v1::DurabilityChange> durability_change;
        {
          std::scoped_lock lock(mutex_);
          auto it = sessions_.find(session_id);
          if (it != sessions_.end() && slot >= 0 &&
              slot < static_cast<std::int32_t>(it->second.equipment_items.size())) {
            auto& item = it->second.equipment_items[static_cast<std::size_t>(slot)];
            if (item.make_index != 0) {
              item.dura = dura;
              item.dura_max = dura_max;
              durability_change = client_v1::DurabilityChange{
                  item.make_index, static_cast<std::int32_t>(dura),
                  static_cast<std::int32_t>(dura_max)};
              equipment_snapshot = client_v1::EquipmentSnapshot{
                  item_slot_snapshot(it->second.equipment_items)};
            }
          }
        }
        if (durability_change.has_value()) {
          messages.push_back(*durability_change);
        }
        if (equipment_snapshot.has_value()) {
          messages.push_back(std::move(*equipment_snapshot));
        }
      }
      messages.push_back(client_v1::SysMessage{
          "Durability changed: slot " + std::to_string(decoded->message.param), 0});
      break;
    case kSmDropItemSuccess:
      messages.push_back(client_v1::SysMessage{
          "Dropped " + legacy_decode_string(decoded->body), 0});
      break;
    case kSmDropItemFail:
      messages.push_back(client_v1::SysMessage{
          "Drop failed: " + legacy_decode_string(decoded->body), 1});
      break;
    case kSmTakeOnOk:
      messages.push_back(client_v1::SysMessage{"Equipped item.", 0});
      break;
    case kSmTakeOnFail:
      messages.push_back(client_v1::SysMessage{"Equip failed.", 1});
      break;
    case kSmTakeOffOk:
      messages.push_back(client_v1::SysMessage{"Unequipped item.", 0});
      break;
    case kSmTakeOffFail:
      messages.push_back(client_v1::SysMessage{"Unequip failed.", 1});
      break;
    case kSmWinExp: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.ability.exp = static_cast<std::uint32_t>(
            std::max<std::int32_t>(0, decoded->message.recog));
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{
          "Experience +" + std::to_string(decoded->message.param), 0});
      break;
    }
    case kSmLevelUp: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.ability.exp =
            static_cast<std::uint32_t>(std::max<std::int32_t>(0, decoded->message.recog));
        current.character.ability.level =
            static_cast<std::uint8_t>(std::clamp<std::int32_t>(decoded->message.param, 0, 255));
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{"Level up!", 0});
      break;
    }
    case kSmWeightChanged: {
      const auto checksum = static_cast<std::uint16_t>(decoded->message.series);
      const auto expected = static_cast<std::uint16_t>(
          (((decoded->message.recog + decoded->message.param + decoded->message.tag) ^ 0x3A5F) ^
           0x1F35) ^
          0xAA21);
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        if (checksum == expected) {
          current.character.ability.weight = static_cast<std::uint16_t>(
              std::clamp<std::int32_t>(decoded->message.recog, 0, 65535));
          current.character.ability.wear_weight = static_cast<std::uint8_t>(
              std::clamp<std::int32_t>(decoded->message.param, 0, 255));
          current.character.ability.hand_weight = static_cast<std::uint8_t>(
              std::clamp<std::int32_t>(decoded->message.tag, 0, 255));
        }
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      break;
    }
    case kSmGoldChanged: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.gold = decoded->message.recog;
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{
          "Gold: " + std::to_string(decoded->message.recog), 0});
      break;
    }
    case kSmDealChangeGoldOk:
    case kSmDealChangeGoldFail: {
      const auto gold = (decoded->message.param & 0xffff) |
                        ((decoded->message.tag & 0xffff) << 16);
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.gold = gold;
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      break;
    }
    case kSmDealCancel:
    case kSmDealSuccess: {
      bool was_visible = false;
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          was_visible = it->second.trade_visible;
          clear_trade_locked(it->second);
        }
      }
      if (was_visible) {
        messages.push_back(client_v1::TradeState{});
      }
      break;
    }
    case kSmHear: {
      const auto text = legacy_decode_string(decoded->body);
      messages.push_back(client_v1::SysMessage{text, 0});
      if (text.find("Trade cancelled.") != std::string::npos ||
          text.find("Trade completed.") != std::string::npos) {
        bool was_visible = false;
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          was_visible = it->second.trade_visible;
          clear_trade_locked(it->second);
        }
        if (was_visible) {
          messages.push_back(client_v1::TradeState{});
        }
      } else if (text.find("Trade failed.") != std::string::npos) {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end() && it->second.trade_visible) {
          it->second.trade_local_accept = false;
          if (auto peer_it = sessions_.find(it->second.trade_peer_session_id);
              peer_it != sessions_.end()) {
            peer_it->second.trade_local_accept = false;
          }
          messages.push_back(trade_state_locked(session_id));
        }
      }
      break;
    }
    case kSmMerchantSay: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.current_merchant_id = merchant_id;
      }
      messages.push_back(client_v1::NpcDialog{
          merchant_id, decoded->message.param, merchant_dialog_text(decoded->body)});
      break;
    }
    case kSmMerchantDlgClose: {
      std::uint64_t merchant_id = 0;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        merchant_id = current.current_merchant_id;
        current.current_merchant_id = 0;
      }
      messages.push_back(client_v1::NpcDialogClose{merchant_id});
      break;
    }
    case kSmSendGoodsList: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id].current_merchant_id = merchant_id;
      }
      messages.push_back(client_v1::MerchantGoodsList{
          merchant_id, merchant_goods_from_legacy_body(decoded->body)});
      break;
    }
    case kSmSendDetailGoodsList: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      const auto legacy_items = decode_client_item_list(decoded->body);
      std::vector<client_v1::MerchantGoodsItem> goods;
      goods.reserve(legacy_items.size());
      for (const auto& legacy_item : legacy_items) {
        const auto item = item_state_from_legacy(legacy_item);
        goods.push_back(client_v1::MerchantGoodsItem{
            item.make_index, item.name, item.looks, item.std_mode,
            static_cast<std::int32_t>(legacy_item.dura_max)});
      }
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id].current_merchant_id = merchant_id;
      }
      messages.push_back(client_v1::MerchantGoodsList{merchant_id, std::move(goods)});
      break;
    }
    case kSmSendUserSell: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id].current_merchant_id = merchant_id;
      }
      messages.push_back(client_v1::MerchantPriceResult{merchant_id, 0, 0, true, true});
      break;
    }
    case kSmSendBuyPrice: {
      client_v1::MerchantPriceResult result;
      result.price = decoded->message.recog;
      result.sell = true;
      result.ok = decoded->message.recog > 0;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        result.merchant_id = current.current_merchant_id;
        result.item_index = current.pending_sell_item_make_index;
      }
      messages.push_back(result);
      break;
    }
    case kSmUserSellItemOk: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.gold = decoded->message.recog;
        current.pending_sell_item_make_index = 0;
        current.pending_sell_item_name.clear();
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      request_bag_items = true;
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{
          "Sold item. Gold: " + std::to_string(decoded->message.recog), 0});
      break;
    }
    case kSmUserSellItemFail:
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.pending_sell_item_make_index = 0;
        current.pending_sell_item_name.clear();
      }
      messages.push_back(client_v1::SysMessage{"Sell failed.", 1});
      break;
    case kSmBuyItemSuccess: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.gold = decoded->message.recog;
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{
          "Bought item. Gold: " + std::to_string(decoded->message.recog), 0});
      break;
    }
    case kSmBuyItemFail:
      messages.push_back(client_v1::SysMessage{"Buy failed.", 1});
      break;
    case kSmSendUserRepair:
      {
        const auto merchant_id =
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
        {
          std::scoped_lock lock(mutex_);
          sessions_[session_id].current_merchant_id = merchant_id;
        }
        messages.push_back(client_v1::MerchantRepairPriceResult{merchant_id, 0, 0, true});
      }
      break;
    case kSmSendRepairCost: {
      client_v1::MerchantRepairPriceResult result;
      result.price = decoded->message.recog;
      result.ok = decoded->message.recog >= 0;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        result.merchant_id = current.current_merchant_id;
        result.item_make_index = current.pending_repair_item_make_index;
      }
      messages.push_back(result);
      break;
    }
    case kSmUserRepairItemOk: {
      client_v1::SelfAbility self_ability;
      client_v1::SelfAbilityDetail self_ability_detail;
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.character.gold = decoded->message.recog;
        current.pending_repair_item_make_index = 0;
        current.pending_repair_item_name.clear();
        self_ability = self_ability_from_character(current.character);
        self_ability_detail = self_ability_detail_from_character(current.character);
      }
      request_bag_items = true;
      messages.push_back(self_ability);
      messages.push_back(self_ability_detail);
      messages.push_back(client_v1::SysMessage{
          "Repaired item. Gold: " + std::to_string(decoded->message.recog), 0});
      break;
    }
    case kSmUserRepairItemFail:
      {
        std::scoped_lock lock(mutex_);
        auto& current = sessions_[session_id];
        current.pending_repair_item_make_index = 0;
        current.pending_repair_item_name.clear();
      }
      messages.push_back(client_v1::SysMessage{"Repair failed.", 1});
      break;
    case kSmSendUserStorageItem: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id].current_merchant_id = merchant_id;
      }
      messages.push_back(client_v1::StorageList{merchant_id, {}});
      request_storage_items = true;
      break;
    }
    case kSmSaveItemList: {
      const auto merchant_id =
          static_cast<std::uint64_t>(static_cast<std::uint32_t>(decoded->message.recog));
      client_v1::StorageList list;
      list.merchant_id = merchant_id;
      const auto legacy_items = decode_client_item_list(decoded->body);
      list.items.reserve(legacy_items.size());
      for (const auto& legacy_item : legacy_items) {
        list.items.push_back(item_state_from_legacy(legacy_item));
      }
      {
        std::scoped_lock lock(mutex_);
        sessions_[session_id].current_merchant_id = merchant_id;
      }
      messages.push_back(std::move(list));
      break;
    }
    case kSmStorageOk:
      request_bag_items = true;
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Stored item.", 0});
      break;
    case kSmStorageFull:
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Storage is full.", 1});
      break;
    case kSmStorageFail:
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Storage failed.", 1});
      break;
    case kSmTakeBackStorageItemOk:
      request_bag_items = true;
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Withdrew item.", 0});
      break;
    case kSmTakeBackStorageItemFullBag:
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Bag is full.", 1});
      break;
    case kSmTakeBackStorageItemFail:
      request_storage_items = true;
      messages.push_back(client_v1::SysMessage{"Withdraw failed.", 1});
      break;
    case kSmMoveFail:
      messages.push_back(client_v1::ActionAck{false, 0});
      messages.push_back(client_v1::ActorStateDelta{
          actor_id, decoded->message.param, decoded->message.tag,
          static_cast<std::uint8_t>(decoded->message.series)});
      break;
    case kSmUsername:
      break;
    case kSmAddMagic: {
      const auto legacy_magic = decode_client_magic(decoded->body);
      if (!legacy_magic.has_value()) {
        break;
      }
      const auto entry = magic_entry_from_legacy(*legacy_magic);
      client_v1::MagicList list;
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          upsert_magic_entry(it->second.magics, entry);
          upsert_character_magic(it->second.character, entry);
          list.magics = it->second.magics;
        } else {
          list.magics.push_back(entry);
        }
      }
      messages.push_back(std::move(list));
      break;
    }
    case kSmDelMagic: {
      const auto magic_id = decoded->message.recog;
      client_v1::MagicList list;
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          remove_magic_entry(it->second.magics, magic_id);
          remove_character_magic(it->second.character, magic_id);
          list.magics = it->second.magics;
        }
      }
      messages.push_back(std::move(list));
      break;
    }
    case kSmMagicLvExp: {
      const auto magic_id = decoded->message.recog;
      const auto level = static_cast<std::uint8_t>(
          std::clamp<std::int32_t>(decoded->message.param, 0, 255));
      const auto train = make_long(decoded->message.tag, decoded->message.series);
      client_v1::MagicList list;
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          for (auto& magic : it->second.magics) {
            if (magic.magic_id == magic_id) {
              magic.level = level;
              magic.train = train;
              upsert_character_magic(it->second.character, magic);
              break;
            }
          }
          list.magics = it->second.magics;
        }
      }
      messages.push_back(std::move(list));
      break;
    }
    case kSmSendMyMagic: {
      client_v1::MagicList list;
      list.magics = decode_client_magic_entries(decoded->body);
      {
        std::scoped_lock lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
          it->second.magics = list.magics;
          it->second.character.magics.fill(LegacyUseMagicInfo{});
          for (const auto& magic : it->second.magics) {
            upsert_character_magic(it->second.character, magic);
          }
        }
      }
      messages.push_back(std::move(list));
      break;
    }
    default:
      break;
  }

  if (request_bag_items) {
    post_canonical_command(decode_client_v1_query_bag_items_command(session_id));
  }
  if (request_storage_items) {
    const auto current = session(session_id);
    if (current.has_value() && current->current_merchant_id != 0) {
      post_canonical_command(decode_client_v1_query_storage_items_command(
          session_id, current->current_merchant_id));
    }
  }
}

std::optional<MapConfig> ClientV1GameGatewayService::find_map(std::string_view map_id) const {
  for (const auto& map : context().config.maps) {
    if (map.id == map_id) {
      return map;
    }
  }
  return std::nullopt;
}

std::optional<ClientV1GameGatewayService::SessionState> ClientV1GameGatewayService::session(
    std::uint64_t session_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace mir2
