#include "importer/legacy_character_importer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace mir2 {

namespace {

constexpr std::size_t kLegacyDbHeaderSize = 124;
constexpr std::size_t kLegacyMirRecordSize = 4937;
constexpr std::size_t kLegacyHumRecordSize = 72;

#pragma pack(push, 1)

struct LegacyMirHuman {
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

struct LegacyMirBagItem {
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

struct LegacyMirUseMagic {
  std::array<LegacyUseMagicInfo, kMaxUserMagic> magics{};
};

struct LegacyMirSaveItem {
  std::array<LegacyUserItem, kMaxSaveItems> items{};
};

struct LegacyMirBlockData {
  LegacyMirHuman human{};
  LegacyMirBagItem bag_item{};
  LegacyMirUseMagic use_magic{};
  LegacyMirSaveItem save_item{};
};

struct LegacyMirRecord {
  std::uint8_t deleted{0};
  double update_date_time{0};
  std::array<char, 15> key{};
  LegacyMirBlockData block{};
};

struct LegacyHumRecord {
  std::uint8_t deleted{0};
  std::array<std::uint8_t, 3> pad{};
  double update_date_time{0};
  std::array<char, 15> key{};
  std::uint8_t pad_after_key{0};
  std::array<std::uint8_t, 44> block{};
};

#pragma pack(pop)

static_assert(sizeof(LegacyMirHuman) == 393);
static_assert(sizeof(LegacyMirBagItem) == 2360);
static_assert(sizeof(LegacyMirRecord) == kLegacyMirRecordSize);
static_assert(sizeof(LegacyHumRecord) == kLegacyHumRecordSize);

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open legacy DB: " + path.string());
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
}

std::int32_t read_le_i32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + sizeof(std::int32_t) > bytes.size()) {
    return 0;
  }
  std::int32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

std::string trim_nul(std::string text) {
  const auto nul = text.find('\0');
  if (nul != std::string::npos) {
    text.resize(nul);
  }
  while (!text.empty() && static_cast<unsigned char>(text.back()) <= 0x20) {
    text.pop_back();
  }
  const auto first = std::find_if(text.begin(), text.end(), [](unsigned char ch) {
    return ch > 0x20;
  });
  text.erase(text.begin(), first);
  return text;
}

template <std::size_t N>
std::string char_array_to_string(const std::array<char, N>& value) {
  return trim_nul(std::string(value.data(), value.data() + value.size()));
}

std::string short_string_to_string(const std::uint8_t* data, std::size_t capacity) {
  if (capacity == 0) {
    return {};
  }
  const auto length = std::min<std::size_t>(data[0], capacity - 1);
  return trim_nul(std::string(reinterpret_cast<const char*>(data + 1),
                              reinterpret_cast<const char*>(data + 1 + length)));
}

std::unordered_map<std::string, std::string> read_hum_accounts(
    const std::filesystem::path& hum_db_path) {
  std::unordered_map<std::string, std::string> accounts;
  if (hum_db_path.empty() || !std::filesystem::exists(hum_db_path)) {
    return accounts;
  }

  const auto bytes = read_binary_file(hum_db_path);
  if (bytes.size() < kLegacyDbHeaderSize) {
    return accounts;
  }
  const auto max_count = std::max(read_le_i32(bytes, 104), 0);
  for (std::int32_t index = 0; index < max_count; ++index) {
    const auto offset = kLegacyDbHeaderSize + static_cast<std::size_t>(index) * kLegacyHumRecordSize;
    if (offset + kLegacyHumRecordSize > bytes.size()) {
      break;
    }
    LegacyHumRecord record;
    std::memcpy(&record, bytes.data() + offset, sizeof(record));
    if (record.deleted != 0) {
      continue;
    }
    const auto* block = record.block.data();
    const auto character_name = short_string_to_string(block, 15);
    const auto account_id = short_string_to_string(block + 15, 11);
    if (!character_name.empty() && !account_id.empty()) {
      accounts[character_name] = account_id;
    }
  }
  return accounts;
}

bool contains_map(const HostConfig* config, const std::string& map_id) {
  if (config == nullptr) {
    return true;
  }
  return std::any_of(config->maps.begin(), config->maps.end(), [&](const MapConfig& map) {
    return map.id == map_id;
  });
}

std::set<std::int32_t> item_ids(const HostConfig* config) {
  std::set<std::int32_t> ids;
  if (config == nullptr) {
    return ids;
  }
  for (const auto& item : config->items) {
    ids.insert(item.id);
  }
  return ids;
}

std::set<std::int32_t> magic_ids(const HostConfig* config) {
  std::set<std::int32_t> ids;
  if (config == nullptr) {
    return ids;
  }
  for (const auto& magic : config->magics) {
    ids.insert(magic.id);
  }
  return ids;
}

void push_warning(LegacyCharacterImportReport& report, std::string character_name,
                  std::string kind, std::string value) {
  report.warnings.push_back(
      LegacyCharacterImportWarning{std::move(character_name), std::move(kind), std::move(value)});
}

void validate_item(const LegacyUserItem& item, const std::set<std::int32_t>& known_items,
                   const std::string& character_name, LegacyCharacterImportReport& report) {
  if (item.index == 0 || known_items.empty()) {
    return;
  }
  if (!known_items.contains(item.index)) {
    push_warning(report, character_name, "unknown_item", std::to_string(item.index));
  }
}

void validate_magic(const LegacyUseMagicInfo& magic, const std::set<std::int32_t>& known_magics,
                    const std::string& character_name, LegacyCharacterImportReport& report) {
  if (magic.magic_id == 0 || known_magics.empty()) {
    return;
  }
  if (!known_magics.contains(magic.magic_id)) {
    push_warning(report, character_name, "unknown_magic", std::to_string(magic.magic_id));
  }
}

CharacterRecord to_character_record(const LegacyMirRecord& record,
                                    const std::unordered_map<std::string, std::string>& hum_accounts) {
  CharacterRecord character;
  const auto& human = record.block.human;
  character.character_name = char_array_to_string(human.user_name);
  if (character.character_name.empty()) {
    character.character_name = char_array_to_string(record.key);
  }
  character.account_id = char_array_to_string(human.user_id);
  if (character.account_id.empty()) {
    if (const auto it = hum_accounts.find(character.character_name); it != hum_accounts.end()) {
      character.account_id = it->second;
    }
  }
  if (character.account_id.empty() && !character.character_name.empty()) {
    character.account_id = "legacy_" + character.character_name;
  }

  character.map_id = char_array_to_string(human.map_name);
  if (character.map_id.empty()) {
    character.map_id = "0";
  }
  character.x = human.x;
  character.y = human.y;
  character.dir = human.dir;
  character.hair = human.hair;
  character.sex = human.sex;
  character.job = human.job;
  character.gold = human.gold;
  character.attack_mode = static_cast<std::uint8_t>(std::clamp<std::int32_t>(human.attack_mode, 0, 4));
  character.pk_point = human.pk_point;
  character.daily_quest = human.daily_quest;
  character.body_luck = human.body_luck;
  character.ability.level = std::max<std::uint8_t>(human.level, 1);
  character.ability.hp = human.hp;
  character.ability.mp = human.mp;
  character.ability.max_hp = std::max<std::uint16_t>(human.hp, 15);
  character.ability.max_mp = std::max<std::uint16_t>(human.mp, 15);
  character.ability.exp = human.exp;
  character.ability.max_exp = std::max<std::uint32_t>(character.ability.max_exp, 100);
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;

  const auto& bag = record.block.bag_item;
  character.equipped_items[kEquipDress] = bag.dress;
  character.equipped_items[kEquipWeapon] = bag.weapon;
  character.equipped_items[kEquipRightHand] = bag.right_hand;
  character.equipped_items[kEquipNecklace] = bag.necklace;
  character.equipped_items[kEquipHelmet] = bag.helmet;
  character.equipped_items[kEquipArmRingLeft] = bag.arm_ring_left;
  character.equipped_items[kEquipArmRingRight] = bag.arm_ring_right;
  character.equipped_items[kEquipRingLeft] = bag.ring_left;
  character.equipped_items[kEquipRingRight] = bag.ring_right;
  character.equipped_items[kEquipBujuk] = bag.bujuk;
  character.equipped_items[kEquipBelt] = bag.belt;
  character.equipped_items[kEquipBoots] = bag.boots;
  character.equipped_items[kEquipCharm] = bag.charm;
  character.bag_items = bag.bags;
  character.storage_items = record.block.save_item.items;
  character.magics = record.block.use_magic.magics;
  std::copy(human.quest.begin(), human.quest.end(), character.quest_marks.begin());
  std::copy(human.quest_open_index.begin(), human.quest_open_index.end(),
            character.quest_open_units.begin());
  std::copy(human.quest_fin_index.begin(), human.quest_fin_index.end(),
            character.quest_units.begin());
  return character;
}

AccountRecord make_import_account(const std::string& account_id, const std::string& password) {
  AccountRecord account;
  account.account_id = account_id;
  account.password = password;
  account.display_name = account_id;
  account.user_name = account_id;
  account.birthday = "1970/01/01";
  account.quiz = "legacy";
  account.answer = "legacy";
  account.quiz2 = "legacy";
  account.answer2 = "legacy";
  return account;
}

void record_import(Repository& repository, const std::filesystem::path& source_file,
                   std::int32_t record_index, const CharacterRecord& character,
                   std::string status, std::string message,
                   const std::vector<std::uint8_t>& raw_record) {
  LegacyImportRecord import_record;
  import_record.source_file = source_file.string();
  import_record.record_index = record_index;
  import_record.account_id = character.account_id;
  import_record.character_name = character.character_name;
  import_record.status = std::move(status);
  import_record.message = std::move(message);
  import_record.raw_record = raw_record;
  repository.record_legacy_import(import_record);
}

}  // namespace

LegacyCharacterImportReport LegacyCharacterImporter::import_characters(
    const LegacyCharacterImportOptions& options, Repository& repository) const {
  if (options.mir_db_path.empty()) {
    throw std::runtime_error("legacy Mir.DB path is required for character import.");
  }

  LegacyCharacterImportReport report;
  const auto hum_accounts = read_hum_accounts(options.hum_db_path);
  const auto known_items = item_ids(options.config);
  const auto known_magics = magic_ids(options.config);
  const auto bytes = read_binary_file(options.mir_db_path);
  if (bytes.size() < kLegacyDbHeaderSize) {
    throw std::runtime_error("legacy Mir.DB is too small: " + options.mir_db_path.string());
  }

  const auto max_count = std::max(read_le_i32(bytes, 104), 0);
  for (std::int32_t index = 0; index < max_count; ++index) {
    const auto offset = kLegacyDbHeaderSize + static_cast<std::size_t>(index) * kLegacyMirRecordSize;
    if (offset + kLegacyMirRecordSize > bytes.size()) {
      ++report.failed;
      break;
    }
    ++report.scanned;

    std::vector<std::uint8_t> raw(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + kLegacyMirRecordSize));
    LegacyMirRecord record;
    std::memcpy(&record, raw.data(), sizeof(record));

    CharacterRecord character = to_character_record(record, hum_accounts);
    if (record.deleted != 0 || character.character_name.empty() || character.account_id.empty()) {
      ++report.failed;
      record_import(repository, options.mir_db_path, index, character, "failed",
                    "deleted_or_missing_identity", raw);
      continue;
    }

    if (!contains_map(options.config, character.map_id)) {
      push_warning(report, character.character_name, "unknown_map", character.map_id);
    }
    for (const auto& item : character.equipped_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& item : character.bag_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& item : character.storage_items) {
      validate_item(item, known_items, character.character_name, report);
    }
    for (const auto& magic : character.magics) {
      validate_magic(magic, known_magics, character.character_name, report);
    }

    const auto existing_characters = repository.list_characters(character.account_id);
    const auto duplicate = std::any_of(
        existing_characters.begin(), existing_characters.end(), [&](const CharacterRecord& existing) {
          return existing.character_name == character.character_name;
        });
    if (duplicate) {
      ++report.skipped;
      record_import(repository, options.mir_db_path, index, character, "skipped",
                    "duplicate_character", raw);
      continue;
    }

    if (!repository.load_account(character.account_id).has_value()) {
      static_cast<void>(
          repository.create_account(make_import_account(character.account_id, options.default_password)));
    }
    if (!repository.create_character(character)) {
      ++report.failed;
      record_import(repository, options.mir_db_path, index, character, "failed",
                    "create_character_failed", raw);
      continue;
    }

    ++report.imported;
    record_import(repository, options.mir_db_path, index, character, "imported", {}, raw);
  }

  return report;
}

}  // namespace mir2
