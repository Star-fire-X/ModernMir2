#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_gm_skill_item_smoke failed at " << stage << '\n';
  return 1;
}

void append_dispatch(mir2::RuntimeDispatch& target, mir2::RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
}

mir2::CharacterRecord character(std::string account, std::string name) {
  mir2::CharacterRecord record;
  record.account_id = std::move(account);
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = 10;
  record.y = 10;
  record.ability.level = 20;
  record.ability.hp = 20;
  record.ability.max_hp = 20;
  record.ability.mp = 20;
  record.ability.max_mp = 20;
  record.magics[0].magic_id = 1;
  record.magics[0].level = 1;
  record.equipped_items[mir2::kEquipWeapon].index = 2;
  record.equipped_items[mir2::kEquipWeapon].make_index = 900;
  record.equipped_items[mir2::kEquipWeapon].dura = 1000;
  record.equipped_items[mir2::kEquipWeapon].dura_max = 1000;
  return record;
}

void enter(mir2::LogicRuntime& runtime, std::uint64_t session_id,
           const mir2::CharacterRecord& record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = record;
  static_cast<void>(runtime.route_logic_command(command));
  static_cast<void>(runtime.tick());
}

mir2::RuntimeDispatch say(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          std::string text, std::uint64_t now_ms) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::say;
  command.session_id = session_id;
  command.text = std::move(text);
  command.timestamp_ms = now_ms;
  auto dispatch = runtime.route_logic_command(command);
  append_dispatch(dispatch, runtime.tick(now_ms));
  return dispatch;
}

bool has_audit(const mir2::RuntimeDispatch& dispatch, std::string_view category,
               std::string_view needle) {
  for (const auto& audit : dispatch.audit_events) {
    if (audit.category == category && audit.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool has_save(const mir2::RuntimeDispatch& dispatch, std::string_view name) {
  for (const auto& request : dispatch.persist_requests) {
    if (request.kind == mir2::PersistRequestKind::save_character &&
        request.character_name == name) {
      return true;
    }
  }
  return false;
}

mir2::LogicRuntime make_runtime() {
  mir2::HostConfig config;
  const auto admin_list =
      std::filesystem::temp_directory_path() / "mir2_legacy_gm_skill_item_admin.txt";
  {
    std::ofstream output(admin_list);
    output << "3Admin\n";
  }
  config.runtime.legacy_admin_list = admin_list;
  config.maps.push_back(mir2::MapConfig{"0", "MainMap", {}, 100, 100, 10, 10});
  mir2::MagicConfig fireball;
  fireball.id = 1;
  fireball.name = "FireBall";
  config.magics.push_back(fireball);
  mir2::ItemConfig potion;
  potion.id = 1;
  potion.name = "Potion";
  potion.weight = 1;
  potion.dura_max = 1000;
  config.items.push_back(potion);
  mir2::ItemConfig healing_potion;
  healing_potion.id = 3;
  healing_potion.name = "Healing Potion";
  healing_potion.weight = 1;
  healing_potion.dura_max = 1000;
  config.items.push_back(healing_potion);
  mir2::ItemConfig sword;
  sword.id = 2;
  sword.name = "Sword";
  sword.std_mode = 5;
  sword.equip_slot = static_cast<int>(mir2::kEquipWeapon);
  sword.dura_max = 1000;
  config.items.push_back(sword);
  mir2::LogicRuntime runtime(config);
  runtime.initialize();
  return runtime;
}

std::size_t bag_count(const mir2::CharacterRecord& record, std::int32_t item_id) {
  auto count = std::size_t{0};
  for (const auto& item : record.bag_items) {
    if (item.index == item_id) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int main() {
  auto runtime = make_runtime();
  enter(runtime, 1, character("superadmin", "Admin"));
  enter(runtime, 2, character("acct_bob", "Bob"));

  const auto train = say(runtime, 1, "@Training FireBall 2", 1000);
  auto admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->magics[0].level != 2 ||
      !has_audit(train, "gm.command.ok", "cmd=Training") || !has_save(train, "Admin")) {
    return fail("training");
  }

  const auto op_train = say(runtime, 1, "@OPTraining Bob FireBall 3", 1200);
  auto bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->magics[0].level != 3 ||
      !has_audit(op_train, "gm.command.ok", "cmd=OPTraining") || !has_save(op_train, "Bob")) {
    return fail("op training");
  }

  const auto make = say(runtime, 1, "@Make Potion 2", 1400);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || bag_count(*admin, 1) != 2 ||
      !has_audit(make, "gm.command.ok", "cmd=Make") || !has_save(make, "Admin")) {
    return fail("make");
  }

  const auto del_item = say(runtime, 1, "@DeleteItem Potion 1", 1600);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || bag_count(*admin, 1) != 1 ||
      !has_audit(del_item, "gm.command.ok", "cmd=DeleteItem")) {
    return fail("delete item");
  }

  const auto make_multi = say(runtime, 1, "@Make Healing Potion 2", 1700);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || bag_count(*admin, 3) != 2 ||
      !has_audit(make_multi, "gm.command.ok", "cmd=Make") || !has_save(make_multi, "Admin")) {
    return fail("make multi word");
  }

  const auto del_multi = say(runtime, 1, "@DeleteItem Healing Potion 1", 1750);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || bag_count(*admin, 3) != 1 ||
      !has_audit(del_multi, "gm.command.ok", "cmd=DeleteItem")) {
    return fail("delete multi word");
  }

  const auto gold = say(runtime, 1, "@AddGold Bob 500", 1800);
  bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->gold != 500 ||
      !has_audit(gold, "gm.command.ok", "cmd=AddGold") || !has_save(gold, "Bob")) {
    return fail("add gold");
  }

  const auto del_gold = say(runtime, 1, "@DelGold Bob 200", 2000);
  bob = runtime.snapshot_character_actor("Bob");
  if (!bob.has_value() || bob->gold != 300 ||
      !has_audit(del_gold, "gm.command.ok", "cmd=DelGold")) {
    return fail("del gold");
  }

  const auto set_gold = say(runtime, 1, "@Test_GOLD_Change 123", 2200);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->gold != 123 ||
      !has_audit(set_gold, "gm.command.ok", "cmd=Test_GOLD_Change")) {
    return fail("test gold");
  }

  const auto refine = say(runtime, 1, "@WeaponRefinery 1 2 3 4", 2400);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->equipped_items[mir2::kEquipWeapon].desc[0] != 1 ||
      admin->equipped_items[mir2::kEquipWeapon].desc[1] != 2 ||
      admin->equipped_items[mir2::kEquipWeapon].desc[2] != 3 ||
      admin->equipped_items[mir2::kEquipWeapon].desc[5] != 4 ||
      !has_audit(refine, "gm.command.ok", "cmd=WeaponRefinery")) {
    return fail("weapon refinery");
  }

  const auto dura = say(runtime, 1, "@ChangeWeaponDura 5", 2600);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->equipped_items[mir2::kEquipWeapon].dura != 5000 ||
      !has_audit(dura, "gm.command.ok", "cmd=ChangeWeaponDura")) {
    return fail("weapon dura");
  }

  const auto del_skill = say(runtime, 1, "@DeleteSkill FireBall", 2800);
  admin = runtime.snapshot_character_actor("Admin");
  if (!admin.has_value() || admin->magics[0].magic_id != 0 ||
      !has_audit(del_skill, "gm.command.ok", "cmd=DeleteSkill")) {
    return fail("delete skill");
  }

  const auto offline = say(runtime, 1, "@AddGold Nobody 10", 3000);
  if (!has_audit(offline, "gm.command.pending", "offline_character_mutation")) {
    return fail("offline gold pending");
  }

  return 0;
}
