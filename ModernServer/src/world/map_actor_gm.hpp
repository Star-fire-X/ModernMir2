#pragma once

namespace {

bool gm_command_equals(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(std::string(lhs)) == util::lower_copy(std::string(rhs));
}

std::int32_t gm_parse_int(const std::vector<std::string>& args, std::size_t index,
                          std::int32_t fallback = 0) {
  if (index >= args.size()) {
    return fallback;
  }
  return parse_int32(util::trim(args[index])).value_or(fallback);
}

double gm_parse_double(const std::vector<std::string>& args, std::size_t index,
                       double fallback = 0.0) {
  if (index >= args.size()) {
    return fallback;
  }
  try {
    return std::stod(util::trim(args[index]));
  } catch (...) {
    return fallback;
  }
}

std::string gm_join_args(const std::vector<std::string>& args, std::size_t begin,
                         std::size_t end) {
  std::string result;
  for (auto index = begin; index < end && index < args.size(); ++index) {
    if (!result.empty()) {
      result += ' ';
    }
    result += args[index];
  }
  return result;
}

const MagicConfig* gm_find_magic(
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
    std::string_view value) {
  const auto maybe_id = parse_int32(util::trim(std::string(value)));
  if (maybe_id.has_value()) {
    const auto it = magic_configs.find(*maybe_id);
    return it != magic_configs.end() ? &it->second : nullptr;
  }

  const auto wanted = util::lower_copy(util::trim(std::string(value)));
  for (const auto& [_, magic] : magic_configs) {
    if (util::lower_copy(magic.name) == wanted) {
      return &magic;
    }
  }
  return nullptr;
}

const ItemConfig* gm_find_item_arg(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    const std::vector<std::string>& args, std::int32_t& count) {
  count = 1;
  for (auto split = args.size(); split > 0; --split) {
    const auto name = gm_join_args(args, 0, split);
    const auto* item_config = find_item_config_by_name_or_id(item_configs, name);
    if (item_config == nullptr) {
      continue;
    }
    if (split < args.size()) {
      count = gm_parse_int(args, split, 1);
    }
    return item_config;
  }
  return nullptr;
}

std::uint16_t gm_item_dura_max(const ItemConfig& item_config) {
  const auto dura_max = item_config.dura_max > 0 ? item_config.dura_max : 1000;
  return static_cast<std::uint16_t>(std::clamp(dura_max, 0, 65535));
}

LegacyUserItem gm_make_item(const ItemConfig& item_config, std::int32_t make_index) {
  LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(std::clamp(item_config.id, 0, 65535));
  item.dura_max = gm_item_dura_max(item_config);
  item.dura = item.dura_max;
  return item;
}

void gm_refresh_ability(RuntimeDispatch& dispatch, Player& player,
                        const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  player.refresh_derived_state(item_configs);
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(), make_sub_ability_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
}

void gm_refresh_weight(RuntimeDispatch& dispatch, Player& player,
                       const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  player.refresh_derived_state(item_configs);
  queue_packet(dispatch, player.session_id(),
               make_weight_changed_packet(player.session_id(), player.character()));
}

void gm_broadcast_feature(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const Player& player) {
  queue_actor_origin_packet(objects, dispatch, player, true, [&](const Player& watcher) {
    queue_packet(dispatch, watcher.session_id(),
                 make_feature_changed_packet(watcher.session_id(), player.id(),
                                             actor_feature(player)));
  });
}

void gm_broadcast_name_color(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const Player& player) {
  queue_actor_origin_packet(objects, dispatch, player, true, [&](const Player& watcher) {
    queue_packet(dispatch, watcher.session_id(),
                 make_username_packet(watcher.session_id(), player.id(),
                                      player.character().character_name,
                                      actor_name_color(player)));
  });
}

std::string gm_mark_text(const Player& player, std::string_view label,
                         std::int32_t index, std::uint8_t value) {
  return player.character().character_name + ": " + std::string(label) + "[" +
         std::to_string(index) + "]=" + (value != 0 ? "ON" : "OFF");
}

}  // namespace

MapActor::LegacyGmCommandResult MapActor::legacy_apply_gm_command(
    std::uint64_t actor_id, const std::string& command_name,
    const std::vector<std::string>& args, std::uint64_t current_tick,
    std::uint64_t now_ms) {
  LegacyGmCommandResult result;
  auto* player = find_player(actor_id);
  if (player == nullptr) {
    result.handled = true;
    result.reason = "target_not_found";
    return result;
  }

  auto save = [&]() { queue_save_character(result.dispatch, *player); };
  auto ok = [&](std::string reason = "ok") {
    result.handled = true;
    result.success = true;
    result.reason = std::move(reason);
    return result;
  };
  auto fail = [&](std::string reason) {
    result.handled = true;
    result.success = false;
    result.reason = std::move(reason);
    return result;
  };

  if (gm_command_equals(command_name, "Level") || gm_command_equals(command_name, "Level0") ||
      gm_command_equals(command_name, "AdjustLevel") ||
      gm_command_equals(command_name, "AdjustTestLevel")) {
    const auto level_arg = gm_command_equals(command_name, "AdjustLevel") ? 1 : 0;
    const auto max_level = gm_command_equals(command_name, "AdjustTestLevel") ? 50 : 40;
    player->set_legacy_level(gm_parse_int(args, level_arg, player->character().ability.level),
                             max_level);
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    queue_packet(result.dispatch, player->session_id(),
                 make_level_up_packet(player->session_id(), *player));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "AdjustExp")) {
    player->set_legacy_exp(gm_parse_int(args, 1, player->character().ability.exp));
    queue_packet(result.dispatch, player->session_id(),
                 make_ability_packet(player->session_id(), player->character()));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "FreePenalty")) {
    player->set_pk_point(0);
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    save();
    result.messages.push_back(player->character().character_name + " : PK point = 0.");
    return ok();
  }

  if (gm_command_equals(command_name, "PKpoint")) {
    result.messages.push_back(player->character().character_name + " PK point = " +
                              std::to_string(player->pk_point()));
    return ok();
  }

  if (gm_command_equals(command_name, "IncPkPoint")) {
    player->inc_pk_point(100);
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "LuckyPoint")) {
    result.messages.push_back(player->character().character_name + ": BodyLuck= " +
                              std::to_string(player->body_luck_level()) + "/" +
                              std::to_string(static_cast<std::int32_t>(
                                  std::lround(player->character().body_luck))) +
                              " Luck = " + std::to_string(player->legacy_luck()));
    return ok();
  }

  if (gm_command_equals(command_name, "ChangeLuck")) {
    player->set_body_luck_value(gm_parse_double(args, 0, player->character().body_luck));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "hair")) {
    player->set_hair(gm_parse_int(args, 0, player->character().hair));
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    gm_broadcast_feature(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "NameColor")) {
    player->set_legacy_name_color(gm_parse_int(args, 0, 255));
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    return ok();
  }

  if (gm_command_equals(command_name, "ChangeJob")) {
    auto job = gm_parse_int(args, 0, -1);
    if (!args.empty()) {
      const auto lowered = util::lower_copy(args[0]);
      if (lowered == "warrior") {
        job = 0;
      } else if (lowered == "wizard") {
        job = 1;
      } else if (lowered == "taoist") {
        job = 2;
      }
    }
    if (job < 0) {
      return fail("bad_args");
    }
    player->set_job(job);
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "ChangeGender")) {
    player->toggle_sex();
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    gm_broadcast_feature(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "Transparency")) {
    if (player->legacy_transparent_active(current_tick)) {
      static_cast<void>(player->clear_legacy_transparent(current_tick));
    } else {
      const auto duration_ticks =
          std::max<std::uint64_t>(1, (3600ULL * 1000ULL + budgets_.tick_ms - 1) /
                                         std::max<std::uint32_t>(budgets_.tick_ms, 1));
      static_cast<void>(player->activate_legacy_transparent(duration_ticks, current_tick));
    }
    broadcast_legacy_char_status_changed(result.dispatch, *player);
    return ok();
  }

  if (gm_command_equals(command_name, "flag") || gm_command_equals(command_name, "showopen") ||
      gm_command_equals(command_name, "showunit")) {
    const auto index = gm_parse_int(args, 1, 0);
    std::uint8_t value = 0;
    std::string label;
    if (gm_command_equals(command_name, "flag")) {
      value = player->quest_mark(index);
      label = "flag";
    } else if (gm_command_equals(command_name, "showopen")) {
      value = player->quest_open_unit(index);
      label = "open";
    } else {
      value = player->quest_unit(index);
      label = "unit";
    }
    result.messages.push_back(gm_mark_text(*player, label, index, value));
    return ok();
  }

  if (gm_command_equals(command_name, "setflag") ||
      gm_command_equals(command_name, "setopen") ||
      gm_command_equals(command_name, "setunit")) {
    const auto index = gm_parse_int(args, 1, 0);
    const auto value = static_cast<std::uint8_t>(gm_parse_int(args, 2, 0) != 0 ? 1 : 0);
    bool changed = false;
    std::string label;
    if (gm_command_equals(command_name, "setflag")) {
      changed = player->set_quest_mark(index, value);
      label = "flag";
    } else if (gm_command_equals(command_name, "setopen")) {
      changed = player->set_quest_open_unit(index, value);
      label = "open";
    } else {
      changed = player->set_quest_unit(index, value);
      label = "unit";
    }
    if (!changed) {
      return fail("bad_args");
    }
    save();
    result.messages.push_back(gm_mark_text(*player, label, index, value));
    return ok();
  }

  if (gm_command_equals(command_name, "Training") ||
      gm_command_equals(command_name, "OPTraining")) {
    const auto magic_arg = gm_command_equals(command_name, "OPTraining") ? 1 : 0;
    const auto level_arg = gm_command_equals(command_name, "OPTraining") ? 2 : 1;
    if (magic_arg >= static_cast<int>(args.size())) {
      return fail("bad_args");
    }
    const auto* magic = gm_find_magic(magic_configs_, args[magic_arg]);
    if (magic == nullptr) {
      return fail("magic_not_found");
    }
    auto* learned = player->learned_magic_mutable(magic->id);
    if (learned == nullptr) {
      return fail("magic_not_learned");
    }
    learned->level = static_cast<std::uint8_t>(std::clamp(gm_parse_int(args, level_arg, 0), 0, 3));
    learned->cur_train = 0;
    queue_packet(result.dispatch, player->session_id(),
                 make_magic_lvexp_packet(player->session_id(), learned->magic_id,
                                          learned->level, learned->cur_train));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "DeleteSkill") ||
      gm_command_equals(command_name, "OPDeleteSkill")) {
    const auto magic_arg = gm_command_equals(command_name, "OPDeleteSkill") ? 1 : 0;
    if (magic_arg >= static_cast<int>(args.size())) {
      return fail("bad_args");
    }
    const auto* magic = gm_find_magic(magic_configs_, args[magic_arg]);
    if (magic == nullptr || !player->remove_legacy_magic(magic->id)) {
      return fail("magic_not_found");
    }
    queue_packet(result.dispatch, player->session_id(),
                 make_del_magic_packet(player->session_id(), magic->id));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "Make")) {
    if (args.empty()) {
      return fail("bad_args");
    }
    auto count = 1;
    const auto* item_config = gm_find_item_arg(item_configs_, args, count);
    if (item_config == nullptr) {
      return fail("item_not_found");
    }
    const auto clamped_count = std::clamp(count, 1, 50);
    auto made = 0;
    for (auto index = 0; index < clamped_count; ++index) {
      auto item = gm_make_item(*item_config, allocate_make_index());
      if (!player->has_free_bag_slot() || !player->can_add_bag_item(item, item_configs_) ||
          !player->add_bag_item(item)) {
        break;
      }
      queue_packet(result.dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), player->id(), item, item_configs_));
      ++made;
    }
    if (made == 0) {
      return fail("bag_full");
    }
    gm_refresh_weight(result.dispatch, *player, item_configs_);
    save();
    return ok("made=" + std::to_string(made));
  }

  if (gm_command_equals(command_name, "DeleteItem")) {
    if (args.empty()) {
      return fail("bad_args");
    }
    auto count = 1;
    const auto* item_config = gm_find_item_arg(item_configs_, args, count);
    if (item_config == nullptr) {
      return fail("item_not_found");
    }
    const auto wanted = util::lower_copy(item_config->name);
    const auto clamped_count = std::clamp(count, 1, 50);
    auto removed = 0;
    for (const auto& item : player->character().bag_items) {
      if (removed >= clamped_count || is_empty(item)) {
        continue;
      }
      if (util::lower_copy(item_name(item, item_configs_)) != wanted) {
        continue;
      }
      const auto removed_item = player->remove_bag_item(item.make_index, {}, item_configs_);
      if (!removed_item.has_value()) {
        continue;
      }
      queue_packet(result.dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed_item,
                                        item_configs_));
      ++removed;
    }
    if (removed == 0) {
      return fail("item_not_found");
    }
    gm_refresh_weight(result.dispatch, *player, item_configs_);
    save();
    return ok("removed=" + std::to_string(removed));
  }

  if (gm_command_equals(command_name, "AddGold") ||
      gm_command_equals(command_name, "DelGold") ||
      gm_command_equals(command_name, "Test_GOLD_Change")) {
    const auto amount_arg = gm_command_equals(command_name, "Test_GOLD_Change") ? 0 : 1;
    const auto amount = std::max(gm_parse_int(args, amount_arg, 0), 0);
    if (gm_command_equals(command_name, "AddGold")) {
      player->add_gold(amount);
    } else if (gm_command_equals(command_name, "DelGold")) {
      player->spend_gold(std::min(player->character().gold, amount));
    } else if (amount >= player->character().gold) {
      player->add_gold(amount - player->character().gold);
    } else {
      player->spend_gold(player->character().gold - amount);
    }
    queue_packet(result.dispatch, player->session_id(),
                 make_gold_changed_packet(player->session_id(), player->character().gold));
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "WeaponRefinery")) {
    auto* weapon = player->equipped_item_mutable(kEquipWeapon);
    if (weapon == nullptr || is_empty(*weapon)) {
      return fail("weapon_not_found");
    }
    weapon->desc[0] = static_cast<std::uint8_t>(std::clamp(gm_parse_int(args, 0, 0), 0, 255));
    weapon->desc[1] = static_cast<std::uint8_t>(std::clamp(gm_parse_int(args, 1, 0), 0, 255));
    weapon->desc[2] = static_cast<std::uint8_t>(std::clamp(gm_parse_int(args, 2, 0), 0, 255));
    weapon->desc[5] = static_cast<std::uint8_t>(std::clamp(gm_parse_int(args, 3, 0), 0, 255));
    queue_packet(result.dispatch, player->session_id(),
                 make_update_item_packet(player->session_id(), player->id(), *weapon,
                                         item_configs_));
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    save();
    return ok();
  }

  if (gm_command_equals(command_name, "ChangeWeaponDura")) {
    auto* weapon = player->equipped_item_mutable(kEquipWeapon);
    if (weapon == nullptr || is_empty(*weapon)) {
      return fail("weapon_not_found");
    }
    const auto dura = std::clamp(gm_parse_int(args, 0, 0), 0, 65) * 1000;
    weapon->dura = static_cast<std::uint16_t>(std::clamp(dura, 0, 65535));
    weapon->dura_max = weapon->dura;
    queue_packet(result.dispatch, player->session_id(),
                 make_dura_change_packet(player->session_id(), kEquipWeapon, *weapon,
                                         item_configs_));
    save();
    return ok();
  }

  result.handled = false;
  return result;
}
