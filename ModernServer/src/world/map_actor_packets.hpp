#pragma once

// Implementation detail for map_actor.cpp; included inside namespace mir2.
namespace {
LegacyPacket make_ack_packet(std::uint64_t session_id, bool ok) {
  return make_legacy_raw_packet(session_id,
                                std::string(ok ? "+GOOD/" : "+FAIL/") +
                                    std::to_string(tick_count_ms()));
}

LegacyPacket make_turn_like_packet(std::uint64_t session_id, std::uint16_t ident,
                                   const GameObject& object, bool include_name) {
  const auto desc = make_char_desc(object);
  auto body = legacy_encode_buffer(&desc, sizeof(desc));
  if (include_name) {
    body += legacy_encode_string(actor_name(object) + "/" + std::to_string(actor_name_color(object)));
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ident, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()),
                           make_word(actor_dir(object), actor_light(object))),
      body);
}

std::uint16_t resolve_hit_ident(std::uint16_t cm_ident) {
  return legacy::cm_attack_ident_to_sm(cm_ident);
}

LegacyPacket make_hit_packet(std::uint64_t session_id, const GameObject& object,
                             std::uint16_t cm_ident) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(resolve_hit_ident(cm_ident), static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)));
}

LegacyPacket make_rush_packet(std::uint64_t session_id, const GameObject& object) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmRush, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)));
}

LegacyPacket make_rush_kung_packet(std::uint64_t session_id, const GameObject& object,
                                   std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmRushKung, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(x),
                           static_cast<std::uint16_t>(y), actor_dir(object)));
}

LegacyPacket make_sword_state_packet(std::uint64_t session_id, std::string state) {
  return make_legacy_raw_packet(session_id, std::move(state) + "/" +
                                                std::to_string(tick_count_ms()));
}

LegacyStdItem make_std_item(const LegacyUserItem& item,
                            const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  LegacyStdItem std_item;
  const auto* config = find_item_config(item_configs, item.index);
  const auto name =
      config != nullptr && !config->name.empty() ? config->name : "Item " + std::to_string(item.index);
  set_short_string(std_item.name, name);
  if (config != nullptr) {
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    std_item.std_mode = static_cast<std::uint8_t>(std::clamp(upgraded.std_mode, 0, 255));
    std_item.shape = static_cast<std::uint8_t>(std::clamp(upgraded.shape, 0, 255));
    std_item.weight = static_cast<std::uint8_t>(std::clamp(upgraded.weight, 0, 255));
    std_item.ani_count = static_cast<std::uint8_t>(std::clamp(upgraded.ani_count, 0, 255));
    std_item.looks =
        static_cast<std::uint16_t>(std::clamp(upgraded.looks > 0 ? upgraded.looks : item.index, 0, 65535));
    std_item.price = upgraded.price;
    std_item.dura_max =
        static_cast<std::uint16_t>(std::clamp(upgraded.dura_max > 0 ? upgraded.dura_max : item.dura_max,
                                              0, 65535));
    std_item.hp_add = upgraded.hp_add;
    std_item.mp_add = upgraded.mp_add;
    std_item.ac = upgraded.ac;
    std_item.mac = upgraded.mac;
    std_item.dc = upgraded.dc;
    std_item.mc = upgraded.mc;
    std_item.sc = upgraded.sc;
    std_item.need = static_cast<std::uint8_t>(std::clamp(upgraded.need, 0, 255));
    std_item.need_level = static_cast<std::uint8_t>(std::clamp(upgraded.need_level, 0, 255));
    std_item.stock = upgraded.stock;
    std_item.special_pwr =
        static_cast<std::int8_t>(std::clamp(upgraded.special_pwr, -128, 127));
    std_item.item_desc = static_cast<std::uint8_t>(std::clamp(upgraded.item_desc, 0, 255));
    std_item.accurate = static_cast<std::uint8_t>(std::clamp(upgraded.accurate, 0, 255));
    std_item.agility = static_cast<std::uint8_t>(std::clamp(upgraded.agility, 0, 255));
    std_item.atk_spd = static_cast<std::uint8_t>(std::clamp(upgraded.atk_spd, 0, 255));
    std_item.mg_avoid = static_cast<std::uint8_t>(std::clamp(upgraded.mg_avoid, 0, 255));
    std_item.strong = static_cast<std::uint8_t>(std::clamp(upgraded.strong, 0, 255));
    std_item.undead = static_cast<std::uint8_t>(std::clamp(upgraded.undead, 0, 255));
    std_item.exp_add = upgraded.exp_add;
    std_item.eff_type1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_type1, 0, 255));
    std_item.eff_rate1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_rate1, 0, 255));
    std_item.eff_value1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_value1, 0, 255));
    std_item.eff_type2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_type2, 0, 255));
    std_item.eff_rate2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_rate2, 0, 255));
    std_item.eff_value2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_value2, 0, 255));
  } else {
    std_item.looks =
        static_cast<std::uint16_t>(std::clamp<std::int32_t>(item.index, 0, 65535));
    std_item.dura_max = item.dura_max;
  }
  return std_item;
}

LegacyClientItem make_client_item(const LegacyUserItem& item,
                                  const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  LegacyClientItem client_item;
  client_item.item = make_std_item(item, item_configs);
  client_item.make_index = item.make_index;
  client_item.dura = item.dura;
  client_item.dura_max = item_dura_max(item, item_configs);
  return client_item;
}

LegacyDefMagic make_def_magic(const LegacyUseMagicInfo& magic,
                              const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  LegacyDefMagic def;
  const auto config_it = magic_configs.find(magic.magic_id);
  const auto name = config_it != magic_configs.end() && !config_it->second.name.empty()
                        ? config_it->second.name
                        : "Magic " + std::to_string(magic.magic_id);
  set_short_string(def.magic_name, name);
  set_short_string(def.desc, name);
  def.magic_id = magic.magic_id;
  def.effect = static_cast<std::uint8_t>(magic.magic_id);
  def.delay_time = 1000;
  def.max_train_level = 3;
  def.need_level = {1, 1, 1, 1};
  def.max_train = {100, 300, 600, 900};
  if (config_it != magic_configs.end()) {
    const auto& config = config_it->second;
    if (config.legacy.legacy_present) {
      def.effect_type = static_cast<std::uint8_t>(std::clamp(config.legacy.effect_type, 0, 255));
      def.effect = static_cast<std::uint8_t>(std::clamp(config.legacy.effect, 0, 255));
      def.spell = static_cast<std::uint16_t>(std::clamp(config.legacy.spell, 0, 65535));
      def.min_power =
          static_cast<std::uint16_t>(std::clamp(config.legacy.min_power, 0, 65535));
      def.max_power =
          static_cast<std::uint16_t>(std::clamp(config.legacy.max_power, 0, 65535));
      def.job = static_cast<std::uint8_t>(std::clamp(config.legacy.job, 0, 255));
      for (std::size_t index = 0; index < def.need_level.size(); ++index) {
        def.need_level[index] =
            static_cast<std::uint8_t>(std::clamp(config.legacy.need_level[index], 0, 255));
        def.max_train[index] = config.legacy.max_train[index];
      }
      def.max_train_level =
          static_cast<std::uint8_t>(std::clamp(config.legacy.max_train_level, 0, 255));
      def.delay_time = std::max(config.legacy.delay_time, 0);
      def.def_spell = static_cast<std::uint8_t>(std::clamp(config.legacy.def_spell, 0, 255));
      def.def_min_power =
          static_cast<std::uint8_t>(std::clamp(config.legacy.def_min_power, 0, 255));
      def.def_max_power =
          static_cast<std::uint8_t>(std::clamp(config.legacy.def_max_power, 0, 255));
      set_short_string(def.desc, config.legacy.desc.empty() ? name : config.legacy.desc);
    } else {
      def.spell = static_cast<std::uint16_t>(std::max(config.mp_cost, 0));
      def.min_power = static_cast<std::uint16_t>(std::max(config.power, 0));
      def.max_power = static_cast<std::uint16_t>(std::max(config.power, 0));
      def.effect = static_cast<std::uint8_t>(std::clamp(config.id, 0, 255));
    }
  }
  return def;
}

LegacyClientMagic make_client_magic(
    const LegacyUseMagicInfo& magic,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  LegacyClientMagic client_magic;
  client_magic.key = magic.key;
  client_magic.level = magic.level;
  client_magic.cur_train = magic.cur_train;
  client_magic.def = make_def_magic(magic, magic_configs);
  return client_magic;
}

LegacyPacket make_new_map_packet(std::uint64_t session_id, const Player& player,
                                 const MapConfig& map_config) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmNewMap, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(player.x()),
                           static_cast<std::uint16_t>(player.y()), 0),
      legacy_encode_string(map_config.id));
}

LegacyPacket make_logon_packet(std::uint64_t session_id, const Player& player) {
  LegacyMessageBodyWL body;
  body.lparam1 = player.character().feature;
  body.lparam2 = player.character().status;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmLogon, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(player.x()),
                           static_cast<std::uint16_t>(player.y()),
                           make_word(player.character().dir, player.character().light)),
      legacy_encode_buffer(&body, sizeof(body)));
}

LegacyPacket make_area_state_packet(std::uint64_t session_id, std::int32_t area_state) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmAreaState, area_state, 0, 0, 0));
}

LegacyPacket make_map_description_packet(std::uint64_t session_id, const MapConfig& map_config) {
  const auto title = map_config.title.empty() ? map_config.id : map_config.title;
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMapDescription, 0, 0, 0, 0),
                                 legacy_encode_string(title));
}

LegacyPacket make_username_packet(std::uint64_t session_id, std::uint64_t actor_id,
                                  std::string user_name, std::uint8_t color = kDefaultNameColor) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmUsername, static_cast<std::int32_t>(actor_id), color, 0, 0),
      legacy_encode_string(std::move(user_name)));
}

LegacyPacket make_ability_packet(std::uint64_t session_id, const CharacterRecord& character) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmAbility, character.gold, character.job, 0, 0),
      legacy_encode_buffer(&character.ability, sizeof(character.ability)));
}

LegacyPacket make_bag_items_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : player.character().bag_items) {
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmBagItems, static_cast<std::int32_t>(player.id()), 0, 0, count), body);
}

LegacyPacket make_use_items_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  for (std::size_t index = 0; index < player.character().equipped_items.size(); ++index) {
    const auto& item = player.character().equipped_items[index];
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += std::to_string(index);
    body.push_back('/');
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
  }
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendUseItems, 0, 0, 0, 0), body);
}

LegacyPacket make_my_magic_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  std::string body;
  std::int32_t total_delay = 0;
  std::uint16_t count = 0;
  for (const auto& magic : player.character().magics) {
    if (is_empty(magic)) {
      continue;
    }
    const auto client_magic = make_client_magic(magic, magic_configs);
    total_delay += client_magic.def.delay_time;
    body += legacy_encode_buffer(&client_magic, sizeof(client_magic));
    body.push_back('/');
    ++count;
  }
  const auto checksum = (total_delay ^ 0x773F1A34) ^ 0x4BBC2255;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendMyMagic, checksum, 0, 0, count), body);
}

LegacyPacket make_add_magic_packet(
    std::uint64_t session_id, const LegacyUseMagicInfo& magic,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  const auto client_magic = make_client_magic(magic, magic_configs);
  return make_legacy_game_packet(
      session_id, 0, 0, make_default_message(kSmAddMagic, 0, 0, 0, 1),
      legacy_encode_buffer(&client_magic, sizeof(client_magic)));
}

LegacyPacket make_del_magic_packet(std::uint64_t session_id, std::int32_t magic_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDelMagic, magic_id, 0, 0, 1));
}

LegacyPacket make_magic_lvexp_packet(std::uint64_t session_id, std::int32_t magic_id,
                                     std::int32_t level, std::int32_t cur_train) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMagicLvExp, magic_id, static_cast<std::uint16_t>(level),
                           low_word(cur_train), high_word(cur_train)));
}

LegacyPacket make_hear_packet(std::uint64_t session_id, std::uint64_t actor_id,
                              const std::string& message) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmHear, static_cast<std::int32_t>(actor_id),
                           make_word(kDefaultChatColor, kDefaultChatShadow), 0, 0),
      legacy_encode_string(message));
}

LegacyPacket make_system_notice_packet(std::uint64_t session_id, const std::string& message) {
  return make_hear_packet(session_id, 0, message);
}

std::string make_shield_apply_self_notice(const std::string& shield_name) {
  return (shield_name.empty() ? "A magical shield" : shield_name) + " surrounds you.";
}

std::string make_shield_apply_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + " is surrounded by " +
         (shield_name.empty() ? std::string("a magical shield") : shield_name) + ".";
}

std::string make_shield_break_self_notice(const std::string& shield_name) {
  return "Your " + (shield_name.empty() ? std::string("magical shield") : shield_name) +
         " shatters.";
}

std::string make_shield_break_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + "'s " +
         (shield_name.empty() ? std::string("magical shield") : shield_name) + " shatters.";
}

std::string make_shield_fade_self_notice(const std::string& shield_name) {
  return "Your " + (shield_name.empty() ? std::string("magical shield") : shield_name) +
         " fades.";
}

std::string make_shield_fade_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + "'s " +
         (shield_name.empty() ? std::string("magical shield") : shield_name) + " fades.";
}

LegacyPacket make_move_fail_packet(std::uint64_t session_id, const GameObject& object) {
  const auto desc = make_char_desc(object);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMoveFail, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

LegacyPacket make_disappear_packet(std::uint64_t session_id, std::uint64_t actor_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmDisappear,
                                                      static_cast<std::int32_t>(actor_id),
                                                      0, 0, 0));
}

LegacyPacket make_open_door_packet(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmOpenDoorOk, 0,
                                                      static_cast<std::uint16_t>(x),
                                                      static_cast<std::uint16_t>(y), 0));
}

LegacyPacket make_close_door_packet(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmCloseDoor, 0,
                                                      static_cast<std::uint16_t>(x),
                                                      static_cast<std::uint16_t>(y), 0));
}

LegacyPacket make_clear_objects_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmClearObjects, 0, 0, 0, 0));
}

LegacyPacket make_change_map_packet(std::uint64_t session_id, const std::string& map_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmChangeMap, 0, 0, 0, 0),
                                 legacy_encode_string(map_id));
}

LegacyPacket make_spell_packet(
    std::uint64_t session_id, const GameObject& object, const ActorMail& mail,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  const auto magic_id = static_cast<std::int32_t>(mail.game_message.tag);
  auto effect = static_cast<std::uint16_t>(magic_id);
  if (const auto it = magic_configs.find(magic_id); it != magic_configs.end()) {
    effect = static_cast<std::uint16_t>(std::clamp(
        it->second.legacy.legacy_present ? it->second.legacy.effect : it->second.id, 0, 65535));
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpell, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(mail.x), static_cast<std::uint16_t>(mail.y),
                           effect),
      std::to_string(magic_id));
}

LegacyPacket make_magic_fire_packet(std::uint64_t session_id, const GameObject& caster,
                                    std::int32_t x, std::int32_t y,
                                    const MagicConfig& magic,
                                    std::uint64_t target_actor_id) {
  const auto effect_type = magic.legacy.legacy_present ? magic.legacy.effect_type : 0;
  const auto effect = magic.legacy.legacy_present ? magic.legacy.effect : magic.id;
  auto target = static_cast<std::int32_t>(target_actor_id);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMagicFire, static_cast<std::int32_t>(caster.id()),
                           static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                           make_word(static_cast<std::uint8_t>(std::clamp(effect_type, 0, 255)),
                                     static_cast<std::uint8_t>(std::clamp(effect, 0, 255)))),
      legacy_encode_buffer(&target, sizeof(target)));
}

LegacyPacket make_magic_fire_fail_packet(std::uint64_t session_id, const GameObject& caster) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMagicFireFail,
                                                      static_cast<std::int32_t>(caster.id()), 0,
                                                      0, 0));
}

LegacyPacket make_add_item_packet(
    std::uint64_t session_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(session_id, 0, 0, make_default_message(kSmAddItem, 0, 0, 0, 0),
                                 legacy_encode_buffer(&client_item, sizeof(client_item)));
}

LegacyPacket make_update_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmUpdateItem, static_cast<std::int32_t>(actor_id), 0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

LegacyPacket make_del_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDelItem, static_cast<std::int32_t>(actor_id), 0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

LegacyPacket make_item_show_packet(std::uint64_t session_id, const MapActor::GroundItem& item) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmItemShow, static_cast<std::int32_t>(item.id),
                           static_cast<std::uint16_t>(item.x), static_cast<std::uint16_t>(item.y),
                           static_cast<std::uint16_t>(std::clamp(item.looks, 0, 65535))),
      legacy_encode_string(item.name));
}

LegacyPacket make_item_hide_packet(std::uint64_t session_id, const MapActor::GroundItem& item) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmItemHide, static_cast<std::int32_t>(item.id),
                           static_cast<std::uint16_t>(item.x), static_cast<std::uint16_t>(item.y), 0));
}

LegacyPacket make_drop_result_packet(std::uint64_t session_id, bool ok, std::int32_t make_index,
                                     const std::string& item_name) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmDropItemSuccess : kSmDropItemFail, make_index, 0, 0, 0),
      legacy_encode_string(item_name));
}

LegacyPacket make_take_on_result_packet(std::uint64_t session_id, bool ok, std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmTakeOnOk : kSmTakeOnFail, ok ? feature : 0, 0, 0, 0));
}

LegacyPacket make_take_off_result_packet(std::uint64_t session_id, bool ok, std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmTakeOffOk : kSmTakeOffFail, ok ? feature : 0, 0, 0, 0));
}

LegacyPacket make_eat_result_packet(std::uint64_t session_id, bool ok) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmEatOk : kSmEatFail, 0, 0, 0, 0));
}

LegacyPacket make_feature_changed_packet(std::uint64_t session_id, std::uint64_t actor_id,
                                         std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmFeatureChanged, static_cast<std::int32_t>(actor_id), low_word(feature),
                           high_word(feature), 0));
}

LegacyPacket make_char_status_changed_packet(std::uint64_t session_id, const Player& player) {
  const auto status = player.character().status;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmCharStatusChanged, static_cast<std::int32_t>(player.id()),
                           low_word(status), high_word(status), 0));
}

LegacyPacket make_weight_changed_packet(std::uint64_t session_id, const CharacterRecord& character) {
  const auto total_weight = static_cast<std::uint16_t>(
      std::clamp<std::int32_t>(character.ability.weight, 0, 65535));
  const auto wear_weight = static_cast<std::uint16_t>(character.ability.wear_weight);
  const auto hand_weight = static_cast<std::uint16_t>(character.ability.hand_weight);
  const auto checksum = static_cast<std::uint16_t>(
      (((total_weight + wear_weight + hand_weight) ^ 0x3A5F) ^ 0x1F35) ^ 0xAA21);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmWeightChanged, total_weight, static_cast<std::uint16_t>(wear_weight),
                           static_cast<std::uint16_t>(hand_weight), checksum));
}

LegacyPacket make_gold_changed_packet(std::uint64_t session_id, std::int32_t gold) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmGoldChanged, gold, 0, 0, 0));
}

LegacyPacket make_deal_menu_packet(std::uint64_t session_id, std::string_view peer_name) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmDealMenu, 0, 0, 0, 0),
                                 legacy_encode_string(std::string(peer_name)));
}

LegacyPacket make_deal_simple_packet(std::uint64_t session_id, std::uint16_t ident) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, 0, 0, 0, 0));
}

LegacyPacket make_deal_change_gold_packet(std::uint64_t session_id, std::uint16_t ident,
                                          std::int32_t deal_gold, std::int32_t bag_gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ident, deal_gold, low_word(bag_gold), high_word(bag_gold), 0));
}

LegacyPacket make_deal_remote_change_gold_packet(std::uint64_t session_id,
                                                 std::int32_t deal_gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteChangeGold, deal_gold, 0, 0, 0));
}

LegacyPacket make_deal_remote_add_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteAddItem, static_cast<std::int32_t>(actor_id),
                           0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

LegacyPacket make_deal_remote_del_item_packet(std::uint64_t session_id,
                                              std::uint64_t actor_id,
                                              const LegacyUserItem& item,
                                              const std::unordered_map<std::int32_t, ItemConfig>&
                                                  item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteDelItem, static_cast<std::int32_t>(actor_id),
                           0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

LegacyPacket make_send_user_sell_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserSell, static_cast<std::int32_t>(merchant_actor_id), 0, 0, 0));
}

std::int32_t compute_merchant_sell_price(
    const Npc& merchant, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs);

LegacyPacket make_send_goods_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, const Npc& merchant,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  struct GoodsEntry {
    std::int32_t item_index{0};
    std::string name{};
    std::int32_t submenu{0};
    std::int32_t price{0};
    std::int32_t stock{0};
  };

  std::vector<GoodsEntry> entries;
  for (const auto& item : merchant.merchant_items()) {
    if (is_empty(item)) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      continue;
    }
    auto it = std::find_if(entries.begin(), entries.end(), [&](const GoodsEntry& entry) {
      return entry.item_index == item.index;
    });
    if (it == entries.end()) {
      entries.push_back(GoodsEntry{item.index, config->name,
                                   requires_detail_goods_list(*config) ? 1 : 0,
                                   compute_merchant_sell_price(merchant, item, item_configs),
                                   1});
    } else {
      ++it->stock;
    }
  }

  std::string body;
  for (const auto& entry : entries) {
    body += entry.name + "/" + std::to_string(entry.submenu) + "/" + std::to_string(entry.price) +
            "/" + std::to_string(entry.stock) + "/";
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendGoodsList, static_cast<std::int32_t>(merchant_actor_id),
                           static_cast<std::uint16_t>(entries.size()), 0, 0),
      legacy_encode_string(body));
}

LegacyPacket make_merchant_say_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id,
                                      const Npc& merchant, std::string_view text) {
  const auto body = actor_name(merchant) + "/" + std::string(text);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMerchantSay, static_cast<std::int32_t>(merchant_actor_id),
                           kDefaultMerchantFace, 0, 0),
      legacy_encode_string(body));
}

LegacyPacket make_merchant_dlg_close_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMerchantDlgClose, 0, 0, 0, 0));
}

LegacyPacket make_send_buy_price_packet(std::uint64_t session_id, std::int32_t price) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendBuyPrice, price, 0, 0, 0));
}

LegacyPacket make_send_detail_goods_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, std::int32_t top_line,
    const std::vector<LegacyUserItem>& merchant_items,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs, const Npc& merchant) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : merchant_items) {
    if (is_empty(item)) {
      continue;
    }
    auto client_item = make_client_item(item, item_configs);
    client_item.dura_max = static_cast<std::uint16_t>(
        std::clamp(compute_merchant_sell_price(merchant, item, item_configs), 0, 65535));
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendDetailGoodsList, static_cast<std::int32_t>(merchant_actor_id),
                           count, static_cast<std::uint16_t>(std::clamp(top_line, 0, 65535)), 0),
      legacy_encode_string(body));
}

LegacyPacket make_user_sell_result_packet(std::uint64_t session_id, bool ok, std::int32_t gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmUserSellItemOk : kSmUserSellItemFail, ok ? gold : 0, 0, 0, 0));
}

LegacyPacket make_buy_item_result_packet(std::uint64_t session_id, bool ok, std::int32_t value,
                                         std::int32_t item_make_index) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmBuyItemSuccess : kSmBuyItemFail, value, low_word(item_make_index),
                           high_word(item_make_index), 0));
}

LegacyPacket make_send_user_repair_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserRepair, static_cast<std::int32_t>(merchant_actor_id), 0, 0, 0));
}

LegacyPacket make_send_user_storage_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id,
                                           std::uint16_t count) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserStorageItem, static_cast<std::int32_t>(merchant_actor_id),
                           count, 0, 0));
}

LegacyPacket make_send_repair_cost_packet(std::uint64_t session_id, std::int32_t cost) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendRepairCost, cost, 0, 0, 0));
}

LegacyPacket make_user_repair_result_packet(std::uint64_t session_id, bool ok, std::int32_t gold,
                                            std::uint16_t dura, std::uint16_t dura_max) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmUserRepairItemOk : kSmUserRepairItemFail, ok ? gold : 0,
                           ok ? dura : 0, ok ? dura_max : 0, 0));
}

LegacyPacket make_save_item_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : player.character().storage_items) {
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSaveItemList, static_cast<std::int32_t>(merchant_actor_id), 0, 0,
                           count),
      body);
}

LegacyPacket make_storage_result_packet(std::uint64_t session_id, std::uint16_t ident) {
  return make_legacy_game_packet(session_id, 0, 0, make_default_message(ident, 0, 0, 0, 0));
}

LegacyPacket make_take_back_storage_result_packet(std::uint64_t session_id, std::uint16_t ident,
                                                  std::int32_t make_index) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, make_index, 0, 0, 0));
}

LegacyPacket make_dura_change_packet(std::uint64_t session_id, std::size_t slot,
                                     const LegacyUserItem& item,
                                     const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto dura_max = item_dura_max(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDuraChange, item.dura, static_cast<std::uint16_t>(slot),
                           low_word(dura_max), high_word(dura_max)));
}

LegacyPacket make_health_spell_changed_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmHealthSpellChanged, static_cast<std::int32_t>(player.id()),
                           player.character().ability.hp, player.character().ability.mp,
                           player.character().ability.max_hp));
}

LegacyPacket make_struck_packet(std::uint64_t session_id, const GameObject& target,
                                std::uint64_t hitter_id, std::int32_t damage, bool magic_struck) {
  LegacyMessageBodyWL body;
  body.lparam1 = actor_feature(target);
  body.lparam2 = actor_status(target);
  body.ltag1 = static_cast<std::int32_t>(hitter_id);
  body.ltag2 = magic_struck ? 1 : 0;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmStruck, static_cast<std::int32_t>(target.id()),
                           static_cast<std::uint16_t>(std::clamp(actor_hp(target), 0, 65535)),
                           static_cast<std::uint16_t>(std::clamp(actor_max_hp(target), 0, 65535)),
                           static_cast<std::uint16_t>(std::clamp(damage, 0, 65535))),
      legacy_encode_buffer(&body, sizeof(body)));
}

LegacyPacket make_death_packet(std::uint64_t session_id, const GameObject& target,
                               bool now_death = false) {
  const auto desc = make_char_desc(target);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(now_death ? kSmNowDeath : kSmDeath,
                           static_cast<std::int32_t>(target.id()),
                           static_cast<std::uint16_t>(target.x()),
                           static_cast<std::uint16_t>(target.y()), actor_dir(target)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

LegacyPacket make_space_move_hide2_packet(std::uint64_t session_id, const GameObject& object) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveHide2, static_cast<std::int32_t>(object.id()), 0, 0, 0));
}

LegacyPacket make_space_move_show2_packet(std::uint64_t session_id, const GameObject& object) {
  const auto desc = make_char_desc(object);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveShow2, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()),
                           make_word(actor_dir(object), actor_light(object))),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

LegacyPacket make_alive_packet(std::uint64_t session_id, const Player& player) {
  const auto desc = make_char_desc(player);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmAlive, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(player.x()),
                           static_cast<std::uint16_t>(player.y()), actor_dir(player)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

LegacyPacket make_win_exp_packet(std::uint64_t session_id, std::int32_t current_exp,
                                 std::int32_t gained_exp) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmWinExp, current_exp,
                           static_cast<std::uint16_t>(std::clamp(gained_exp, 0, 65535)), 0, 0));
}

LegacyPacket make_level_up_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmLevelUp, static_cast<std::int32_t>(player.character().ability.exp),
                           player.character().ability.level, 0, 0));
}

LegacyPacket make_break_weapon_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmBreakWeapon, static_cast<std::int32_t>(player.id()), 0, 0, 0));
}

LegacyPacket make_sub_ability_packet(std::uint64_t session_id, const Player& player) {
  const auto hit_point =
      static_cast<std::uint8_t>(std::clamp(player.accuracy_point(), 0, 255));
  const auto speed_point = static_cast<std::uint8_t>(std::clamp(player.speed_point(), 0, 255));
  const auto anti_poison =
      static_cast<std::uint8_t>(std::clamp(player.legacy_anti_poison(), 0, 255));
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSubAbility, 0, make_word(hit_point, speed_point),
                           make_word(anti_poison, 0), 0));
}

void dispatch_login_sequence(RuntimeDispatch& dispatch, const Player& player,
                             const MapConfig& map_config,
                             const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                             const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
                             std::int32_t area_state) {
  queue_packet(dispatch, player.session_id(), make_new_map_packet(player.session_id(), player, map_config));
  queue_packet(dispatch, player.session_id(), make_logon_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_username_packet(player.session_id(), player.id(), player.character().character_name,
                                    actor_name_color(player)));
  queue_packet(dispatch, player.session_id(), make_area_state_packet(player.session_id(), area_state));
  queue_packet(dispatch, player.session_id(),
               make_map_description_packet(player.session_id(), map_config));
  queue_packet(dispatch, player.session_id(), make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_use_items_packet(player.session_id(), player, item_configs));
  queue_packet(dispatch, player.session_id(),
               make_my_magic_packet(player.session_id(), player, magic_configs));
}

void sync_area_state(RuntimeDispatch& dispatch, const MapConfig& map_config, Player& player,
                     bool force = false) {
  const auto in_safe = is_safe_zone(map_config, player.x(), player.y());
  if (force || in_safe != player.in_safe_zone()) {
    player.set_in_safe_zone(in_safe);
    queue_packet(dispatch, player.session_id(),
                 make_area_state_packet(player.session_id(),
                                        area_state_mask(map_config, player.x(), player.y())));
  }
}

}  // namespace
