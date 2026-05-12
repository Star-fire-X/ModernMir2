#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/canonical_legacy_command.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"

namespace {

struct LegacyCommandCase {
  std::string name{};
  std::uint16_t ident{0};
  std::int32_t recog{0};
  std::uint16_t param{0};
  std::uint16_t tag{0};
  std::uint16_t series{0};
  std::string body_encoding{};
  std::string body_hex{};
};

struct ExpectedKinds {
  mir2::CanonicalLegacyCommandKind canonical{};
  mir2::LogicCommandKind logic{};
};

int fail(std::string_view stage) {
  std::cerr << "canonical_legacy_command_smoke failed at " << stage << '\n';
  return 1;
}

std::optional<std::string> string_field(const std::string& line, std::string_view key) {
  const auto token = std::string{"\""} + std::string(key) + "\"";
  auto pos = line.find(token);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = line.find(':', pos + token.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = line.find('"', pos + 1);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  const auto end = line.find('"', pos + 1);
  if (end == std::string::npos) {
    return std::nullopt;
  }
  return line.substr(pos + 1, end - pos - 1);
}

std::optional<std::int64_t> int_field(const std::string& line, std::string_view key) {
  const auto token = std::string{"\""} + std::string(key) + "\"";
  auto pos = line.find(token);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = line.find(':', pos + token.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
    ++pos;
  }
  bool negative = false;
  if (pos < line.size() && line[pos] == '-') {
    negative = true;
    ++pos;
  }
  if (pos >= line.size() || std::isdigit(static_cast<unsigned char>(line[pos])) == 0) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
    value = (value * 10) + (line[pos] - '0');
    ++pos;
  }
  return negative ? -value : value;
}

int hex_digit(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

std::optional<std::string> bytes_from_hex(std::string_view hex) {
  if ((hex.size() % 2) != 0) {
    return std::nullopt;
  }
  std::string bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    const auto high = hex_digit(hex[index]);
    const auto low = hex_digit(hex[index + 1]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }
    bytes.push_back(static_cast<char>((high << 4) | low));
  }
  return bytes;
}

std::optional<LegacyCommandCase> parse_case_line(const std::string& line) {
  const auto name = string_field(line, "name");
  if (!name.has_value()) {
    return std::nullopt;
  }

  const auto ident = int_field(line, "ident");
  const auto recog = int_field(line, "recog");
  const auto param = int_field(line, "param");
  const auto tag = int_field(line, "tag");
  const auto series = int_field(line, "series");
  const auto body_encoding = string_field(line, "body_encoding");
  const auto body_hex = string_field(line, "body_hex");
  if (!ident.has_value() || !recog.has_value() || !param.has_value() || !tag.has_value() ||
      !series.has_value() || !body_encoding.has_value() || !body_hex.has_value()) {
    return std::nullopt;
  }

  LegacyCommandCase parsed;
  parsed.name = *name;
  parsed.ident = static_cast<std::uint16_t>(*ident);
  parsed.recog = static_cast<std::int32_t>(*recog);
  parsed.param = static_cast<std::uint16_t>(*param);
  parsed.tag = static_cast<std::uint16_t>(*tag);
  parsed.series = static_cast<std::uint16_t>(*series);
  parsed.body_encoding = *body_encoding;
  parsed.body_hex = *body_hex;
  return parsed;
}

std::vector<LegacyCommandCase> load_cases(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<LegacyCommandCase> cases;
  std::string line;
  while (std::getline(input, line)) {
    auto parsed = parse_case_line(line);
    if (parsed.has_value()) {
      cases.push_back(*parsed);
    }
  }
  return cases;
}

std::optional<std::string> make_case_body(const LegacyCommandCase& test_case,
                                          std::string* decoded_bytes) {
  const auto bytes = bytes_from_hex(test_case.body_hex);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  *decoded_bytes = *bytes;
  if (test_case.body_encoding == "none") {
    return std::string{};
  }
  if (test_case.body_encoding == "raw") {
    return *bytes;
  }
  if (test_case.body_encoding == "legacy_string") {
    return mir2::legacy_encode_string(*bytes);
  }
  return std::nullopt;
}

bool same_message(const mir2::LegacyDefaultMessage& lhs,
                  const mir2::LegacyDefaultMessage& rhs) {
  return lhs.ident == rhs.ident && lhs.recog == rhs.recog && lhs.param == rhs.param &&
         lhs.tag == rhs.tag && lhs.series == rhs.series;
}

std::optional<ExpectedKinds> expected_kinds(std::uint16_t ident) {
  using C = mir2::CanonicalLegacyCommandKind;
  using L = mir2::LogicCommandKind;
  switch (ident) {
    case mir2::kCmTurn:
      return ExpectedKinds{C::turn, L::turn};
    case mir2::kCmWalk:
      return ExpectedKinds{C::walk, L::walk};
    case mir2::kCmRun:
      return ExpectedKinds{C::run, L::run};
    case mir2::kCmHit:
    case mir2::kCmHeavyHit:
    case mir2::kCmBigHit:
    case mir2::kCmPowerHit:
    case mir2::kCmLongHit:
    case mir2::kCmWideHit:
    case mir2::kCmFireHit:
    case mir2::kCmCrossHit:
      return ExpectedKinds{C::attack, L::attack};
    case mir2::kCmSpell:
      return ExpectedKinds{C::spell, L::spell};
    case mir2::kCmSay:
      return ExpectedKinds{C::say, L::say};
    case mir2::kCmClickNpc:
      return ExpectedKinds{C::click_npc, L::click_npc};
    case mir2::kCmMerchantDlgSelect:
      return ExpectedKinds{C::merchant_select, L::merchant_select};
    case mir2::kCmQueryUsername:
      return ExpectedKinds{C::query_username, L::query_username};
    case mir2::kCmQueryBagItems:
      return ExpectedKinds{C::query_bag_items, L::query_bag_items};
    case mir2::kCmUserGetDetailItem:
      return ExpectedKinds{C::query_detail_goods, L::query_detail_goods};
    case mir2::kCmMerchantQuerySellPrice:
      return ExpectedKinds{C::query_sell_price, L::query_sell_price};
    case mir2::kCmMerchantQueryRepairCost:
      return ExpectedKinds{C::query_repair_cost, L::query_repair_cost};
    case mir2::kCmDropItem:
      return ExpectedKinds{C::drop_item, L::drop_item};
    case mir2::kCmPickup:
      return ExpectedKinds{C::pickup_item, L::pickup_item};
    case mir2::kCmTakeOnItem:
      return ExpectedKinds{C::take_on_item, L::take_on_item};
    case mir2::kCmTakeOffItem:
      return ExpectedKinds{C::take_off_item, L::take_off_item};
    case mir2::kCmEat:
      return ExpectedKinds{C::eat_item, L::eat_item};
    case mir2::kCmDropGold:
      return ExpectedKinds{C::drop_gold, L::drop_gold};
    case mir2::kCmDealTry:
      return ExpectedKinds{C::trade_try, L::trade_try};
    case mir2::kCmDealAddItem:
      return ExpectedKinds{C::trade_add_item, L::trade_add_item};
    case mir2::kCmDealDelItem:
      return ExpectedKinds{C::trade_remove_item, L::trade_remove_item};
    case mir2::kCmDealCancel:
      return ExpectedKinds{C::trade_cancel, L::trade_cancel};
    case mir2::kCmDealChangeGold:
      return ExpectedKinds{C::trade_set_gold, L::trade_set_gold};
    case mir2::kCmDealEnd:
      return ExpectedKinds{C::trade_accept, L::trade_accept};
    case mir2::kCmUserBuyItem:
      return ExpectedKinds{C::buy_item, L::buy_item};
    case mir2::kCmUserSellItem:
      return ExpectedKinds{C::sell_item, L::sell_item};
    case mir2::kCmUserRepairItem:
      return ExpectedKinds{C::repair_item, L::repair_item};
    case mir2::kCmUserStorageItem:
      return ExpectedKinds{C::storage_item, L::storage_item};
    case mir2::kCmUserTakeBackStorageItem:
      return ExpectedKinds{C::take_back_storage_item, L::take_back_storage_item};
    default:
      return std::nullopt;
  }
}

std::string expected_text(std::uint16_t ident, const std::string& decoded_bytes) {
  switch (ident) {
    case mir2::kCmSpell:
      return decoded_bytes;
    case mir2::kCmSay:
    case mir2::kCmMerchantDlgSelect:
    case mir2::kCmUserStorageItem:
    case mir2::kCmUserTakeBackStorageItem:
    case mir2::kCmUserGetDetailItem:
    case mir2::kCmMerchantQuerySellPrice:
    case mir2::kCmMerchantQueryRepairCost:
    case mir2::kCmDropItem:
    case mir2::kCmTakeOnItem:
    case mir2::kCmTakeOffItem:
    case mir2::kCmEat:
    case mir2::kCmDealTry:
    case mir2::kCmDealAddItem:
    case mir2::kCmDealDelItem:
    case mir2::kCmUserSellItem:
    case mir2::kCmUserBuyItem:
    case mir2::kCmUserRepairItem:
      return decoded_bytes;
    default:
      return {};
  }
}

bool check_expected_fields(const LegacyCommandCase& test_case,
                           const mir2::CanonicalLegacyCommand& canonical,
                           const mir2::LogicCommand& logic,
                           const mir2::LegacyPacket& packet,
                           const std::string& decoded_bytes) {
  const auto kinds = expected_kinds(test_case.ident);
  if (!kinds.has_value() || canonical.kind != kinds->canonical || logic.kind != kinds->logic ||
      canonical.source_protocol != mir2::CanonicalSourceProtocol::legacy_framed ||
      canonical.session_id != 321 || logic.session_id != 321 ||
      !same_message(canonical.game_message, logic.game_message) ||
      canonical.packet.body != packet.body || logic.packet.body != packet.body) {
    return false;
  }

  std::int32_t expected_x = 0;
  std::int32_t expected_y = 0;
  std::uint8_t expected_dir = 0;
  std::uint64_t expected_target = 0;
  std::int32_t expected_item = 0;
  std::int32_t expected_slot = -1;
  std::int32_t expected_amount = 0;
  switch (test_case.ident) {
    case mir2::kCmTurn:
    case mir2::kCmWalk:
    case mir2::kCmRun:
    case mir2::kCmHit:
    case mir2::kCmHeavyHit:
    case mir2::kCmBigHit:
    case mir2::kCmPowerHit:
    case mir2::kCmLongHit:
    case mir2::kCmWideHit:
    case mir2::kCmFireHit:
    case mir2::kCmCrossHit:
      expected_x = mir2::low_word(test_case.recog);
      expected_y = mir2::high_word(test_case.recog);
      expected_dir = static_cast<std::uint8_t>(test_case.tag);
      break;
    case mir2::kCmSpell:
      expected_x = mir2::low_word(test_case.recog);
      expected_y = mir2::high_word(test_case.recog);
      expected_target = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(mir2::make_long(test_case.param, test_case.series)));
      break;
    case mir2::kCmClickNpc:
    case mir2::kCmMerchantDlgSelect:
      expected_target = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(test_case.recog));
      break;
    case mir2::kCmQueryUsername:
      expected_target = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(test_case.recog));
      expected_x = test_case.param;
      expected_y = test_case.tag;
      break;
    case mir2::kCmUserStorageItem:
    case mir2::kCmUserTakeBackStorageItem:
    case mir2::kCmMerchantQuerySellPrice:
    case mir2::kCmMerchantQueryRepairCost:
    case mir2::kCmUserSellItem:
    case mir2::kCmUserBuyItem:
    case mir2::kCmUserRepairItem:
      expected_target = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(test_case.recog));
      expected_item = mir2::make_long(test_case.param, test_case.tag);
      break;
    case mir2::kCmUserGetDetailItem:
      expected_target = static_cast<std::uint64_t>(
          static_cast<std::uint32_t>(test_case.recog));
      expected_item = test_case.param;
      break;
    case mir2::kCmDropItem:
    case mir2::kCmTakeOnItem:
    case mir2::kCmTakeOffItem:
    case mir2::kCmEat:
    case mir2::kCmDealAddItem:
    case mir2::kCmDealDelItem:
      expected_item = test_case.recog;
      break;
    case mir2::kCmPickup:
      expected_x = test_case.param;
      expected_y = test_case.tag;
      break;
    case mir2::kCmDropGold:
    case mir2::kCmDealChangeGold:
      expected_amount = test_case.recog;
      break;
    default:
      break;
  }
  if (test_case.ident == mir2::kCmTakeOnItem || test_case.ident == mir2::kCmTakeOffItem) {
    expected_slot = test_case.param;
  }

  const auto text = expected_text(test_case.ident, decoded_bytes);
  return canonical.x == expected_x && logic.x == expected_x &&
         canonical.y == expected_y && logic.y == expected_y &&
         canonical.dir == expected_dir && logic.dir == expected_dir &&
         canonical.target_actor_id == expected_target &&
         logic.target_actor_id == expected_target &&
         canonical.item_make_index == expected_item &&
         logic.item_make_index == expected_item &&
         canonical.item_slot == expected_slot && logic.item_slot == expected_slot &&
         canonical.amount == expected_amount && logic.amount == expected_amount &&
         canonical.text == text && logic.text == text;
}

bool check_case(const LegacyCommandCase& test_case) {
  std::string decoded_bytes;
  const auto body = make_case_body(test_case, &decoded_bytes);
  if (!body.has_value()) {
    return false;
  }
  const auto message =
      mir2::make_default_message(test_case.ident, test_case.recog, test_case.param,
                                 test_case.tag, test_case.series);
  const auto packet = mir2::make_legacy_game_packet(321, 4, 8, message, *body);
  const auto result = mir2::decode_legacy_game_command(321, packet);
  const auto kinds = expected_kinds(test_case.ident);
  if (!kinds.has_value()) {
    return result.status == mir2::CanonicalParseStatus::unsupported_ident &&
           !result.command.has_value() && same_message(result.game_message, message);
  }
  if (result.status != mir2::CanonicalParseStatus::ok || !result.command.has_value() ||
      !same_message(result.game_message, message)) {
    return false;
  }
  const auto logic = mir2::to_logic_command(*result.command);
  return check_expected_fields(test_case, *result.command, logic, packet, decoded_bytes);
}

bool check_cases(const std::vector<LegacyCommandCase>& cases) {
  if (cases.size() < 30) {
    return false;
  }
  bool saw_unsupported = false;
  bool saw_gameplay = false;
  for (const auto& test_case : cases) {
    if (!check_case(test_case)) {
      std::cerr << "case: " << test_case.name << '\n';
      return false;
    }
    if (expected_kinds(test_case.ident).has_value()) {
      saw_gameplay = true;
    } else {
      saw_unsupported = true;
    }
  }
  return saw_unsupported && saw_gameplay;
}

bool check_malformed_packet() {
  mir2::LegacyPacket packet;
  packet.body.assign({'x', 'y', 'z'});
  const auto result = mir2::decode_legacy_game_command(321, packet);
  return result.status == mir2::CanonicalParseStatus::malformed_packet &&
         !result.command.has_value();
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto golden_path =
      source_root / "tests" / "golden" / "protocol_phase1" / "legacy_command_cases.json";
  if (!check_cases(load_cases(golden_path))) {
    return fail("golden cases");
  }
  if (!check_malformed_packet()) {
    return fail("malformed packet");
  }
  return 0;
}
