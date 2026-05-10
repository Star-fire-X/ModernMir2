#include <iostream>
#include <string>
#include <string_view>

#include "protocol/canonical_legacy_command.hpp"
#include "protocol/client_v1_legacy_command_decoder.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_string.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_string_semantics_smoke failed at " << stage << '\n';
  return 1;
}

std::string bytes(std::initializer_list<unsigned char> values) {
  std::string output;
  output.reserve(values.size());
  for (const auto value : values) {
    output.push_back(static_cast<char>(value));
  }
  return output;
}

bool check_core_bytes() {
  const auto gbk = bytes({0xB0, 0xA1});
  const auto utf8 = bytes({0xE4, 0xB8, 0xAD});
  const mir2::LegacyString legacy{gbk + "/name\twith tab"};
  if (legacy.byte_size() != 16 || legacy.bytes().substr(0, 2) != gbk ||
      mir2::legacy_debug_hex(legacy.view()).substr(0, 4) != "b0a1") {
    return false;
  }

  const auto fields = mir2::split_legacy_fields(mir2::LegacyStringView{"a//b/"}, '/');
  if (fields.size() != 4 || fields[0].bytes() != "a" || !fields[1].empty() ||
      fields[2].bytes() != "b" || !fields[3].empty()) {
    return false;
  }

  const mir2::LegacyStringView utf8_view{utf8};
  return utf8_view.byte_size() == 3 && mir2::copy_legacy_bytes(utf8_view.bytes()) == utf8;
}

bool check_validation() {
  const auto gbk = bytes({0xB0, 0xA1});
  const auto utf8 = bytes({0xE4, 0xB8, 0xAD});

  if (!mir2::is_valid_legacy_account_id("alpha") ||
      !mir2::is_valid_legacy_account_id(gbk) ||
      mir2::is_valid_legacy_account_id("") ||
      mir2::is_valid_legacy_account_id("has space") ||
      mir2::is_valid_legacy_account_id("slash/name") ||
      mir2::is_valid_legacy_account_id(utf8)) {
    return false;
  }

  return mir2::is_valid_legacy_character_name("Abc") &&
         mir2::is_valid_legacy_character_name("abcdefghijklmn") &&
         !mir2::is_valid_legacy_character_name("Ab") &&
         !mir2::is_valid_legacy_character_name("abcdefghijklmnop") &&
         !mir2::is_valid_legacy_character_name("Name-With-Dash") &&
         !mir2::is_valid_legacy_character_name(gbk) &&
         !mir2::is_valid_legacy_character_name(utf8);
}

bool check_codec_and_canonical_text() {
  const auto gbk_chat = bytes({0xB0, 0xA1}) + std::string{" chat"};
  if (mir2::legacy_decode_string(mir2::legacy_encode_string(gbk_chat)) != gbk_chat) {
    return false;
  }

  const auto legacy_packet = mir2::make_legacy_game_packet(
      55, 0, 0, mir2::make_default_message(mir2::kCmSay, 0, 0, 0, 0),
      mir2::legacy_encode_string(gbk_chat));
  const auto legacy = mir2::decode_legacy_game_command(55, legacy_packet);
  if (legacy.status != mir2::CanonicalParseStatus::ok || !legacy.command.has_value() ||
      legacy.command->text != gbk_chat ||
      legacy.command->source_protocol != mir2::CanonicalSourceProtocol::legacy_framed) {
    return false;
  }

  const auto client = mir2::decode_client_v1_chat_command(
      55, mir2::client_v1::ChatSend{gbk_chat});
  if (client.text != gbk_chat ||
      client.source_protocol != mir2::CanonicalSourceProtocol::client_v1) {
    return false;
  }

  const auto selection = bytes({0xB0, 0xA1}) + std::string{"@buy"};
  const auto npc = mir2::decode_client_v1_npc_dialog_select_command(55, 77, selection);
  return npc.text == selection;
}

}  // namespace

int main() {
  if (!check_core_bytes()) {
    return fail("core bytes");
  }
  if (!check_validation()) {
    return fail("validation");
  }
  if (!check_codec_and_canonical_text()) {
    return fail("codec canonical text");
  }
  return 0;
}
