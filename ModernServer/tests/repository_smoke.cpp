#include <algorithm>
#include <filesystem>

#include "storage/repository.hpp"

namespace {

mir2::CharacterRecord make_character(const std::string& account_id, const std::string& character_name,
                                     std::uint8_t job, std::uint8_t sex, std::uint8_t hair) {
  mir2::CharacterRecord record;
  record.account_id = account_id;
  record.character_name = character_name;
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.job = job;
  record.sex = sex;
  record.hair = hair;
  record.gold = 1000;
  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  return record;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto temp_root = std::filesystem::temp_directory_path() / "mir2_repository_smoke";
  std::error_code ignored;
  std::filesystem::remove_all(temp_root, ignored);
  std::filesystem::create_directories(temp_root, ignored);

  mir2::Repository repository(temp_root / "mir2.sqlite");
  repository.ensure_schema(source_root / "schema" / "mir2.sql");
  repository.seed_runtime();

  const auto seeded = repository.list_characters("guest");
  if (seeded.empty()) {
    return 1;
  }

  const auto created = make_character("guest", "Mage", 1, 1, 2);
  if (!repository.create_character(created)) {
    return 1;
  }

  auto updated = created;
  updated.guild_name = "DragonSlayers";
  updated.guild_title = "Lord";
  updated.magics[0].magic_id = 1;
  updated.magics[0].level = 2;
  updated.magics[0].key = '1';
  updated.magics[0].cur_train = 345;
  updated.magics[1].magic_id = 5;
  updated.magics[1].level = 1;
  updated.magics[1].key = '5';
  updated.magics[1].cur_train = 678;
  updated.quest_marks[7] = 3;
  updated.quest_open_units[8] = 1;
  updated.quest_units[9] = 2;
  updated.script_params[1] = 42;
  updated.daily_quest = 99;
  updated.slaves[0].name = "__WhiteSkeleton";
  updated.slaves[0].slave_exp = 123;
  updated.slaves[0].slave_exp_level = 2;
  updated.slaves[0].slave_make_level = 1;
  updated.slaves[0].remain_royalty_sec = 86400;
  updated.slaves[0].hp = 77;
  updated.slaves[0].mp = 3;
  updated.body_luck = 12500.0;
  repository.save_character(updated);

  const auto loaded = repository.load_character("guest", "Mage");
  if (!loaded.has_value() || loaded->guild_name != "DragonSlayers" ||
      loaded->guild_title != "Lord" || loaded->magics[0].magic_id != 1 ||
      loaded->magics[0].level != 2 || loaded->magics[0].key != '1' ||
      loaded->magics[0].cur_train != 345 || loaded->magics[1].magic_id != 5 ||
      loaded->magics[1].level != 1 || loaded->magics[1].key != '5' ||
      loaded->magics[1].cur_train != 678 || loaded->quest_marks[7] != 3 ||
      loaded->quest_open_units[8] != 1 || loaded->quest_units[9] != 2 ||
      loaded->script_params[1] != 42 || loaded->daily_quest != 99 ||
      loaded->slaves[0].name != "__WhiteSkeleton" ||
      loaded->slaves[0].slave_exp != 123 ||
      loaded->slaves[0].slave_exp_level != 2 ||
      loaded->slaves[0].slave_make_level != 1 ||
      loaded->slaves[0].remain_royalty_sec != 86400 ||
      loaded->slaves[0].hp != 77 || loaded->slaves[0].mp != 3 ||
      loaded->body_luck != 12500.0) {
    return 1;
  }

  mir2::MerchantStateRecord merchant;
  merchant.merchant_key = "smith-0";
  merchant.npc_id = "smith";
  merchant.map_id = "0";
  merchant.goods.push_back(mir2::LegacyUserItem{});
  merchant.goods.front().index = 7;
  merchant.goods.front().make_index = 7001;
  mir2::LegacyWeaponUpgradeRecord upgrade;
  upgrade.character_name = "Mage";
  upgrade.item.index = 8;
  upgrade.item.make_index = 8001;
  upgrade.updc = 9;
  upgrade.upsc = 3;
  upgrade.upmc = 4;
  upgrade.durapoint = 15;
  upgrade.ready_time_ms = 123456;
  merchant.weapon_upgrades.push_back(upgrade);
  repository.save_merchant_state(merchant);
  const auto merchants = repository.load_merchant_states();
  const auto merchant_it =
      std::find_if(merchants.begin(), merchants.end(),
                   [](const mir2::MerchantStateRecord& state) {
                     return state.merchant_key == "smith-0";
                   });
  if (merchant_it == merchants.end() || merchant_it->goods.size() != 1 ||
      merchant_it->goods.front().make_index != 7001 ||
      merchant_it->weapon_upgrades.size() != 1 ||
      merchant_it->weapon_upgrades.front().character_name != "Mage" ||
      merchant_it->weapon_upgrades.front().item.make_index != 8001 ||
      merchant_it->weapon_upgrades.front().updc != 9 ||
      merchant_it->weapon_upgrades.front().upsc != 3 ||
      merchant_it->weapon_upgrades.front().upmc != 4 ||
      merchant_it->weapon_upgrades.front().durapoint != 15 ||
      merchant_it->weapon_upgrades.front().ready_time_ms != 0) {
    return 1;
  }

  const auto loaded_by_name = repository.load_character_by_name("Mage");
  if (!loaded_by_name.has_value() || loaded_by_name->account_id != "guest" ||
      loaded_by_name->character_name != "Mage" ||
      loaded_by_name->guild_name != "DragonSlayers") {
    return 1;
  }

  mir2::GuildState guild_state;
  guild_state.guild_name = "DragonSlayers";
  guild_state.lord = "Mage";
  guild_state.members = {"Mage", "Knight"};
  guild_state.applicants = {"Visitor"};
  repository.save_guild_state(guild_state);
  const auto guild_snapshot = repository.load_guild_castle_snapshot();
  const auto guild_it =
      std::find_if(guild_snapshot.guilds.begin(), guild_snapshot.guilds.end(),
                   [](const mir2::GuildState& guild) {
                     return guild.guild_name == "DragonSlayers";
                   });
  if (guild_it == guild_snapshot.guilds.end() || guild_it->lord != "Mage" ||
      guild_it->members.size() != 2 || guild_it->applicants.size() != 1 ||
      guild_it->applicants.front() != "Visitor") {
    return 1;
  }

  const auto characters = repository.list_characters("guest");
  const auto created_it =
      std::find_if(characters.begin(), characters.end(), [](const mir2::CharacterRecord& character) {
        return character.character_name == "Mage" && character.hair == 2 && character.job == 1 &&
               character.sex == 1 && character.guild_name == "DragonSlayers" &&
               character.guild_title == "Lord";
      });
  if (created_it == characters.end()) {
    return 1;
  }

  if (!repository.delete_character("guest", "Mage")) {
    return 1;
  }

  const auto after_delete = repository.list_characters("guest");
  if (std::any_of(after_delete.begin(), after_delete.end(),
                  [](const mir2::CharacterRecord& character) {
                    return character.character_name == "Mage";
                  })) {
    return 1;
  }

  if (repository.load_character("guest", "Mage").has_value() ||
      repository.load_character("guest", "Missing").has_value()) {
    return 1;
  }

  const auto alpha = make_character("alpha", "SharedName", 0, 0, 1);
  const auto beta = make_character("beta", "SharedName", 1, 1, 2);
  const auto beta_unique = make_character("beta", "BetaOnly", 2, 0, 3);
  if (!repository.create_character(alpha) || !repository.create_character(beta) ||
      !repository.create_character(beta_unique)) {
    return 1;
  }

  const auto alpha_loaded = repository.load_character("alpha", "SharedName");
  const auto beta_loaded = repository.load_character("beta", "SharedName");
  if (!alpha_loaded.has_value() || !beta_loaded.has_value() ||
      alpha_loaded->account_id != "alpha" || beta_loaded->account_id != "beta" ||
      alpha_loaded->job != 0 || beta_loaded->job != 1) {
    return 1;
  }

  const auto alpha_list = repository.list_characters("alpha");
  const auto beta_list = repository.list_characters("beta");
  if (alpha_list.size() != 1 || beta_list.size() != 2 ||
      std::none_of(beta_list.begin(), beta_list.end(),
                   [](const mir2::CharacterRecord& character) {
                     return character.character_name == "BetaOnly";
                   })) {
    return 1;
  }

  if (!repository.delete_character("alpha", "SharedName") ||
      repository.load_character("alpha", "SharedName").has_value() ||
      !repository.load_character("beta", "SharedName").has_value()) {
    return 1;
  }

  if (repository.delete_character("alpha", "SharedName")) {
    return 1;
  }

  std::filesystem::remove_all(temp_root, ignored);
  return 0;
}
