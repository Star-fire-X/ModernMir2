#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/messages.hpp"
#include "protocol/canonical_legacy_command.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"

namespace {

struct BagCommandCase {
  std::string name{};
  std::uint16_t ident{0};
  std::int32_t recog{0};
  std::uint16_t param{0};
  std::uint16_t tag{0};
  std::uint16_t series{0};
  std::string body_encoding{};
  std::string body_hex{};
  std::int32_t expected_make_index{0};
  std::string expected_name{};
  std::int32_t expected_slot{-1};
  std::uint64_t expected_target_actor{0};
};

int fail(std::string_view stage) {
  std::cerr << "legacy_bag_protocol_baseline_smoke failed at " << stage << '\n';
  return 1;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

std::optional<BagCommandCase> parse_case_line(const std::string& line) {
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
  const auto expected_make_index = int_field(line, "expected_make_index");
  const auto expected_name = string_field(line, "expected_name");
  const auto expected_slot = int_field(line, "expected_slot");
  const auto expected_target_actor = int_field(line, "expected_target_actor");
  if (!ident.has_value() || !recog.has_value() || !param.has_value() || !tag.has_value() ||
      !series.has_value() || !body_encoding.has_value() || !body_hex.has_value() ||
      !expected_make_index.has_value() || !expected_name.has_value() ||
      !expected_slot.has_value() || !expected_target_actor.has_value()) {
    return std::nullopt;
  }

  BagCommandCase parsed;
  parsed.name = *name;
  parsed.ident = static_cast<std::uint16_t>(*ident);
  parsed.recog = static_cast<std::int32_t>(*recog);
  parsed.param = static_cast<std::uint16_t>(*param);
  parsed.tag = static_cast<std::uint16_t>(*tag);
  parsed.series = static_cast<std::uint16_t>(*series);
  parsed.body_encoding = *body_encoding;
  parsed.body_hex = *body_hex;
  parsed.expected_make_index = static_cast<std::int32_t>(*expected_make_index);
  parsed.expected_name = *expected_name;
  parsed.expected_slot = static_cast<std::int32_t>(*expected_slot);
  parsed.expected_target_actor = static_cast<std::uint64_t>(*expected_target_actor);
  return parsed;
}

std::vector<BagCommandCase> load_cases(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<BagCommandCase> cases;
  std::string line;
  while (std::getline(input, line)) {
    auto parsed = parse_case_line(line);
    if (parsed.has_value()) {
      cases.push_back(*parsed);
    }
  }
  return cases;
}

std::optional<std::string> make_case_body(const BagCommandCase& test_case,
                                          std::string* decoded_bytes) {
  const auto bytes = bytes_from_hex(test_case.body_hex);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  *decoded_bytes = *bytes;
  if (test_case.body_encoding == "legacy_string") {
    return mir2::legacy_encode_string(*bytes);
  }
  if (test_case.body_encoding == "none") {
    return std::string{};
  }
  if (test_case.body_encoding == "raw") {
    return *bytes;
  }
  return std::nullopt;
}

bool check_current_struct_baseline() {
  if (sizeof(mir2::LegacyUserItem) != 40 || mir2::kMaxBagItems != 46) {
    return false;
  }

  mir2::CharacterRecord character;
  if (character.bag_items.size() != mir2::kMaxBagItems) {
    return false;
  }

  mir2::LegacyUserItem original{};
  original.make_index = 10001;
  original.index = 2;
  original.dura = 300;
  original.dura_max = 500;
  original.desc[8] = 1;
  original.color_r = 2;
  original.color_g = 3;
  original.color_b = 4;
  original.prefix[0] = 'A';

  const auto encoded = mir2::legacy_encode_buffer(&original, sizeof(original));
  mir2::LegacyUserItem decoded{};
  if (!mir2::legacy_decode_buffer(encoded, &decoded, sizeof(decoded))) {
    return false;
  }
  return decoded.make_index == original.make_index && decoded.index == original.index &&
         decoded.dura == original.dura && decoded.dura_max == original.dura_max &&
         decoded.desc[8] == original.desc[8] && decoded.color_r == original.color_r &&
         decoded.color_g == original.color_g && decoded.color_b == original.color_b &&
         decoded.prefix[0] == original.prefix[0];
}

bool check_case_names_unique(const std::vector<BagCommandCase>& cases) {
  std::set<std::string> names;
  for (const auto& test_case : cases) {
    if (!names.insert(test_case.name).second) {
      std::cerr << "duplicate case name: " << test_case.name << '\n';
      return false;
    }
  }
  return true;
}

bool check_case(const BagCommandCase& test_case) {
  std::string decoded_bytes;
  const auto body = make_case_body(test_case, &decoded_bytes);
  if (!body.has_value()) {
    std::cerr << "bad body encoding for case: " << test_case.name << '\n';
    return false;
  }

  const auto message = mir2::make_default_message(test_case.ident, test_case.recog,
                                                  test_case.param, test_case.tag,
                                                  test_case.series);
  const auto packet = mir2::make_legacy_game_packet(321, 4, 8, message, *body);
  const auto result = mir2::decode_legacy_game_command(321, packet);
  if (result.status != mir2::CanonicalParseStatus::ok || !result.command.has_value()) {
    std::cerr << "decode failed for case: " << test_case.name << '\n';
    return false;
  }

  const auto logic = mir2::to_logic_command(*result.command);
  const auto& command = *result.command;
  if (command.item_make_index != test_case.expected_make_index ||
      logic.item_make_index != test_case.expected_make_index ||
      command.item_slot != test_case.expected_slot || logic.item_slot != test_case.expected_slot ||
      command.target_actor_id != test_case.expected_target_actor ||
      logic.target_actor_id != test_case.expected_target_actor ||
      command.text != test_case.expected_name || logic.text != test_case.expected_name) {
    std::cerr << "field mismatch for case: " << test_case.name << '\n';
    return false;
  }
  return true;
}

bool check_cases(const std::vector<BagCommandCase>& cases) {
  if (cases.size() != 9 || !check_case_names_unique(cases)) {
    return false;
  }
  bool saw_direct_make_index = false;
  bool saw_split_make_index = false;
  bool saw_slot = false;
  for (const auto& test_case : cases) {
    if (!check_case(test_case)) {
      return false;
    }
    saw_direct_make_index = saw_direct_make_index ||
                            test_case.ident == mir2::kCmDropItem ||
                            test_case.ident == mir2::kCmEat ||
                            test_case.ident == mir2::kCmDealAddItem ||
                            test_case.ident == mir2::kCmDealDelItem;
    saw_split_make_index = saw_split_make_index ||
                           test_case.ident == mir2::kCmUserSellItem ||
                           test_case.ident == mir2::kCmUserStorageItem ||
                           test_case.ident == mir2::kCmUserTakeBackStorageItem;
    saw_slot = saw_slot || test_case.ident == mir2::kCmTakeOnItem ||
               test_case.ident == mir2::kCmTakeOffItem;
  }
  return saw_direct_make_index && saw_split_make_index && saw_slot;
}

bool check_fixture_hygiene(const std::filesystem::path& golden_root) {
  const auto constants = read_text(golden_root / "bag_protocol_constants.json");
  const auto sequences = read_text(golden_root / "bag_sequence_cases.json");
  return constants.find("\"schema_version\": 1") != std::string::npos &&
         constants.find("\"TUserItem\": 40") != std::string::npos &&
         constants.find("\"TStdItem\": 76") != std::string::npos &&
         constants.find("\"TClientItem\": 84") != std::string::npos &&
         constants.find("\"MAXBAGITEM\": 46") != std::string::npos &&
         sequences.find("\"schema_version\": 1") != std::string::npos &&
         sequences.find("\"status\": \"pending_target\"") != std::string::npos &&
         sequences.find("take_on_uses_resolved_tlist_bagindex") != std::string::npos;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto golden_root = source_root / "tests" / "golden" / "bag_phase0";
  if (!check_fixture_hygiene(golden_root)) {
    return fail("fixture hygiene");
  }
  if (!check_current_struct_baseline()) {
    return fail("current struct baseline");
  }
  if (!check_cases(load_cases(golden_root / "bag_command_cases.json"))) {
    return fail("bag command cases");
  }
  return 0;
}
