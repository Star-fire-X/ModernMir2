#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_protocol.hpp"
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

int fail(std::string_view stage) {
  std::cerr << "legacy_protocol_command_golden_smoke failed at " << stage << '\n';
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

  LegacyCommandCase parsed;
  parsed.name = *name;
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

bool check_case_roundtrip(const LegacyCommandCase& test_case) {
  std::string decoded_text;
  const auto body = make_case_body(test_case, &decoded_text);
  if (!body.has_value()) {
    return false;
  }
  const auto message =
      mir2::make_default_message(test_case.ident, test_case.recog, test_case.param,
                                 test_case.tag, test_case.series);
  const auto packet = mir2::make_legacy_game_packet(123, 7, 9, message, *body);
  if (packet.header.socket_number != 123 || packet.header.user_gate_index != 7 ||
      packet.header.user_list_index != 9 || packet.header.ident != test_case.ident ||
      packet.header.length != static_cast<std::int32_t>(packet.body.size())) {
    return false;
  }

  const auto wire = mir2::LegacyProtocolCodec::encode(packet);
  if (wire.size() < 2 || wire.front() != static_cast<std::uint8_t>('#') ||
      wire.back() != static_cast<std::uint8_t>('!')) {
    return false;
  }

  std::vector<std::uint8_t> buffer = wire;
  const auto drained = mir2::LegacyProtocolCodec::drain_packets(buffer);
  if (!buffer.empty() || drained.size() != 1 ||
      drained.front().header.length != static_cast<std::int32_t>(packet.body.size()) ||
      drained.front().body != packet.body) {
    return false;
  }

  const auto decoded = mir2::decode_legacy_game_packet(drained.front());
  if (!decoded.has_value() || !same_message(decoded->message, message) ||
      decoded->body != *body) {
    return false;
  }
  const std::string payload(drained.front().body.begin(), drained.front().body.end());
  const auto decoded_prefix =
      mir2::legacy_decode_message(payload.substr(0, mir2::legacy_message_encoded_size()));
  if (!decoded_prefix.has_value() || !same_message(*decoded_prefix, message)) {
    return false;
  }
  if (test_case.body_encoding == "legacy_string" &&
      mir2::legacy_decode_string(decoded->body) != decoded_text) {
    return false;
  }
  return true;
}

bool check_golden_roundtrips(const std::vector<LegacyCommandCase>& cases) {
  if (cases.size() < 20) {
    return false;
  }
  for (const auto& test_case : cases) {
    if (!check_case_roundtrip(test_case)) {
      std::cerr << "case: " << test_case.name << '\n';
      return false;
    }
  }
  return true;
}

mir2::LegacyPacket packet_for_case(const LegacyCommandCase& test_case) {
  std::string decoded_text;
  const auto body = make_case_body(test_case, &decoded_text).value_or(std::string{});
  return mir2::make_legacy_game_packet(
      123, 7, 9,
      mir2::make_default_message(test_case.ident, test_case.recog, test_case.param,
                                 test_case.tag, test_case.series),
      body);
}

std::vector<std::uint8_t> checked_uplink_frame(const mir2::LegacyPacket& packet, char check_code) {
  auto frame = mir2::LegacyProtocolCodec::encode(packet);
  frame.insert(frame.begin() + 1, static_cast<std::uint8_t>(check_code));
  return frame;
}

bool check_frame_boundaries(const std::vector<LegacyCommandCase>& cases) {
  if (cases.size() < 3) {
    return false;
  }

  const auto first = mir2::LegacyProtocolCodec::encode(packet_for_case(cases[0]));
  const auto second = mir2::LegacyProtocolCodec::encode(packet_for_case(cases[1]));
  const auto third = mir2::LegacyProtocolCodec::encode(packet_for_case(cases[2]));

  std::vector<std::uint8_t> combined{'n', 'o', 'i', 's', 'e'};
  combined.insert(combined.end(), first.begin(), first.end());
  combined.insert(combined.end(), second.begin(), second.end());
  combined.insert(combined.end(), third.begin(), third.end());
  const auto packets = mir2::LegacyProtocolCodec::drain_packets(combined);
  if (!combined.empty() || packets.size() != 3) {
    return false;
  }
  for (std::size_t index = 0; index < packets.size(); ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(packets[index]);
    if (!decoded.has_value() || decoded->message.ident != cases[index].ident) {
      return false;
    }
  }

  std::vector<std::uint8_t> partial(first.begin(), first.begin() + first.size() / 2);
  const auto no_packets = mir2::LegacyProtocolCodec::drain_packets(partial);
  if (!no_packets.empty() || partial.size() != first.size() / 2) {
    return false;
  }
  partial.insert(partial.end(), first.begin() + static_cast<std::ptrdiff_t>(first.size() / 2),
                 first.end());
  const auto completed = mir2::LegacyProtocolCodec::drain_packets(partial);
  if (!partial.empty() || completed.size() != 1) {
    return false;
  }

  std::vector<std::uint8_t> empty_frame{'#', '!'};
  const auto skipped = mir2::LegacyProtocolCodec::drain_packets(empty_frame);
  return skipped.empty() && empty_frame.empty();
}

bool check_rungate_uplink_downstream_paths(const std::vector<LegacyCommandCase>& cases) {
  if (cases.size() < 3) {
    return false;
  }

  const auto first_packet = packet_for_case(cases[0]);
  const auto first_uplink = checked_uplink_frame(first_packet, '7');
  std::vector<std::uint8_t> single_buffer = first_uplink;
  const auto single = mir2::LegacyProtocolCodec::drain_packets(single_buffer);
  if (!single_buffer.empty() || single.size() != 1 || single.front().body != first_packet.body) {
    return false;
  }
  const auto single_decoded = mir2::decode_legacy_game_packet(single.front());
  if (!single_decoded.has_value() || single_decoded->message.ident != cases[0].ident ||
      single_decoded->message.recog != cases[0].recog ||
      single_decoded->message.param != cases[0].param ||
      single_decoded->message.tag != cases[0].tag ||
      single_decoded->message.series != cases[0].series) {
    return false;
  }

  const auto downstream = mir2::LegacyProtocolCodec::encode(first_packet);
  if (downstream.size() < 2 || downstream.front() != static_cast<std::uint8_t>('#') ||
      downstream.back() != static_cast<std::uint8_t>('!') ||
      std::isdigit(static_cast<unsigned char>(downstream[1])) != 0) {
    return false;
  }
  const std::vector<std::uint8_t> downstream_payload(downstream.begin() + 1,
                                                     downstream.end() - 1);
  if (downstream_payload != first_packet.body) {
    return false;
  }

  std::vector<std::uint8_t> combined{'n', 'o', 'i', 's', 'e'};
  for (std::size_t index = 0; index < 3; ++index) {
    const auto frame =
        checked_uplink_frame(packet_for_case(cases[index]), static_cast<char>('1' + index));
    combined.insert(combined.end(), frame.begin(), frame.end());
  }
  const auto packets = mir2::LegacyProtocolCodec::drain_packets(combined);
  if (!combined.empty() || packets.size() != 3) {
    return false;
  }
  for (std::size_t index = 0; index < packets.size(); ++index) {
    const auto decoded = mir2::decode_legacy_game_packet(packets[index]);
    if (!decoded.has_value() || decoded->message.ident != cases[index].ident ||
        packets[index].body != packet_for_case(cases[index]).body) {
      return false;
    }
  }

  std::vector<std::uint8_t> partial(first_uplink.begin(),
                                    first_uplink.begin() +
                                        static_cast<std::ptrdiff_t>(first_uplink.size() / 2));
  const auto empty = mir2::LegacyProtocolCodec::drain_packets(partial);
  if (!empty.empty() || partial.size() != first_uplink.size() / 2) {
    return false;
  }
  partial.insert(partial.end(),
                 first_uplink.begin() + static_cast<std::ptrdiff_t>(first_uplink.size() / 2),
                 first_uplink.end());
  const auto completed = mir2::LegacyProtocolCodec::drain_packets(partial);
  if (!partial.empty() || completed.size() != 1 || completed.front().body != first_packet.body) {
    return false;
  }

  return true;
}

bool check_edcode_byte_semantics() {
  std::vector<std::string> samples;
  samples.emplace_back("");
  samples.emplace_back("alice/secret");
  samples.emplace_back("hello world");
  samples.emplace_back("say\twith\ttabs");
  samples.emplace_back("Slash/Space Tab\t");
  samples.push_back(std::string{static_cast<char>(0xB0), static_cast<char>(0xA1),
                                static_cast<char>(0xC4), static_cast<char>(0xE3)});
  samples.push_back(std::string{static_cast<char>(0x81), static_cast<char>(0xFE),
                                static_cast<char>(0xA1), static_cast<char>(0x40)});

  for (const auto& sample : samples) {
    if (mir2::legacy_decode_string(mir2::legacy_encode_string(sample)) != sample) {
      return false;
    }
  }

  const std::array<std::uint8_t, 8> raw{{0x00, 0x01, 0xB0, 0x00, 0xFF, 0x7F, 0x3C, 0x21}};
  std::array<std::uint8_t, raw.size()> decoded{};
  const auto encoded = mir2::legacy_encode_buffer(raw.data(), raw.size());
  if (!mir2::legacy_decode_buffer(encoded, decoded.data(), decoded.size())) {
    return false;
  }
  return decoded == raw;
}

bool check_decode_rejects_short_payload() {
  mir2::LegacyPacket packet;
  packet.body.assign({'a', 'b', 'c'});
  return !mir2::decode_legacy_game_packet(packet).has_value() &&
         !mir2::legacy_decode_message("abc").has_value();
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto golden_path =
      source_root / "tests" / "golden" / "protocol_phase1" / "legacy_command_cases.json";
  const auto cases = load_cases(golden_path);
  if (!check_golden_roundtrips(cases)) {
    return fail("golden roundtrips");
  }
  if (!check_frame_boundaries(cases)) {
    return fail("frame boundaries");
  }
  if (!check_rungate_uplink_downstream_paths(cases)) {
    return fail("rungate uplink downstream paths");
  }
  if (!check_edcode_byte_semantics()) {
    return fail("edcode byte semantics");
  }
  if (!check_decode_rejects_short_payload()) {
    return fail("short payload rejection");
  }
  return 0;
}
