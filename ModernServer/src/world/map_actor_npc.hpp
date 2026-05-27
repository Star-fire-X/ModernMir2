#pragma once

// Implementation detail for map_actor.cpp: NPC script and map quest members.
bool MapActor::legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms) {
  LegacyScriptExecutionContext script_context;
  return legacy_execute_npc_script(player, npc, std::move(action), dispatch, current_tick,
                                   now_ms, script_context, 0);
}

bool MapActor::legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms,
                                         LegacyScriptExecutionContext& script_context,
                                         std::int32_t depth) {
  action = util::trim(std::move(action));
  if (action.empty()) {
    action = "@main";
  }
  auto lowered_action = util::lower_copy(action);

  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::merchant_select;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = player.id();
  trace_mail.session_id = player.session_id();
  trace_mail.target_actor_id = npc.id();
  trace_mail.payload = action;

  auto trace = [&](std::string stage_action, bool success, std::int32_t value,
                   std::string label) {
    add_legacy_trace(dispatch, "LegacyScript", std::move(stage_action), trace_mail,
                     current_tick, now_ms, success, value, 0, std::move(label));
  };

  trace("begin", true, 0, action);

  if (lowered_action == "@exit") {
    queue_packet(dispatch, player.session_id(),
                 make_merchant_dlg_close_packet(player.session_id()));
    trace("close", true, 0, "@exit");
    return true;
  }

  if (depth > 8) {
    trace("goto_depth_reject", false, depth, action);
    return true;
  }

  const auto* dialog = find_npc_dialog_text(npc, action);
  if (dialog == nullptr && lowered_action == "@main" && should_open_merchant_dialog(npc)) {
    queue_packet(dispatch, player.session_id(),
                 make_merchant_say_packet(
                     player.session_id(), npc.id(), npc,
                     render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                            build_merchant_dialog_text(npc), item_configs_,
                                            *script_global_params_)));
    trace("say", true, 0, "default_merchant_dialog");
    return true;
  }
  if (dialog == nullptr) {
    trace("missing_section", false, 0, action);
    return false;
  }

  const auto block = parse_legacy_script_block(*dialog);
  auto& script_global_params = *script_global_params_;

  auto list_key = [&](std::string_view name) {
    auto key = util::lower_copy(util::trim(std::string(name)));
    if (key.empty()) {
      key = "default";
    }
    return key;
  };

  std::array<std::int32_t, 5> local_param_values{};
  std::array<std::string, 5> local_param_text{};
  std::array<bool, 5> local_param_set{};
  std::vector<LegacyBatchMoveRequest> batch_move_requests;

  auto legacy_seconds_to_ticks = [&](std::int32_t seconds) {
    const auto tick_ms =
        static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1));
    const auto delay_ms = static_cast<std::uint64_t>(std::max(seconds, 0)) * 1000ULL;
    return delay_ms == 0 ? 1ULL : std::max<std::uint64_t>((delay_ms + tick_ms - 1) / tick_ms, 1);
  };
  std::uint64_t batch_delay_ticks = legacy_seconds_to_ticks(10);

  auto script_value = [&](std::string_view raw) -> std::int32_t {
    auto token = util::trim(std::string(raw));
    if (token.empty()) {
      return 0;
    }
    const auto upper = script_upper_copy(token);
    if (const auto variable = parse_legacy_script_variable_token(upper); variable.has_value()) {
      const auto [group, index] = *variable;
      if (group == 'P') {
        return player.script_param(index);
      }
      if (group == 'G') {
        return script_global_params[static_cast<std::size_t>(index)];
      }
      return player.script_dice_param(index);
    }
    if (upper == "LEVEL") {
      return player.character().ability.level;
    }
    if (upper == "GOLD") {
      return player.character().gold;
    }
    if (upper == "PKPOINT") {
      return player.pk_point();
    }
    if (upper == "DAILYQUEST") {
      return static_cast<std::int32_t>(player.daily_quest());
    }
    if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
      return player.quest_mark(parse_script_index(token).value_or(0));
    }
    return parse_int32(token).value_or(0);
  };

  auto set_script_value = [&](std::string_view raw, std::int32_t value) {
    auto token = util::trim(std::string(raw));
    const auto upper = script_upper_copy(token);
    if (const auto variable = parse_legacy_script_variable_token(upper); variable.has_value()) {
      const auto [group, index] = *variable;
      if (group == 'P') {
        return player.set_script_param(index, value);
      }
      if (group == 'G') {
        script_global_params[static_cast<std::size_t>(index)] = value;
        return true;
      }
      return player.set_script_dice_param(index, value);
    }
    if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
      return player.set_quest_mark(parse_script_index(token).value_or(0),
                                   static_cast<std::uint8_t>(std::clamp(value, 0, 255)));
    }
    return false;
  };

  auto is_persistent_script_value = [](std::string_view raw) {
    auto token = util::trim(std::string(raw));
    const auto variable = parse_legacy_script_variable_token(token);
    if (variable.has_value()) {
      return variable->first == 'P';
    }
    return token.size() >= 2 && token.front() == '[' && token.back() == ']';
  };

  auto set_script_group_sum = [&](std::string_view raw, std::int32_t value) {
    auto token = util::trim(std::string(raw));
    const auto variable = parse_legacy_script_variable_token(token);
    if (!variable.has_value()) {
      return false;
    }
    const auto group = variable->first;
    if (group == 'P') {
      return player.set_script_param(9, value);
    }
    if (group == 'G') {
      script_global_params[9] = value;
      return true;
    }
    return player.set_script_dice_param(9, value);
  };

  auto evaluate_condition = [&](const std::string& condition_line) {
    const auto command_name = script_command_name(condition_line);
    const auto payload = script_command_payload(condition_line);
    const auto tokens = split_script_tokens(payload);
    auto int_token = [&](std::size_t index, std::int32_t fallback = 0) {
      if (index >= tokens.size()) {
        return fallback;
      }
      return parse_int32(tokens[index]).value_or(fallback);
    };
    auto compare_mark = [&](auto reader) {
      if (tokens.empty()) {
        trace("condition", false, 0, condition_line);
        return false;
      }
      const auto index = parse_script_index(tokens[0]).value_or(0);
      const auto expected = tokens.size() > 1 ? int_token(1, 1) : 1;
      const auto actual = static_cast<std::int32_t>(reader(index));
      const auto success = actual == expected;
      trace("condition", success, actual, condition_line);
      return success;
    };
    auto local_time = [] {
      const auto now = std::chrono::system_clock::now();
      const auto time = std::chrono::system_clock::to_time_t(now);
      std::tm tm{};
#if defined(_WIN32)
      localtime_s(&tm, &time);
#else
      localtime_r(&time, &tm);
#endif
      return tm;
    };
    if (command_name == "CHECK") {
      return compare_mark([&](std::int32_t index) { return player.quest_mark(index); });
    }
    if (command_name == "CHECKOPEN") {
      return compare_mark([&](std::int32_t index) { return player.quest_open_unit(index); });
    }
    if (command_name == "CHECKUNIT") {
      return compare_mark([&](std::int32_t index) { return player.quest_unit(index); });
    }
    if (command_name == "CHECKLEVEL") {
      const auto value = int_token(0, 0);
      const auto success = player.character().ability.level >= value;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "CHECKJOB") {
      std::int32_t value = int_token(0, -1);
      if (value < 0 && !tokens.empty()) {
        const auto job = script_upper_copy(tokens[0]);
        if (job == "WARRIOR" || job == "FIGHTER" || job == "0") {
          value = 0;
        } else if (job == "WIZARD" || job == "MAGE" || job == "1") {
          value = 1;
        } else if (job == "TAOIST" || job == "TAO" || job == "2") {
          value = 2;
        }
      }
      const auto success = value >= 0 && player.character().job == value;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "GENDER") {
      std::int32_t value = int_token(0, -1);
      if (value < 0 && !tokens.empty()) {
        const auto gender = script_upper_copy(tokens[0]);
        if (gender == "MAN" || gender == "MALE" || gender == "0") {
          value = 0;
        } else if (gender == "WOMAN" || gender == "FEMALE" || gender == "1") {
          value = 1;
        }
      }
      const auto success = value >= 0 && player.character().sex == value;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "CHECKGOLD") {
      const auto value = int_token(0, 0);
      const auto success = player.character().gold >= value;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "CHECKITEM") {
      const auto target = parse_script_amount_target(payload);
      const auto count = count_player_bag_items_by_name(player, target.target, item_configs_);
      const auto success = count >= target.amount;
      script_context.last_checked_item.reset();
      script_context.last_checked_item_name.reset();
      if (success) {
        const auto wanted = util::lower_copy(util::trim(target.target));
        for (const auto& item : player.character().bag_items) {
          if (!is_empty(item) && util::lower_copy(item_name(item, item_configs_)) == wanted) {
            script_context.last_checked_item = item;
            script_context.last_checked_item_name = item_name(item, item_configs_);
            break;
          }
        }
      }
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "ISTAKEITEM") {
      const auto target = parse_script_amount_target(payload);
      const auto wanted = util::lower_copy(util::trim(target.target));
      const auto taken = script_context.last_taken_item_name.has_value()
                             ? util::lower_copy(util::trim(*script_context.last_taken_item_name))
                             : std::string{};
      const auto success = !wanted.empty() && wanted == taken;
      const auto value = success ? 1 : 0;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "CHECKITEMW") {
      const auto target = parse_script_amount_target(payload);
      const auto slots = legacy_equipment_slots_for_alias(target.target);
      if (!slots.empty()) {
        std::int32_t count = 0;
        for (const auto slot : slots) {
          const auto* item = player.equipped_item(slot);
          if (item != nullptr && !is_empty(*item)) {
            ++count;
          }
        }
        const auto success = count >= target.amount;
        trace("condition", success, count, condition_line);
        return success;
      }
      const auto count = count_player_equipped_items_by_name(player, target.target, item_configs_);
      const auto success = count >= target.amount;
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "CHECKDURA" || command_name == "CHECKDURAEVA") {
      const auto target = parse_script_amount_target(payload);
      std::int32_t best_dura = 0;
      std::int32_t dura_sum = 0;
      std::int32_t dura_count = 0;
      for (const auto& item : player.character().bag_items) {
        if (!is_empty(item) && util::lower_copy(item_name(item, item_configs_)) ==
                                   util::lower_copy(target.target)) {
          const auto dura = static_cast<std::int32_t>(item.dura);
          best_dura = std::max(best_dura, dura);
          dura_sum += dura;
          ++dura_count;
        }
      }
      for (const auto& item : player.character().equipped_items) {
        if (!is_empty(item) && util::lower_copy(item_name(item, item_configs_)) ==
                                   util::lower_copy(target.target)) {
          const auto dura = static_cast<std::int32_t>(item.dura);
          best_dura = std::max(best_dura, dura);
          dura_sum += dura;
          ++dura_count;
        }
      }
      const auto value = command_name == "CHECKDURAEVA" && dura_count > 0
                             ? static_cast<std::int32_t>(std::lround(
                                   (static_cast<double>(dura_sum) /
                                    static_cast<double>(dura_count)) /
                                   1000.0))
                             : static_cast<std::int32_t>(std::lround(
                                   static_cast<double>(best_dura) / 1000.0));
      const auto success = dura_count > 0 && value >= target.amount;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "RANDOM") {
      const auto range = int_token(0, 0);
      const auto value = legacy_random_value(dispatch, "LegacyScript", "RANDOM", range,
                                             player.id(), npc.id(), action, now_ms,
                                             current_tick);
      const auto success = value == 0;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "RANDOMEX") {
      const auto point = int_token(0, 0);
      const auto range = std::max(int_token(1, 100), 1);
      const auto value = legacy_random_value(dispatch, "LegacyScript", "RANDOMEX", range,
                                             player.id(), npc.id(), action, now_ms,
                                             current_tick);
      const auto success = value < point;
      trace("condition", success, value, condition_line);
      return success;
    }
    if (command_name == "DAYTIME") {
      const auto wanted = tokens.empty() ? std::string("DAY") : script_upper_copy(tokens[0]);
      const auto is_day = !config_.darkness;
      const auto success = wanted == "DAY" ? is_day : wanted == "NIGHT" ? !is_day : true;
      trace("condition", success, is_day ? 1 : 0, condition_line);
      return success;
    }
    if (command_name == "DAYOFWEEK") {
      const auto tm = local_time();
      const auto delphi_day = tm.tm_wday + 1;
      auto expected = int_token(0, delphi_day);
      if (!tokens.empty()) {
        const auto day = script_upper_copy(tokens[0]);
        if (day.starts_with("SUN")) {
          expected = 1;
        } else if (day.starts_with("MON")) {
          expected = 2;
        } else if (day.starts_with("TUE")) {
          expected = 3;
        } else if (day.starts_with("WED")) {
          expected = 4;
        } else if (day.starts_with("THU")) {
          expected = 5;
        } else if (day.starts_with("FRI")) {
          expected = 6;
        } else if (day.starts_with("SAT")) {
          expected = 7;
        }
      }
      const auto success = delphi_day == expected;
      trace("condition", success, delphi_day, condition_line);
      return success;
    }
    if (command_name == "HOUR") {
      const auto start = int_token(0, 0);
      const auto end = tokens.size() > 1 ? int_token(1, start) : start;
      const auto hour = local_time().tm_hour;
      const auto success = hour >= std::min(start, end) && hour <= std::max(start, end);
      trace("condition", success, hour, condition_line);
      return success;
    }
    if (command_name == "MIN") {
      const auto start = int_token(0, 0);
      const auto end = tokens.size() > 1 ? int_token(1, start) : start;
      const auto minute = local_time().tm_min;
      const auto success = minute >= std::min(start, end) && minute <= std::max(start, end);
      trace("condition", success, minute, condition_line);
      return success;
    }
    if (command_name == "CHECKPKPOINT") {
      const auto value = int_token(0, 0);
      const auto success = player.pk_point() >= value;
      trace("condition", success, player.pk_point(), condition_line);
      return success;
    }
    if (command_name == "CHECKLUCKYPOINT") {
      const auto value = int_token(0, 0);
      const auto lucky = player.body_luck_level();
      const auto success = lucky >= value;
      trace("condition", success, lucky, condition_line);
      return success;
    }
    if (command_name == "CHECKMONMAP") {
      std::string map_id = config_.id;
      std::int32_t needed = 1;
      if (tokens.size() == 1) {
        if (const auto maybe_needed = parse_int32(tokens[0]); maybe_needed.has_value()) {
          needed = *maybe_needed;
        } else {
          map_id = util::trim(tokens[0]);
        }
      } else if (tokens.size() >= 2) {
        map_id = util::trim(tokens[0]);
        needed = int_token(1, 1);
      }
      const auto count = legacy_script_map_hooks_.monster_count
                             ? legacy_script_map_hooks_.monster_count(map_id)
                             : (map_id == config_.id ? legacy_live_monster_count() : 0);
      const auto success = count >= needed;
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "CHECKMONAREA") {
      std::string name;
      std::int32_t needed = 1;
      std::int32_t range = kLegacyViewRange;
      if (tokens.size() == 1) {
        needed = int_token(0, 1);
      } else if (tokens.size() >= 2) {
        name = tokens[0];
        needed = int_token(1, 1);
      }
      if (tokens.size() >= 3) {
        range = int_token(2, kLegacyViewRange);
      }
      const auto wanted = util::lower_copy(util::trim(name));
      std::int32_t count = 0;
      for (const auto& [_, object] : objects_) {
        const auto* monster = as_monster(object.get());
        if (monster == nullptr || monster->is_dead()) {
          continue;
        }
        if (!wanted.empty() && util::lower_copy(monster->name()) != wanted) {
          continue;
        }
        if (command_name == "CHECKMONAREA" &&
            (std::abs(monster->x() - player.x()) > range ||
             std::abs(monster->y() - player.y()) > range)) {
          continue;
        }
        ++count;
      }
      const auto success = count >= needed;
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "CHECKHUM") {
      std::string map_id = config_.id;
      std::int32_t needed = 1;
      if (tokens.size() == 1) {
        if (const auto maybe_needed = parse_int32(tokens[0]); maybe_needed.has_value()) {
          needed = *maybe_needed;
        } else {
          map_id = util::trim(tokens[0]);
        }
      } else if (tokens.size() >= 2) {
        map_id = util::trim(tokens[0]);
        needed = int_token(1, 1);
      }
      const auto count = legacy_script_map_hooks_.player_count
                             ? legacy_script_map_hooks_.player_count(map_id)
                             : (map_id == config_.id ? legacy_live_player_count() : 0);
      const auto success = count >= needed;
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "CHECKBAGGAGE") {
      bool success = player.has_free_bag_slot();
      if (success && !tokens.empty()) {
        const auto* item_config = find_item_config_by_name_or_id(item_configs_, tokens[0]);
        if (item_config == nullptr) {
          success = false;
        } else {
          LegacyUserItem item;
          item.index = static_cast<std::uint16_t>(std::clamp(item_config->id, 0, 65535));
          item.dura_max =
              static_cast<std::uint16_t>(std::clamp(item_config->dura_max, 0, 65535));
          item.dura = item.dura_max;
          success = player.can_add_bag_item(item, item_configs_);
        }
      }
      trace("condition", success, success ? 1 : 0, condition_line);
      return success;
    }
    if (command_name == "CHECKNAMELIST" || command_name == "CHECKIDLIST" ||
        command_name == "CHECK_DELETE_NAMELIST" || command_name == "CHECK_DELETE_IDLIST") {
      const auto key = list_key(tokens.empty() ? std::string{} : tokens[0]);
      const auto subject = tokens.size() > 1 ? util::lower_copy(tokens[1])
                                             : util::lower_copy(command_name == "CHECKIDLIST" ||
                                                                       command_name == "CHECK_DELETE_IDLIST"
                                                                   ? player.character().account_id
                                                                   : player.character().character_name);
      const auto found = script_name_lists_->contains(key, subject);
      std::size_t list_size = script_name_lists_->size(key);
      if (found && (command_name == "CHECK_DELETE_NAMELIST" ||
                    command_name == "CHECK_DELETE_IDLIST")) {
        list_size = script_name_lists_->remove(key, subject);
      }
      trace("condition", found, static_cast<std::int32_t>(list_size), condition_line);
      return found;
    }
    if (command_name == "IFGETDAILYQUEST") {
      const auto success = player.daily_quest() == 0;
      trace("condition", success, static_cast<std::int32_t>(player.daily_quest()), condition_line);
      return success;
    }
    if (command_name == "CHECKDAILYQUEST") {
      const auto expected = int_token(0, static_cast<std::int32_t>(player.daily_quest()));
      const auto success = static_cast<std::int32_t>(player.daily_quest()) == expected;
      trace("condition", success, static_cast<std::int32_t>(player.daily_quest()), condition_line);
      return success;
    }
    if (command_name == "EQUAL" || command_name == "LARGE" || command_name == "SMALL") {
      if (tokens.size() < 2) {
        trace("condition", false, 0, condition_line);
        return false;
      }
      const auto lhs = script_value(tokens[0]);
      const auto rhs = script_value(tokens[1]);
      const auto success = command_name == "EQUAL" ? lhs == rhs
                          : command_name == "LARGE" ? lhs > rhs
                                                     : lhs < rhs;
      trace("condition", success, lhs, condition_line);
      return success;
    }
    trace("unsupported_condition", false, 0, condition_line);
    return false;
  };

  auto evaluate_conditions = [&](const std::vector<std::string>& conditions) {
    for (const auto& condition : conditions) {
      if (!evaluate_condition(condition)) {
        return false;
      }
    }
    return true;
  };

  bool script_state_mutated = false;
  bool stop_script = false;
  bool player_transferred = false;
  bool suppress_pending_say = false;
  std::vector<std::string> pending_say_lines;

  auto flush_pending_say = [&]() {
    if (pending_say_lines.empty() || player_transferred || suppress_pending_say) {
      return;
    }
    queue_packet(dispatch, player.session_id(),
                 make_merchant_say_packet(
                     player.session_id(), npc.id(), npc,
                     render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                            join_dialog_lines(pending_say_lines),
                                            item_configs_, script_global_params)));
    trace("say", true, static_cast<std::int32_t>(pending_say_lines.size()), action);
    pending_say_lines.clear();
  };

  auto remove_bag_item_by_name = [&](std::string_view item_name_text) -> std::optional<LegacyUserItem> {
    const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
    for (std::size_t slot = 0; slot < player.character().bag_items.size(); ++slot) {
      const auto& item = player.character().bag_items[slot];
      if (!is_empty(item) && util::lower_copy(item_name(item, item_configs_)) == wanted) {
        return player.remove_bag_item_at(slot);
      }
    }
    return std::nullopt;
  };

  auto count_bag_items_by_name = [&](std::string_view item_name_text) {
    const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
    return static_cast<std::int32_t>(std::count_if(
        player.character().bag_items.begin(), player.character().bag_items.end(),
        [&](const LegacyUserItem& item) {
          return !is_empty(item) && util::lower_copy(item_name(item, item_configs_)) == wanted;
        }));
  };

  auto queue_inventory_refresh = [&]() {
    player.refresh_derived_state(item_configs_);
    queue_packet(dispatch, player.session_id(),
                 make_bag_items_packet(player.session_id(), player, item_configs_));
    queue_packet(dispatch, player.session_id(),
                 make_weight_changed_packet(player.session_id(), player.character()));
  };

  auto execute_action = [&](const std::string& action_line) -> std::optional<std::string> {
    const auto command_name = script_command_name(action_line);
    const auto payload = script_command_payload(action_line);
    const auto tokens = split_script_tokens(payload);
    auto int_token = [&](std::size_t index, std::int32_t fallback = 0) {
      if (index >= tokens.size()) {
        return fallback;
      }
      return parse_int32(tokens[index]).value_or(fallback);
    };
    if (command_name == "SAY") {
      queue_packet(dispatch, player.session_id(),
                   make_merchant_say_packet(
                       player.session_id(), npc.id(), npc,
                       render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                              payload, item_configs_, script_global_params)));
      trace("say", true, 0, payload);
      return std::nullopt;
    }
    if (command_name == "GOTO") {
      const auto target = util::trim(payload);
      if (target.empty() || util::lower_copy(target) == lowered_action) {
        trace("goto_reject", false, 0, action_line);
      } else {
        trace("goto", true, 0, target);
        return target;
      }
      return std::nullopt;
    }
    if (command_name == "CALL") {
      std::string target;
      for (const auto& token : tokens) {
        if (!token.empty() && token.front() == '@') {
          target = token;
        }
      }
      if (target.empty() || util::lower_copy(target) == lowered_action) {
        trace("call_reject", false, 0, action_line);
      } else {
        trace("call", true, 0, target);
        return target;
      }
      return std::nullopt;
    }
    if (command_name == "CLOSE") {
      pending_say_lines.clear();
      suppress_pending_say = true;
      queue_packet(dispatch, player.session_id(),
                   make_merchant_dlg_close_packet(player.session_id()));
      trace("close", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }
    if (command_name == "SENDMSG" || command_name == "SYSMSG") {
      std::size_t message_start = 0;
      std::int32_t channel = 0;
      if (tokens.size() > 1) {
        const auto maybe_channel = parse_int32(tokens[0]);
        if (maybe_channel.has_value()) {
          channel = *maybe_channel;
          message_start = 1;
        }
      }
      auto message = tokens.empty() ? std::string{} : join_tokens(tokens, message_start);
      message = render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                       std::move(message), item_configs_, script_global_params);
      if (message.empty()) {
        trace(util::lower_copy(command_name) + "_reject", false, channel, action_line);
        return std::nullopt;
      }
      queue_system_notice(dispatch, player, message);
      trace(util::lower_copy(command_name), true, channel, message);
      return std::nullopt;
    }
    if (command_name == "BREAK") {
      trace("break", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }
    if (command_name == "ENDQUEST") {
      script_context.end_quest = true;
      trace("endquest", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }
    if (command_name == "SET" || command_name == "RESET" || command_name == "SETOPEN" ||
        command_name == "SETUNIT" || command_name == "RESETUNIT") {
      if (tokens.empty()) {
        trace("quest_mark_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto index = parse_script_index(tokens[0]).value_or(0);
      std::uint8_t value = 0;
      if (command_name == "SET" || command_name == "SETOPEN" || command_name == "SETUNIT") {
        value = static_cast<std::uint8_t>(std::clamp(tokens.size() > 1 ? int_token(1, 1) : 1, 0, 255));
      }
      bool ok = false;
      if (command_name == "SET" || command_name == "RESET") {
        ok = player.set_quest_mark(index, value);
      } else if (command_name == "SETOPEN") {
        ok = player.set_quest_open_unit(index, value);
      } else {
        ok = player.set_quest_unit(index, value);
      }
      script_state_mutated = script_state_mutated || ok;
      trace("quest_mark", ok, value, action_line);
      return std::nullopt;
    }
    if (command_name == "PARAM1" || command_name == "PARAM2" || command_name == "PARAM3" ||
        command_name == "PARAM4") {
      const auto index = command_name.back() - '0';
      const auto raw_value = util::trim(payload);
      const auto value = tokens.empty() ? 0 : script_value(tokens[0]);
      local_param_values[static_cast<std::size_t>(index)] = value;
      local_param_text[static_cast<std::size_t>(index)] = raw_value;
      local_param_set[static_cast<std::size_t>(index)] = true;
      trace("param", true, value, action_line);
      return std::nullopt;
    }
    if (command_name == "MOV" || command_name == "INC" || command_name == "DEC" ||
        command_name == "SUM" || command_name == "MOVR") {
      if (tokens.empty()) {
        trace("variable_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto current = script_value(tokens[0]);
      const auto operand = tokens.size() > 1 ? script_value(tokens[1])
                                             : command_name == "SUM" ? 0 : 1;
      std::int32_t value = current;
      bool ok = false;
      if (command_name == "MOV") {
        value = operand;
        ok = set_script_value(tokens[0], value);
      } else if (command_name == "INC") {
        value = current + operand;
        ok = set_script_value(tokens[0], value);
      } else if (command_name == "DEC") {
        value = current - operand;
        ok = set_script_value(tokens[0], value);
      } else if (command_name == "SUM") {
        value = current + operand;
        ok = set_script_group_sum(tokens[0], value);
      } else if (command_name == "MOVR") {
        const auto range = std::max(operand, 1);
        value = legacy_random_value(dispatch, "LegacyScript", "MOVR", range, player.id(),
                                    npc.id(), action_line, now_ms, current_tick);
        ok = set_script_value(tokens[0], value);
      }
      script_state_mutated = script_state_mutated || (ok && is_persistent_script_value(tokens[0]));
      trace("variable", ok, value, action_line);
      return std::nullopt;
    }
    if (command_name == "ADDNAMELIST" || command_name == "DELNAMELIST" ||
        command_name == "ADDIDLIST" || command_name == "DELIDLIST") {
      const auto key = list_key(tokens.empty() ? std::string{} : tokens[0]);
      const auto subject = tokens.size() > 1 ? util::lower_copy(tokens[1])
                                             : util::lower_copy(command_name == "ADDIDLIST" ||
                                                                       command_name == "DELIDLIST"
                                                                   ? player.character().account_id
                                                                   : player.character().character_name);
      const auto list_size =
          command_name == "ADDNAMELIST" || command_name == "ADDIDLIST"
              ? script_name_lists_->add(key, subject)
              : script_name_lists_->remove(key, subject);
      trace("namelist", true, static_cast<std::int32_t>(list_size), action_line);
      return std::nullopt;
    }
    if (command_name == "SETDAILYQUEST" || command_name == "RANDOMSETDAILYQUEST") {
      std::int32_t value = int_token(0, 0);
      if (command_name == "RANDOMSETDAILYQUEST") {
        const auto count = std::max<int>(tokens.empty() ? 0 : static_cast<int>(tokens.size()), 1);
        const auto index = legacy_random_value(dispatch, "LegacyScript", "RANDOMSETDAILYQUEST",
                                               count, player.id(), npc.id(), action_line,
                                               now_ms, current_tick);
        value = int_token(static_cast<std::size_t>(std::clamp(index, 0, count - 1)), value);
      }
      player.set_daily_quest(static_cast<std::uint32_t>(std::max(value, 0)));
      script_state_mutated = true;
      trace("daily_quest", true, value, action_line);
      return std::nullopt;
    }
    if (command_name == "GIVE") {
      const auto target = parse_script_amount_target(payload);
      if (is_legacy_script_gold_token(target.target)) {
        const auto new_gold =
            static_cast<std::int64_t>(player.character().gold) + target.amount;
        if (target.amount < 0 || new_gold > kLegacyBagGold) {
          trace("give_gold_reject", false, target.amount, action_line);
          return std::nullopt;
        }
        player.add_gold(target.amount);
        queue_packet(dispatch, player.session_id(),
                     make_gold_changed_packet(player.session_id(), player.character().gold));
        script_state_mutated = true;
        trace("give_gold", true, target.amount, action_line);
        return std::nullopt;
      }
      const auto* item_config = find_item_config_by_name_or_id(item_configs_, target.target);
      if (item_config == nullptr) {
        trace("give_item_reject", false, target.amount, action_line);
        return std::nullopt;
      }
      for (std::int32_t index = 0; index < target.amount; ++index) {
        LegacyUserItem item;
        item.index = static_cast<std::uint16_t>(std::clamp(item_config->id, 0, 65535));
        item.make_index = allocate_make_index();
        item.dura_max =
            static_cast<std::uint16_t>(std::clamp(item_config->dura_max, 0, 65535));
        item.dura = item.dura_max;
        if (!player.can_add_bag_item(item, item_configs_) || !player.add_bag_item(item)) {
          trace("give_item_reject", false, index, action_line);
          break;
        }
        queue_packet(dispatch, player.session_id(),
                     make_add_item_packet(player.session_id(), player.id(), item, item_configs_));
        script_state_mutated = true;
        trace("give_item", true, item.make_index, item_config->name);
      }
      queue_inventory_refresh();
      return std::nullopt;
    }
    if (command_name == "TAKE") {
      const auto target = parse_script_amount_target(payload);
      if (is_legacy_script_gold_token(target.target)) {
        if (!player.can_spend_gold(target.amount)) {
          trace("take_gold_reject", false, target.amount, action_line);
          return std::nullopt;
        }
        player.spend_gold(target.amount);
        queue_packet(dispatch, player.session_id(),
                     make_gold_changed_packet(player.session_id(), player.character().gold));
        script_state_mutated = true;
        trace("take_gold", true, target.amount, action_line);
        return std::nullopt;
      }
      if (target.amount > 0 && count_bag_items_by_name(target.target) < target.amount) {
        trace("take_item_reject", false, count_bag_items_by_name(target.target), action_line);
        queue_inventory_refresh();
        return std::nullopt;
      }
      std::vector<LegacyUserItem> removed;
      for (std::int32_t index = 0; index < target.amount; ++index) {
        auto item = remove_bag_item_by_name(target.target);
        if (!item.has_value()) {
          for (const auto& rollback_item : removed) {
            static_cast<void>(player.add_bag_item(rollback_item));
          }
          trace("take_item_reject", false, index, action_line);
          queue_inventory_refresh();
          return std::nullopt;
        }
        removed.push_back(*item);
      }
      trace("take_item", true, target.amount, action_line);
      if (!removed.empty()) {
        script_context.last_taken_item_name = item_name(removed.back(), item_configs_);
      }
      script_state_mutated = true;
      queue_inventory_refresh();
      return std::nullopt;
    }
    if (command_name == "TAKECHECKITEM") {
      const auto target = parse_script_amount_target(payload);
      if (target.amount <= 0) {
        trace("takecheckitem", true, 0, action_line);
        return std::nullopt;
      }
      if (!script_context.last_checked_item.has_value()) {
        trace("takecheckitem_reject", false, 0, action_line);
        queue_inventory_refresh();
        return std::nullopt;
      }
      std::vector<LegacyUserItem> removed;
      auto checked_item = player.remove_bag_item(
          script_context.last_checked_item->make_index,
          script_context.last_checked_item_name.value_or(std::string{}), item_configs_);
      if (!checked_item.has_value()) {
        trace("takecheckitem_reject", false, 0, action_line);
        queue_inventory_refresh();
        return std::nullopt;
      }
      removed.push_back(*checked_item);
      for (std::int32_t index = 1; index < target.amount; ++index) {
        auto item = remove_bag_item_by_name(target.target);
        if (!item.has_value()) {
          for (const auto& rollback_item : removed) {
            static_cast<void>(player.add_bag_item(rollback_item));
          }
          trace("takecheckitem_reject", false, index, action_line);
          queue_inventory_refresh();
          return std::nullopt;
        }
        removed.push_back(*item);
      }
      script_state_mutated = true;
      trace("takecheckitem", true, target.amount, action_line);
      queue_inventory_refresh();
      return std::nullopt;
    }
    if (command_name == "TAKEW") {
      const auto target = parse_script_amount_target(payload);
      const auto slots = legacy_equipment_slots_for_alias(target.target);
      if (!slots.empty()) {
        std::int32_t equipped_count = 0;
        for (const auto slot : slots) {
          const auto* item = player.equipped_item(slot);
          if (item != nullptr && !is_empty(*item)) {
            ++equipped_count;
          }
        }
        if (equipped_count < target.amount) {
          trace("takew_reject", false, equipped_count, action_line);
          return std::nullopt;
        }
        std::int32_t removed_count = 0;
        for (const auto slot : slots) {
          auto* item = player.equipped_item_mutable(slot);
          if (item == nullptr || is_empty(*item)) {
            continue;
          }
          script_context.last_taken_item_name = item_name(*item, item_configs_);
          *item = LegacyUserItem{};
          ++removed_count;
          if (removed_count >= target.amount) {
            break;
          }
        }
        const auto success = removed_count >= target.amount;
        script_state_mutated = script_state_mutated || success;
        queue_inventory_refresh();
        trace("takew", success, removed_count, action_line);
        return std::nullopt;
      }
      const auto wanted = util::lower_copy(util::trim(target.target));
      if (count_player_equipped_items_by_name(player, target.target, item_configs_) < target.amount) {
        trace("takew_reject", false, 0, action_line);
        return std::nullopt;
      }
      std::int32_t removed_count = 0;
      for (std::size_t slot = 0; slot < player.character().equipped_items.size() &&
                                 removed_count < target.amount; ++slot) {
        auto* item = player.equipped_item_mutable(slot);
        if (item == nullptr || is_empty(*item) ||
            util::lower_copy(item_name(*item, item_configs_)) != wanted) {
          continue;
        }
        script_context.last_taken_item_name = item_name(*item, item_configs_);
        *item = LegacyUserItem{};
        ++removed_count;
      }
      const auto success = removed_count >= target.amount;
      script_state_mutated = script_state_mutated || success;
      queue_inventory_refresh();
      trace("takew", success, removed_count, action_line);
      return std::nullopt;
    }
    if (command_name == "MAPMOVE") {
      if (tokens.size() < 3) {
        trace("mapmove_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto map_id = util::trim(tokens[0]);
      const auto x = parse_int32(tokens[1]);
      const auto y = parse_int32(tokens[2]);
      if (!x.has_value() || !y.has_value()) {
        trace("mapmove_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto cross_map = !map_id.empty() && map_id != config_.id;
      if (!cross_map && *x == player.x() && *y == player.y()) {
        trace("mapmove", true, 0, action_line);
        return std::nullopt;
      }
      if (!try_item_map_move(player, map_id, *x, *y, dispatch, current_tick, now_ms)) {
        trace("mapmove_reject", false, 0, action_line);
        return std::nullopt;
      }
      player_transferred = cross_map;
      stop_script = stop_script || cross_map;
      trace("mapmove", true, 0, action_line);
      return std::nullopt;
    }
    if (command_name == "MAP") {
      const auto target_map = tokens.empty() ? config_.id : tokens[0];
      auto target = random_item_scroll_target(dispatch, player, current_tick, now_ms);
      if (!target.has_value()) {
        trace("map_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto cross_map = !target_map.empty() && target_map != config_.id;
      if (!try_item_map_move(player, target_map, target->first, target->second, dispatch,
                             current_tick, now_ms)) {
        trace("map_reject", false, 0, action_line);
        return std::nullopt;
      }
      player_transferred = cross_map;
      stop_script = stop_script || cross_map;
      trace("map", true, 0, action_line);
      return std::nullopt;
    }
    if (command_name == "GOQUEST") {
      const auto target = tokens.empty() ? std::string{} : tokens[0];
      if (!target.empty()) {
        trace("goquest", true, 0, target);
        return target;
      }
      trace("goquest_reject", false, 0, action_line);
      return std::nullopt;
    }
    if (command_name == "PLAYDICE") {
      const auto dice_count = std::max(int_token(0, 0), 0);
      const auto target = tokens.size() > 1 ? tokens[1] : std::string{};
      flush_pending_say();
      queue_packet(dispatch, player.session_id(),
                   make_play_dice_packet(player.session_id(), npc.id(), dice_count,
                                         player.script_dice_params(), target));
      stop_script = true;
      trace("playdice", true, dice_count, target.empty() ? action_line : target);
      return std::nullopt;
    }
    if (command_name == "MONGEN") {
      if (tokens.empty()) {
        trace("mongen_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto spawn_count = std::max(int_token(1, 1), 1);
      const auto scatter_range = std::max(int_token(2, 0), 0);
      auto target_map = local_param_set[1] ? util::trim(local_param_text[1]) : config_.id;
      if (target_map.empty()) {
        target_map = config_.id;
      }
      const auto base_x = local_param_set[2] ? local_param_values[2] : player.x();
      const auto base_y = local_param_set[3] ? local_param_values[3] : player.y();
      std::int32_t spawned = 0;
      for (std::int32_t index = 0; index < spawn_count; ++index) {
        const auto dx = scatter_range > 0
                            ? legacy_random_value(dispatch, "LegacyScript", "MONGEN_X",
                                                  scatter_range * 2 + 1, player.id(), npc.id(),
                                                  action_line, now_ms, current_tick) -
                                  scatter_range
                            : 0;
        const auto dy = scatter_range > 0
                            ? legacy_random_value(dispatch, "LegacyScript", "MONGEN_Y",
                                                  scatter_range * 2 + 1, player.id(), npc.id(),
                                                  action_line, now_ms, current_tick + index) -
                                  scatter_range
                            : 0;
        ActorMail spawn;
        spawn.kind = ActorMailKind::spawn_monster;
        spawn.map_id = target_map;
        spawn.actor_id = next_script_monster_id_++;
        spawn.name = tokens[0];
        spawn.x = base_x + dx;
        spawn.y = base_y + dy;
        spawn.level = 1;
        spawn.max_hp = 30;
        spawn.attack_power = 3;
        spawn.defense = 0;
        spawn.magic_defense = 0;
        spawn.exp_reward = 1;
        spawn.home_x = spawn.x;
        spawn.home_y = spawn.y;
        spawn.home_area = 6;
        spawn.legacy_spawn_group = true;
        if (target_map == config_.id) {
          handle_mail(spawn, dispatch, current_tick, now_ms);
        } else {
          dispatch.cross_map_mails.push_back(spawn);
        }
        ++spawned;
      }
      trace("mongen", true, spawned, action_line);
      return std::nullopt;
    }
    if (command_name == "MONCLEAR") {
      const auto map_id = tokens.empty() ? config_.id : util::trim(tokens[0]);
      const auto removed = legacy_script_map_hooks_.clear_monsters
                               ? legacy_script_map_hooks_.clear_monsters(map_id, dispatch,
                                                                         current_tick, now_ms)
                               : (map_id == config_.id
                                      ? legacy_clear_monsters(dispatch, current_tick, now_ms)
                                      : 0);
      trace("monclear", true, removed, action_line);
      return std::nullopt;
    }
    if (command_name == "TIMERECALL") {
      const auto minutes = int_token(0, 0);
      const auto tick_ms =
          static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1));
      const auto delay_ticks =
          minutes <= 0
              ? 1ULL
              : std::max<std::uint64_t>(
                    1, (static_cast<std::uint64_t>(minutes) * 60'000ULL + tick_ms - 1) /
                           tick_ms);
      dispatch.legacy_time_recall_requests.push_back(
          LegacyTimeRecallRequest{LegacyTimeRecallRequestKind::schedule,
                                  player.session_id(), player.id(), config_.id,
                                  player.x(), player.y(), delay_ticks});
      trace("time_recall", true, minutes, action_line);
      return std::nullopt;
    }
    if (command_name == "BREAKTIMERECALL") {
      dispatch.legacy_time_recall_requests.push_back(
          LegacyTimeRecallRequest{LegacyTimeRecallRequestKind::cancel,
                                  player.session_id(), player.id(), config_.id,
                                  player.x(), player.y(), 0});
      trace("time_recall_cancel", true, 0, action_line);
      return std::nullopt;
    }
    if (command_name == "EXCHANGEMAP") {
      if (tokens.empty()) {
        trace("deferred_action_reject", false, 0, action_line);
        return std::nullopt;
      }
      dispatch.legacy_batch_move_requests.push_back(LegacyBatchMoveRequest{
          LegacyBatchMoveRequestKind::exchange_map, player.id(), config_.id,
          util::trim(tokens[0]), 0});
      trace("deferred_action", true, 1, action_line);
      return std::nullopt;
    }
    if (command_name == "RECALLMAP") {
      if (tokens.empty()) {
        trace("deferred_action_reject", false, 0, action_line);
        return std::nullopt;
      }
      dispatch.legacy_batch_move_requests.push_back(LegacyBatchMoveRequest{
          LegacyBatchMoveRequestKind::recall_map, player.id(), util::trim(tokens[0]),
          config_.id, 0});
      trace("deferred_action", true, 1, action_line);
      return std::nullopt;
    }
    if (command_name == "BATCHDELAY") {
      const auto seconds = tokens.empty() ? 10 : std::max(int_token(0, 10), 0);
      batch_delay_ticks = legacy_seconds_to_ticks(seconds);
      trace("deferred_action", true, seconds, action_line);
      return std::nullopt;
    }
    if (command_name == "ADDBATCH") {
      if (tokens.empty()) {
        trace("deferred_action_reject", false, 0, action_line);
        return std::nullopt;
      }
      batch_move_requests.push_back(LegacyBatchMoveRequest{
          LegacyBatchMoveRequestKind::random_actor_to_map, player.id(), config_.id,
          util::trim(tokens[0]), batch_delay_ticks});
      trace("deferred_action", true, static_cast<std::int32_t>(batch_move_requests.size()),
            action_line);
      return std::nullopt;
    }
    if (command_name == "BATCHMOVE") {
      if (!tokens.empty()) {
        batch_move_requests.push_back(LegacyBatchMoveRequest{
            LegacyBatchMoveRequestKind::random_actor_to_map, player.id(), config_.id,
            util::trim(tokens[0]), batch_delay_ticks});
      }
      if (batch_move_requests.empty()) {
        trace("deferred_action_reject", false, 0, action_line);
        return std::nullopt;
      }
      const auto request_count = static_cast<std::int32_t>(batch_move_requests.size());
      for (auto& request : batch_move_requests) {
        dispatch.legacy_batch_move_requests.push_back(std::move(request));
      }
      batch_move_requests.clear();
      trace("deferred_action", true, request_count, action_line);
      return std::nullopt;
    }

    trace("unsupported_action", false, 0, action_line);
    return std::nullopt;
  };

  for (std::size_t proc_index = 0; proc_index < block.procs.size(); ++proc_index) {
    const auto& proc = block.procs[proc_index];
    const auto condition_result = evaluate_conditions(proc.conditions);
    trace("condition_result", condition_result, static_cast<std::int32_t>(proc_index), action);

    const auto& selected_say_lines =
        condition_result ? proc.say_lines : proc.else_say_lines;
    pending_say_lines.insert(pending_say_lines.end(), selected_say_lines.begin(),
                             selected_say_lines.end());

    const auto& actions = condition_result ? proc.act_lines : proc.else_act_lines;
    for (const auto& action_line : actions) {
      const auto maybe_goto = execute_action(action_line);
      if (stop_script || player_transferred) {
        break;
      }
      if (maybe_goto.has_value()) {
        flush_pending_say();
        static_cast<void>(legacy_execute_npc_script(player, npc, *maybe_goto, dispatch,
                                                   current_tick, now_ms, script_context,
                                                   depth + 1));
        stop_script = true;
        break;
      }
    }
    if (stop_script || player_transferred) {
      break;
    }
  }
  flush_pending_say();

  if (script_state_mutated && !player_transferred) {
    queue_save_character(dispatch, player);
  }

  return true;
}

bool MapActor::trigger_startup_quest(Player& player, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick, std::uint64_t now_ms) {
  if (startup_quest_dialog_sections_.empty()) {
    return false;
  }

  Npc quest_npc(kStartupQuestNpcObjectId, "StartupQuest", config_.id, player.x(), player.y(),
                "none", {}, startup_quest_dialog_sections_);
  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::merchant_select;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = player.id();
  trace_mail.session_id = player.session_id();
  trace_mail.target_actor_id = quest_npc.id();
  trace_mail.payload = "StartupQuest";
  add_legacy_trace(dispatch, "LegacyScript", "startupquest_trigger", trace_mail, current_tick,
                   now_ms, true, 0, 0, "enter");
  LegacyScriptExecutionContext script_context;
  static_cast<void>(legacy_execute_npc_script(player, quest_npc, "@main", dispatch,
                                              current_tick, now_ms, script_context, 0));
  return true;
}

bool MapActor::trigger_map_quest(Player& player, std::string monster_name, std::string item_name,
                                 bool group_call, std::string source, RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto wanted_monster = util::lower_copy(util::trim(monster_name));
  const auto wanted_item = util::lower_copy(util::trim(item_name));
  bool triggered = false;
  for (std::size_t index = 0; index < map_quests_.size(); ++index) {
    const auto& quest = map_quests_[index];
    if (quest.map_id != config_.id) {
      continue;
    }
    if (player.quest_mark(quest.set_number) !=
        static_cast<std::uint8_t>(std::clamp(quest.value, 0, 1))) {
      continue;
    }
    if (quest.enable_group != group_call) {
      continue;
    }

    const auto quest_monster = util::lower_copy(util::trim(quest.monster_name));
    const auto quest_item = util::lower_copy(util::trim(quest.item_name));
    bool matches = false;
    if (quest_monster.empty() && quest_item.empty()) {
      matches = false;
    } else if (!quest_monster.empty() && !quest_item.empty()) {
      matches = quest_monster == wanted_monster && quest_item == wanted_item;
    } else if (!quest_monster.empty()) {
      matches = quest_monster == wanted_monster;
    } else if (!quest_item.empty()) {
      matches = quest_item == wanted_item;
    }
    if (!matches) {
      continue;
    }
    if (quest.dialog_sections.empty()) {
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::merchant_select;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = player.id();
      trace_mail.session_id = player.session_id();
      trace_mail.payload = "MapQuest:" + quest.qfile;
      add_legacy_trace(dispatch, "LegacyScript", "mapquest_missing_script", trace_mail,
                       current_tick, now_ms, false, static_cast<std::int32_t>(index), 0, source);
      continue;
    }

    Npc quest_npc(kMapQuestNpcObjectBase + index,
                  quest.qfile.empty() ? std::string("MapQuest") : quest.qfile, config_.id,
                  player.x(), player.y(), "none", {}, quest.dialog_sections);
    ActorMail trace_mail;
    trace_mail.kind = ActorMailKind::merchant_select;
    trace_mail.map_id = config_.id;
    trace_mail.actor_id = player.id();
    trace_mail.session_id = player.session_id();
    trace_mail.target_actor_id = quest_npc.id();
    trace_mail.payload = "MapQuest:" + quest.qfile;
    add_legacy_trace(dispatch, "LegacyScript", "mapquest_trigger", trace_mail, current_tick,
                     now_ms, true, static_cast<std::int32_t>(index), 0, source);
    LegacyScriptExecutionContext script_context;
    static_cast<void>(legacy_execute_npc_script(player, quest_npc, "@main", dispatch,
                                                current_tick, now_ms, script_context, 0));
    triggered = true;
    if (script_context.end_quest) {
      break;
    }
  }
  return triggered;
}

