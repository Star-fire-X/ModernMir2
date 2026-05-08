#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

#undef assert
#define assert(expression)                                                     \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);  \
      std::abort();                                                            \
    }                                                                          \
  } while (false)

namespace {

mir2::ItemConfig item(std::int32_t id, std::string name, std::int32_t std_mode) {
  mir2::ItemConfig config;
  config.id = id;
  config.name = std::move(name);
  config.std_mode = std_mode;
  config.weight = 1;
  config.price = 100;
  config.looks = id;
  config.dura_max = 1000;
  return config;
}

mir2::LegacyUserItem user_item(std::int32_t make_index, std::int32_t id,
                               std::uint16_t dura = 1000,
                               std::uint16_t dura_max = 1000) {
  mir2::LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(id);
  item.dura = dura;
  item.dura_max = dura_max;
  return item;
}

mir2::CharacterRecord character(std::string name, std::int32_t x = 10,
                                std::int32_t y = 10) {
  mir2::CharacterRecord record;
  record.account_id = "acct_" + name;
  record.character_name = std::move(name);
  record.map_id = "0";
  record.x = x;
  record.y = y;
  record.gold = 10000;
  record.ability.level = 40;
  record.ability.hp = 100;
  record.ability.max_hp = 100;
  record.ability.mp = 50;
  record.ability.max_mp = 50;
  record.ability.dc = mir2::make_word(20, 20);
  record.ability.max_exp = 1000;
  record.ability.max_weight = 60;
  record.ability.max_wear_weight = 100;
  record.ability.max_hand_weight = 100;
  record.attack_mode = 0;
  return record;
}

mir2::LogicCommand enter(std::uint64_t session_id, mir2::CharacterRecord record) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = record.account_id;
  command.character_name = record.character_name;
  command.map_id = record.map_id;
  command.x = record.x;
  command.y = record.y;
  command.character = std::move(record);
  return command;
}

mir2::LogicCommand merchant(std::uint64_t session_id, std::uint64_t npc_id,
                            std::string action) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::merchant_select;
  command.session_id = session_id;
  command.target_actor_id = npc_id;
  command.text = std::move(action);
  return command;
}

mir2::LogicCommand take_on(std::uint64_t session_id, std::int32_t make_index,
                           std::string name, std::int32_t slot) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::take_on_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.item_slot = slot;
  command.text = std::move(name);
  return command;
}

mir2::LogicCommand attack(std::uint64_t session_id, std::uint64_t target_actor_id) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::attack;
  command.session_id = session_id;
  command.target_actor_id = target_actor_id;
  command.x = 10;
  command.y = 9;
  command.game_message.ident = mir2::kCmHit;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(
    const mir2::RuntimeDispatch& dispatch, std::uint16_t ident,
    std::uint64_t session_id = 0) {
  for (const auto& event : dispatch.session_events) {
    if (session_id != 0 && event.session_id != session_id) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

bool has_persist(const mir2::RuntimeDispatch& dispatch, mir2::PersistRequestKind kind) {
  return std::any_of(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                     [&](const mir2::PersistRequest& request) {
                       return request.kind == kind;
                     });
}

const mir2::PersistRequest* find_persist(const mir2::RuntimeDispatch& dispatch,
                                         mir2::PersistRequestKind kind) {
  const auto it = std::find_if(dispatch.persist_requests.begin(), dispatch.persist_requests.end(),
                               [&](const mir2::PersistRequest& request) {
                                 return request.kind == kind;
                               });
  return it != dispatch.persist_requests.end() ? &*it : nullptr;
}

std::uint64_t enter_actor(mir2::LogicRuntime& runtime, std::uint64_t session_id,
                          mir2::CharacterRecord record, std::uint64_t& now_ms) {
  static_cast<void>(runtime.route_logic_command(enter(session_id, std::move(record))));
  now_ms += 251;
  const auto dispatch = runtime.tick(now_ms);
  const auto map = find_packet(dispatch, mir2::kSmNewMap, session_id);
  assert(map.has_value());
  return static_cast<std::uint64_t>(static_cast<std::uint32_t>(map->message.recog));
}

mir2::RuntimeDispatch route_due(mir2::LogicRuntime& runtime,
                                const mir2::LogicCommand& command,
                                std::uint64_t& now_ms) {
  static_cast<void>(runtime.route_logic_command(command));
  now_ms += 251;
  return runtime.tick(now_ms);
}

mir2::HostConfig upgrade_config() {
  mir2::HostConfig config;
  config.runtime.legacy_random_seed = 3;
  config.runtime.upgrade_weapon_fee = 500;
  config.maps.push_back(mir2::MapConfig{"0", "UpgradeMap", {}, 0, 0, 20, 20});
  auto sword = item(1, "Upgradeable Sword", 5);
  sword.dc = mir2::make_word(5, 10);
  config.items.push_back(sword);
  config.items.push_back(item(2, "BlackStone", 41));
  auto necklace = item(3, "Power Necklace", 19);
  necklace.dc = mir2::make_word(3, 4);
  config.items.push_back(necklace);
  mir2::NpcConfig npc;
  npc.id = "upgrader";
  npc.map_id = "0";
  npc.name = "Blacksmith";
  npc.x = 11;
  npc.y = 10;
  npc.service = "upgrade";
  npc.dialog_sections.push_back(mir2::NpcDialogSectionConfig{"@upgradenow", "Upgrade"});
  npc.dialog_sections.push_back(mir2::NpcDialogSectionConfig{"@getbackupgnow", "Get back"});
  config.npcs.push_back(std::move(npc));
  return config;
}

}  // namespace

int main() {
  {
    auto config = upgrade_config();
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    std::uint64_t now_ms = 1000;

    auto hero = character("Hero");
    hero.equipped_items[mir2::kEquipWeapon] = user_item(1001, 1, 1000, 1000);
    hero.bag_items[0] = user_item(1002, 2, 5000, 5000);
    hero.bag_items[1] = user_item(1003, 3, 1000, 1000);
    static_cast<void>(enter_actor(runtime, 10, hero, now_ms));

    const auto start = route_due(runtime, merchant(10, 1, "@upgradenow"), now_ms);
    const auto* state_request =
        find_persist(start, mir2::PersistRequestKind::save_merchant_state);
    assert(state_request != nullptr);
    assert(has_persist(start, mir2::PersistRequestKind::save_character));
    assert(state_request->merchant_state.weapon_upgrades.size() == 1);
    assert(state_request->merchant_state.weapon_upgrades.front().item.make_index == 1001);
    assert(state_request->merchant_state.weapon_upgrades.front().updc > 0);
    auto snapshot = runtime.snapshot_character_actor("Hero");
    assert(snapshot.has_value());
    assert(mir2::is_empty(snapshot->equipped_items[mir2::kEquipWeapon]));
    assert(mir2::is_empty(snapshot->bag_items[0]));
    assert(mir2::is_empty(snapshot->bag_items[1]));
    assert(snapshot->gold == 9500);

    const auto early = route_due(runtime, merchant(10, 1, "@getbackupgnow"), now_ms);
    assert(!find_packet(early, mir2::kSmAddItem, 10).has_value());

    auto ready_state = state_request->merchant_state;
    ready_state.weapon_upgrades.front().ready_time_ms = 0;
    runtime.apply_merchant_states({ready_state});
    const auto get_back = route_due(runtime, merchant(10, 1, "@getbackupgnow"), now_ms);
    assert(find_packet(get_back, mir2::kSmAddItem, 10).has_value());
    snapshot = runtime.snapshot_character_actor("Hero");
    assert(snapshot.has_value());
    const auto bag_it =
        std::find_if(snapshot->bag_items.begin(), snapshot->bag_items.end(),
                     [](const mir2::LegacyUserItem& item) {
                       return !mir2::is_empty(item) && item.make_index == 1001;
                     });
    assert(bag_it != snapshot->bag_items.end());
    assert(bag_it->desc[10] != 0);
  }

  {
    auto config = upgrade_config();
    config.maps.front().fight_zone = true;
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    std::uint64_t now_ms = 5000;

    auto hero = character("IdentifyHero");
    hero.equipped_items[mir2::kEquipWeapon] = user_item(2001, 1, 1000, 1000);
    hero.equipped_items[mir2::kEquipWeapon].desc[10] = 10;
    const auto hero_id = enter_actor(runtime, 20, hero, now_ms);
    static_cast<void>(hero_id);
    auto target = character("Target", 10, 9);
    target.ability.hp = 200;
    target.ability.max_hp = 200;
    const auto target_id = enter_actor(runtime, 21, target, now_ms);

    const auto hit = route_due(runtime, attack(20, target_id), now_ms);
    assert(find_packet(hit, mir2::kSmUpdateItem, 20).has_value());
    auto snapshot = runtime.snapshot_character_actor("IdentifyHero");
    assert(snapshot.has_value());
    const auto& weapon = snapshot->equipped_items[mir2::kEquipWeapon];
    assert(!mir2::is_empty(weapon));
    assert(weapon.desc[0] == 1);
    assert(weapon.desc[10] == 0);
  }

  return 0;
}
