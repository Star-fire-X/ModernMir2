#include "storage/repository.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "spdlog/spdlog.h"
#include "sqlite3.h"

namespace mir2 {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

void exec_or_throw(sqlite3* database, const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error != nullptr ? error : "sqlite_exec failed";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

bool table_has_column(sqlite3* database, const std::string& table_name, const std::string& column_name) {
  sqlite3_stmt* statement = nullptr;
  const auto sql = "PRAGMA table_info(" + table_name + ");";
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }

  bool found = false;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    if (text != nullptr && column_name == text) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(statement);
  return found;
}

void ensure_column(sqlite3* database, const std::string& table_name, const std::string& column_name,
                   const std::string& alter_sql) {
  if (!table_has_column(database, table_name, column_name)) {
    exec_or_throw(database, alter_sql);
  }
}

void finalize_statement(sqlite3_stmt* statement) {
  if (statement != nullptr) {
    sqlite3_finalize(statement);
  }
}

void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
  sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

template <typename T, std::size_t N>
void bind_blob_array(sqlite3_stmt* statement, int index, const std::array<T, N>& values) {
  sqlite3_bind_blob(statement, index, values.data(), static_cast<int>(sizeof(values)),
                    SQLITE_TRANSIENT);
}

template <typename T, std::size_t N>
void read_blob_array(sqlite3_stmt* statement, int column, std::array<T, N>& values) {
  values.fill(T{});
  const auto* blob = sqlite3_column_blob(statement, column);
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (blob == nullptr || bytes <= 0) {
    return;
  }
  if (static_cast<std::size_t>(bytes) != sizeof(values)) {
    spdlog::warn("Legacy blob column {} has {} bytes; expected {} bytes", column, bytes,
                 sizeof(values));
  }
  std::memcpy(values.data(), blob,
              std::min<std::size_t>(static_cast<std::size_t>(bytes), sizeof(values)));
}

void bind_blob_vector(sqlite3_stmt* statement, int index, const std::vector<std::uint8_t>& values) {
  sqlite3_bind_blob(statement, index, values.empty() ? nullptr : values.data(),
                    static_cast<int>(values.size()), SQLITE_TRANSIENT);
}

std::vector<std::uint8_t> encode_merchant_goods_blob(const std::vector<LegacyUserItem>& goods) {
  std::vector<std::uint8_t> blob;
  blob.resize(goods.size() * sizeof(LegacyUserItem));
  if (!goods.empty()) {
    std::memcpy(blob.data(), goods.data(), blob.size());
  }
  return blob;
}

std::vector<LegacyUserItem> decode_merchant_goods_blob(sqlite3_stmt* statement, int column) {
  std::vector<LegacyUserItem> goods;
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 0) {
    return goods;
  }
  if (static_cast<std::size_t>(bytes) % sizeof(LegacyUserItem) != 0) {
    spdlog::warn("Merchant goods blob column {} has {} bytes; item size is {}", column, bytes,
                 sizeof(LegacyUserItem));
  }
  const auto count = static_cast<std::size_t>(bytes) / sizeof(LegacyUserItem);
  goods.resize(count);
  if (!goods.empty()) {
    std::memcpy(goods.data(), raw, goods.size() * sizeof(LegacyUserItem));
  }
  return goods;
}

void write_i32_le(std::vector<std::uint8_t>& out, std::int32_t value) {
  auto bits = static_cast<std::uint32_t>(value);
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xffu));
  }
}

std::int32_t read_i32_le(const std::uint8_t* data) {
  const auto bits = static_cast<std::uint32_t>(data[0]) |
                    (static_cast<std::uint32_t>(data[1]) << 8) |
                    (static_cast<std::uint32_t>(data[2]) << 16) |
                    (static_cast<std::uint32_t>(data[3]) << 24);
  return static_cast<std::int32_t>(bits);
}

void write_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

std::uint64_t read_u64_le(const std::uint8_t* data) {
  std::uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(data[shift / 8]) << shift;
  }
  return value;
}

std::vector<std::uint8_t> encode_weapon_upgrade_blob(
    const std::vector<LegacyWeaponUpgradeRecord>& records) {
  constexpr std::size_t kNameBytes = 32;
  std::vector<std::uint8_t> blob;
  write_i32_le(blob, static_cast<std::int32_t>(records.size()));
  for (const auto& record : records) {
    std::array<std::uint8_t, kNameBytes> name{};
    const auto copy_size = std::min(kNameBytes, record.character_name.size());
    std::memcpy(name.data(), record.character_name.data(), copy_size);
    blob.insert(blob.end(), name.begin(), name.end());
    const auto* item_bytes = reinterpret_cast<const std::uint8_t*>(&record.item);
    blob.insert(blob.end(), item_bytes, item_bytes + sizeof(LegacyUserItem));
    blob.push_back(record.updc);
    blob.push_back(record.upsc);
    blob.push_back(record.upmc);
    blob.push_back(record.durapoint);
    write_u64_le(blob, record.ready_time_ms);
  }
  return blob;
}

std::vector<LegacyWeaponUpgradeRecord> decode_weapon_upgrade_blob(sqlite3_stmt* statement,
                                                                  int column) {
  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kRecordBytes = kNameBytes + sizeof(LegacyUserItem) + 4 + 8;
  std::vector<LegacyWeaponUpgradeRecord> records;
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 4) {
    return records;
  }
  const auto available = static_cast<std::size_t>(bytes);
  const auto count = std::max(read_i32_le(raw), 0);
  std::size_t offset = 4;
  records.reserve(static_cast<std::size_t>(count));
  for (std::int32_t index = 0; index < count; ++index) {
    if (available < offset + kRecordBytes) {
      spdlog::warn("Merchant upgrade blob column {} ended early at record {}", column, index);
      break;
    }
    LegacyWeaponUpgradeRecord record;
    const auto* name = raw + offset;
    const auto name_len = std::find(name, name + kNameBytes, std::uint8_t{0}) - name;
    record.character_name.assign(reinterpret_cast<const char*>(name), name_len);
    offset += kNameBytes;
    std::memcpy(&record.item, raw + offset, sizeof(LegacyUserItem));
    offset += sizeof(LegacyUserItem);
    record.updc = raw[offset++];
    record.upsc = raw[offset++];
    record.upmc = raw[offset++];
    record.durapoint = raw[offset++];
    static_cast<void>(read_u64_le(raw + offset));
    offset += 8;
    record.ready_time_ms = 0;
    records.push_back(record);
  }
  return records;
}

std::vector<std::uint8_t> encode_slave_blob(
    const std::array<CharacterSlaveRecord, kMaxLegacySlaves>& slaves) {
  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kSlotBytes = kNameBytes + 4 + 1 + 1 + 4 + 4 + 4;
  std::vector<std::uint8_t> blob;
  blob.reserve(kMaxLegacySlaves * kSlotBytes);
  for (const auto& slave : slaves) {
    std::array<std::uint8_t, kNameBytes> name{};
    const auto copy_size = std::min(kNameBytes, slave.name.size());
    std::memcpy(name.data(), slave.name.data(), copy_size);
    blob.insert(blob.end(), name.begin(), name.end());
    write_i32_le(blob, slave.slave_exp);
    blob.push_back(slave.slave_exp_level);
    blob.push_back(slave.slave_make_level);
    write_i32_le(blob, slave.remain_royalty_sec);
    write_i32_le(blob, slave.hp);
    write_i32_le(blob, slave.mp);
  }
  return blob;
}

void decode_slave_blob(sqlite3_stmt* statement, int column,
                       std::array<CharacterSlaveRecord, kMaxLegacySlaves>& slaves) {
  slaves.fill(CharacterSlaveRecord{});
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 0) {
    return;
  }

  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kSlotBytes = kNameBytes + 4 + 1 + 1 + 4 + 4 + 4;
  const auto available = static_cast<std::size_t>(bytes);
  for (std::size_t index = 0; index < kMaxLegacySlaves; ++index) {
    const auto offset = index * kSlotBytes;
    if (available < offset + kSlotBytes) {
      break;
    }
    const auto* slot = raw + offset;
    auto& slave = slaves[index];
    const auto name_len =
        std::find(slot, slot + kNameBytes, std::uint8_t{0}) - slot;
    slave.name.assign(reinterpret_cast<const char*>(slot), name_len);
    slave.slave_exp = read_i32_le(slot + kNameBytes);
    slave.slave_exp_level = *(slot + kNameBytes + 4);
    slave.slave_make_level = *(slot + kNameBytes + 5);
    slave.remain_royalty_sec = read_i32_le(slot + kNameBytes + 6);
    slave.hp = read_i32_le(slot + kNameBytes + 10);
    slave.mp = read_i32_le(slot + kNameBytes + 14);
  }
}

std::string column_text(sqlite3_stmt* statement, int column) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
  return text != nullptr ? std::string(text) : std::string{};
}

std::int64_t elapsed_ms(std::int64_t now_ms, std::int64_t then_ms) {
  return now_ms >= then_ms ? now_ms - then_ms : 0;
}

std::string lower_trim_copy(std::string_view value) {
  std::string text(value);
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(),
                          [&](unsigned char ch) { return !is_space(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](unsigned char ch) { return !is_space(ch); })
                 .base(),
             text.end());
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_legacy_unclaimed(std::string_view value) {
  const auto lowered = lower_trim_copy(value);
  return lowered.empty() || lowered == "none" || lowered == "unclaimed" || lowered == "-";
}

void skip_json_ws(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

std::optional<std::size_t> find_json_value_start(std::string_view json, std::string_view key) {
  const auto marker = "\"" + std::string(key) + "\"";
  auto pos = json.find(marker);
  while (pos != std::string_view::npos) {
    pos += marker.size();
    skip_json_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
      pos = json.find(marker, pos);
      continue;
    }
    ++pos;
    skip_json_ws(json, pos);
    return pos;
  }
  return std::nullopt;
}

std::optional<std::string> json_string_field(std::string_view json, std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escape = false;
  for (std::size_t pos = *value_pos + 1; pos < json.size(); ++pos) {
    const auto ch = json[pos];
    if (escape) {
      switch (ch) {
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
      escape = false;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return std::nullopt;
}

std::optional<std::int32_t> json_int_field(std::string_view json, std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size()) {
    return std::nullopt;
  }

  auto pos = *value_pos;
  const auto begin = pos;
  if (json[pos] == '-' || json[pos] == '+') {
    ++pos;
  }
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos == begin || (pos == begin + 1 && (json[begin] == '-' || json[begin] == '+'))) {
    return std::nullopt;
  }

  try {
    return std::stoi(std::string(json.substr(begin, pos - begin)));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>> json_string_array_field(std::string_view json,
                                                                std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '[') {
    return std::nullopt;
  }

  std::vector<std::string> values;
  auto pos = *value_pos + 1;
  while (pos < json.size()) {
    skip_json_ws(json, pos);
    if (pos >= json.size()) {
      break;
    }
    if (json[pos] == ']') {
      return values;
    }
    if (json[pos] != '"') {
      return std::nullopt;
    }

    bool escape = false;
    std::string value;
    ++pos;
    for (; pos < json.size(); ++pos) {
      const auto ch = json[pos];
      if (escape) {
        value.push_back(ch);
        escape = false;
        continue;
      }
      if (ch == '\\') {
        escape = true;
        continue;
      }
      if (ch == '"') {
        ++pos;
        break;
      }
      value.push_back(ch);
    }
    values.push_back(std::move(value));
    skip_json_ws(json, pos);
    if (pos < json.size() && json[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < json.size() && json[pos] == ']') {
      return values;
    }
  }

  return std::nullopt;
}

std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      joined += separator;
    }
    joined += values[index];
  }
  return joined;
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const auto ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string trim_copy(std::string value) {
  auto first = value.begin();
  while (first != value.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) {
    ++first;
  }
  auto last = value.end();
  while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
    --last;
  }
  return std::string(first, last);
}

void push_unique_member(std::vector<std::string>& members, const std::string& member_name) {
  if (member_name.empty()) {
    return;
  }
  const auto lowered = trim_copy(member_name);
  if (lowered.empty()) {
    return;
  }
  const auto already_present =
      std::any_of(members.begin(), members.end(), [&](const std::string& existing) {
        return trim_copy(existing) == lowered;
      });
  if (!already_present) {
    members.push_back(lowered);
  }
}

std::vector<std::string> split_csv_members(std::string_view text) {
  std::vector<std::string> members;
  std::string current;
  for (const auto ch : text) {
    if (ch == ',') {
      push_unique_member(members, current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  push_unique_member(members, current);
  return members;
}

GuildState read_guild_state_row(sqlite3_stmt* statement) {
  GuildState guild_state;
  guild_state.guild_name = column_text(statement, 0);
  const auto payload = column_text(statement, 1);
  if (const auto value = json_string_field(payload, "lord"); value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  } else if (const auto value = json_string_field(payload, "chief");
             value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  } else if (const auto value = json_string_field(payload, "master");
             value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  }

  if (const auto members = json_string_array_field(payload, "members");
      members.has_value() && !members->empty()) {
    for (const auto& member : *members) {
      push_unique_member(guild_state.members, member);
    }
  } else if (const auto members = json_string_field(payload, "members");
             members.has_value() && !members->empty()) {
    for (const auto& member : split_csv_members(*members)) {
      push_unique_member(guild_state.members, member);
    }
  }

  if (const auto applicants = json_string_array_field(payload, "applicants");
      applicants.has_value() && !applicants->empty()) {
    for (const auto& applicant : *applicants) {
      push_unique_member(guild_state.applicants, applicant);
    }
  } else if (const auto applicants = json_string_field(payload, "applicants");
             applicants.has_value() && !applicants->empty()) {
    for (const auto& applicant : split_csv_members(*applicants)) {
      push_unique_member(guild_state.applicants, applicant);
    }
  }

  if (!guild_state.lord.empty()) {
    push_unique_member(guild_state.members, guild_state.lord);
    std::rotate(guild_state.members.begin(),
                std::find(guild_state.members.begin(), guild_state.members.end(), guild_state.lord),
                guild_state.members.end());
  }
  guild_state.applicants.erase(
      std::remove_if(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return std::any_of(guild_state.members.begin(), guild_state.members.end(),
                                          [&](const std::string& member) {
                                            return trim_copy(member) == trim_copy(applicant);
                                          });
                     }),
      guild_state.applicants.end());
  return guild_state;
}

std::string build_guild_payload_json(const GuildState& guild_state) {
  std::ostringstream payload;
  payload << "{\"lord\":\"" << json_escape(guild_state.lord) << "\",\"members\":[";
  for (std::size_t index = 0; index < guild_state.members.size(); ++index) {
    if (index > 0) {
      payload << ',';
    }
    payload << '"' << json_escape(guild_state.members[index]) << '"';
  }
  payload << "],\"applicants\":[";
  for (std::size_t index = 0; index < guild_state.applicants.size(); ++index) {
    if (index > 0) {
      payload << ',';
    }
    payload << '"' << json_escape(guild_state.applicants[index]) << '"';
  }
  payload << "]}";
  return payload.str();
}

AccountRecord read_account_row(sqlite3_stmt* statement) {
  AccountRecord record;
  record.account_id = column_text(statement, 0);
  record.password = column_text(statement, 1);
  record.display_name = column_text(statement, 2);
  record.user_name = column_text(statement, 3);
  record.ss_no = column_text(statement, 4);
  record.phone = column_text(statement, 5);
  record.quiz = column_text(statement, 6);
  record.answer = column_text(statement, 7);
  record.email = column_text(statement, 8);
  record.quiz2 = column_text(statement, 9);
  record.answer2 = column_text(statement, 10);
  record.birthday = column_text(statement, 11);
  record.mobile_phone = column_text(statement, 12);
  record.memo1 = column_text(statement, 13);
  record.memo2 = column_text(statement, 14);
  record.server_index = sqlite3_column_int(statement, 15);
  record.passwd_fail = sqlite3_column_int(statement, 16);
  record.passwd_fail_time_ms = sqlite3_column_int64(statement, 17);
  record.banned = sqlite3_column_int(statement, 18) != 0;
  return record;
}

CharacterRecord read_character_row(sqlite3_stmt* statement) {
  CharacterRecord record;
  record.account_id = column_text(statement, 0);
  record.character_name = column_text(statement, 1);
  record.map_id = column_text(statement, 2);
  record.x = sqlite3_column_int(statement, 3);
  record.y = sqlite3_column_int(statement, 4);
  record.dir = static_cast<std::uint8_t>(sqlite3_column_int(statement, 5));
  record.light = static_cast<std::uint8_t>(sqlite3_column_int(statement, 6));
  record.job = static_cast<std::uint8_t>(sqlite3_column_int(statement, 7));
  record.sex = static_cast<std::uint8_t>(sqlite3_column_int(statement, 8));
  record.hair = static_cast<std::uint8_t>(sqlite3_column_int(statement, 9));
  record.gold = sqlite3_column_int(statement, 10);
  record.feature = sqlite3_column_int(statement, 11);
  record.status = sqlite3_column_int(statement, 12);
  record.ability.level = static_cast<std::uint8_t>(sqlite3_column_int(statement, 13));
  record.ability.hp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 14));
  record.ability.mp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 15));
  record.ability.max_hp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 16));
  record.ability.max_mp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 17));
  record.ability.ac = static_cast<std::uint16_t>(sqlite3_column_int(statement, 18));
  record.ability.mac = static_cast<std::uint16_t>(sqlite3_column_int(statement, 19));
  record.ability.dc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 20));
  record.ability.mc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 21));
  record.ability.sc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 22));
  record.ability.exp = static_cast<std::uint32_t>(sqlite3_column_int(statement, 23));
  record.ability.max_exp = static_cast<std::uint32_t>(sqlite3_column_int(statement, 24));
  record.ability.weight = static_cast<std::uint16_t>(sqlite3_column_int(statement, 25));
  record.ability.max_weight = static_cast<std::uint16_t>(sqlite3_column_int(statement, 26));
  record.ability.wear_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 27));
  record.ability.max_wear_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 28));
  record.ability.hand_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 29));
  record.ability.max_hand_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 30));
  read_blob_array(statement, 31, record.equipped_items);
  read_blob_array(statement, 32, record.bag_items);
  read_blob_array(statement, 33, record.storage_items);
  read_blob_array(statement, 34, record.magics);
  record.guild_name = column_text(statement, 35);
  record.guild_title = column_text(statement, 36);
  record.attack_mode = static_cast<std::uint8_t>(std::clamp(sqlite3_column_int(statement, 37), 0, 255));
  record.pk_point = sqlite3_column_int(statement, 38);
  record.death_time_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(0, sqlite3_column_int64(statement, 39)));
  read_blob_array(statement, 40, record.quest_marks);
  read_blob_array(statement, 41, record.quest_open_units);
  read_blob_array(statement, 42, record.quest_units);
  read_blob_array(statement, 43, record.script_params);
  record.daily_quest = static_cast<std::uint32_t>(std::max<std::int64_t>(0, sqlite3_column_int64(statement, 44)));
  decode_slave_blob(statement, 45, record.slaves);
  record.body_luck = sqlite3_column_double(statement, 46);
  record.birth_items_granted = sqlite3_column_int(statement, 47) != 0;
  return record;
}

CharacterRecord make_default_character(const std::string& account_id,
                                       const std::string& character_name) {
  CharacterRecord record;
  record.account_id = account_id;
  record.character_name = character_name;
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.dir = 0;
  record.light = 0;
  record.job = 0;
  record.sex = 0;
  record.hair = 0;
  record.gold = 0;
  record.feature = 0;
  record.status = 0;

  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.ac = 0;
  record.ability.mac = 0;
  record.ability.dc = make_word(1, 2);
  record.ability.mc = make_word(1, 2);
  record.ability.sc = make_word(1, 2);
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;

  record.equipped_items[1].make_index = 100001;
  record.equipped_items[1].index = 1;
  record.equipped_items[1].dura = 1000;
  record.equipped_items[1].dura_max = 1000;

  record.bag_items[0].make_index = 100002;
  record.bag_items[0].index = 2;
  record.bag_items[0].dura = 1000;
  record.bag_items[0].dura_max = 1000;

  record.magics[0].magic_id = 1;
  record.magics[0].level = 1;
  record.magics[0].key = 'F';
  record.magics[0].cur_train = 0;
  record.birth_items_granted = true;
  return record;
}

void bind_character_fields(sqlite3_stmt* statement, const CharacterRecord& character) {
  bind_text(statement, 1, character.account_id);
  bind_text(statement, 2, character.character_name);
  bind_text(statement, 3, character.map_id);
  sqlite3_bind_int(statement, 4, character.x);
  sqlite3_bind_int(statement, 5, character.y);
  sqlite3_bind_int(statement, 6, character.dir);
  sqlite3_bind_int(statement, 7, character.light);
  sqlite3_bind_int(statement, 8, character.job);
  sqlite3_bind_int(statement, 9, character.sex);
  sqlite3_bind_int(statement, 10, character.hair);
  sqlite3_bind_int(statement, 11, character.gold);
  sqlite3_bind_int(statement, 12, character.feature);
  sqlite3_bind_int(statement, 13, character.status);
  sqlite3_bind_int(statement, 14, character.ability.level);
  sqlite3_bind_int(statement, 15, character.ability.hp);
  sqlite3_bind_int(statement, 16, character.ability.mp);
  sqlite3_bind_int(statement, 17, character.ability.max_hp);
  sqlite3_bind_int(statement, 18, character.ability.max_mp);
  sqlite3_bind_int(statement, 19, character.ability.ac);
  sqlite3_bind_int(statement, 20, character.ability.mac);
  sqlite3_bind_int(statement, 21, character.ability.dc);
  sqlite3_bind_int(statement, 22, character.ability.mc);
  sqlite3_bind_int(statement, 23, character.ability.sc);
  sqlite3_bind_int(statement, 24, static_cast<int>(character.ability.exp));
  sqlite3_bind_int(statement, 25, static_cast<int>(character.ability.max_exp));
  sqlite3_bind_int(statement, 26, character.ability.weight);
  sqlite3_bind_int(statement, 27, character.ability.max_weight);
  sqlite3_bind_int(statement, 28, character.ability.wear_weight);
  sqlite3_bind_int(statement, 29, character.ability.max_wear_weight);
  sqlite3_bind_int(statement, 30, character.ability.hand_weight);
  sqlite3_bind_int(statement, 31, character.ability.max_hand_weight);
  bind_blob_array(statement, 32, character.equipped_items);
  bind_blob_array(statement, 33, character.bag_items);
  bind_blob_array(statement, 34, character.storage_items);
  bind_blob_array(statement, 35, character.magics);
  bind_text(statement, 36, character.guild_name);
  bind_text(statement, 37, character.guild_title);
  sqlite3_bind_int(statement, 38, character.attack_mode);
  sqlite3_bind_int(statement, 39, character.pk_point);
  sqlite3_bind_int64(statement, 40, static_cast<sqlite3_int64>(character.death_time_ms));
  bind_blob_array(statement, 41, character.quest_marks);
  bind_blob_array(statement, 42, character.quest_open_units);
  bind_blob_array(statement, 43, character.quest_units);
  bind_blob_array(statement, 44, character.script_params);
  sqlite3_bind_int64(statement, 45, static_cast<sqlite3_int64>(character.daily_quest));
  const auto slave_blob = encode_slave_blob(character.slaves);
  bind_blob_vector(statement, 46, slave_blob);
  sqlite3_bind_double(statement, 47, character.body_luck);
  sqlite3_bind_int(statement, 48, character.birth_items_granted ? 1 : 0);
}

AccountRecord make_default_account(const std::string& account_id, const std::string& password) {
  AccountRecord record;
  record.account_id = account_id;
  record.password = password;
  record.display_name = "Guest";
  record.user_name = "Guest";
  record.ss_no = "000000-0000000";
  record.phone = "000-0000";
  record.quiz = "seed";
  record.answer = "seed";
  record.email = "guest@example.invalid";
  record.quiz2 = "seed2";
  record.answer2 = "seed2";
  record.birthday = "2000/01/01";
  record.mobile_phone = "000-0000-0000";
  record.memo1 = "seed";
  record.memo2 = "seed";
  return record;
}

void ensure_account(sqlite3* database, const std::string& account_id) {
  static constexpr const char* kSql =
      "INSERT OR IGNORE INTO accounts(account_id, display_name) VALUES(?1, ?2);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare ensure_account statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, account_id);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute ensure_account statement.");
  }
  finalize_statement(statement);
}

void save_account(sqlite3* database, const AccountRecord& account) {
  static constexpr const char* kSql =
      "INSERT INTO accounts(account_id, password, display_name, user_name, ss_no, phone, quiz,"
      " answer, email, quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index,"
      " passwd_fail, passwd_fail_time_ms, is_banned)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19)"
      " ON CONFLICT(account_id) DO UPDATE SET password = excluded.password,"
      " display_name = excluded.display_name, user_name = excluded.user_name, ss_no = excluded.ss_no,"
      " phone = excluded.phone, quiz = excluded.quiz, answer = excluded.answer, email = excluded.email,"
      " quiz2 = excluded.quiz2, answer2 = excluded.answer2, birthday = excluded.birthday,"
      " mobile_phone = excluded.mobile_phone, memo1 = excluded.memo1, memo2 = excluded.memo2,"
      " server_index = excluded.server_index, passwd_fail = excluded.passwd_fail,"
      " passwd_fail_time_ms = excluded.passwd_fail_time_ms, is_banned = excluded.is_banned,"
      " updated_at = CURRENT_TIMESTAMP;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);
  sqlite3_bind_int(statement, 16, account.server_index);
  sqlite3_bind_int(statement, 17, account.passwd_fail);
  sqlite3_bind_int64(statement, 18, account.passwd_fail_time_ms);
  sqlite3_bind_int(statement, 19, account.banned ? 1 : 0);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_account statement.");
  }
  finalize_statement(statement);
}

void migrate_legacy_characters_table(sqlite3* database, const std::string& schema_sql) {
  if (!table_has_column(database, "characters", "inventory_json") ||
      table_has_column(database, "characters", "equipped_blob")) {
    return;
  }

  const auto equip_blob_size =
      std::to_string(sizeof(std::array<LegacyUserItem, kMaxEquipSlots>));
  const auto bag_blob_size = std::to_string(sizeof(std::array<LegacyUserItem, kMaxBagItems>));
  const auto storage_blob_size =
      std::to_string(sizeof(std::array<LegacyUserItem, kMaxSaveItems>));
  const auto magic_blob_size =
      std::to_string(sizeof(std::array<LegacyUseMagicInfo, kMaxUserMagic>));

  exec_or_throw(database, "BEGIN IMMEDIATE;");
  try {
    exec_or_throw(database, "ALTER TABLE characters RENAME TO characters_legacy;");
    exec_or_throw(database, schema_sql);
    exec_or_throw(
        database,
        "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
        " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
        " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
        " equipped_blob, bag_blob, storage_blob, magic_blob)"
        " SELECT account_id, character_name, map_id, x, y, 0, 0, 0, 0, 0, 0, 0, 0,"
        " COALESCE(level, 1), COALESCE(hp, 15), COALESCE(mp, 15),"
        " COALESCE(hp, 15), COALESCE(mp, 15),"
        " 0, 0, 0, 0, 0, 0, 100, 0, 30, 0, 100, 0, 100,"
        " zeroblob(" +
            equip_blob_size + "), zeroblob(" + bag_blob_size + "), zeroblob(" + storage_blob_size +
            "), zeroblob(" + magic_blob_size + ") FROM characters_legacy;");
    exec_or_throw(database, "DROP TABLE characters_legacy;");
    exec_or_throw(database, "COMMIT;");
  } catch (...) {
    try {
      exec_or_throw(database, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

void ensure_characters_columns(sqlite3* database) {
  if (!table_has_column(database, "characters", "hair")) {
    exec_or_throw(database,
                  "ALTER TABLE characters ADD COLUMN hair INTEGER NOT NULL DEFAULT 0;");
  }
  ensure_column(database, "characters", "guild_name",
                "ALTER TABLE characters ADD COLUMN guild_name TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "characters", "guild_title",
                "ALTER TABLE characters ADD COLUMN guild_title TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "characters", "attack_mode",
                "ALTER TABLE characters ADD COLUMN attack_mode INTEGER NOT NULL DEFAULT 1;");
  ensure_column(database, "characters", "pk_point",
                "ALTER TABLE characters ADD COLUMN pk_point INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "death_time_ms",
                "ALTER TABLE characters ADD COLUMN death_time_ms INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "quest_blob",
                "ALTER TABLE characters ADD COLUMN quest_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "quest_open_blob",
                "ALTER TABLE characters ADD COLUMN quest_open_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "quest_unit_blob",
                "ALTER TABLE characters ADD COLUMN quest_unit_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "script_param_blob",
                "ALTER TABLE characters ADD COLUMN script_param_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "daily_quest",
                "ALTER TABLE characters ADD COLUMN daily_quest INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "slave_blob",
                "ALTER TABLE characters ADD COLUMN slave_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "body_luck",
                "ALTER TABLE characters ADD COLUMN body_luck REAL NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "birth_items_granted",
                "ALTER TABLE characters ADD COLUMN birth_items_granted INTEGER NOT NULL DEFAULT 0;");
}

void ensure_merchant_state_columns(sqlite3* database) {
  ensure_column(database, "merchant_state", "upgrade_blob",
                "ALTER TABLE merchant_state ADD COLUMN upgrade_blob BLOB NOT NULL DEFAULT X'';");
}

void ensure_accounts_columns(sqlite3* database) {
  ensure_column(database, "accounts", "password",
                "ALTER TABLE accounts ADD COLUMN password TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "user_name",
                "ALTER TABLE accounts ADD COLUMN user_name TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "ss_no",
                "ALTER TABLE accounts ADD COLUMN ss_no TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "phone",
                "ALTER TABLE accounts ADD COLUMN phone TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "quiz",
                "ALTER TABLE accounts ADD COLUMN quiz TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "answer",
                "ALTER TABLE accounts ADD COLUMN answer TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "email",
                "ALTER TABLE accounts ADD COLUMN email TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "quiz2",
                "ALTER TABLE accounts ADD COLUMN quiz2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "answer2",
                "ALTER TABLE accounts ADD COLUMN answer2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "birthday",
                "ALTER TABLE accounts ADD COLUMN birthday TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "mobile_phone",
                "ALTER TABLE accounts ADD COLUMN mobile_phone TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "memo1",
                "ALTER TABLE accounts ADD COLUMN memo1 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "memo2",
                "ALTER TABLE accounts ADD COLUMN memo2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "server_index",
                "ALTER TABLE accounts ADD COLUMN server_index INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "passwd_fail",
                "ALTER TABLE accounts ADD COLUMN passwd_fail INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "passwd_fail_time_ms",
                "ALTER TABLE accounts ADD COLUMN passwd_fail_time_ms INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "is_banned",
                "ALTER TABLE accounts ADD COLUMN is_banned INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "updated_at",
                "ALTER TABLE accounts ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';");
  exec_or_throw(
      database,
      "UPDATE accounts SET display_name = account_id WHERE COALESCE(display_name, '') = '';");
  exec_or_throw(database,
                "UPDATE accounts SET updated_at = COALESCE(NULLIF(updated_at, ''), created_at,"
                " CURRENT_TIMESTAMP);");
}

}  // namespace

Repository::Repository(const std::filesystem::path& database_path) {
  std::filesystem::create_directories(database_path.parent_path());
  if (sqlite3_open(database_path.string().c_str(), &database_) != SQLITE_OK) {
    throw std::runtime_error("Failed to open sqlite database: " + database_path.string());
  }
  exec_or_throw(database_, "PRAGMA journal_mode = WAL;");
  exec_or_throw(database_, "PRAGMA synchronous = NORMAL;");
}

Repository::~Repository() {
  if (database_ != nullptr) {
    sqlite3_close(database_);
    database_ = nullptr;
  }
}

void Repository::ensure_schema(const std::filesystem::path& schema_path) {
  const auto schema_sql = read_text_file(schema_path);
  exec_or_throw(database_, schema_sql);
  migrate_legacy_characters_table(database_, schema_sql);
  ensure_accounts_columns(database_);
  ensure_characters_columns(database_);
  ensure_merchant_state_columns(database_);
}

void Repository::seed_runtime() {
  save_account(database_, make_default_account("guest", "pass"));
  save_character(make_default_character("guest", "Hero"));
}

std::optional<AccountRecord> Repository::load_account(const std::string& account_id) {
  static constexpr const char* kSql =
      "SELECT account_id, password, display_name, user_name, ss_no, phone, quiz, answer, email,"
      " quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index, passwd_fail,"
      " passwd_fail_time_ms, is_banned FROM accounts WHERE account_id = ?1 LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_account statement.");
  }

  bind_text(statement, 1, account_id);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    auto account = read_account_row(statement);
    finalize_statement(statement);
    return account;
  }

  finalize_statement(statement);
  return std::nullopt;
}

CastleDialogContext Repository::load_castle_dialog_context() {
  CastleDialogContext context;

  static constexpr const char* kCastleSql =
      "SELECT castle_name, payload_json FROM castle_state ORDER BY castle_id ASC LIMIT 1;";
  sqlite3_stmt* castle_statement = nullptr;
  if (sqlite3_prepare_v2(database_, kCastleSql, -1, &castle_statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_castle_dialog_context castle query.");
  }

  std::string owner_guild;
  if (sqlite3_step(castle_statement) == SQLITE_ROW) {
    context.castle_name = column_text(castle_statement, 0);
    const auto payload = column_text(castle_statement, 1);
    if (const auto value = json_string_field(payload, "owner_guild"); value.has_value() &&
                                                                !value->empty()) {
      if (!is_legacy_unclaimed(*value)) {
        context.owner_guild = *value;
        owner_guild = *value;
      }
    }
    if (const auto value = json_string_field(payload, "lord"); value.has_value() && !value->empty()) {
      if (!is_legacy_unclaimed(*value)) {
        context.lord = *value;
      }
    }
    if (const auto value = json_string_field(payload, "castle_war_date");
        value.has_value() && !value->empty()) {
      context.castle_war_date = *value;
    } else if (const auto value = json_string_field(payload, "war_date");
               value.has_value() && !value->empty()) {
      context.castle_war_date = *value;
    }
    if (const auto value = json_string_field(payload, "list_of_war");
        value.has_value() && !value->empty()) {
      context.list_of_war = *value;
    } else if (const auto wars = json_string_array_field(payload, "list_of_war");
               wars.has_value() && !wars->empty()) {
      context.list_of_war = join_strings(*wars, ", ");
    } else if (const auto wars = json_string_array_field(payload, "wars");
               wars.has_value() && !wars->empty()) {
      context.list_of_war = join_strings(*wars, ", ");
    }
    if (const auto value = json_int_field(payload, "guild_war_fee"); value.has_value()) {
      context.guild_war_fee = *value;
    }
    if (const auto value = json_int_field(payload, "upgrade_weapon_fee"); value.has_value()) {
      context.upgrade_weapon_fee = *value;
    }
    if (const auto value = json_int_field(payload, "guild_create_fee"); value.has_value()) {
      context.guild_create_fee = *value;
    }
  }
  finalize_statement(castle_statement);

  if (!owner_guild.empty()) {
    static constexpr const char* kGuildSql =
        "SELECT payload_json FROM guilds WHERE guild_name = ?1 LIMIT 1;";
    sqlite3_stmt* guild_statement = nullptr;
    if (sqlite3_prepare_v2(database_, kGuildSql, -1, &guild_statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare load_castle_dialog_context guild query.");
    }

    bind_text(guild_statement, 1, owner_guild);
    if (sqlite3_step(guild_statement) == SQLITE_ROW) {
      const auto payload = column_text(guild_statement, 0);
      if (context.lord.empty()) {
        if (const auto value = json_string_field(payload, "lord"); value.has_value() &&
                                                                  !value->empty()) {
          context.lord = *value;
        } else if (const auto value = json_string_field(payload, "chief");
                   value.has_value() && !value->empty()) {
          context.lord = *value;
        } else if (const auto value = json_string_field(payload, "master");
                   value.has_value() && !value->empty()) {
          context.lord = *value;
        }
      }
    }
    finalize_statement(guild_statement);
  }

  return context;
}

GuildCastleSnapshot Repository::load_guild_castle_snapshot() {
  GuildCastleSnapshot snapshot;
  snapshot.castle_dialog = load_castle_dialog_context();

  static constexpr const char* kGuildSql =
      "SELECT guild_name, payload_json FROM guilds ORDER BY guild_name ASC;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kGuildSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_guild_castle_snapshot statement.");
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    snapshot.guilds.push_back(read_guild_state_row(statement));
  }
  finalize_statement(statement);
  return snapshot;
}

void Repository::save_guild_payload(const std::string& guild_name, const std::string& payload_json) {
  static constexpr const char* kSql =
      "INSERT INTO guilds(guild_name, payload_json) VALUES(?1, ?2)"
      " ON CONFLICT(guild_name) DO UPDATE SET payload_json = excluded.payload_json;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_guild_payload statement.");
  }

  bind_text(statement, 1, guild_name);
  bind_text(statement, 2, payload_json);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_guild_payload statement.");
  }
  finalize_statement(statement);
}

void Repository::save_guild_state(const GuildState& guild_state) {
  auto normalized = guild_state;
  normalized.guild_name = trim_copy(normalized.guild_name);
  normalized.lord = trim_copy(normalized.lord);
  std::vector<std::string> members;
  for (const auto& member : normalized.members) {
    push_unique_member(members, member);
  }
  normalized.members = std::move(members);
  if (!normalized.lord.empty()) {
    push_unique_member(normalized.members, normalized.lord);
  }
  std::vector<std::string> applicants;
  for (const auto& applicant : normalized.applicants) {
    const auto normalized_applicant = trim_copy(applicant);
    if (normalized_applicant.empty()) {
      continue;
    }
    const auto already_member =
        std::any_of(normalized.members.begin(), normalized.members.end(),
                    [&](const std::string& member) {
                      return trim_copy(member) == normalized_applicant;
                    });
    if (!already_member) {
      push_unique_member(applicants, normalized_applicant);
    }
  }
  normalized.applicants = std::move(applicants);

  save_guild_payload(normalized.guild_name, build_guild_payload_json(normalized));
}

void Repository::delete_guild(const std::string& guild_name) {
  static constexpr const char* kClearCharactersSql =
      "UPDATE characters SET guild_name = '', guild_title = '' WHERE guild_name = ?1;";

  sqlite3_stmt* clear_statement = nullptr;
  if (sqlite3_prepare_v2(database_, kClearCharactersSql, -1, &clear_statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("Failed to prepare clear_guild_characters statement.");
  }

  bind_text(clear_statement, 1, guild_name);
  if (sqlite3_step(clear_statement) != SQLITE_DONE) {
    finalize_statement(clear_statement);
    throw std::runtime_error("Failed to execute clear_guild_characters statement.");
  }
  finalize_statement(clear_statement);

  static constexpr const char* kSql = "DELETE FROM guilds WHERE guild_name = ?1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare delete_guild statement.");
  }

  bind_text(statement, 1, guild_name);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute delete_guild statement.");
  }
  finalize_statement(statement);
}

void Repository::save_castle_state(const std::string& castle_name, const std::string& payload_json) {
  static constexpr const char* kSql =
      "INSERT INTO castle_state(castle_name, payload_json) VALUES(?1, ?2)"
      " ON CONFLICT(castle_name) DO UPDATE SET payload_json = excluded.payload_json;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_castle_state statement.");
  }

  bind_text(statement, 1, castle_name);
  bind_text(statement, 2, payload_json);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_castle_state statement.");
  }
  finalize_statement(statement);
}

std::vector<MerchantStateRecord> Repository::load_merchant_states() {
  static constexpr const char* kStateSql =
      "SELECT merchant_key, npc_id, map_id, goods_blob, upgrade_blob FROM merchant_state"
      " ORDER BY merchant_key;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kStateSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_merchant_states statement.");
  }

  std::vector<MerchantStateRecord> states;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    MerchantStateRecord state;
    state.merchant_key = column_text(statement, 0);
    state.npc_id = column_text(statement, 1);
    state.map_id = column_text(statement, 2);
    state.goods = decode_merchant_goods_blob(statement, 3);
    state.weapon_upgrades = decode_weapon_upgrade_blob(statement, 4);
    states.push_back(std::move(state));
  }
  finalize_statement(statement);

  static constexpr const char* kPricesSql =
      "SELECT merchant_key, item_id, sell_price FROM merchant_prices ORDER BY merchant_key, item_id;";
  statement = nullptr;
  if (sqlite3_prepare_v2(database_, kPricesSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_merchant_prices statement.");
  }

  std::unordered_map<std::string, std::size_t> index_by_key;
  for (std::size_t i = 0; i < states.size(); ++i) {
    index_by_key[states[i].merchant_key] = i;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto key = column_text(statement, 0);
    const auto state_it = index_by_key.find(key);
    if (state_it == index_by_key.end()) {
      continue;
    }
    states[state_it->second].prices[sqlite3_column_int(statement, 1)] =
        sqlite3_column_int(statement, 2);
  }
  finalize_statement(statement);
  return states;
}

void Repository::save_merchant_state(const MerchantStateRecord& state) {
  if (state.merchant_key.empty()) {
    return;
  }
  exec_or_throw(database_, "BEGIN IMMEDIATE;");
  try {
    static constexpr const char* kStateSql =
        "INSERT INTO merchant_state(merchant_key, npc_id, map_id, goods_blob, upgrade_blob)"
        " VALUES(?1, ?2, ?3, ?4, ?5)"
        " ON CONFLICT(merchant_key) DO UPDATE SET npc_id = excluded.npc_id,"
        " map_id = excluded.map_id, goods_blob = excluded.goods_blob,"
        " upgrade_blob = excluded.upgrade_blob,"
        " updated_at = CURRENT_TIMESTAMP;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kStateSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare save_merchant_state statement.");
    }
    const auto goods_blob = encode_merchant_goods_blob(state.goods);
    const auto upgrade_blob = encode_weapon_upgrade_blob(state.weapon_upgrades);
    bind_text(statement, 1, state.merchant_key);
    bind_text(statement, 2, state.npc_id);
    bind_text(statement, 3, state.map_id);
    bind_blob_vector(statement, 4, goods_blob);
    bind_blob_vector(statement, 5, upgrade_blob);
    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute save_merchant_state statement.");
    }
    finalize_statement(statement);

    static constexpr const char* kDeleteSql = "DELETE FROM merchant_prices WHERE merchant_key = ?1;";
    if (sqlite3_prepare_v2(database_, kDeleteSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare clear_merchant_prices statement.");
    }
    bind_text(statement, 1, state.merchant_key);
    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute clear_merchant_prices statement.");
    }
    finalize_statement(statement);

    static constexpr const char* kPriceSql =
        "INSERT INTO merchant_prices(merchant_key, item_id, sell_price) VALUES(?1, ?2, ?3);";
    for (const auto& [item_id, sell_price] : state.prices) {
      if (sqlite3_prepare_v2(database_, kPriceSql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare save_merchant_price statement.");
      }
      bind_text(statement, 1, state.merchant_key);
      sqlite3_bind_int(statement, 2, item_id);
      sqlite3_bind_int(statement, 3, sell_price);
      if (sqlite3_step(statement) != SQLITE_DONE) {
        finalize_statement(statement);
        throw std::runtime_error("Failed to execute save_merchant_price statement.");
      }
      finalize_statement(statement);
    }
    exec_or_throw(database_, "COMMIT;");
  } catch (...) {
    try {
      exec_or_throw(database_, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

AccountOperationResult Repository::authenticate_account(const std::string& account_id,
                                                        const std::string& password,
                                                        std::int64_t now_ms) {
  auto account = load_account(account_id);
  if (!account.has_value()) {
    return AccountOperationResult{-4, std::nullopt};
  }
  if (account->banned) {
    return AccountOperationResult{-5, account};
  }

  const auto login_locked =
      account->passwd_fail >= 5 && elapsed_ms(now_ms, account->passwd_fail_time_ms) <= 60 * 1000;
  if (login_locked) {
    account->passwd_fail_time_ms = now_ms;
    save_account(database_, *account);
    return AccountOperationResult{-2, account};
  }

  if (account->password == password) {
    account->passwd_fail = 0;
    account->passwd_fail_time_ms = 0;
    save_account(database_, *account);
    return AccountOperationResult{1, account};
  }

  account->passwd_fail += 1;
  account->passwd_fail_time_ms = now_ms;
  save_account(database_, *account);
  return AccountOperationResult{-1, account};
}

bool Repository::create_account(const AccountRecord& account) {
  static constexpr const char* kSql =
      "INSERT INTO accounts(account_id, password, display_name, user_name, ss_no, phone, quiz,"
      " answer, email, quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index,"
      " passwd_fail, passwd_fail_time_ms, is_banned)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare create_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);
  sqlite3_bind_int(statement, 16, account.server_index);
  sqlite3_bind_int(statement, 17, account.passwd_fail);
  sqlite3_bind_int64(statement, 18, account.passwd_fail_time_ms);
  sqlite3_bind_int(statement, 19, account.banned ? 1 : 0);

  const auto result = sqlite3_step(statement);
  finalize_statement(statement);
  return result == SQLITE_DONE;
}

bool Repository::update_account(const AccountRecord& account) {
  static constexpr const char* kSql =
      "UPDATE accounts SET password = ?2, display_name = ?3, user_name = ?4, ss_no = ?5,"
      " phone = ?6, quiz = ?7, answer = ?8, email = ?9, quiz2 = ?10, answer2 = ?11,"
      " birthday = ?12, mobile_phone = ?13, memo1 = ?14, memo2 = ?15,"
      " updated_at = CURRENT_TIMESTAMP WHERE account_id = ?1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare update_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);

  const auto result = sqlite3_step(statement);
  const auto changes = sqlite3_changes(database_);
  finalize_statement(statement);
  return result == SQLITE_DONE && changes > 0;
}

std::int32_t Repository::change_password(const std::string& account_id, const std::string& password,
                                         const std::string& new_password, std::int64_t now_ms) {
  if (new_password.size() < 3) {
    return 0;
  }

  auto account = load_account(account_id);
  if (!account.has_value()) {
    return 0;
  }

  const auto change_locked =
      account->passwd_fail >= 5 && elapsed_ms(now_ms, account->passwd_fail_time_ms) <= 3 * 60 * 1000;
  if (change_locked) {
    return -2;
  }

  if (account->password == password) {
    account->password = new_password;
    account->passwd_fail = 0;
    account->passwd_fail_time_ms = 0;
    save_account(database_, *account);
    return 1;
  }

  account->passwd_fail += 1;
  account->passwd_fail_time_ms = now_ms;
  save_account(database_, *account);
  return -1;
}

std::optional<CharacterRecord> Repository::load_character(const std::string& account_id,
                                                          const std::string& character_name) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted FROM characters"
      " WHERE account_id = ?1 AND character_name = ?2"
      " LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_character statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, character_name);

  CharacterRecord record;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    record = read_character_row(statement);
    finalize_statement(statement);
    return record;
  }

  finalize_statement(statement);
  return std::nullopt;
}

std::optional<CharacterRecord> Repository::load_character_by_name(
    const std::string& character_name) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted FROM characters"
      " WHERE character_name = ?1"
      " ORDER BY updated_at DESC, account_id ASC"
      " LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_character_by_name statement.");
  }

  bind_text(statement, 1, character_name);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    auto record = read_character_row(statement);
    finalize_statement(statement);
    return record;
  }

  finalize_statement(statement);
  return std::nullopt;
}

std::vector<CharacterRecord> Repository::list_characters(const std::string& account_id) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted FROM characters WHERE account_id = ?1"
      " ORDER BY updated_at DESC, character_name ASC;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare list_characters statement.");
  }

  bind_text(statement, 1, account_id);
  std::vector<CharacterRecord> characters;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    characters.push_back(read_character_row(statement));
  }
  finalize_statement(statement);
  return characters;
}

bool Repository::create_character(const CharacterRecord& character) {
  ensure_account(database_, character.account_id);

  static constexpr const char* kSql =
      "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
      " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
      " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
      " equipped_blob, bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode,"
      " pk_point, death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36,"
      " ?37, ?38, ?39, ?40, ?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare create_character statement.");
  }

  bind_character_fields(statement, character);

  const auto result = sqlite3_step(statement);
  finalize_statement(statement);
  return result == SQLITE_DONE;
}

bool Repository::delete_character(const std::string& account_id, const std::string& character_name) {
  static constexpr const char* kSql =
      "DELETE FROM characters WHERE account_id = ?1 AND character_name = ?2;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare delete_character statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, character_name);
  const auto result = sqlite3_step(statement);
  const auto changes = sqlite3_changes(database_);
  finalize_statement(statement);
  return result == SQLITE_DONE && changes > 0;
}

void Repository::save_character(const CharacterRecord& character) {
  ensure_account(database_, character.account_id);

  static constexpr const char* kSql =
      "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
      " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
      " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
      " equipped_blob, bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode,"
      " pk_point, death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36,"
      " ?37, ?38, ?39, ?40, ?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48)"
      " ON CONFLICT(account_id, character_name) DO UPDATE SET map_id = excluded.map_id,"
      " x = excluded.x, y = excluded.y, dir = excluded.dir, light = excluded.light, job = excluded.job,"
      " sex = excluded.sex, hair = excluded.hair, gold = excluded.gold,"
      " feature = excluded.feature, status = excluded.status, level = excluded.level,"
      " hp = excluded.hp, mp = excluded.mp, max_hp = excluded.max_hp,"
      " max_mp = excluded.max_mp, ac = excluded.ac, mac = excluded.mac, dc = excluded.dc,"
      " mc = excluded.mc, sc = excluded.sc, exp = excluded.exp, max_exp = excluded.max_exp,"
      " weight = excluded.weight, max_weight = excluded.max_weight,"
      " wear_weight = excluded.wear_weight, max_wear_weight = excluded.max_wear_weight,"
      " hand_weight = excluded.hand_weight, max_hand_weight = excluded.max_hand_weight,"
      " equipped_blob = excluded.equipped_blob, bag_blob = excluded.bag_blob,"
      " storage_blob = excluded.storage_blob, magic_blob = excluded.magic_blob,"
      " guild_name = excluded.guild_name, guild_title = excluded.guild_title,"
      " attack_mode = excluded.attack_mode, pk_point = excluded.pk_point,"
      " death_time_ms = excluded.death_time_ms, quest_blob = excluded.quest_blob,"
      " quest_open_blob = excluded.quest_open_blob, quest_unit_blob = excluded.quest_unit_blob,"
      " script_param_blob = excluded.script_param_blob, daily_quest = excluded.daily_quest,"
      " slave_blob = excluded.slave_blob, body_luck = excluded.body_luck,"
      " birth_items_granted = excluded.birth_items_granted,"
      " updated_at = CURRENT_TIMESTAMP;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_character statement.");
  }

  bind_character_fields(statement, character);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_character statement.");
  }
  finalize_statement(statement);
}

void Repository::record_legacy_import(const LegacyImportRecord& record) {
  static constexpr const char* kSql =
      "INSERT INTO legacy_import_records(source_file, record_index, account_id, character_name,"
      " status, message, raw_record) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare record_legacy_import statement.");
  }

  bind_text(statement, 1, record.source_file);
  sqlite3_bind_int(statement, 2, record.record_index);
  bind_text(statement, 3, record.account_id);
  bind_text(statement, 4, record.character_name);
  bind_text(statement, 5, record.status);
  bind_text(statement, 6, record.message);
  sqlite3_bind_blob(statement, 7, record.raw_record.data(),
                    static_cast<int>(record.raw_record.size()), SQLITE_TRANSIENT);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute record_legacy_import statement.");
  }
  finalize_statement(statement);
}

std::size_t Repository::count_legacy_import_records() {
  static constexpr const char* kSql = "SELECT COUNT(*) FROM legacy_import_records;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare count_legacy_import_records statement.");
  }

  std::size_t count = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    count = static_cast<std::size_t>(std::max(sqlite3_column_int64(statement, 0),
                                             static_cast<sqlite3_int64>(0)));
  }
  finalize_statement(statement);
  return count;
}

void Repository::record_audit(const AuditEvent& audit) {
  static constexpr const char* kSql =
      "INSERT INTO login_audit(category, message, session_key, created_at)"
      " VALUES(?1, ?2, ?3, datetime('now'));";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare record_audit statement.");
  }

  sqlite3_bind_text(statement, 1, audit.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, audit.message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, audit.session_key.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    sqlite3_finalize(statement);
    throw std::runtime_error("Failed to execute record_audit statement.");
  }
  sqlite3_finalize(statement);
}

}  // namespace mir2
