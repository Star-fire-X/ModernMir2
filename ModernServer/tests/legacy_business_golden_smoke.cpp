#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_protocol.hpp"
#include "protocol/legacy_types.hpp"
#include "util/string_utils.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                    \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
      std::abort();                                                           \
    }                                                                         \
  } while (false)

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

struct PacketSnapshot {
  std::string name{};
  std::string ident{};
  std::optional<std::int32_t> recog{};
  std::optional<std::int32_t> param{};
  std::optional<std::int32_t> tag{};
  std::optional<std::int32_t> series{};
  std::optional<std::int32_t> item_make_index{};
  std::optional<std::string> body_text{};
  std::optional<std::string> wire_hex{};
};

struct BusinessGoldenFixture {
  std::vector<PacketSnapshot> packets{};
  std::int32_t normal_repair_gold{0};
  std::int32_t normal_repair_dura{0};
  std::int32_t normal_repair_dura_max{0};
  std::int32_t special_repair_cost{0};
  std::uint64_t weapon_upgrade_ready_delay_ms{0};
  std::uint64_t weapon_upgrade_expire_after_ready_ms{0};
};

std::string parse_json_string_at(std::string_view text, std::size_t quote_pos) {
  assert(quote_pos < text.size() && text[quote_pos] == '"');
  std::string value;
  bool escaped = false;
  for (std::size_t index = quote_pos + 1; index < text.size(); ++index) {
    const auto ch = text[index];
    if (escaped) {
      switch (ch) {
        case '\\':
          value.push_back('\\');
          break;
        case '"':
          value.push_back('"');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(ch);
          break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  assert(false);
  return {};
}

std::optional<std::string> json_string_field(std::string_view text, std::string_view key) {
  const auto key_token = "\"" + std::string(key) + "\"";
  const auto key_pos = text.find(key_token);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = text.find(':', key_pos + key_token.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  const auto quote = text.find('"', colon + 1);
  if (quote == std::string_view::npos) {
    return std::nullopt;
  }
  return parse_json_string_at(text, quote);
}

std::optional<std::uint64_t> json_uint_field(std::string_view text, std::string_view key) {
  const auto key_token = "\"" + std::string(key) + "\"";
  const auto key_pos = text.find(key_token);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = text.find(':', key_pos + key_token.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t pos = colon + 1;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                              text[pos] == '\r' || text[pos] == '\t')) {
    ++pos;
  }
  std::uint64_t value = 0;
  bool has_digit = false;
  while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
    has_digit = true;
    value = value * 10 + static_cast<std::uint64_t>(text[pos] - '0');
    ++pos;
  }
  if (!has_digit) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::int32_t> json_int_field(std::string_view text, std::string_view key) {
  const auto value = json_uint_field(text, key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(*value);
}

std::string named_json_object(std::string_view text, std::string_view name) {
  const auto name_token = "\"name\": \"" + std::string(name) + "\"";
  const auto name_pos = text.find(name_token);
  assert(name_pos != std::string_view::npos);
  const auto begin = text.rfind('{', name_pos);
  assert(begin != std::string_view::npos);
  std::int32_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = begin; index < text.size(); ++index) {
    const auto ch = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return std::string(text.substr(begin, index - begin + 1));
      }
    }
  }
  assert(false);
  return {};
}

PacketSnapshot load_packet_snapshot(std::string_view fixture, std::string_view name) {
  const auto object = named_json_object(fixture, name);
  PacketSnapshot packet;
  packet.name = std::string(name);
  packet.ident = json_string_field(object, "ident").value_or(std::string{});
  packet.recog = json_int_field(object, "recog");
  packet.param = json_int_field(object, "param");
  packet.tag = json_int_field(object, "tag");
  packet.series = json_int_field(object, "series");
  packet.item_make_index = json_int_field(object, "item_make_index");
  packet.body_text = json_string_field(object, "body_text");
  packet.wire_hex = json_string_field(object, "wire_hex");
  return packet;
}

BusinessGoldenFixture load_business_golden_fixture() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto text = read_text(source_root / "tests" / "golden" / "npc_business_pr6" /
                              "business_protocol_snapshots.json");
  BusinessGoldenFixture fixture;
  fixture.packets.push_back(load_packet_snapshot(text, "merchant_dialog"));
  fixture.packets.push_back(load_packet_snapshot(text, "goods_list"));
  fixture.packets.push_back(load_packet_snapshot(text, "restocked_goods_list"));
  fixture.packets.push_back(load_packet_snapshot(text, "repair_ok"));
  fixture.packets.push_back(load_packet_snapshot(text, "storage_list"));
  fixture.normal_repair_gold = *json_int_field(text, "normal_repair_gold");
  fixture.normal_repair_dura = *json_int_field(text, "normal_repair_dura");
  fixture.normal_repair_dura_max = *json_int_field(text, "normal_repair_dura_max");
  fixture.special_repair_cost = *json_int_field(text, "special_repair_cost");
  fixture.weapon_upgrade_ready_delay_ms =
      *json_uint_field(text, "weapon_upgrade_ready_delay_ms");
  fixture.weapon_upgrade_expire_after_ready_ms =
      *json_uint_field(text, "weapon_upgrade_expire_after_ready_ms");
  return fixture;
}

const BusinessGoldenFixture& golden_fixture() {
  static const auto fixture = load_business_golden_fixture();
  return fixture;
}

const PacketSnapshot& golden_packet(std::string_view name) {
  const auto& fixture = golden_fixture();
  const auto it = std::find_if(fixture.packets.begin(), fixture.packets.end(),
                               [&](const PacketSnapshot& packet) {
                                 return packet.name == name;
                               });
  assert(it != fixture.packets.end());
  return *it;
}

mir2::ItemConfig item(std::int32_t id, std::string name, std::int32_t price,
                      std::int32_t std_mode, std::int32_t equip_slot = -1) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.weight = 1;
  config.price = price;
  config.std_mode = std_mode;
  config.shape = 1;
  config.looks = id;
  config.dura_max = 1000;
  config.equip_slot = equip_slot;
  return config;
}

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t item_id,
                               std::uint16_t dura = 1000,
                               std::uint16_t dura_max = 1000) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(item_id);
  item.dura = dura;
  item.dura_max = dura_max;
  return item;
}

mir2::CharacterRecord character(std::string name) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.gold = 500;
  record.ability.level = 40;
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 50;
  record.ability.max_mp = 50;
  record.ability.max_exp = 1000;
  record.ability.max_weight = 200;
  record.ability.max_wear_weight = 200;
  record.ability.max_hand_weight = 200;
  return record;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
}

mir2::RuntimeDispatch run_ticks(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                                int count = 24, std::uint64_t step_ms = 251) {
  mir2::RuntimeDispatch dispatch;
  for (int i = 0; i < count; ++i) {
    now_ms += step_ms;
    append_dispatch(dispatch, runtime.tick(now_ms));
  }
  return dispatch;
}

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                                mir2::LogicCommand command) {
  static_cast<void>(runtime.route_logic_command(std::move(command)));
  return run_ticks(runtime, now_ms);
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    hex.push_back(digits[(byte >> 4) & 0x0f]);
    hex.push_back(digits[byte & 0x0f]);
  }
  return hex;
}

std::string packet_wire_hex(const mir2::LegacyPacket& packet) {
  return bytes_to_hex(mir2::LegacyProtocolCodec::encode(packet));
}

const mir2::SessionEvent* find_packet_event(const mir2::RuntimeDispatch& dispatch,
                                            std::uint16_t ident,
                                            std::uint64_t session_id = 0) {
  for (const auto& event : dispatch.session_events) {
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return &event;
    }
  }
  return nullptr;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    std::uint64_t session_id = 0) {
  const auto* event = find_packet_event(dispatch, ident, session_id);
  if (event == nullptr) {
    return std::nullopt;
  }
  return mir2::decode_legacy_game_packet(event->packet);
}

void assert_packet_wire_hex(const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
                            std::uint64_t session_id, const PacketSnapshot& snapshot) {
  const auto* event = find_packet_event(dispatch, ident, session_id);
  assert(event != nullptr);
  const auto actual = packet_wire_hex(event->packet);
  if (!snapshot.wire_hex.has_value() || actual != *snapshot.wire_hex) {
    const auto expected = snapshot.wire_hex.value_or("<missing>");
    std::fprintf(stderr, "%s wire_hex expected_len %zu actual_len %zu expected %s actual %s\n",
                 snapshot.name.c_str(), expected.size(), actual.size(), expected.c_str(),
                 actual.c_str());
    assert(false);
  }
}

std::optional<std::size_t> packet_index(const mir2::RuntimeDispatch& dispatch,
                                        std::uint16_t ident) {
  for (std::size_t index = 0; index < dispatch.session_events.size(); ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(dispatch.session_events[index].packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return index;
    }
  }
  return std::nullopt;
}

const mir2::PersistRequest* find_persist(const mir2::RuntimeDispatch& dispatch,
                                         mir2::PersistRequestKind kind) {
  const auto it = std::find_if(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                               [&](const mir2::PersistRequest& request) {
                                 return request.kind == kind;
                               });
  return it == dispatch.persist_requests.end() ? nullptr : &*it;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand click(std::uint64_t session_id, std::uint64_t npc_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::click_npc;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  return command;
}

mir2::LogicCommand menu(std::uint64_t session_id, std::uint64_t npc_id, std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand item_command(mir2::LogicCommandKind kind, std::uint64_t session_id,
                                std::uint64_t npc_id, std::int32_t make_index,
                                std::string name) {
  mir2::LogicCommand command;
  command.kind = kind;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.item_make_index = make_index;
  command.text = std::move(name);
  return command;
}

void enter_running(mir2::LogicRuntime& runtime, std::uint64_t& now_ms,
                   std::uint64_t session_id, mir2::CharacterRecord record) {
  static_cast<void>(runtime.route_logic_command(enter(session_id, std::move(record))));
  const auto dispatch = run_ticks(runtime, now_ms);
  assert(find_packet(dispatch, mir2::kSmNewMap, session_id).has_value());
}

std::vector<mir2::LegacyClientItem> decode_client_items(std::string_view body,
                                                        bool outer_decode) {
  std::vector<mir2::LegacyClientItem> items;
  const auto decoded = outer_decode ? mir2::legacy_decode_string(body) : std::string(body);
  for (const auto& part : mir2::util::split(decoded, '/')) {
    if (part.empty()) {
      continue;
    }
    mir2::LegacyClientItem item;
    if (mir2::legacy_decode_buffer(part, &item, sizeof(item))) {
      items.push_back(item);
    }
  }
  return items;
}

template <std::size_t N>
bool has_user_item(const std::array<mir2::LegacyUserItem, N>& items,
                   std::int32_t make_index) {
  return std::any_of(items.begin(), items.end(), [&](const mir2::LegacyUserItem& item) {
    return !mir2::is_empty(item) && item.make_index == make_index;
  });
}

void check_golden_fixture() {
  for (const auto& packet : golden_fixture().packets) {
    assert(packet.wire_hex.has_value());
  }

  const auto& merchant_dialog = golden_packet("merchant_dialog");
  assert(merchant_dialog.ident == "SM_MERCHANTSAY");
  assert(merchant_dialog.recog.has_value() && *merchant_dialog.recog == 1);
  assert(merchant_dialog.body_text.has_value() &&
         *merchant_dialog.body_text == "Trader/<Buy/@buy>\\<Sell/@sell>\\<Repair/@repair>");

  const auto& goods_list = golden_packet("goods_list");
  assert(goods_list.ident == "SM_SENDGOODSLIST");
  assert(goods_list.param.has_value() && *goods_list.param == 2);
  assert(goods_list.body_text.has_value() &&
         *goods_list.body_text == "Potion/0/60/2/Bronze Sword/1/180/1/");

  const auto& fixture = golden_fixture();
  assert(fixture.weapon_upgrade_ready_delay_ms == 3'600'000);
  assert(fixture.weapon_upgrade_expire_after_ready_ms == 259'200'000);
}

void check_shop_golden() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "BusinessMap", {}, 0, 0, 20, 20});
  config.items.push_back(item(1, "Potion", 40, 0));
  config.items.push_back(item(2, "Bronze Sword", 120, 5, mir2::kEquipWeapon));
  mir2::NpcConfig npc;
  npc.id = "merchant";
  npc.map_id = "0";
  npc.name = "Trader";
  npc.x = 11;
  npc.y = 10;
  npc.service = "sell_repair";
  npc.price_rate_percent = 150;
  npc.dialog_sections.push_back({"@main", "<Buy/@buy>\\<Sell/@sell>\\<Repair/@repair>"});
  npc.merchant_products.push_back({"Potion", 2, 1});
  npc.merchant_products.push_back({"Bronze Sword", 1, 1});
  config.npcs.push_back(std::move(npc));

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 1000;
  auto hero = character("ShopHero");
  hero.gold = 500;
  enter_running(runtime, now_ms, 10, hero);

  const auto main = route_due(runtime, now_ms, click(10, 1));
  const auto say = find_packet(main, mir2::kSmMerchantSay, 10);
  const auto& merchant_dialog = golden_packet("merchant_dialog");
  assert(say.has_value());
  assert_packet_wire_hex(main, mir2::kSmMerchantSay, 10, merchant_dialog);
  assert(merchant_dialog.ident == "SM_MERCHANTSAY");
  assert(merchant_dialog.recog.has_value());
  assert(say->message.recog == *merchant_dialog.recog);
  assert(merchant_dialog.body_text.has_value());
  assert(mir2::legacy_decode_string(say->body) == *merchant_dialog.body_text);

  const auto buy_open = route_due(runtime, now_ms, menu(10, 1, "@buy"));
  const auto goods = find_packet(buy_open, mir2::kSmSendGoodsList, 10);
  const auto& goods_list = golden_packet("goods_list");
  assert(goods.has_value());
  assert_packet_wire_hex(buy_open, mir2::kSmSendGoodsList, 10, goods_list);
  assert(goods_list.ident == "SM_SENDGOODSLIST");
  assert(goods_list.recog.has_value());
  assert(goods_list.param.has_value());
  assert(goods->message.recog == *goods_list.recog);
  assert(goods->message.param == *goods_list.param);
  assert(goods_list.body_text.has_value());
  assert(mir2::legacy_decode_string(goods->body) == *goods_list.body_text);

  const auto detail = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::query_detail_goods, 10, 1, 0,
                                    "Bronze Sword"));
  const auto detail_packet = find_packet(detail, mir2::kSmSendDetailGoodsList, 10);
  assert(detail_packet.has_value());
  const auto detail_items = decode_client_items(detail_packet->body, true);
  assert(detail_items.size() == 1);
  assert(mir2::to_string(detail_items.front().item.name) == "Bronze Sword");
  assert(detail_items.front().dura_max == 180);

  static_cast<void>(route_due(runtime, now_ms,
                              item_command(mir2::LogicCommandKind::buy_item, 10, 1, 0,
                                           "Potion")));
  const auto buy_second = route_due(runtime, now_ms,
                                    item_command(mir2::LogicCommandKind::buy_item, 10, 1, 0,
                                                 "Potion"));
  const auto ok = find_packet(buy_second, mir2::kSmBuyItemSuccess, 10);
  assert(ok.has_value());
  assert(ok->message.recog == 380);

  const auto empty_goods_dispatch = route_due(runtime, now_ms, menu(10, 1, "@buy"));
  const auto empty_goods = find_packet(empty_goods_dispatch, mir2::kSmSendGoodsList, 10);
  assert(empty_goods.has_value());
  assert(mir2::legacy_decode_string(empty_goods->body).find("Potion") == std::string::npos);

  now_ms += 305'000;
  static_cast<void>(run_ticks(runtime, now_ms, 8, 1000));
  const auto restocked_dispatch = route_due(runtime, now_ms, menu(10, 1, "@buy"));
  const auto restocked = find_packet(restocked_dispatch, mir2::kSmSendGoodsList, 10);
  const auto& restocked_goods = golden_packet("restocked_goods_list");
  assert(restocked.has_value());
  assert_packet_wire_hex(restocked_dispatch, mir2::kSmSendGoodsList, 10, restocked_goods);
  assert(restocked_goods.body_text.has_value());
  assert(mir2::legacy_decode_string(restocked->body) == *restocked_goods.body_text);
}

void check_storage_golden() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "StorageMap", {}, 0, 0, 20, 20});
  config.items.push_back(item(1, "Storage Sword", 100, 5));
  config.items.push_back(item(2, "Event Token", 10, 51));
  config.items.push_back(item(3, "Storage Ring", 80, 22));
  config.npcs.push_back(mir2::NpcConfig{"storage", "0", "Warehouse Keeper", 11, 10,
                                        "storage.txt", "storage"});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 2000;
  auto hero = character("StorageHero");
  hero.bag_items[0] = user_item(3001, 1);
  hero.bag_items[1] = user_item(3002, 2);
  hero.storage_items[0] = user_item(3003, 3);
  enter_running(runtime, now_ms, 20, hero);

  const auto opened = route_due(runtime, now_ms, click(20, 1));
  const auto storage_menu = find_packet(opened, mir2::kSmSendUserStorageItem, 20);
  const auto save_list = find_packet(opened, mir2::kSmSaveItemList, 20);
  const auto& storage_list = golden_packet("storage_list");
  assert(storage_menu.has_value());
  assert(save_list.has_value());
  assert_packet_wire_hex(opened, mir2::kSmSaveItemList, 20, storage_list);
  assert(storage_menu->message.recog == 1);
  assert(storage_menu->message.param == 1);
  assert(storage_list.ident == "SM_SAVEITEMLIST");
  assert(storage_list.recog.has_value());
  assert(storage_list.series.has_value());
  assert(save_list->message.recog == *storage_list.recog);
  assert(save_list->message.series == *storage_list.series);
  const auto storage_menu_index = packet_index(opened, mir2::kSmSendUserStorageItem);
  const auto save_list_index = packet_index(opened, mir2::kSmSaveItemList);
  assert(storage_menu_index.has_value());
  assert(save_list_index.has_value());
  assert(*storage_menu_index < *save_list_index);
  const auto stored_items = decode_client_items(save_list->body, false);
  assert(stored_items.size() == 1);
  assert(storage_list.item_make_index.has_value());
  assert(stored_items.front().make_index == *storage_list.item_make_index);

  const auto deposit = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::storage_item, 20, 1, 3001,
                                    "Storage Sword"));
  assert(find_packet(deposit, mir2::kSmDelItem, 20).has_value());
  assert(find_packet(deposit, mir2::kSmStorageOk, 20).has_value());
  const auto del_item_index = packet_index(deposit, mir2::kSmDelItem);
  const auto storage_ok_index = packet_index(deposit, mir2::kSmStorageOk);
  assert(del_item_index.has_value());
  assert(storage_ok_index.has_value());
  assert(*del_item_index < *storage_ok_index);
  auto snapshot = runtime.snapshot_character_actor("StorageHero");
  assert(snapshot.has_value());
  assert(!has_user_item(snapshot->bag_items, 3001));
  assert(has_user_item(snapshot->storage_items, 3001));

  const auto event_fail = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::storage_item, 20, 1, 3002,
                                    "Event Token"));
  assert(find_packet(event_fail, mir2::kSmStorageFail, 20).has_value());
  assert(find_persist(event_fail, mir2::PersistRequestKind::save_character) == nullptr);
  snapshot = runtime.snapshot_character_actor("StorageHero");
  assert(snapshot.has_value());
  assert(has_user_item(snapshot->bag_items, 3002));

  const auto withdraw = route_due(
      runtime, now_ms,
      item_command(mir2::LogicCommandKind::take_back_storage_item, 20, 1, 3003,
                   "Storage Ring"));
  assert(find_packet(withdraw, mir2::kSmAddItem, 20).has_value());
  assert(find_packet(withdraw, mir2::kSmTakeBackStorageItemOk, 20).has_value());
  const auto add_item_index = packet_index(withdraw, mir2::kSmAddItem);
  const auto take_back_index = packet_index(withdraw, mir2::kSmTakeBackStorageItemOk);
  assert(add_item_index.has_value());
  assert(take_back_index.has_value());
  assert(*add_item_index < *take_back_index);
}

void check_repair_golden() {
  mir2::HostConfig config;
  config.maps.push_back(mir2::MapConfig{"0", "RepairMap", {}, 0, 0, 20, 20});
  config.items.push_back(item(1, "Repair Sword", 90, 5, mir2::kEquipWeapon));
  config.items.push_back(item(2, "Repair Axe", 90, 6, mir2::kEquipWeapon));
  config.npcs.push_back(mir2::NpcConfig{"repair", "0", "Repairman", 11, 10,
                                        "repair.txt", "repair"});

  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 3000;
  auto hero = character("RepairHero");
  hero.bag_items[0] = user_item(4001, 1, 600, 1000);
  hero.bag_items[1] = user_item(4002, 2, 600, 1000);
  enter_running(runtime, now_ms, 30, hero);

  static_cast<void>(route_due(runtime, now_ms, click(30, 1)));
  const auto cost = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::query_repair_cost, 30, 1,
                                    4001, "Repair Sword"));
  const auto cost_packet = find_packet(cost, mir2::kSmSendRepairCost, 30);
  assert(cost_packet.has_value());
  assert(cost_packet->message.recog == 10);

  const auto repaired = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::repair_item, 30, 1, 4001,
                                    "Repair Sword"));
  const auto ok = find_packet(repaired, mir2::kSmUserRepairItemOk, 30);
  const auto& fixture = golden_fixture();
  const auto& repair_ok = golden_packet("repair_ok");
  assert(ok.has_value());
  assert_packet_wire_hex(repaired, mir2::kSmUserRepairItemOk, 30, repair_ok);
  assert(repair_ok.ident == "SM_USERREPAIRITEM_OK");
  assert(repair_ok.recog.has_value());
  assert(repair_ok.param.has_value());
  assert(repair_ok.tag.has_value());
  assert(ok->message.recog == *repair_ok.recog);
  assert(ok->message.param == *repair_ok.param);
  assert(ok->message.tag == *repair_ok.tag);
  auto snapshot = runtime.snapshot_character_actor("RepairHero");
  assert(snapshot.has_value());
  assert(snapshot->gold == fixture.normal_repair_gold);
  assert(snapshot->bag_items[0].dura == fixture.normal_repair_dura);
  assert(snapshot->bag_items[0].dura_max == fixture.normal_repair_dura_max);

  static_cast<void>(route_due(runtime, now_ms, menu(30, 1, "@s_repair")));
  const auto special_cost = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::query_repair_cost, 30, 1,
                                    4002, "Repair Axe"));
  const auto special_cost_packet = find_packet(special_cost, mir2::kSmSendRepairCost, 30);
  assert(special_cost_packet.has_value());
  assert(special_cost_packet->message.recog == fixture.special_repair_cost);

  const auto special_repaired = route_due(
      runtime, now_ms, item_command(mir2::LogicCommandKind::repair_item, 30, 1, 4002,
                                    "Repair Axe"));
  const auto special_ok = find_packet(special_repaired, mir2::kSmUserRepairItemOk, 30);
  assert(special_ok.has_value());
  assert(special_ok->message.recog == 460);
  assert(special_ok->message.param == 1000);
  assert(special_ok->message.tag == 1000);
}

mir2::HostConfig upgrade_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 3;
  config.runtime.upgrade_weapon_fee = 500;
  config.maps.push_back(mir2::MapConfig{"0", "UpgradeMap", {}, 0, 0, 20, 20});
  auto sword = item(1, "Upgradeable Sword", 100, 5, mir2::kEquipWeapon);
  sword.dc = mir2::make_word(5, 10);
  config.items.push_back(sword);
  config.items.push_back(item(2, "BlackStone", 100, 41));
  auto necklace = item(3, "Power Necklace", 100, 19);
  necklace.dc = mir2::make_word(3, 4);
  config.items.push_back(necklace);
  config.npcs.push_back(mir2::NpcConfig{"upgrader", "0", "Blacksmith", 11, 10,
                                        "upgrade.txt", "upgrade"});
  return config;
}

void check_weapon_upgrade_golden() {
  auto config = upgrade_config();
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  std::uint64_t now_ms = 4000;
  auto hero = character("UpgradeHero");
  hero.gold = 10000;
  hero.equipped_items[mir2::kEquipWeapon] = user_item(5001, 1, 1000, 1000);
  hero.bag_items[0] = user_item(5002, 2, 5000, 5000);
  hero.bag_items[1] = user_item(5003, 3, 1000, 1000);
  enter_running(runtime, now_ms, 40, hero);

  const auto start_request_now_ms = now_ms;
  const auto start = route_due(runtime, now_ms, menu(40, 1, "@upgradenow"));
  const auto* state = find_persist(start, mir2::PersistRequestKind::save_merchant_state);
  assert(state != nullptr);
  assert(state->merchant_state.weapon_upgrades.size() == 1);
  const auto& record = state->merchant_state.weapon_upgrades.front();
  assert(record.item.make_index == 5001);
  const auto& fixture = golden_fixture();
  assert(record.ready_time_ms >= fixture.weapon_upgrade_ready_delay_ms);
  const auto ready_base_ms = record.ready_time_ms - fixture.weapon_upgrade_ready_delay_ms;
  assert(ready_base_ms >= start_request_now_ms);
  assert(ready_base_ms <= now_ms);
  auto snapshot = runtime.snapshot_character_actor("UpgradeHero");
  assert(snapshot.has_value());
  assert(snapshot->gold == 9500);
  assert(mir2::is_empty(snapshot->equipped_items[mir2::kEquipWeapon]));

  const auto early = route_due(runtime, now_ms, menu(40, 1, "@getbackupgnow"));
  assert(!find_packet(early, mir2::kSmAddItem, 40).has_value());

  auto ready_state = state->merchant_state;
  ready_state.weapon_upgrades.front().ready_time_ms = now_ms;
  runtime.apply_merchant_states({ready_state});
  const auto get_back = route_due(runtime, now_ms, menu(40, 1, "@getbackupgnow"));
  assert(find_packet(get_back, mir2::kSmAddItem, 40).has_value());
  snapshot = runtime.snapshot_character_actor("UpgradeHero");
  assert(snapshot.has_value());
  assert(has_user_item(snapshot->bag_items, 5001));

  mir2::LogicRuntime cleanup_runtime(config);
  cleanup_runtime.initialize();
  std::uint64_t cleanup_now_ms = 8000;
  enter_running(cleanup_runtime, cleanup_now_ms, 41, character("CleanupHero"));
  mir2::MerchantStateRecord expired_state;
  expired_state.merchant_key = "upgrader-0";
  expired_state.npc_id = "upgrader";
  expired_state.map_id = "0";
  mir2::LegacyWeaponUpgradeRecord expired;
  expired.character_name = "CleanupHero";
  expired.item = user_item(9001, 1);
  expired.ready_time_ms = cleanup_now_ms - fixture.weapon_upgrade_expire_after_ready_ms - 1;
  expired_state.weapon_upgrades.push_back(expired);
  cleanup_runtime.apply_merchant_states({expired_state});
  auto cleanup_dispatch = run_ticks(cleanup_runtime, cleanup_now_ms, 8, 1000);
  const auto* cleanup_state =
      find_persist(cleanup_dispatch, mir2::PersistRequestKind::save_merchant_state);
  assert(cleanup_state != nullptr);
  assert(cleanup_state->merchant_state.weapon_upgrades.empty());
}

}  // namespace

int main() {
  check_golden_fixture();
  check_shop_golden();
  check_storage_golden();
  check_repair_golden();
  check_weapon_upgrade_golden();
  return 0;
}
