#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "protocol/legacy_types.hpp"

namespace mir2::tests {

constexpr std::size_t kFixtureLegacyHeaderSize = 124;

#pragma pack(push, 1)

struct FixtureLegacyMirHuman {
  std::array<char, 20> user_name{};
  std::array<char, 20> map_name{};
  std::uint16_t x{0};
  std::uint16_t y{0};
  std::uint8_t dir{0};
  std::uint8_t hair{0};
  std::uint8_t hair_color_r{0};
  std::uint8_t hair_color_g{0};
  std::uint8_t hair_color_b{0};
  std::uint8_t sex{0};
  std::uint8_t job{0};
  std::int32_t gold{0};
  std::uint8_t level{1};
  std::uint16_t hp{15};
  std::uint16_t mp{15};
  std::uint32_t exp{0};
  std::array<std::uint16_t, 12> status_arr{};
  std::array<char, 20> home_map{};
  std::uint16_t home_x{0};
  std::uint16_t home_y{0};
  std::int32_t pk_point{0};
  std::uint8_t allow_party{0};
  std::uint8_t free_gulity_count{0};
  std::uint8_t attack_mode{0};
  std::uint8_t inc_health{0};
  std::uint8_t inc_spell{0};
  std::uint8_t inc_healing{0};
  std::uint8_t fight_zone_die{0};
  std::array<char, 20> user_id{};
  std::uint8_t db_version{0};
  std::uint8_t bonus_apply{0};
  std::int32_t bonus_point{0};
  std::uint32_t daily_quest{0};
  std::uint8_t horse_ride{0};
  std::uint16_t cghi_use_time{0};
  double body_luck{0};
  std::uint8_t enable_g_recall{0};
  std::array<std::uint8_t, 3> bytes_1{};
  std::array<std::uint8_t, 24> quest_open_index{};
  std::array<std::uint8_t, 24> quest_fin_index{};
  std::array<std::uint8_t, 176> quest{};
  std::uint8_t horse_race{0};
};

struct FixtureLegacyMirBagItem {
  LegacyUserItem dress{};
  LegacyUserItem weapon{};
  LegacyUserItem right_hand{};
  LegacyUserItem helmet{};
  LegacyUserItem necklace{};
  LegacyUserItem arm_ring_left{};
  LegacyUserItem arm_ring_right{};
  LegacyUserItem ring_left{};
  LegacyUserItem ring_right{};
  LegacyUserItem bujuk{};
  LegacyUserItem belt{};
  LegacyUserItem boots{};
  LegacyUserItem charm{};
  std::array<LegacyUserItem, kMaxBagItems> bags{};
};

struct FixtureLegacyMirRecord {
  std::uint8_t deleted{0};
  double update_date_time{0};
  std::array<char, 15> key{};
  FixtureLegacyMirHuman human{};
  FixtureLegacyMirBagItem bag_item{};
  std::array<LegacyUseMagicInfo, kMaxUserMagic> magics{};
  std::array<LegacyUserItem, kMaxSaveItems> storage{};
};

struct FixtureLegacyHumRecord {
  std::uint8_t deleted{0};
  std::array<std::uint8_t, 3> pad{};
  double update_date_time{0};
  std::array<char, 15> key{};
  std::uint8_t pad_after_key{0};
  std::array<std::uint8_t, 44> block{};
};

#pragma pack(pop)

static_assert(sizeof(FixtureLegacyMirHuman) == 393);
static_assert(sizeof(FixtureLegacyMirRecord) == 4937);
static_assert(sizeof(FixtureLegacyHumRecord) == 72);

struct LegacyCharacterFixturePaths {
  std::filesystem::path hum_db;
  std::filesystem::path mir_db;
};

inline void set_char_array(auto& target, const std::string& value) {
  target.fill('\0');
  std::memcpy(target.data(), value.data(), std::min(target.size(), value.size()));
}

inline void set_short_string(std::uint8_t* target, std::size_t capacity,
                             const std::string& value) {
  target[0] = static_cast<std::uint8_t>(std::min<std::size_t>(capacity - 1, value.size()));
  std::memset(target + 1, 0, capacity - 1);
  std::memcpy(target + 1, value.data(), target[0]);
}

inline void set_i32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

inline std::vector<std::uint8_t> make_header(std::int32_t max_count) {
  std::vector<std::uint8_t> header(kFixtureLegacyHeaderSize, 0);
  const std::string title = "legend of mir database file 1999/1";
  header[0] = static_cast<std::uint8_t>(title.size());
  std::memcpy(header.data() + 1, title.data(), title.size());
  set_i32(header, 104, max_count);
  return header;
}

inline void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

inline LegacyUserItem make_fixture_item(std::int32_t make_index, std::uint16_t item_index) {
  LegacyUserItem item;
  item.make_index = make_index;
  item.index = item_index;
  item.dura = 1000;
  item.dura_max = 2000;
  return item;
}

inline LegacyCharacterFixturePaths write_legacy_character_fixture(
    const std::filesystem::path& root, std::string account_id = "legacyacct",
    std::string character_name = "LegacyHero", std::string map_id = "0",
    std::uint16_t item_index = 1, std::uint16_t magic_id = 1) {
  std::filesystem::create_directories(root);
  const auto hum_db = root / "Hum.DB";
  const auto mir_db = root / "Mir.DB";

  FixtureLegacyHumRecord hum;
  set_short_string(reinterpret_cast<std::uint8_t*>(hum.key.data()), hum.key.size(), character_name);
  set_short_string(hum.block.data(), 15, character_name);
  set_short_string(hum.block.data() + 15, 11, account_id);

  auto hum_bytes = make_header(1);
  const auto* hum_ptr = reinterpret_cast<const std::uint8_t*>(&hum);
  hum_bytes.insert(hum_bytes.end(), hum_ptr, hum_ptr + sizeof(hum));
  write_bytes(hum_db, hum_bytes);

  FixtureLegacyMirRecord mir;
  set_char_array(mir.key, character_name);
  set_char_array(mir.human.user_name, character_name);
  set_char_array(mir.human.user_id, account_id);
  set_char_array(mir.human.map_name, map_id);
  mir.human.x = 123;
  mir.human.y = 234;
  mir.human.dir = 3;
  mir.human.hair = 2;
  mir.human.sex = 1;
  mir.human.job = 2;
  mir.human.gold = 9876;
  mir.human.level = 7;
  mir.human.hp = 88;
  mir.human.mp = 44;
  mir.human.exp = 12345;
  mir.bag_item.weapon = make_fixture_item(1001, item_index);
  mir.bag_item.bags[0] = make_fixture_item(1002, item_index);
  mir.storage[0] = make_fixture_item(1003, item_index);
  mir.magics[0].magic_id = magic_id;
  mir.magics[0].level = 2;
  mir.magics[0].key = '1';
  mir.magics[0].cur_train = 321;

  auto mir_bytes = make_header(1);
  const auto* mir_ptr = reinterpret_cast<const std::uint8_t*>(&mir);
  mir_bytes.insert(mir_bytes.end(), mir_ptr, mir_ptr + sizeof(mir));
  write_bytes(mir_db, mir_bytes);

  return LegacyCharacterFixturePaths{hum_db, mir_db};
}

}  // namespace mir2::tests
