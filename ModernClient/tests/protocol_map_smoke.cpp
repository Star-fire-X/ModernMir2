#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "protocol/delphi_protocol_map.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

struct StatusCounts {
  std::size_t implemented{0};
  std::size_t partial{0};
  std::size_t planned{0};
  std::size_t internal{0};
};

template <std::size_t Size>
bool has_entry(const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries,
               const std::string_view name) {
  return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.delphi_entry == name;
  });
}

template <std::size_t Size>
const mir2::client::protocol_migration::MappingEntry* find_entry(
    const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries,
    const std::string_view name) {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.delphi_entry == name;
  });
  return it == entries.end() ? nullptr : &*it;
}

template <std::size_t Size>
StatusCounts count_statuses(
    const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries) {
  StatusCounts counts;
  for (const auto& entry : entries) {
    switch (entry.status) {
      case mir2::client::protocol_migration::MigrationStatus::implemented:
        ++counts.implemented;
        break;
      case mir2::client::protocol_migration::MigrationStatus::partial:
        ++counts.partial;
        break;
      case mir2::client::protocol_migration::MigrationStatus::planned:
        ++counts.planned;
        break;
      case mir2::client::protocol_migration::MigrationStatus::internal:
        ++counts.internal;
        break;
    }
  }
  return counts;
}

void assert_counts(const StatusCounts& actual, const StatusCounts& expected) {
  assert(actual.implemented == expected.implemented);
  assert(actual.partial == expected.partial);
  assert(actual.planned == expected.planned);
  assert(actual.internal == expected.internal);
}

template <std::size_t Size>
void assert_partial_and_planned_notes(
    const std::array<mir2::client::protocol_migration::MappingEntry, Size>& entries) {
  using mir2::client::protocol_migration::MigrationStatus;
  for (const auto& entry : entries) {
    if (entry.status == MigrationStatus::partial || entry.status == MigrationStatus::planned) {
      assert(!entry.notes.empty());
    }
  }
}

template <std::size_t Size>
const mir2::client::protocol_migration::LegacyMessageMappingEntry* find_legacy_entry(
    const std::array<mir2::client::protocol_migration::LegacyMessageMappingEntry, Size>& entries,
    const std::string_view name) {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
    return entry.delphi_symbol == name;
  });
  return it == entries.end() ? nullptr : &*it;
}

template <std::size_t Size>
void assert_legacy_non_implemented_notes(
    const std::array<mir2::client::protocol_migration::LegacyMessageMappingEntry, Size>& entries) {
  using mir2::client::protocol_migration::LegacyProtocolMappingStatus;
  for (const auto& entry : entries) {
    if (entry.status != LegacyProtocolMappingStatus::implemented) {
      assert(!entry.notes.empty());
    }
  }
}

template <std::size_t Size>
void assert_legacy_sources(
    const std::array<mir2::client::protocol_migration::LegacyMessageMappingEntry, Size>& entries) {
  for (const auto& entry : entries) {
    assert(entry.delphi_source == "Source/Common/Grobal2.pas");
  }
}

template <std::size_t Size>
void assert_unique_legacy_symbols(
    const std::array<mir2::client::protocol_migration::LegacyMessageMappingEntry, Size>& entries) {
  for (std::size_t left = 0; left < entries.size(); ++left) {
    for (std::size_t right = left + 1; right < entries.size(); ++right) {
      assert(entries[left].delphi_symbol != entries[right].delphi_symbol);
    }
  }
}

template <std::size_t Size>
void assert_unique_legacy_idents(
    const std::array<mir2::client::protocol_migration::LegacyMessageMappingEntry, Size>& entries) {
  for (std::size_t left = 0; left < entries.size(); ++left) {
    for (std::size_t right = left + 1; right < entries.size(); ++right) {
      const auto same_ident = entries[left].delphi_ident == entries[right].delphi_ident;
      const auto allowed_action_alias =
          entries[left].delphi_ident == 5 &&
          ((entries[left].delphi_symbol == "SM_ACTION_MIN" &&
            entries[right].delphi_symbol == "SM_THROW") ||
           (entries[left].delphi_symbol == "SM_THROW" &&
            entries[right].delphi_symbol == "SM_ACTION_MIN"));
      assert(!same_ident || allowed_action_alias);
    }
  }
}

void append_u8(Bytes& bytes, std::uint8_t value) {
  bytes.push_back(value);
}

void append_bool(Bytes& bytes, bool value) {
  append_u8(bytes, value ? 1U : 0U);
}

void append_u16(Bytes& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
  }
}

void append_u64(Bytes& bytes, std::uint64_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
  }
}

void append_i32(Bytes& bytes, std::int32_t value) {
  append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_string(Bytes& bytes, std::string_view value) {
  append_u16(bytes, static_cast<std::uint16_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

Bytes expected_frame(std::uint16_t message_id, std::uint32_t sequence, Bytes payload) {
  Bytes bytes;
  append_u32(bytes, static_cast<std::uint32_t>(8U + payload.size()));
  append_u16(bytes, message_id);
  append_u16(bytes, 0);
  append_u32(bytes, sequence);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

void assert_bytes(const Bytes& actual, const Bytes& expected) {
  assert(actual.size() == expected.size());
  assert(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

template <typename T>
void assert_golden(const T& message, std::uint16_t message_id, std::uint32_t sequence,
                   Bytes payload) {
  using namespace mir2::client_v1;
  const auto actual = encode_frame(make_frame(message, sequence));
  assert_bytes(actual, expected_frame(message_id, sequence, std::move(payload)));
  auto buffer = actual;
  const auto frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(buffer.empty());
  assert(frames.front().message_id == static_cast<MessageId>(message_id));
  const auto decoded = decode_message<T>(frames.front());
  assert(decoded.has_value());
}

void append_server_entry(Bytes& bytes, std::string_view name, std::string_view address,
                         std::uint16_t port) {
  append_string(bytes, name);
  append_string(bytes, address);
  append_u16(bytes, port);
}

void append_character_summary(Bytes& bytes, std::string_view name, std::uint16_t level,
                              std::uint8_t job, std::uint8_t sex, std::uint8_t hair,
                              std::string_view map_id) {
  append_string(bytes, name);
  append_u16(bytes, level);
  append_u8(bytes, job);
  append_u8(bytes, sex);
  append_u8(bytes, hair);
  append_string(bytes, map_id);
}

void append_world_actor(Bytes& bytes, std::uint64_t actor_id, std::string_view name,
                        std::int32_t x, std::int32_t y, std::uint8_t dir,
                        std::int32_t feature, std::int32_t status, std::uint8_t actor_type) {
  append_u64(bytes, actor_id);
  append_string(bytes, name);
  append_i32(bytes, x);
  append_i32(bytes, y);
  append_u8(bytes, dir);
  append_i32(bytes, feature);
  append_i32(bytes, status);
  append_u8(bytes, actor_type);
}

void append_item_state(Bytes& bytes, std::string_view name, std::int32_t make_index,
                       std::int32_t looks, std::uint8_t std_mode, std::uint16_t dura,
                       std::uint16_t dura_max) {
  append_string(bytes, name);
  append_i32(bytes, make_index);
  append_i32(bytes, looks);
  append_u8(bytes, std_mode);
  append_u16(bytes, dura);
  append_u16(bytes, dura_max);
}

void append_item_slot(Bytes& bytes, std::int32_t slot, std::string_view name,
                      std::int32_t make_index, std::int32_t looks, std::uint8_t std_mode,
                      std::uint16_t dura, std::uint16_t dura_max) {
  append_i32(bytes, slot);
  append_item_state(bytes, name, make_index, looks, std_mode, dura, dura_max);
}

void append_guild_member(Bytes& bytes, std::string_view name, std::string_view rank,
                         bool online) {
  append_string(bytes, name);
  append_string(bytes, rank);
  append_bool(bytes, online);
}

void assert_p0_protocol_goldens() {
  using namespace mir2::client_v1;

  Bytes payload;
  append_u32(payload, kProtocolVersion);
  append_u32(payload, 0x01020304U);
  append_u32(payload, 0xA0B0C0D0U);
  append_u32(payload, 0x0F0E0D0CU);
  assert_golden(ClientHello{kProtocolVersion, 0x01020304U, 0xA0B0C0D0U, 0x0F0E0D0CU}, 1, 1,
                payload);

  payload.clear();
  append_string(payload, "id");
  append_string(payload, "pw");
  assert_golden(LoginRequest{"id", "pw"}, 100, 2, payload);

  payload.clear();
  append_bool(payload, true);
  append_i32(payload, 7);
  append_string(payload, "id");
  append_string(payload, "Hero");
  append_string(payload, "");
  assert_golden(LoginResult{true, 7, "id", "Hero", ""}, 101, 3, payload);

  payload.clear();
  append_u16(payload, 1);
  append_server_entry(payload, "S1", "127.0.0.1", 5600);
  assert_golden(ServerList{{ServerEntry{"S1", "127.0.0.1", 5600}}}, 110, 4, payload);

  payload.clear();
  append_string(payload, "S1");
  assert_golden(SelectServerRequest{"S1"}, 111, 5, payload);

  payload.clear();
  append_bool(payload, true);
  append_string(payload, "S1");
  append_string(payload, "127.0.0.1");
  append_u16(payload, 5601);
  append_string(payload, "lobby");
  append_string(payload, "");
  assert_golden(SelectServerResult{true, "S1", "127.0.0.1", 5601, "lobby", ""}, 112, 6,
                payload);

  payload.clear();
  append_string(payload, "lobby");
  assert_golden(CharacterListRequest{"lobby"}, 200, 7, payload);

  payload.clear();
  append_u16(payload, 1);
  append_character_summary(payload, "Hero", 1, 0, 1, 2, "0");
  append_string(payload, "Hero");
  CharacterList characters;
  characters.characters.push_back(CharacterSummary{"Hero", 1, 0, 1, 2, "0"});
  characters.selected_name = "Hero";
  assert_golden(characters, 201, 8, payload);

  payload.clear();
  append_string(payload, "Hero");
  assert_golden(SelectCharacterRequest{"Hero"}, 206, 9, payload);

  payload.clear();
  append_bool(payload, true);
  append_string(payload, "Hero");
  append_string(payload, "world");
  append_string(payload, "127.0.0.1");
  append_u16(payload, 5602);
  append_string(payload, "");
  assert_golden(SelectCharacterResult{true, "Hero", "world", "127.0.0.1", 5602, ""}, 207,
                10, payload);

  payload.clear();
  append_string(payload, "world");
  append_u32(payload, 0x01020304U);
  append_u32(payload, 0x05060708U);
  assert_golden(EnterWorldRequest{"world", 0x01020304U, 0x05060708U}, 300, 11, payload);

  payload.clear();
  append_bool(payload, true);
  append_u64(payload, 1000);
  append_string(payload, "Hero");
  append_string(payload, "0");
  append_i32(payload, 330);
  append_i32(payload, 270);
  append_string(payload, "");
  assert_golden(EnterWorldResult{true, 1000, "Hero", "0", 330, 270, ""}, 301, 12,
                payload);

  const WorldActor hero{1000, "Hero", 330, 270, 2, 0x01020304, 8, ActorType::player};
  const WorldActor hen{2000, "Hen", 332, 271, 4, 0, 0, ActorType::monster};

  payload.clear();
  append_string(payload, "0");
  append_i32(payload, 700);
  append_i32(payload, 700);
  append_u64(payload, 1000);
  append_u16(payload, 2);
  append_world_actor(payload, 1000, "Hero", 330, 270, 2, 0x01020304, 8, 1);
  append_world_actor(payload, 2000, "Hen", 332, 271, 4, 0, 0, 2);
  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors = {hero, hen};
  assert_golden(snapshot, 302, 13, payload);

  payload.clear();
  assert_golden(WorldClearObjects{}, 318, 142, payload);

  payload.clear();
  append_string(payload, "1");
  assert_golden(MapChange{"1"}, 319, 143, payload);

  payload.clear();
  append_i32(payload, 12);
  append_i32(payload, 13);
  append_u8(payload, 1);
  assert_golden(MapDoorState{12, 13, true}, 320, 144, payload);

  payload.clear();
  append_world_actor(payload, 2000, "Hen", 332, 271, 4, 0, 0, 2);
  assert_golden(ActorUpsert{hen}, 306, 14, payload);

  payload.clear();
  append_u64(payload, 2000);
  append_u16(payload, 0);
  assert_golden(ActorRemove{2000}, 316, 141, payload);

  payload.clear();
  append_u64(payload, 1000);
  append_i32(payload, 331);
  append_i32(payload, 270);
  append_u8(payload, 2);
  assert_golden(ActorStateDelta{1000, 331, 270, 2}, 303, 15, payload);

  payload.clear();
  append_u64(payload, 1000);
  append_u8(payload, 1);
  append_i32(payload, 331);
  append_i32(payload, 270);
  append_u8(payload, 2);
  append_u64(payload, 0);
  append_i32(payload, 0);
  append_u16(payload, 0);
  append_u16(payload, 0);
  append_bool(payload, false);
  append_u16(payload, 0);
  assert_golden(ActorAction{1000, ActorActionKind::walk, 331, 270, 2, 0, 0, 0, 0, false, 0},
                307, 16, payload);

  payload.clear();
  append_i32(payload, 331);
  append_i32(payload, 270);
  append_u8(payload, 0);
  assert_golden(MoveIntent{331, 270, MoveMode::walk}, 400, 17, payload);

  payload.clear();
  append_bool(payload, true);
  append_u32(payload, 1234);
  assert_golden(ActionAck{true, 1234}, 403, 18, payload);

  payload.clear();
  append_u64(payload, 123456789);
  assert_golden(Ping{123456789}, 600, 19, payload);

  payload.clear();
  append_u64(payload, 123456789);
  append_u64(payload, 123456999);
  assert_golden(Pong{123456789, 123456999}, 601, 20, payload);
}

void assert_p2_protocol_goldens() {
  using namespace mir2::client_v1;

  Bytes payload;
  append_i32(payload, 12345);
  assert_golden(DropGoldRequest{12345}, 419, 421, payload);

  payload.clear();
  append_i32(payload, 1001);
  append_i32(payload, 550);
  append_i32(payload, 1000);
  assert_golden(DurabilityChange{1001, 550, 1000}, 420, 422, payload);
}

void assert_p3_protocol_goldens() {
  using namespace mir2::client_v1;

  ItemState ruby;
  ruby.name = "Ruby";
  ruby.make_index = 1001;
  ruby.looks = 7;
  ruby.std_mode = 0;
  ruby.dura = 10;
  ruby.dura_max = 20;

  ItemState sapphire;
  sapphire.name = "Sapphire";
  sapphire.make_index = 2001;
  sapphire.looks = 8;
  sapphire.std_mode = 0;
  sapphire.dura = 30;
  sapphire.dura_max = 40;

  Bytes payload;
  append_bool(payload, true);
  assert_golden(GroupModeRequest{true}, 535, 501, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(GroupCreateRequest{"Ally"}, 536, 502, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(GroupAddMemberRequest{"Ally"}, 537, 503, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(GroupRemoveMemberRequest{"Ally"}, 538, 504, payload);

  payload.clear();
  append_bool(payload, true);
  append_bool(payload, true);
  append_u16(payload, 2);
  append_string(payload, "Hero");
  append_string(payload, "Ally");
  assert_golden(GroupState{true, true, {"Hero", "Ally"}}, 539, 505, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(TradeTryRequest{"Ally"}, 540, 506, payload);

  payload.clear();
  assert_golden(TradeCancelRequest{}, 541, 507, payload);

  payload.clear();
  append_i32(payload, 1001);
  append_string(payload, "Ruby");
  assert_golden(TradeAddItemRequest{1001, "Ruby"}, 542, 508, payload);

  payload.clear();
  append_i32(payload, 1001);
  append_string(payload, "Ruby");
  assert_golden(TradeRemoveItemRequest{1001, "Ruby"}, 543, 509, payload);

  payload.clear();
  append_i32(payload, 25);
  assert_golden(TradeSetGoldRequest{25}, 544, 510, payload);

  payload.clear();
  assert_golden(TradeAcceptRequest{}, 545, 511, payload);

  payload.clear();
  append_bool(payload, true);
  append_string(payload, "Ally");
  append_u16(payload, 1);
  append_item_slot(payload, 0, "Ruby", 1001, 7, 0, 10, 20);
  append_u16(payload, 1);
  append_item_slot(payload, 1, "Sapphire", 2001, 8, 0, 30, 40);
  append_i32(payload, 25);
  append_i32(payload, 9);
  append_bool(payload, true);
  append_bool(payload, false);
  assert_golden(TradeState{true, "Ally", {ItemSlotState{0, ruby}},
                           {ItemSlotState{1, sapphire}}, 25, 9, true, false},
                546, 512, payload);

  payload.clear();
  assert_golden(GuildOpenRequest{}, 547, 513, payload);

  payload.clear();
  assert_golden(GuildHomeRequest{}, 548, 514, payload);

  payload.clear();
  assert_golden(GuildMemberListRequest{}, 549, 515, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(GuildAddMemberRequest{"Ally"}, 550, 516, payload);

  payload.clear();
  append_string(payload, "Ally");
  assert_golden(GuildRemoveMemberRequest{"Ally"}, 551, 517, payload);

  payload.clear();
  append_string(payload, "Notice");
  assert_golden(GuildUpdateNoticeRequest{"Notice"}, 552, 518, payload);

  payload.clear();
  append_string(payload, "Leader/Member");
  assert_golden(GuildUpdateGradeRequest{"Leader/Member"}, 553, 519, payload);

  payload.clear();
  append_bool(payload, true);
  append_string(payload, "Guild");
  append_string(payload, "Leader");
  append_string(payload, "Notice");
  append_u16(payload, 2);
  append_guild_member(payload, "Hero", "Leader", true);
  append_guild_member(payload, "Ally", "Member", false);
  append_u16(payload, 2);
  append_string(payload, "Leader");
  append_string(payload, "Member");
  append_bool(payload, true);
  assert_golden(GuildState{true,
                           "Guild",
                           "Leader",
                           "Notice",
                           {GuildMemberState{"Hero", "Leader", true},
                            GuildMemberState{"Ally", "Member", false}},
                           {"Leader", "Member"},
                           true},
                554, 520, payload);
}

void assert_scene_transition_drain_order() {
  using namespace mir2::client_v1;

  std::vector<std::uint8_t> buffer;
  const auto append_frame = [&buffer](const auto& message, const std::uint32_t sequence) {
    auto bytes = encode_frame(make_frame(message, sequence));
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
  };

  append_frame(LoginResult{true, 0, "guest", "Guest", ""}, 1);
  append_frame(ServerList{{ServerEntry{"S1", "127.0.0.1", 5600}}}, 2);
  append_frame(SelectServerResult{true, "S1", "127.0.0.1", 5601, "lobby", ""}, 3);

  CharacterList characters;
  characters.characters.push_back(CharacterSummary{"Hero", 1, 0, 1, 2, "0"});
  characters.selected_name = "Hero";
  append_frame(characters, 4);

  append_frame(SelectCharacterResult{true, "Hero", "world", "127.0.0.1", 5602, ""}, 5);
  append_frame(LoginNotice{"Welcome", "Notice"}, 6);
  append_frame(EnterWorldResult{true, 1000, "Hero", "0", 330, 270, ""}, 7);

  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors.push_back(WorldActor{1000, "Hero", 330, 270, 2, 0, 0, ActorType::player});
  append_frame(snapshot, 8);

  auto frames = drain_frames(buffer);
  assert(buffer.empty());
  assert(frames.size() == 8);
  assert(frames[0].message_id == MessageId::login_result);
  assert(frames[1].message_id == MessageId::server_list);
  assert(frames[2].message_id == MessageId::select_server_result);
  assert(frames[3].message_id == MessageId::character_list);
  assert(frames[4].message_id == MessageId::select_character_result);
  assert(frames[5].message_id == MessageId::login_notice);
  assert(frames[6].message_id == MessageId::enter_world_result);
  assert(frames[7].message_id == MessageId::world_snapshot);
  assert(decode_message<LoginResult>(frames[0]).has_value());
  assert(decode_message<ServerList>(frames[1]).has_value());
  assert(decode_message<SelectServerResult>(frames[2]).has_value());
  assert(decode_message<CharacterList>(frames[3]).has_value());
  assert(decode_message<SelectCharacterResult>(frames[4]).has_value());
  assert(decode_message<LoginNotice>(frames[5]).has_value());
  assert(decode_message<EnterWorldResult>(frames[6]).has_value());
  assert(decode_message<WorldSnapshot>(frames[7]).has_value());
}

}  // namespace

int main() {
  using namespace mir2::client::protocol_migration;
  using namespace mir2::client_v1;

  static_assert(kDelphiSendMappings.size() == 57);
  static_assert(kDelphiClientGetMappings.size() == 39);
  static_assert(kSceneTransitionMappings.size() == 13);
  static_assert(kDelphiClientToServerMessageMappings.size() == 87);
  static_assert(kDelphiServerToClientMessageMappings.size() == 220);

  assert_counts(count_statuses(kDelphiSendMappings), StatusCounts{11, 38, 6, 2});
  assert_counts(count_statuses(kDelphiClientGetMappings), StatusCounts{5, 30, 4, 0});
  assert_counts(count_statuses(kSceneTransitionMappings), StatusCounts{11, 2, 0, 0});
  assert_partial_and_planned_notes(kDelphiSendMappings);
  assert_partial_and_planned_notes(kDelphiClientGetMappings);
  assert_partial_and_planned_notes(kSceneTransitionMappings);
  assert_legacy_non_implemented_notes(kDelphiClientToServerMessageMappings);
  assert_legacy_non_implemented_notes(kDelphiServerToClientMessageMappings);
  assert_legacy_sources(kDelphiClientToServerMessageMappings);
  assert_legacy_sources(kDelphiServerToClientMessageMappings);
  assert_unique_legacy_symbols(kDelphiClientToServerMessageMappings);
  assert_unique_legacy_symbols(kDelphiServerToClientMessageMappings);
  assert_unique_legacy_idents(kDelphiClientToServerMessageMappings);
  assert_unique_legacy_idents(kDelphiServerToClientMessageMappings);

  assert(has_entry(kDelphiSendMappings, "SendSelectServer"));
  assert(has_entry(kDelphiSendMappings, "SendRunLogin"));
  assert(has_entry(kDelphiSendMappings, "SendUpdateAccount"));
  assert(has_entry(kDelphiSendMappings, "CM_CLICKNPC"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetNeedUpdateAccount"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetSelectServer"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetStartPlay"));
  assert(has_entry(kSceneTransitionMappings, "SendLogin"));
  assert(has_entry(kSceneTransitionMappings, "ClientGetPasswdSuccess"));
  assert(has_entry(kSceneTransitionMappings, "ClientGetStartPlay"));
  assert(has_entry(kSceneTransitionMappings, "ClientGetServerDown"));
  assert(has_entry(kDelphiClientGetMappings, "ClientGetReconnect"));

  const auto assert_not_planned = [](const auto* entry) {
    assert(entry != nullptr);
    assert(entry->status != MigrationStatus::planned);
  };
  assert_not_planned(find_entry(kDelphiSendMappings, "SendTakeOnItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendTakeOffItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendDropItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendBuyItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendStorageItem"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendDropGold"));
  assert_not_planned(find_entry(kDelphiSendMappings, "SendDealTry"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetBagItmes"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetSenduseItems"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetDuraChange"));
  assert_not_planned(find_entry(kDelphiClientGetMappings, "ClientGetReadMiniMap"));
  assert(find_entry(kDelphiClientGetMappings, "ClientGetReconnect")->status ==
         MigrationStatus::planned);
  assert(find_entry(kSceneTransitionMappings, "ClientGetServerDown")->status ==
         MigrationStatus::partial);

  const auto assert_legacy_status = [](const auto& entries, const std::string_view name,
                                       const LegacyProtocolMappingStatus status) {
    const auto* entry = find_legacy_entry(entries, name);
    assert(entry != nullptr);
    assert(entry->status == status);
  };
  assert_legacy_status(kDelphiClientToServerMessageMappings, "CM_OPENDOOR",
                       LegacyProtocolMappingStatus::not_migrated);
  assert_legacy_status(kDelphiClientToServerMessageMappings, "CM_QUERYUSERSTATE",
                       LegacyProtocolMappingStatus::planned);
  assert_legacy_status(kDelphiClientToServerMessageMappings, "CM_CLIENT_CHECKTIME",
                       LegacyProtocolMappingStatus::internal);
  assert_legacy_status(kDelphiServerToClientMessageMappings, "SM_GOLDCHANGED",
                       LegacyProtocolMappingStatus::partial);
  assert_legacy_status(kDelphiServerToClientMessageMappings, "SM_RECONNECT",
                       LegacyProtocolMappingStatus::planned);
  assert_legacy_status(kDelphiServerToClientMessageMappings, "SM_CHECK_CLIENTVALID",
                       LegacyProtocolMappingStatus::internal);

  assert_p0_protocol_goldens();
  assert_p2_protocol_goldens();
  assert_p3_protocol_goldens();
  assert_scene_transition_drain_order();

  SelectServerRequest request;
  request.name = "ModernServer";
  auto frame_bytes = encode_frame(make_frame(request, 11));
  std::vector<std::uint8_t> buffer = frame_bytes;
  auto frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::select_server_request);
  const auto decoded_request = decode_message<SelectServerRequest>(frames.front());
  assert(decoded_request.has_value());
  assert(decoded_request->name == "ModernServer");

  SelectServerResult result;
  result.success = true;
  result.name = "ModernServer";
  result.address = "127.0.0.1";
  result.port = 5601;
  result.lobby_token = "lobby-ticket-1";
  frame_bytes = encode_frame(make_frame(result, 12));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::select_server_result);
  const auto decoded_result = decode_message<SelectServerResult>(frames.front());
  assert(decoded_result.has_value());
  assert(decoded_result->success);
  assert(decoded_result->name == "ModernServer");
  assert(decoded_result->port == 5601);
  assert(decoded_result->lobby_token == "lobby-ticket-1");

  CharacterListRequest character_list_request;
  character_list_request.lobby_token = "lobby-ticket-1";
  frame_bytes = encode_frame(make_frame(character_list_request, 13));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::character_list_request);
  const auto decoded_character_list_request =
      decode_message<CharacterListRequest>(frames.front());
  assert(decoded_character_list_request.has_value());
  assert(decoded_character_list_request->lobby_token == "lobby-ticket-1");

  AccountProfile profile;
  profile.display_name = "guest";
  profile.user_name = "Guest User";
  profile.ss_no = "650101-1455111";
  profile.birthday = "1975/08/21";
  profile.quiz = "q1";
  profile.answer = "a1";
  profile.quiz2 = "q2";
  profile.answer2 = "a2";
  profile.phone = "021";
  profile.mobile_phone = "13900000000";
  profile.email = "guest@example.test";

  UpdateAccountRequest update;
  update.account_id = "guest";
  update.password = "pass";
  update.profile = profile;
  frame_bytes = encode_frame(make_frame(update, 14));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::update_account_request);
  const auto decoded_update = decode_message<UpdateAccountRequest>(frames.front());
  assert(decoded_update.has_value());
  assert(decoded_update->account_id == "guest");
  assert(decoded_update->profile.quiz2 == "q2");
  assert(decoded_update->profile.mobile_phone == "13900000000");

  NeedUpdateAccount need_update;
  need_update.account_id = "guest";
  need_update.profile = profile;
  need_update.message = "account_profile_required";
  frame_bytes = encode_frame(make_frame(need_update, 15));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::need_update_account);
  const auto decoded_need_update = decode_message<NeedUpdateAccount>(frames.front());
  assert(decoded_need_update.has_value());
  assert(decoded_need_update->message == "account_profile_required");
  assert(decoded_need_update->profile.email == "guest@example.test");

  LoginNotice notice;
  notice.title = "Welcome";
  notice.text = "Read this before entering.";
  frame_bytes = encode_frame(make_frame(notice, 16));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::login_notice);
  const auto decoded_notice = decode_message<LoginNotice>(frames.front());
  assert(decoded_notice.has_value());
  assert(decoded_notice->title == "Welcome");
  assert(decoded_notice->text == "Read this before entering.");

  LoginNoticeOk notice_ok;
  frame_bytes = encode_frame(make_frame(notice_ok, 17));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::login_notice_ok);
  const auto decoded_notice_ok = decode_message<LoginNoticeOk>(frames.front());
  assert(decoded_notice_ok.has_value());

  PickupIntent pickup;
  pickup.x = 330;
  pickup.y = 270;
  frame_bytes = encode_frame(make_frame(pickup, 18));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::pickup_intent);
  const auto decoded_pickup = decode_message<PickupIntent>(frames.front());
  assert(decoded_pickup.has_value());
  assert(decoded_pickup->y == 270);

  UseItemIntent use_item;
  use_item.item_make_index = 1001;
  use_item.item_slot = 1;
  use_item.name = "Potion";
  frame_bytes = encode_frame(make_frame(use_item, 19));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::use_item_intent);
  const auto decoded_use_item = decode_message<UseItemIntent>(frames.front());
  assert(decoded_use_item.has_value());
  assert(decoded_use_item->item_make_index == 1001);
  assert(decoded_use_item->name == "Potion");

  GroundItemAdd ground_add;
  ground_add.item.object_id = 77;
  ground_add.item.x = 330;
  ground_add.item.y = 271;
  ground_add.item.looks = 5;
  ground_add.item.name = "Gold";
  frame_bytes = encode_frame(make_frame(ground_add, 20));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::ground_item_add);
  const auto decoded_ground_add = decode_message<GroundItemAdd>(frames.front());
  assert(decoded_ground_add.has_value());
  assert(decoded_ground_add->item.object_id == 77);
  assert(decoded_ground_add->item.name == "Gold");

  frame_bytes = encode_frame(make_frame(GroundItemRemove{77, 330, 271}, 21));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::ground_item_remove);
  const auto decoded_ground_remove = decode_message<GroundItemRemove>(frames.front());
  assert(decoded_ground_remove.has_value());
  assert(decoded_ground_remove->object_id == 77);

  frame_bytes = encode_frame(make_frame(ActorRemove{2000}, 211));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::actor_remove);
  const auto decoded_actor_remove = decode_message<ActorRemove>(frames.front());
  assert(decoded_actor_remove.has_value());
  assert(decoded_actor_remove->actor_id == 2000);

  frame_bytes = encode_frame(make_frame(WorldClearObjects{}, 212));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::world_clear_objects);
  assert(decode_message<WorldClearObjects>(frames.front()).has_value());

  frame_bytes = encode_frame(make_frame(MapChange{"1"}, 213));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::map_change);
  const auto decoded_map_change = decode_message<MapChange>(frames.front());
  assert(decoded_map_change.has_value());
  assert(decoded_map_change->map_id == "1");

  frame_bytes = encode_frame(make_frame(MapDoorState{12, 13, true}, 214));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::map_door_state);
  const auto decoded_door_state = decode_message<MapDoorState>(frames.front());
  assert(decoded_door_state.has_value());
  assert(decoded_door_state->x == 12);
  assert(decoded_door_state->y == 13);
  assert(decoded_door_state->open);

  WorldSnapshot transfer_snapshot;
  transfer_snapshot.map_id = "1";
  transfer_snapshot.width = 500;
  transfer_snapshot.height = 400;
  transfer_snapshot.self_actor_id = 1000;
  transfer_snapshot.actors.push_back(
      WorldActor{1000, "Hero", 5, 5, 2, 0, 0, ActorType::player});
  const auto ordered_hen = WorldActor{2000, "Hen", 6, 5, 4, 0, 0, ActorType::monster};
  Bytes ordered_frames;
  const auto clear_frame = encode_frame(make_frame(WorldClearObjects{}, 300));
  const auto change_frame = encode_frame(make_frame(MapChange{"1"}, 301));
  const auto snapshot_frame = encode_frame(make_frame(transfer_snapshot, 302));
  const auto upsert_frame = encode_frame(make_frame(ActorUpsert{ordered_hen}, 303));
  ordered_frames.insert(ordered_frames.end(), clear_frame.begin(), clear_frame.end());
  ordered_frames.insert(ordered_frames.end(), change_frame.begin(), change_frame.end());
  ordered_frames.insert(ordered_frames.end(), snapshot_frame.begin(), snapshot_frame.end());
  ordered_frames.insert(ordered_frames.end(), upsert_frame.begin(), upsert_frame.end());
  frames = drain_frames(ordered_frames);
  assert(ordered_frames.empty());
  assert(frames.size() == 4);
  assert(frames[0].message_id == MessageId::world_clear_objects);
  assert(frames[1].message_id == MessageId::map_change);
  assert(frames[2].message_id == MessageId::world_snapshot);
  assert(frames[3].message_id == MessageId::actor_upsert);

  frame_bytes = encode_frame(make_frame(UseItemResult{true}, 22));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::use_item_result);
  const auto decoded_use_result = decode_message<UseItemResult>(frames.front());
  assert(decoded_use_result.has_value());
  assert(decoded_use_result->ok);

  ItemState potion;
  potion.name = "Potion";
  potion.make_index = 1001;
  potion.looks = 7;
  potion.std_mode = 0;
  potion.dura = 10;
  potion.dura_max = 20;

  BagSnapshot bag_snapshot;
  bag_snapshot.items.push_back(ItemSlotState{6, potion});
  frame_bytes = encode_frame(make_frame(bag_snapshot, 23));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::bag_snapshot);
  const auto decoded_bag_snapshot = decode_message<BagSnapshot>(frames.front());
  assert(decoded_bag_snapshot.has_value());
  assert(decoded_bag_snapshot->items.size() == 1);
  assert(decoded_bag_snapshot->items.front().slot == 6);
  assert(decoded_bag_snapshot->items.front().item.looks == 7);

  InventoryAdd inventory_add;
  inventory_add.entry = ItemSlotState{6, potion};
  frame_bytes = encode_frame(make_frame(inventory_add, 24));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_add);
  const auto decoded_inventory_add = decode_message<InventoryAdd>(frames.front());
  assert(decoded_inventory_add.has_value());
  assert(decoded_inventory_add->entry.slot == 6);

  InventoryUpdate inventory_update;
  inventory_update.entry = ItemSlotState{7, potion};
  frame_bytes = encode_frame(make_frame(inventory_update, 25));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_update);
  const auto decoded_inventory_update = decode_message<InventoryUpdate>(frames.front());
  assert(decoded_inventory_update.has_value());
  assert(decoded_inventory_update->entry.slot == 7);
  assert(decoded_inventory_update->entry.item.name == "Potion");

  frame_bytes = encode_frame(make_frame(InventoryRemove{7}, 26));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_remove);
  const auto decoded_inventory_remove = decode_message<InventoryRemove>(frames.front());
  assert(decoded_inventory_remove.has_value());
  assert(decoded_inventory_remove->slot == 7);

  frame_bytes = encode_frame(make_frame(InventoryClearRange{6, 12}, 27));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::inventory_clear_range);
  const auto decoded_inventory_clear = decode_message<InventoryClearRange>(frames.front());
  assert(decoded_inventory_clear.has_value());
  assert(decoded_inventory_clear->first_slot == 6);
  assert(decoded_inventory_clear->last_slot == 12);

  EquipmentSnapshot equipment_snapshot;
  equipment_snapshot.items.push_back(ItemSlotState{1, potion});
  frame_bytes = encode_frame(make_frame(equipment_snapshot, 28));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::equipment_snapshot);
  const auto decoded_equipment_snapshot = decode_message<EquipmentSnapshot>(frames.front());
  assert(decoded_equipment_snapshot.has_value());
  assert(decoded_equipment_snapshot->items.front().slot == 1);
  assert(decoded_equipment_snapshot->items.front().item.make_index == 1001);

  frame_bytes = encode_frame(make_frame(EquipItemRequest{1, 1001, "Potion"}, 29));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::equip_item_request);
  const auto decoded_equip = decode_message<EquipItemRequest>(frames.front());
  assert(decoded_equip.has_value());
  assert(decoded_equip->equipment_slot == 1);
  assert(decoded_equip->name == "Potion");

  frame_bytes = encode_frame(make_frame(UnequipItemRequest{1, 1001, "Potion"}, 30));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::unequip_item_request);
  const auto decoded_unequip = decode_message<UnequipItemRequest>(frames.front());
  assert(decoded_unequip.has_value());
  assert(decoded_unequip->item_make_index == 1001);

  frame_bytes = encode_frame(make_frame(DropItemRequest{1001, "Potion"}, 31));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::drop_item_request);
  const auto decoded_drop = decode_message<DropItemRequest>(frames.front());
  assert(decoded_drop.has_value());
  assert(decoded_drop->name == "Potion");

  frame_bytes = encode_frame(make_frame(DropGoldRequest{250}, 32));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::drop_gold_request);
  const auto decoded_drop_gold = decode_message<DropGoldRequest>(frames.front());
  assert(decoded_drop_gold.has_value());
  assert(decoded_drop_gold->amount == 250);

  frame_bytes = encode_frame(make_frame(DurabilityChange{1001, 550, 1000}, 33));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::durability_change);
  const auto decoded_dura = decode_message<DurabilityChange>(frames.front());
  assert(decoded_dura.has_value());
  assert(decoded_dura->item_make_index == 1001);
  assert(decoded_dura->dura == 550);
  assert(decoded_dura->dura_max == 1000);

  ChatLine chat_line{"Hero: hello", 0xFFFFFF00U, 0x00000000U};
  frame_bytes = encode_frame(make_frame(chat_line, 34));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::chat_line);
  const auto decoded_chat_line = decode_message<ChatLine>(frames.front());
  assert(decoded_chat_line.has_value());
  assert(decoded_chat_line->text == "Hero: hello");

  ActorSay actor_say{1000, "Hero: over here", 0xFFFFFFFFU, 0x00000000U};
  frame_bytes = encode_frame(make_frame(actor_say, 33));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::actor_say);
  const auto decoded_actor_say = decode_message<ActorSay>(frames.front());
  assert(decoded_actor_say.has_value());
  assert(decoded_actor_say->actor_id == 1000);
  assert(decoded_actor_say->text == "Hero: over here");

  frame_bytes = encode_frame(make_frame(NpcClickRequest{2000}, 34));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_click_request);
  const auto decoded_npc_click = decode_message<NpcClickRequest>(frames.front());
  assert(decoded_npc_click.has_value());
  assert(decoded_npc_click->actor_id == 2000);

  NpcDialog npc_dialog{2000, 384, "Shopkeeper/Hello <Buy/@buy>"};
  frame_bytes = encode_frame(make_frame(npc_dialog, 35));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog);
  const auto decoded_npc_dialog = decode_message<NpcDialog>(frames.front());
  assert(decoded_npc_dialog.has_value());
  assert(decoded_npc_dialog->merchant_id == 2000);
  assert(decoded_npc_dialog->text == "Shopkeeper/Hello <Buy/@buy>");

  NpcDialogSelectRequest npc_select{2000, "@buy"};
  frame_bytes = encode_frame(make_frame(npc_select, 36));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog_select_request);
  const auto decoded_npc_select = decode_message<NpcDialogSelectRequest>(frames.front());
  assert(decoded_npc_select.has_value());
  assert(decoded_npc_select->merchant_id == 2000);
  assert(decoded_npc_select->selection == "@buy");

  frame_bytes = encode_frame(make_frame(NpcDialogClose{2000}, 37));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::npc_dialog_close);
  const auto decoded_npc_close = decode_message<NpcDialogClose>(frames.front());
  assert(decoded_npc_close.has_value());
  assert(decoded_npc_close->merchant_id == 2000);

  frame_bytes =
      encode_frame(make_frame(MerchantRepairPriceRequest{2000, 1001, "Sword"}, 38));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::merchant_repair_price_request);
  const auto decoded_repair_price = decode_message<MerchantRepairPriceRequest>(frames.front());
  assert(decoded_repair_price.has_value());
  assert(decoded_repair_price->name == "Sword");

  StorageList storage;
  storage.merchant_id = 2000;
  storage.items.push_back(potion);
  frame_bytes = encode_frame(make_frame(storage, 39));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::storage_list);
  const auto decoded_storage = decode_message<StorageList>(frames.front());
  assert(decoded_storage.has_value());
  assert(decoded_storage->items.size() == 1);

  frame_bytes = encode_frame(make_frame(GroupState{true, true, {"Hero"}}, 40));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::group_state);
  const auto decoded_group = decode_message<GroupState>(frames.front());
  assert(decoded_group.has_value());
  assert(decoded_group->allow_group);

  frame_bytes =
      encode_frame(make_frame(TradeState{true, "Ally", {ItemSlotState{0, potion}}, {}, 10, 0,
                                         true, false},
                              41));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::trade_state);
  const auto decoded_trade = decode_message<TradeState>(frames.front());
  assert(decoded_trade.has_value());
  assert(decoded_trade->local_items.size() == 1);

  frame_bytes = encode_frame(make_frame(
      GuildState{true, "Guild", "Rank", "Notice", {GuildMemberState{"Hero", "Rank", true}},
                 {"Rank"}, true},
      42));
  buffer = frame_bytes;
  frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().message_id == MessageId::guild_state);
  const auto decoded_guild = decode_message<GuildState>(frames.front());
  assert(decoded_guild.has_value());
  assert(decoded_guild->guild_name == "Guild");

  return 0;
}
