#pragma once

// Implementation detail for map_actor.cpp: NPC script and map quest members.
bool MapActor::legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms) {
  return legacy_execute_npc_script(player, npc, std::move(action), dispatch, current_tick,
                                   now_ms, 0);
}

bool MapActor::legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms,
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
                                            build_merchant_dialog_text(npc), item_configs_)));
    trace("say", true, 0, "default_merchant_dialog");
    return true;
  }
  if (dialog == nullptr) {
    trace("missing_section", false, 0, action);
    return false;
  }

  const auto block = parse_legacy_script_block(*dialog);

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

  auto script_value = [&](std::string_view raw) -> std::int32_t {
    auto token = util::trim(std::string(raw));
    if (token.empty()) {
      return 0;
    }
    const auto upper = script_upper_copy(token);
    if (upper.size() == 2 && upper[0] == 'P' && std::isdigit(static_cast<unsigned char>(upper[1])) != 0) {
      return player.script_param(upper[1] - '0');
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
    if (upper.size() == 2 && upper[0] == 'P' && std::isdigit(static_cast<unsigned char>(upper[1])) != 0) {
      return player.set_script_param(upper[1] - '0', value);
    }
    if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
      return player.set_quest_mark(parse_script_index(token).value_or(0),
                                   static_cast<std::uint8_t>(std::clamp(value, 0, 255)));
    }
    return false;
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
    if (command_name == "CHECKITEM" || command_name == "ISTAKEITEM") {
      const auto target = parse_script_amount_target(payload);
      const auto count = count_player_bag_items_by_name(player, target.target, item_configs_);
      const auto success = count >= target.amount;
      trace("condition", success, count, condition_line);
      return success;
    }
    if (command_name == "CHECKITEMW") {
      const auto target = parse_script_amount_target(payload);
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
      const auto success = value <= 0;
      trace("condition", success, 0, condition_line);
      return success;
    }
    if (command_name == "CHECKMONMAP" || command_name == "CHECKMONAREA") {
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
      const auto needed = int_token(0, 1);
      std::int32_t count = 0;
      for (const auto& [_, object] : objects_) {
        const auto* other = as_player(object.get());
        if (other != nullptr && !other->is_dead()) {
          ++count;
        }
      }
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
    if (command_name == "CHECKNAMELIST" || command_name == "CHECK_DELETE_NAMELIST" ||
        command_name == "CHECK_DELETE_IDLIST") {
      const auto key = list_key(tokens.empty() ? std::string{} : tokens[0]);
      const auto subject = tokens.size() > 1 ? util::lower_copy(tokens[1])
                                             : util::lower_copy(command_name == "CHECK_DELETE_IDLIST"
                                                                    ? player.character().account_id
                                                                    : player.character().character_name);
      auto& list = script_name_lists_[key];
      const auto found = list.find(subject) != list.end();
      if (found && command_name != "CHECKNAMELIST") {
        list.erase(subject);
      }
      trace("condition", found, static_cast<std::int32_t>(list.size()), condition_line);
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

  bool condition_result = true;
  for (const auto& condition : block.conditions) {
    if (!evaluate_condition(condition)) {
      condition_result = false;
      break;
    }
  }
  trace("condition_result", condition_result, 0, action);

  const auto& selected_say_lines =
      condition_result ? block.say_lines : block.else_say_lines;
  if (!selected_say_lines.empty()) {
    queue_packet(dispatch, player.session_id(),
                 make_merchant_say_packet(
                     player.session_id(), npc.id(), npc,
                     render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                            join_dialog_lines(selected_say_lines),
                                            item_configs_)));
    trace("say", true, static_cast<std::int32_t>(selected_say_lines.size()), action);
  }

  bool script_state_mutated = false;
  bool stop_script = false;
  bool player_transferred = false;

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
                                              payload, item_configs_)));
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
      queue_packet(dispatch, player.session_id(),
                   make_merchant_dlg_close_packet(player.session_id()));
      trace("close", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }
    if (command_name == "BREAK" || command_name == "ENDQUEST") {
      trace(util::lower_copy(command_name), true, 0, action_line);
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
      const auto operand = tokens.size() > 1 ? script_value(tokens[1]) : 1;
      std::int32_t value = current;
      if (command_name == "MOV") {
        value = operand;
      } else if (command_name == "INC") {
        value = current + operand;
      } else if (command_name == "DEC") {
        value = current - operand;
      } else if (command_name == "SUM") {
        value = 0;
        for (std::size_t index = 1; index < tokens.size(); ++index) {
          value += script_value(tokens[index]);
        }
      } else if (command_name == "MOVR") {
        const auto range = std::max(operand, 1);
        value = legacy_random_value(dispatch, "LegacyScript", "MOVR", range, player.id(),
                                    npc.id(), action_line, now_ms, current_tick);
      }
      const auto ok = set_script_value(tokens[0], value);
      script_state_mutated = script_state_mutated || ok;
      trace("variable", ok, value, action_line);
      return std::nullopt;
    }
    if (command_name == "ADDNAMELIST" || command_name == "DELNAMELIST") {
      const auto key = list_key(tokens.empty() ? std::string{} : tokens[0]);
      const auto subject = tokens.size() > 1 ? util::lower_copy(tokens[1])
                                             : util::lower_copy(player.character().character_name);
      auto& list = script_name_lists_[key];
      if (command_name == "ADDNAMELIST") {
        list.insert(subject);
      } else {
        list.erase(subject);
      }
      trace("namelist", true, static_cast<std::int32_t>(list.size()), action_line);
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
      if (util::lower_copy(target.target) == "gold") {
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
        item.make_index = next_script_make_index_++;
        item.dura_max =
            static_cast<std::uint16_t>(std::clamp(item_config->dura_max, 0, 65535));
        item.dura = item.dura_max;
        if (!player.can_add_bag_item(item, item_configs_) || !player.add_bag_item(item)) {
          trace("give_item_reject", false, index, action_line);
          break;
        }
        queue_packet(dispatch, player.session_id(),
                     make_add_item_packet(player.session_id(), item, item_configs_));
        script_state_mutated = true;
        trace("give_item", true, item.make_index, item_config->name);
      }
      queue_inventory_refresh();
      return std::nullopt;
    }
    if (command_name == "TAKE") {
      const auto target = parse_script_amount_target(payload);
      if (util::lower_copy(target.target) == "gold") {
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
      script_state_mutated = true;
      queue_inventory_refresh();
      return std::nullopt;
    }
    if (command_name == "TAKECHECKITEM") {
      const auto target = parse_script_amount_target(payload);
      std::vector<LegacyUserItem> removed;
      for (std::int32_t index = 0; index < target.amount; ++index) {
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
      const auto sides = std::max(int_token(0, 6), 1);
      const auto value = legacy_random_value(dispatch, "LegacyScript", "PLAYDICE", sides,
                                             player.id(), npc.id(), action_line, now_ms,
                                             current_tick) + 1;
      if (tokens.size() > static_cast<std::size_t>(value)) {
        const auto target = tokens[static_cast<std::size_t>(value)];
        trace("playdice", true, value, target);
        return target;
      }
      trace("playdice", true, value, action_line);
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
      const auto wanted = tokens.empty() ? std::string{} : util::lower_copy(tokens[0]);
      std::vector<std::uint64_t> remove_ids;
      for (const auto& [actor_id, object] : objects_) {
        const auto* monster = as_monster(object.get());
        if (monster == nullptr) {
          continue;
        }
        if (!wanted.empty() && util::lower_copy(monster->name()) != wanted) {
          continue;
        }
        remove_ids.push_back(actor_id);
      }
      for (const auto actor_id : remove_ids) {
        if (const auto it = objects_.find(actor_id); it != objects_.end()) {
          static_cast<void>(environment_.delete_from_map(
              it->second->x(), it->second->y(), LegacyMapObjectShape::moving_object,
              it->second->id()));
          remove_actor_from_visibility(actor_id, dispatch);
          objects_.erase(it);
        }
      }
      trace("monclear", true, static_cast<std::int32_t>(remove_ids.size()), action_line);
      return std::nullopt;
    }
    if (command_name == "TIMERECALL" || command_name == "BREAKTIMERECALL" ||
        command_name == "EXCHANGEMAP" || command_name == "RECALLMAP" ||
        command_name == "ADDBATCH" || command_name == "BATCHDELAY" ||
        command_name == "BATCHMOVE") {
      trace("deferred_action", true, 0, action_line);
      return std::nullopt;
    }

    trace("unsupported_action", false, 0, action_line);
    return std::nullopt;
  };

  const auto& actions = condition_result ? block.act_lines : block.else_act_lines;
  for (const auto& action_line : actions) {
    const auto maybe_goto = execute_action(action_line);
    if (stop_script || player_transferred) {
      break;
    }
    if (maybe_goto.has_value()) {
      static_cast<void>(legacy_execute_npc_script(player, npc, *maybe_goto, dispatch,
                                                 current_tick, now_ms, depth + 1));
      break;
    }
  }

  if (script_state_mutated && !player_transferred) {
    queue_save_character(dispatch, player);
  }

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
    if (player.quest_mark(quest.set_number) != quest.value) {
      continue;
    }
    if (group_call != quest.enable_group && group_call) {
      continue;
    }

    const auto quest_monster = util::lower_copy(util::trim(quest.monster_name));
    const auto quest_item = util::lower_copy(util::trim(quest.item_name));
    bool matches = false;
    if (quest_monster.empty() && quest_item.empty()) {
      matches = wanted_monster.empty() && wanted_item.empty();
    } else if (!quest_monster.empty() && !quest_item.empty()) {
      matches = quest_monster == wanted_monster && quest_item == wanted_item;
    } else if (!quest_monster.empty()) {
      matches = quest_monster == wanted_monster && wanted_item.empty();
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
    static_cast<void>(
        legacy_execute_npc_script(player, quest_npc, "@main", dispatch, current_tick, now_ms));
    triggered = true;
  }
  return triggered;
}

