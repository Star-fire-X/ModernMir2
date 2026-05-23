#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_types.hpp"
#include "world/logic_runtime.hpp"

namespace {

mir2::LogicCommand make_enter(std::uint64_t session_id, mir2::CharacterRecord character) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::enter_world;
  command.session_id = session_id;
  command.account_id = character.account_id;
  command.character_name = character.character_name;
  command.map_id = character.map_id;
  command.x = character.x;
  command.y = character.y;
  command.character = std::move(character);
  return command;
}

mir2::LogicCommand make_use(std::uint64_t session_id, std::int32_t make_index,
                            std::string item_name) {
  mir2::LogicCommand command;
  command.kind = mir2::LogicCommandKind::eat_item;
  command.session_id = session_id;
  command.item_make_index = make_index;
  command.text = std::move(item_name);
  command.game_message.ident = mir2::kCmEat;
  command.game_message.recog = make_index;
  return command;
}

std::optional<mir2::DecodedLegacyGamePacket> find_packet(const mir2::RuntimeDispatch& dispatch,
                                                         std::uint16_t ident) {
  for (const auto& event : dispatch.session_events) {
    if (event.kind != mir2::SessionEventKind::send_packet) {
      continue;
    }
    const auto decoded = mir2::decode_legacy_game_packet(event.packet);
    if (decoded.has_value() && decoded->message.ident == ident) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::optional<mir2::LegacyClientItem> decode_client_item(std::string_view body) {
  mir2::LegacyClientItem item;
  if (!mir2::legacy_decode_buffer(body, &item, sizeof(item))) {
    return std::nullopt;
  }
  return item;
}

bool bag_has_make_index(const mir2::CharacterRecord& character, std::int32_t make_index) {
  return std::any_of(character.bag_items.begin(), character.bag_items.end(),
                     [&](const mir2::LegacyUserItem& item) {
                       return item.make_index == make_index;
                     });
}

std::int32_t bag_count_by_index(const mir2::CharacterRecord& character, std::int32_t item_index) {
  return static_cast<std::int32_t>(
      std::count_if(character.bag_items.begin(), character.bag_items.end(),
                    [&](const mir2::LegacyUserItem& item) { return item.index == item_index; }));
}

mir2::RuntimeDispatch tick_due(mir2::LogicRuntime& runtime, std::uint64_t& now_ms) {
  now_ms += 251;
  return runtime.tick(now_ms);
}

mir2::MagicConfig make_magic(std::int32_t id, std::string name, std::int32_t job = 99,
                             std::int32_t need_level = 1) {
  mir2::MagicConfig magic;
  magic.id = id;
  magic.name = std::move(name);
  magic.legacy.legacy_present = true;
  magic.legacy.effect_type = 1;
  magic.legacy.effect = id;
  magic.legacy.spell = 4;
  magic.legacy.job = job;
  magic.legacy.need_level = {need_level, need_level, need_level, need_level};
  magic.legacy.max_train = {500, 1500, 3000, 3000};
  magic.legacy.max_train_level = 3;
  return magic;
}

mir2::CharacterRecord base_character(std::string name, std::string map_id = "0") {
  mir2::CharacterRecord character;
  character.account_id = "acct_" + name;
  character.character_name = std::move(name);
  character.map_id = std::move(map_id);
  character.x = 10;
  character.y = 10;
  character.ability.level = 7;
  character.ability.hp = 5;
  character.ability.max_hp = 15;
  character.ability.mp = 5;
  character.ability.max_mp = 15;
  character.ability.max_weight = 100;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

}  // namespace

int main() {
  mir2::HostConfig config;
  auto no_drug = mir2::MapConfig{"0", "NoDrug", {}, 20, 20, 10, 10};
  no_drug.no_drug = true;
  auto no_random = mir2::MapConfig{"1", "NoRandom", {}, 20, 20, 10, 10};
  no_random.no_random_move = true;
  auto no_recall = mir2::MapConfig{"2", "NoRecall", {}, 20, 20, 10, 10};
  no_recall.no_recall = true;
  config.maps.push_back(no_drug);
  config.maps.push_back(no_random);
  config.maps.push_back(no_recall);
  config.maps.push_back(mir2::MapConfig{"3", "Open", {}, 20, 20, 10, 10});

  config.items.push_back(mir2::ItemConfig{1, "Healing Potion", 1, 10, 0, 0, 1, 1, -1, 10, 0});
  mir2::ItemConfig random_scroll{2, "Random Scroll", 1, 10, 31, 1, 2, 1, -1, 0, 0};
  random_scroll.scroll_kind = "random";
  config.items.push_back(random_scroll);
  mir2::ItemConfig town_scroll{3, "Town Portal", 1, 10, 31, 0, 3, 1, -1, 0, 0};
  town_scroll.scroll_kind = "town";
  config.items.push_back(town_scroll);
  mir2::ItemConfig bundle{4, "Potion Bundle", 1, 10, 3, 0, 4, 1, -1, 0, 0};
  bundle.unbind_item = "Healing Potion";
  bundle.unbind_count = 2;
  config.items.push_back(bundle);
  mir2::ItemConfig book{5, "Fireball", 1, 10, 4, 0, 5, 1, -1, 0, 0};
  config.items.push_back(book);
  config.magics.push_back(make_magic(1, "Fireball"));

  {
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = base_character("PotionUser", "3");
    character.bag_items[0] = mir2::LegacyUserItem{1001, 1, 3, 3};
    static_cast<void>(runtime.route_logic_command(make_enter(501, character)));
    std::uint64_t now_ms = 20;
    static_cast<void>(runtime.tick(now_ms));
    static_cast<void>(runtime.route_logic_command(make_use(501, 1001, "Healing Potion")));
    auto dispatch = tick_due(runtime, now_ms);
    const auto del = find_packet(dispatch, mir2::kSmDelItem);
    assert(del.has_value());
    const auto deleted = decode_client_item(del->body);
    assert(deleted.has_value());
    assert(deleted->make_index == 1001);
    assert(!find_packet(dispatch, mir2::kSmUpdateItem).has_value());
    assert(find_packet(dispatch, mir2::kSmEatOk).has_value());
    assert(!dispatch.persist_requests.empty());

    auto snapshot = runtime.snapshot_character_actor("PotionUser");
    assert(snapshot.has_value());
    assert(!bag_has_make_index(*snapshot, 1001));

    auto full_character = base_character("FullPotionUser", "3");
    full_character.ability.hp = full_character.ability.max_hp;
    full_character.ability.mp = full_character.ability.max_mp;
    full_character.bag_items[0] = mir2::LegacyUserItem{1002, 1, 3, 3};
    mir2::LogicRuntime full_runtime(config);
    full_runtime.initialize();
    static_cast<void>(full_runtime.route_logic_command(make_enter(502, full_character)));
    std::uint64_t full_now_ms = 20;
    static_cast<void>(full_runtime.tick(full_now_ms));
    static_cast<void>(full_runtime.route_logic_command(make_use(502, 1002, "Healing Potion")));
    dispatch = tick_due(full_runtime, full_now_ms);
    assert(find_packet(dispatch, mir2::kSmEatFail).has_value());
    snapshot = full_runtime.snapshot_character_actor("PotionUser");
    assert(!snapshot.has_value());
    snapshot = full_runtime.snapshot_character_actor("FullPotionUser");
    assert(snapshot.has_value());
    assert(snapshot->bag_items[0].make_index == 1002);
    assert(snapshot->bag_items[0].dura == 3);
  }

  {
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = base_character("NoDrugUser", "0");
    character.bag_items[0] = mir2::LegacyUserItem{2001, 1, 1, 1};
    static_cast<void>(runtime.route_logic_command(make_enter(601, character)));
    std::uint64_t now_ms = 20;
    static_cast<void>(runtime.tick(now_ms));
    static_cast<void>(runtime.route_logic_command(make_use(601, 2001, "Healing Potion")));
    const auto dispatch = tick_due(runtime, now_ms);
    assert(find_packet(dispatch, mir2::kSmEatFail).has_value());
    const auto snapshot = runtime.snapshot_character_actor("NoDrugUser");
    assert(snapshot.has_value());
    assert(bag_has_make_index(*snapshot, 2001));
  }

  {
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = base_character("ScrollUser", "1");
    character.bag_items[0] = mir2::LegacyUserItem{3001, 2, 1, 1};
    static_cast<void>(runtime.route_logic_command(make_enter(701, character)));
    std::uint64_t now_ms = 20;
    static_cast<void>(runtime.tick(now_ms));
    static_cast<void>(runtime.route_logic_command(make_use(701, 3001, "Random Scroll")));
    auto dispatch = tick_due(runtime, now_ms);
    assert(find_packet(dispatch, mir2::kSmEatFail).has_value());
    auto snapshot = runtime.snapshot_character_actor("ScrollUser");
    assert(snapshot.has_value());
    assert(bag_has_make_index(*snapshot, 3001));

    mir2::LogicRuntime recall_runtime(config);
    recall_runtime.initialize();
    auto recall_user = base_character("RecallUser", "2");
    recall_user.bag_items[0] = mir2::LegacyUserItem{3002, 3, 1, 1};
    static_cast<void>(recall_runtime.route_logic_command(make_enter(702, recall_user)));
    std::uint64_t recall_now_ms = 20;
    static_cast<void>(recall_runtime.tick(recall_now_ms));
    static_cast<void>(recall_runtime.route_logic_command(make_use(702, 3002, "Town Portal")));
    dispatch = tick_due(recall_runtime, recall_now_ms);
    assert(find_packet(dispatch, mir2::kSmEatFail).has_value());
    snapshot = recall_runtime.snapshot_character_actor("RecallUser");
    assert(snapshot.has_value());
    assert(bag_has_make_index(*snapshot, 3002));
  }

  {
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = base_character("BookUser", "3");
    character.bag_items[0] = mir2::LegacyUserItem{4001, 5, 1, 1};
    static_cast<void>(runtime.route_logic_command(make_enter(801, character)));
    std::uint64_t now_ms = 20;
    static_cast<void>(runtime.tick(now_ms));
    static_cast<void>(runtime.route_logic_command(make_use(801, 4001, "Fireball")));
    const auto dispatch = tick_due(runtime, now_ms);
    assert(find_packet(dispatch, mir2::kSmAddMagic).has_value());
    assert(find_packet(dispatch, mir2::kSmDelItem).has_value());
    assert(!dispatch.persist_requests.empty());
    const auto snapshot = runtime.snapshot_character_actor("BookUser");
    assert(snapshot.has_value());
    assert(snapshot->magics[0].magic_id == 1);
    assert(!bag_has_make_index(*snapshot, 4001));
  }

  {
    mir2::LogicRuntime runtime(config);
    runtime.initialize();
    auto character = base_character("BundleUser", "3");
    character.bag_items[0] = mir2::LegacyUserItem{5001, 4, 1, 1};
    static_cast<void>(runtime.route_logic_command(make_enter(901, character)));
    std::uint64_t now_ms = 20;
    static_cast<void>(runtime.tick(now_ms));
    static_cast<void>(runtime.route_logic_command(make_use(901, 5001, "Potion Bundle")));
    const auto dispatch = tick_due(runtime, now_ms);
    assert(find_packet(dispatch, mir2::kSmEatOk).has_value());
    assert(find_packet(dispatch, mir2::kSmDelItem).has_value());
    assert(!dispatch.persist_requests.empty());
    const auto snapshot = runtime.snapshot_character_actor("BundleUser");
    assert(snapshot.has_value());
    assert(!bag_has_make_index(*snapshot, 5001));
    assert(bag_count_by_index(*snapshot, 1) == 2);
  }

  return 0;
}
