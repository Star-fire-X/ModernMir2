/**
 * @file map_actor_npc.hpp
 * @brief NPC 脚本执行和地图任务触发实现 - MapActor 的脚本引擎
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含 NPC 脚本引擎的完整实现。
 *          实现以下核心功能：
 *          1. legacy_execute_npc_script：NPC 脚本执行主引擎
 *             - 条件评估系统（CHECK、CHECKLEVEL、RANDOM、DAYTIME 等 20+ 种条件）
 *             - 动作执行系统（SAY、GOTO、GIVE、TAKE、MAPMOVE、MONGEN 等 30+ 种动作）
 *             - 变量系统（P0-P4 局部变量、G0-G9 全局变量、D0-D4 骰子变量）
 *             - 脚本控制流（条件分支、跳转、调用、子过程）
 *          2. trigger_startup_quest：玩家登录时触发的启动任务
 *          3. trigger_map_quest：怪物击杀/物品触发的地图任务
 *
 *          脚本引擎解析 NPC 对话段（NpcDialogSectionConfig）中的条件-动作对，
 *          每个过程（proc）包含条件列表、执行行（say_lines）和动作行（act_lines）。
 *          支持 8 层深度的 GOTO/CALL 嵌套调用保护。
 *
 * @see map_actor.hpp
 * @see game_object.hpp
 */

#pragma once

// Implementation detail for map_actor.cpp: NPC script and map quest members.

/**
 * @brief NPC 脚本执行入口（简化版本）
 * @param player 执行脚本的玩家
 * @param npc 对话的 NPC 对象
 * @param action 脚本动作标识（如 "@main"）
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return 脚本是否执行成功
 * @details 创建空的脚本执行上下文后委托给完整版本的执行函数。
 */
bool MapActor::legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms) {
  LegacyScriptExecutionContext script_context;
  return legacy_execute_npc_script(player, npc, std::move(action), dispatch, current_tick,
                                   now_ms, script_context, 0);
}

/**
 * @brief NPC 脚本执行主引擎（完整版本）
 * @param player 执行脚本的玩家
 * @param npc 对话的 NPC 对象
 * @param action 脚本动作标识（如 "@main"、"@buy" 等）
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @param script_context 脚本执行上下文（跟踪状态副作用）
 * @param depth 当前递归深度（防止无限递归，上限 8 层）
 * @return 脚本是否执行成功
 * @details 这是整个 NPC 脚本系统的核心执行引擎。
 *
 *          脚本执行流程：
 *          1. 规范化动作名称（空 => "@main"）
 *          2. 处理 @exit 关闭对话框
 *          3. 查找 NPC 对话段中匹配 action 的脚本块
 *          4. 解析脚本块为过程列表（procs）
 *          5. 遍历每个过程，依次评估条件和执行动作
 *          6. 条件满足时执行 say_lines + act_lines，否则执行 else_say_lines + else_act_lines
 *          7. 支持 GOTO/CALL 跳转到其他动作段（递归调用）
 *
 *          条件命令（evaluate_condition）：
 *          CHECK/CHECKOPEN/CHECKUNIT、CHECKLEVEL、CHECKJOB、GENDER、
 *          CHECKGOLD、CHECKITEM、ISTAKEITEM、CHECKITEMW、CHECKDURA、
 *          RANDOM/RANDOMEX、DAYTIME/DAYOFWEEK/HOUR/MIN、
 *          CHECKPKPOINT/CHECKLUCKYPOINT、CHECKMONMAP/CHECKMONAREA、
 *          CHECKHUM/CHECKBAGGAGE、CHECKNAMELIST/CHECKIDLIST、
 *          IFGETDAILYQUEST/CHECKDAILYQUEST、EQUAL/LARGE/SMALL
 *
 *          动作命令（execute_action）：
 *          SAY、GOTO、CALL、CLOSE、SENDMSG/SYSMSG、BREAK、ENDQUEST、
 *          SET/RESET/SETOPEN/SETUNIT/RESETUNIT、PARAM1-PARAM4、
 *          MOV/INC/DEC/SUM/MOVR、ADDNAMELIST/DELNAMELIST/ADDIDLIST/DELIDLIST、
 *          SETDAILYQUEST/RANDOMSETDAILYQUEST、GIVE、TAKE、TAKECHECKITEM、TAKEW、
 *          MAPMOVE、MAP、GOQUEST、PLAYDICE、MONGEN、MONCLEAR、
 *          TIMERECALL/BREAKTIMERECALL、EXCHANGEMAP/RECALLMAP、
 *          ADDBATCH/BATCHMOVE/BATCHDELAY
 *
 * @note 脚本状态变化（物品、金币、标志位等）在脚本结束时自动保存角色
 * @warning GOTO/CALL 深度超过 8 层将被拒绝执行
 */
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

  // ── 处理 @exit 直接关闭对话框 ──────────────────────────────────
  if (lowered_action == "@exit") {
    queue_packet(dispatch, player.session_id(),
                 make_merchant_dlg_close_packet(player.session_id()));
    trace("close", true, 0, "@exit");
    return true;
  }

  // ── 递归深度保护 ────────────────────────────────────────────────
  if (depth > 8) {
    trace("goto_depth_reject", false, depth, action);
    return true;
  }

  // ── 查找 NPC 对话段 ─────────────────────────────────────────────
  const auto* dialog = find_npc_dialog_text(npc, action);
  // 如果找不到 @main 段但 NPC 是商人，发送默认对话文本
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

  // ── 解析脚本块并设置局部状态 ──────────────────────────────────
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

  // ── 脚本值读取 ──────────────────────────────────────────────────
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

  // ── 脚本值写入 ──────────────────────────────────────────────────
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

  // ── 检查脚本值是否需要持久化保存 ──────────────────────────────
  auto is_persistent_script_value = [](std::string_view raw) {
    auto token = util::trim(std::string(raw));
    const auto variable = parse_legacy_script_variable_token(token);
    if (variable.has_value()) {
      return variable->first == 'P';
    }
    return token.size() >= 2 && token.front() == '[' && token.back() == ']';
  };

  // ── 设置变量组累加和（索引 9 为总和位置） ────────────────────
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

  // ── 条件评估系统 ──────────────────────────────────────────────
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

    // 通用标志位比较辅助函数
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

    // 获取本地时间辅助函数
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

    /// @name 任务标志位条件
    /// @{

    // CHECK [index] [value] — 检查任务标记位是否等于指定值
    if (command_name == "CHECK") {
      return compare_mark([&](std::int32_t index) { return player.quest_mark(index); });
    }
    // CHECKOPEN [index] [value] — 检查任务开启单元
    if (command_name == "CHECKOPEN") {
      return compare_mark([&](std::int32_t index) { return player.quest_open_unit(index); });
    }
    // CHECKUNIT [index] [value] — 检查任务单元
    if (command_name == "CHECKUNIT") {
      return compare_mark([&](std::int32_t index) { return player.quest_unit(index); });
    }

    /// @}

    /// @name 角色属性条件
    /// @{

    // CHECKLEVEL <level> — 检查等级是否 >= 指定值
    if (command_name == "CHECKLEVEL") {
      const auto value = int_token(0, 0);
      const auto success = player.character().ability.level >= value;
      trace("condition", success, value, condition_line);
      return success;
    }
    // CHECKJOB <0/1/2|WARRIOR/WIZARD/TAOIST> — 检查职业
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
    // GENDER <0/1|MAN/WOMAN> — 检查性别
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

    /// @}

    /// @name 物品/金币条件
    /// @{

    // CHECKGOLD <amount> — 检查金币是否 >= 指定值
    if (command_name == "CHECKGOLD") {
      const auto value = int_token(0, 0);
      const auto success = player.character().gold >= value;
      trace("condition", success, value, condition_line);
      return success;
    }
    // CHECKITEM <物品名称> [数量] — 检查背包中是否有指定数量物品
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
    // ISTAKEITEM <物品名称> — 检查最后一次 TAKE 的是否为指定物品
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
    // CHECKITEMW <物品名称|装备槽别名> [数量] — 检查装备的物品
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
    // CHECKDURA <物品名称> <耐久值> — 检查背包/装备中最高耐久
    // CHECKDURAEVA <物品名称> <耐久值> — 检查平均耐久值
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

    /// @}

    /// @name 随机条件
    /// @{

    // RANDOM <range> — 1/range 概率成功（random(range) == 0）
    if (command_name == "RANDOM") {
      const auto range = int_token(0, 0);
      const auto value = legacy_random_value(dispatch, "LegacyScript", "RANDOM", range,
                                             player.id(), npc.id(), action, now_ms,
                                             current_tick);
      const auto success = value == 0;
      trace("condition", success, value, condition_line);
      return success;
    }
    // RANDOMEX <point> <range=100> — point% 概率成功
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

    /// @}

    /// @name 时间条件
    /// @{

    // DAYTIME [DAY/NIGHT] — 检查是否为白天/黑夜
    if (command_name == "DAYTIME") {
      const auto wanted = tokens.empty() ? std::string("DAY") : script_upper_copy(tokens[0]);
      const auto is_day = !config_.darkness;
      const auto success = wanted == "DAY" ? is_day : wanted == "NIGHT" ? !is_day : true;
      trace("condition", success, is_day ? 1 : 0, condition_line);
      return success;
    }
    // DAYOFWEEK <1-7|SUN/MON/.../SAT> — 检查星期几
    if (command_name == "DAYOFWEEK") {
      const auto tm = local_time();
      const auto delphi_day = tm.tm_wday + 1;  // Delphi 星期：1=Sun, 2=Mon, ..., 7=Sat
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
    // HOUR <start> [end] — 检查当前小时是否在范围内
    if (command_name == "HOUR") {
      const auto start = int_token(0, 0);
      const auto end = tokens.size() > 1 ? int_token(1, start) : start;
      const auto hour = local_time().tm_hour;
      const auto success = hour >= std::min(start, end) && hour <= std::max(start, end);
      trace("condition", success, hour, condition_line);
      return success;
    }
    // MIN <start> [end] — 检查当前分钟是否在范围内
    if (command_name == "MIN") {
      const auto start = int_token(0, 0);
      const auto end = tokens.size() > 1 ? int_token(1, start) : start;
      const auto minute = local_time().tm_min;
      const auto success = minute >= std::min(start, end) && minute <= std::max(start, end);
      trace("condition", success, minute, condition_line);
      return success;
    }

    /// @}

    /// @name PK/幸运条件
    /// @{

    // CHECKPKPOINT <value> — 检查 PK 值是否 >= 指定值
    if (command_name == "CHECKPKPOINT") {
      const auto value = int_token(0, 0);
      const auto success = player.pk_point() >= value;
      trace("condition", success, player.pk_point(), condition_line);
      return success;
    }
    // CHECKLUCKYPOINT <value> — 检查幸运等级是否 >= 指定值
    if (command_name == "CHECKLUCKYPOINT") {
      const auto value = int_token(0, 0);
      const auto lucky = player.body_luck_level();
      const auto success = lucky >= value;
      trace("condition", success, lucky, condition_line);
      return success;
    }

    /// @}

    /// @name 地图/怪物/玩家计数条件
    /// @{

    // CHECKMONMAP [map_id] <count> — 检查地图上的怪物数量
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
    // CHECKMONAREA [名称] <数量> [范围] — 检查周围怪物数量
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
        if (std::abs(monster->x() - player.x()) > range ||
            std::abs(monster->y() - player.y()) > range) {
          continue;
        }
        ++count;
      }
      const auto success = count >= needed;
      trace("condition", success, count, condition_line);
      return success;
    }
    // CHECKHUM [map_id] <count> — 检查地图上的玩家数量
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
    // CHECKBAGGAGE [物品名称] — 检查背包是否有空位（且能否容纳指定物品）
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

    /// @}

    /// @name 名单条件
    /// @{

    // CHECKNAMELIST <list> [name] — 检查角色名是否在名单中
    // CHECKIDLIST <list> [id] — 检查账号 ID 是否在名单中
    // CHECK_DELETE_NAMELIST <list> [name] — 检查并在名单中删除角色名
    // CHECK_DELETE_IDLIST <list> [id] — 检查并在名单中删除账号 ID
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

    /// @}

    /// @name 日常任务条件
    /// @{

    // IFGETDAILYQUEST — 检查是否未接取日常任务
    if (command_name == "IFGETDAILYQUEST") {
      const auto success = player.daily_quest() == 0;
      trace("condition", success, static_cast<std::int32_t>(player.daily_quest()), condition_line);
      return success;
    }
    // CHECKDAILYQUEST <value> — 检查日常任务 ID 是否匹配
    if (command_name == "CHECKDAILYQUEST") {
      const auto expected = int_token(0, static_cast<std::int32_t>(player.daily_quest()));
      const auto success = static_cast<std::int32_t>(player.daily_quest()) == expected;
      trace("condition", success, static_cast<std::int32_t>(player.daily_quest()), condition_line);
      return success;
    }

    /// @}

    /// @name 通用比较条件
    /// @{

    // EQUAL <lhs> <rhs> — lhs == rhs
    // LARGE <lhs> <rhs> — lhs > rhs
    // SMALL <lhs> <rhs> — lhs < rhs
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

    /// @}

    trace("unsupported_condition", false, 0, condition_line);
    return false;
  };

  // ── 多条件评估（所有条件必须同时满足） ──────────────────────────
  auto evaluate_conditions = [&](const std::vector<std::string>& conditions) {
    for (const auto& condition : conditions) {
      if (!evaluate_condition(condition)) {
        return false;
      }
    }
    return true;
  };

  // ── 脚本执行状态变量 ──────────────────────────────────────────
  bool script_state_mutated = false;
  bool stop_script = false;
  bool player_transferred = false;
  bool suppress_pending_say = false;
  std::vector<std::string> pending_say_lines;

  // 刷新待发送的对话行
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

  // 按名称从背包中移除物品（移除第一个匹配的）
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

  // 统计背包中指定物品的数量
  auto count_bag_items_by_name = [&](std::string_view item_name_text) {
    const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
    return static_cast<std::int32_t>(std::count_if(
        player.character().bag_items.begin(), player.character().bag_items.end(),
        [&](const LegacyUserItem& item) {
          return !is_empty(item) && util::lower_copy(item_name(item, item_configs_)) == wanted;
        }));
  };

  // 刷新背包物品列表和重量到客户端
  auto queue_inventory_refresh = [&]() {
    player.refresh_derived_state(item_configs_);
    queue_packet(dispatch, player.session_id(),
                 make_bag_items_packet(player.session_id(), player, item_configs_));
    queue_packet(dispatch, player.session_id(),
                 make_weight_changed_packet(player.session_id(), player.character()));
  };

  // ── 动作执行系统 ──────────────────────────────────────────────
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

    /// @name 对话控制
    /// @{

    // SAY <text> — 发送 NPC 对话文本
    if (command_name == "SAY") {
      queue_packet(dispatch, player.session_id(),
                   make_merchant_say_packet(
                       player.session_id(), npc.id(), npc,
                       render_npc_dialog_text(npc, player, config_, castle_dialog_context_,
                                              payload, item_configs_, script_global_params)));
      trace("say", true, 0, payload);
      return std::nullopt;
    }
    // GOTO <@label> — 跳转到其他动作段（递归调用脚本）
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
    // CALL <@label> — 调用子过程（返回后可继续执行后续动作）
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
    // CLOSE — 关闭 NPC 对话框
    if (command_name == "CLOSE") {
      pending_say_lines.clear();
      suppress_pending_say = true;
      queue_packet(dispatch, player.session_id(),
                   make_merchant_dlg_close_packet(player.session_id()));
      trace("close", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }

    /// @}

    /// @name 消息系统
    /// @{

    // SENDMSG <channel> <text> — 发送系统消息到客户端
    // SYSMSG <channel> <text> — 同上
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

    /// @}

    /// @name 流程控制
    /// @{

    // BREAK — 中断当前脚本执行
    if (command_name == "BREAK") {
      trace("break", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }
    // ENDQUEST — 结束任务（标记后外部可检测）
    if (command_name == "ENDQUEST") {
      script_context.end_quest = true;
      trace("endquest", true, 0, action_line);
      stop_script = true;
      return std::nullopt;
    }

    /// @}

    /// @name 任务标志位（持久化）
    /// @{

    // SET <index> [value=1]    — 设置任务标记位
    // RESET <index>            — 重置任务标记位（设为 0）
    // SETOPEN <index> [value]  — 设置开启单元
    // SETUNIT <index> [value]  — 设置任务单元
    // RESETUNIT <index>         — 重置任务单元（设为 0）
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

    /// @}

    /// @name 参数变量（PARAM1-PARAM4）
    /// @{

    // PARAM1-PARAM4 <value|variable> — 设置局部参数值
    // 用于为后续命令（如 MONGEN）提供参数
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

    /// @}

    /// @name 变量操作
    /// @{

    // MOV <var> <value> — 变量赋值
    // INC <var> <amount> — 变量增加
    // DEC <var> <amount> — 变量减少
    // SUM <var> <amount> — 设置变量组累加和（索引 9）
    // MOVR <var> <range> — 随机赋值 [0, range)
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

    /// @}

    /// @name 名单管理
    /// @{

    // ADDNAMELIST <list> [name] — 添加角色名到名单
    // DELNAMELIST <list> [name] — 从名单删除角色名
    // ADDIDLIST <list> [id] — 添加账号 ID 到名单
    // DELIDLIST <list> [id] — 从名单删除账号 ID
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

    /// @}

    /// @name 日常任务
    /// @{

    // SETDAILYQUEST <id> — 设置日常任务 ID
    // RANDOMSETDAILYQUEST <id1> [id2...] — 随机选择并设置日常任务 ID
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

    /// @}

    /// @name 物品/金币操作
    /// @{

    // GIVE <物品名称|Gold> <数量> — 给予物品或金币
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

    // TAKE <物品名称|Gold> <数量> — 取走物品或金币
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

    // TAKECHECKITEM <物品名称> <数量> — 取走 CHECKITEM 检查到的特定物品
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

    // TAKEW <装备别名|物品名称> <数量> — 取走已装备的物品
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

    /// @}

    /// @name 地图传送
    /// @{

    // MAPMOVE <map_id> <x> <y> — 传送到指定地图的指定坐标
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

    // MAP [map_id] — 随机传送到地图（或当前地图的随机位置）
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

    // GOQUEST <target> — 跳转到另一个任务对话段
    if (command_name == "GOQUEST") {
      const auto target = tokens.empty() ? std::string{} : tokens[0];
      if (!target.empty()) {
        trace("goquest", true, 0, target);
        return target;
      }
      trace("goquest_reject", false, 0, action_line);
      return std::nullopt;
    }

    /// @}

    /// @name 骰子系统
    /// @{

    // PLAYDICE <count> [target_label] — 投掷骰子并跳转
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

    /// @}

    /// @name 怪物生成/清除
    /// @{

    // MONGEN <怪物名称> [数量=1] [散布范围=0]
    // 使用 PARAM1=map_id, PARAM2=base_x, PARAM3=base_y
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

    // MONCLEAR [map_id] — 清除指定地图上的所有怪物
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

    /// @}

    /// @name 时间召回系统
    /// @{

    // TIMERECALL <minutes> — 定时将玩家召回当前位置
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
    // BREAKTIMERECALL — 取消时间召回
    if (command_name == "BREAKTIMERECALL") {
      dispatch.legacy_time_recall_requests.push_back(
          LegacyTimeRecallRequest{LegacyTimeRecallRequestKind::cancel,
                                  player.session_id(), player.id(), config_.id,
                                  player.x(), player.y(), 0});
      trace("time_recall_cancel", true, 0, action_line);
      return std::nullopt;
    }

    /// @}

    /// @name 批量移动（行会战/活动）
    /// @{

    // EXCHANGEMAP <map_id> — 将玩家交换到另一地图
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
    // RECALLMAP <map_id> — 将其他地图的玩家召回
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
    // BATCHDELAY <seconds=10> — 设置批量移动的延迟秒数
    if (command_name == "BATCHDELAY") {
      const auto seconds = tokens.empty() ? 10 : std::max(int_token(0, 10), 0);
      batch_delay_ticks = legacy_seconds_to_ticks(seconds);
      trace("deferred_action", true, seconds, action_line);
      return std::nullopt;
    }
    // ADDBATCH <map_id> — 添加一个批量移动目标
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
    // BATCHMOVE [map_id] — 执行批量移动（将玩家随机传送到目标地图）
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

    /// @}

    trace("unsupported_action", false, 0, action_line);
    return std::nullopt;
  };

  // ── 主脚本执行循环 ────────────────────────────────────────────
  // 遍历所有过程（procs），每个 proc 包含条件、对话行和动作行
  for (std::size_t proc_index = 0; proc_index < block.procs.size(); ++proc_index) {
    const auto& proc = block.procs[proc_index];
    const auto condition_result = evaluate_conditions(proc.conditions);
    trace("condition_result", condition_result, static_cast<std::int32_t>(proc_index), action);

    // 根据条件结果选择对话行（say_lines 或 else_say_lines）
    const auto& selected_say_lines =
        condition_result ? proc.say_lines : proc.else_say_lines;
    pending_say_lines.insert(pending_say_lines.end(), selected_say_lines.begin(),
                             selected_say_lines.end());

    // 根据条件结果选择动作行（act_lines 或 else_act_lines）
    const auto& actions = condition_result ? proc.act_lines : proc.else_act_lines;
    for (const auto& action_line : actions) {
      const auto maybe_goto = execute_action(action_line);
      if (stop_script || player_transferred) {
        break;
      }
      // 如果动作返回了跳转目标，递归执行
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

  // ── 脚本状态持久化 ────────────────────────────────────────────
  if (script_state_mutated && !player_transferred) {
    queue_save_character(dispatch, player);
  }

  return true;
}

/**
 * @brief 触发启动任务（玩家登录时执行）
 * @param player 登录的玩家
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return 是否成功执行了启动任务
 * @details 在玩家首次登录地图时执行，使用 startup_quest_dialog_sections_ 配置
 *          创建一个临时的 NPC 对象并执行 @main 脚本段。
 *          用于发放首次登录奖励、展示公告等场景。
 */
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

/**
 * @brief 触发地图任务（怪物击杀或物品触发）
 * @param player 触发任务的玩家
 * @param monster_name 击杀的怪物名称
 * @param item_name 触发的物品名称
 * @param group_call 是否为组队触发
 * @param source 触发来源描述
 * @param dispatch 运行时调度输出
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return 是否至少触发了一个任务
 * @details 遍历 map_quests_ 配置列表，匹配满足以下条件的任务：
 *          1. 地图 ID 匹配当前地图
 *          2. 玩家任务标志位匹配任务条件
 *          3. 组队标志匹配
 *          4. 怪物名称或物品名称匹配
 *
 *          匹配成功后创建临时 NPC 对象并执行脚本。
 *          如果脚本执行了 ENDQUEST 则停止后续任务触发。
 *
 * @note 允许怪物名称和物品名称同时为空（全匹配），但不触发以此方式匹配的任务
 */
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

    // 匹配规则：怪物名和物品名的组合匹配
    const auto quest_monster = util::lower_copy(util::trim(quest.monster_name));
    const auto quest_item = util::lower_copy(util::trim(quest.item_name));
    bool matches = false;
    if (quest_monster.empty() && quest_item.empty()) {
      matches = false;  // 两者都为空不触发
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

    // 创建临时 NPC 执行任务脚本
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
