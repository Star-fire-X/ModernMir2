/**
 * @file map_actor_gm.hpp
 * @brief GM 命令处理实现 - MapActor 的 GM 命令执行逻辑
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含 GM 命令的完整处理逻辑。
 *          实现以下功能：
 *          - 匿名命名空间辅助函数：命令名称匹配、参数解析、物品查找等
 *          - legacy_apply_gm_command：GM 命令主分发器
 *
 *          支持的 GM 命令包括：
 *          - Level/AdjustLevel/AdjustTestLevel：等级调整
 *          - AdjustExp：经验值调整
 *          - FreePenalty/PKpoint/IncPkPoint：PK 值管理
 *          - ChangeLuck/LuckyPoint：幸运值管理
 *          - hair/NameColor：外观修改
 *          - ChangeJob/ChangeGender：职业/性别变更
 *          - Transparency：隐身模式
 *          - flag/showopen/showunit：标志位查询
 *          - setflag/setopen/setunit：标志位设置
 *          - Training/OPTraining：技能等级修改
 *          - DeleteSkill/OPDeleteSkill：技能删除
 *          - Make：物品生成
 *          - DeleteItem：物品删除
 *          - AddGold/DelGold/Test_GOLD_Change：金币操作
 *          - WeaponRefinery：武器属性修改
 *          - ChangeWeaponDura：武器耐久修改
 *
 * @see map_actor.hpp
 * @see map_actor_mail.hpp
 */

#pragma once

namespace {

/**
 * @brief GM 命令名称大小写不敏感比较
 * @param lhs 命令名称左值
 * @param rhs 命令名称右值
 * @return true 如果两个名称忽略大小写后相等
 * @details 将两个字符串都转换为小写后进行比较，实现对 GM 命令名称的
 *          大小写不敏感匹配。注意：此函数在每次调用时都会创建两个 std::string。
 */
bool gm_command_equals(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(std::string(lhs)) == util::lower_copy(std::string(rhs));
}

/**
 * @brief 从 GM 命令参数列表中解析整数
 * @param args 参数列表
 * @param index 要解析的参数索引
 * @param fallback 解析失败时的默认值
 * @return 解析后的整数值，如果索引越界或解析失败则返回 fallback
 * @details 使用 parse_int32 进行解析，解析前会去除参数前后的空白字符。
 */
std::int32_t gm_parse_int(const std::vector<std::string>& args, std::size_t index,
                          std::int32_t fallback = 0) {
  if (index >= args.size()) {
    return fallback;
  }
  return parse_int32(util::trim(args[index])).value_or(fallback);
}

/**
 * @brief 从 GM 命令参数列表中解析双精度浮点数
 * @param args 参数列表
 * @param index 要解析的参数索引
 * @param fallback 解析失败时的默认值（默认 0.0）
 * @return 解析后的双精度浮点值，如果索引越界或解析失败则返回 fallback
 * @note 使用 std::stod 进行转换，捕获所有异常类型以确保稳健性
 */
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

/**
 * @brief 将 GM 命令参数列表中的一段连接为字符串
 * @param args 参数列表
 * @param begin 起始索引（包含）
 * @param end 结束索引（不包含）
 * @return 连接后的字符串，各参数间用空格分隔
 * @details 用于将多个单词组成的物品名称重新连接成一个完整的字符串。
 *          例如参数 ["木", "剑"] 会连接成 "木 剑"。
 */
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

/**
 * @brief 根据名称或 ID 查找魔法配置
 * @param magic_configs 魔法配置表
 * @param value 魔法名称或 ID 字符串
 * @return 魔法配置指针，未找到时返回 nullptr
 * @details 首先尝试将 value 解析为整数 ID，如果成功则在配置表中按 ID 查找。
 *          如果不是有效 ID 或未找到，则按名称（忽略大小写）遍历查找。
 */
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

/**
 * @brief 根据参数列表查找物品配置及数量
 * @param item_configs 物品配置表
 * @param args GM 命令参数列表
 * @param count [输出] 物品数量
 * @return 物品配置指针，未找到时返回 nullptr
 * @details 从参数末尾开始往前匹配物品名称，以支持多词物品名称。
 *          第一个匹配的物品名称部分之前的所有参数被视为数量值。
 *          例如 args=["5", "木", "剑"] 会匹配到"木 剑"数量为 5。
 */
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

/**
 * @brief 获取物品最大耐久度（适配 uint16_t 范围）
 * @param item_config 物品配置
 * @return 裁剪到 [0, 65535] 范围内的最大耐久度值
 * @details 如果配置的 dura_max 为 0（表示不可磨损物品），则使用 1000 作为默认值。
 */
std::uint16_t gm_item_dura_max(const ItemConfig& item_config) {
  const auto dura_max = item_config.dura_max > 0 ? item_config.dura_max : 1000;
  return static_cast<std::uint16_t>(std::clamp(dura_max, 0, 65535));
}

/**
 * @brief 创建 GM 生成的物品实例
 * @param item_config 物品配置
 * @param make_index 制造索引（用于物品唯一标识）
 * @return 创建的 LegacyUserItem 实例
 * @details 使用物品配置创建新的物品实例，分配制造索引，
 *          设置标准耐久度（最大耐久等于当前耐久）。
 */
LegacyUserItem gm_make_item(const ItemConfig& item_config, std::int32_t make_index) {
  LegacyUserItem item;
  item.make_index = make_index;
  item.index = static_cast<std::uint16_t>(std::clamp(item_config.id, 0, 65535));
  item.dura_max = gm_item_dura_max(item_config);
  item.dura = item.dura_max;
  return item;
}

/**
 * @brief 刷新玩家能力值并发送数据包
 * @param dispatch 运行时调度输出
 * @param player 目标玩家
 * @param item_configs 物品配置表
 * @details 重新计算玩家派生状态（装备效果、Buff 等），
 *          发送能力值、子能力值和生命值变化三个数据包给客户端。
 */
void gm_refresh_ability(RuntimeDispatch& dispatch, Player& player,
                        const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  player.refresh_derived_state(item_configs);
  queue_packet(dispatch, player.session_id(),
               make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(), make_sub_ability_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
}

/**
 * @brief 刷新玩家负重并发送数据包
 * @param dispatch 运行时调度输出
 * @param player 目标玩家
 * @param item_configs 物品配置表
 * @details 重新计算玩家重量状态，发送负重变更数据包给客户端。
 *          通常用于物品增删后的重量同步。
 */
void gm_refresh_weight(RuntimeDispatch& dispatch, Player& player,
                       const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  player.refresh_derived_state(item_configs);
  queue_packet(dispatch, player.session_id(),
               make_weight_changed_packet(player.session_id(), player.character()));
}

/**
 * @brief 广播玩家外观特征变更给所有观察者
 * @param objects 地图上所有游戏对象映射表
 * @param dispatch 运行时调度输出
 * @param player 变更外观的玩家
 * @details 遍历所有玩家，向能够看到目标玩家的观察者发送 feature_changed 数据包。
 *          用于发型变更、性别变更等外观变化场景。
 */
void gm_broadcast_feature(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const Player& player) {
  queue_actor_origin_packet(objects, dispatch, player, true, [&](const Player& watcher) {
    queue_packet(dispatch, watcher.session_id(),
                 make_feature_changed_packet(watcher.session_id(), player.id(),
                                             actor_feature(player)));
  });
}

/**
 * @brief 广播玩家名称颜色变更给所有观察者
 * @param objects 地图上所有游戏对象映射表
 * @param dispatch 运行时调度输出
 * @param player 变更名称颜色的玩家
 * @details 遍历所有玩家，向能够看到目标玩家的观察者发送包含新名称颜色的
 *          username 数据包。用于 PK 值变化、GM 状态等场景。
 */
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

/**
 * @brief 生成 GM 标志位查询的响应消息文本
 * @param player 目标玩家
 * @param label 标志类型标签（"flag"/"open"/"unit"）
 * @param index 标志位索引
 * @param value 标志位当前值（0/1）
 * @return 格式化的消息字符串
 * @details 生成如 "角色名: flag[5]=ON" 格式的查询结果文本。
 */
std::string gm_mark_text(const Player& player, std::string_view label,
                         std::int32_t index, std::uint8_t value) {
  return player.character().character_name + ": " + std::string(label) + "[" +
         std::to_string(index) + "]=" + (value != 0 ? "ON" : "OFF");
}

}  // namespace

/**
 * @brief 应用 GM 命令
 * @param actor_id 执行 GM 命令的玩家角色 ID
 * @param command_name 命令名称（不区分大小写）
 * @param args 命令参数列表
 * @param current_tick 当前逻辑 tick
 * @param now_ms 当前系统时间（毫秒）
 * @return GM 命令执行结果，包含处理状态、成功标志和返回消息
 * @details 这是 GM 命令的主分发函数，按命令名称分派到对应的处理逻辑。
 *          每个命令处理分支通常包含：
 *          1. 参数验证（数量、类型合法性）
 *          2. 执行操作（修改玩家状态、生成/删除物品等）
 *          3. 客户端同步（发送对应的网络数据包）
 *          4. 持久化保存（如有必要）
 *
 *          支持 Level/AdjustExp/FreePenalty/Make/DeleteSkill 等 20+ 种 GM 命令。
 *          所有命令名称匹配忽略大小写。
 *
 * @note 未识别的命令设置 handled=false 供上层继续处理
 */
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

  // 本地辅助 lambda：保存角色、标记成功、标记失败
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

  // ── 等级调整 ──────────────────────────────────────────────────
  // Level <level>       : 直接设置等级（上限 40）
  // Level0 <level>      : Level 的别名
  // AdjustLevel <level> : 设置等级（参数索引 1，上限 40）
  // AdjustTestLevel <level> : 设置等级（参数索引 0，上限 50）
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

  // ── 经验值调整 ─────────────────────────────────────────────────
  // AdjustExp <exp> : 直接设置经验值
  if (gm_command_equals(command_name, "AdjustExp")) {
    player->set_legacy_exp(gm_parse_int(args, 1, player->character().ability.exp));
    queue_packet(result.dispatch, player->session_id(),
                 make_ability_packet(player->session_id(), player->character()));
    save();
    return ok();
  }

  // ── 清除红名/PK 值 ──────────────────────────────────────────────
  // FreePenalty : PK 值归零，广播名称颜色更新
  if (gm_command_equals(command_name, "FreePenalty")) {
    player->set_pk_point(0);
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    save();
    result.messages.push_back(player->character().character_name + " : PK point = 0.");
    return ok();
  }

  // ── 查看 PK 值 ──────────────────────────────────────────────────
  // PKpoint : 显示当前 PK 值
  if (gm_command_equals(command_name, "PKpoint")) {
    result.messages.push_back(player->character().character_name + " PK point = " +
                              std::to_string(player->pk_point()));
    return ok();
  }

  // ── 增加 PK 值 ──────────────────────────────────────────────────
  // IncPkPoint : PK 值增加 100
  if (gm_command_equals(command_name, "IncPkPoint")) {
    player->inc_pk_point(100);
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  // ── 查看幸运值 ──────────────────────────────────────────────────
  // LuckyPoint : 显示 BodyLuck 等级/值和 Luck 值
  if (gm_command_equals(command_name, "LuckyPoint")) {
    result.messages.push_back(player->character().character_name + ": BodyLuck= " +
                              std::to_string(player->body_luck_level()) + "/" +
                              std::to_string(static_cast<std::int32_t>(
                                  std::lround(player->character().body_luck))) +
                              " Luck = " + std::to_string(player->legacy_luck()));
    return ok();
  }

  // ── 修改幸运值 ──────────────────────────────────────────────────
  // ChangeLuck <value> : 设置 BodyLuck 值
  if (gm_command_equals(command_name, "ChangeLuck")) {
    player->set_body_luck_value(gm_parse_double(args, 0, player->character().body_luck));
    save();
    return ok();
  }

  // ── 修改发型 ────────────────────────────────────────────────────
  // hair <index> : 设置发型索引，刷新外观并广播
  if (gm_command_equals(command_name, "hair")) {
    player->set_hair(gm_parse_int(args, 0, player->character().hair));
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    gm_broadcast_feature(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  // ── 修改名称颜色 ────────────────────────────────────────────────
  // NameColor <color> : 设置名称颜色（0-255），广播更新
  if (gm_command_equals(command_name, "NameColor")) {
    player->set_legacy_name_color(gm_parse_int(args, 0, 255));
    gm_broadcast_name_color(objects_, result.dispatch, *player);
    return ok();
  }

  // ── 变更职业 ────────────────────────────────────────────────────
  // ChangeJob <job> : 0=Warrior, 1=Wizard, 2=Taoist
  //                   支持名称 "warrior"/"wizard"/"taoist"
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

  // ── 变更性别 ────────────────────────────────────────────────────
  // ChangeGender : 切换性别，刷新外观和属性并广播
  if (gm_command_equals(command_name, "ChangeGender")) {
    player->toggle_sex();
    gm_refresh_ability(result.dispatch, *player, item_configs_);
    gm_broadcast_feature(objects_, result.dispatch, *player);
    save();
    return ok();
  }

  // ── 隐身模式 ────────────────────────────────────────────────────
  // Transparency : 切换隐身状态（1 小时持续时间），广播状态变化
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

  // ── 标志位查询 ──────────────────────────────────────────────────
  // flag <index> <value>    : 查询任务标记位
  // showopen <index> <value> : 查询任务开启单元
  // showunit <index> <value> : 查询任务单元
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

  // ── 标志位设置 ──────────────────────────────────────────────────
  // setflag <index> <value>  : 设置任务标记位
  // setopen <index> <value>  : 设置任务开启单元
  // setunit <index> <value>  : 设置任务单元
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

  // ── 技能等级修改 ────────────────────────────────────────────────
  // Training <魔法名称> <等级>       : 设置已学魔法等级
  // OPTraining <魔法名称> <等级>     : 同上（参数索引偏移不同）
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

  // ── 删除技能 ────────────────────────────────────────────────────
  // DeleteSkill <魔法名称>     : 删除已学魔法（参数索引 0）
  // OPDeleteSkill <魔法名称>   : 删除已学魔法（参数索引 1）
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

  // ── 生成物品 ────────────────────────────────────────────────────
  // Make <物品名称> [数量] : 生成 1-50 个物品放入背包
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

  // ── 删除物品 ────────────────────────────────────────────────────
  // DeleteItem <物品名称> [数量] : 从背包删除 1-50 个指定物品
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

  // ── 金币操作 ────────────────────────────────────────────────────
  // AddGold <amount>         : 增加金币
  // DelGold <amount>         : 减少金币（不超过当前持有量）
  // Test_GOLD_Change <amount> : 将金币设为指定值（补差或扣除）
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

  // ── 武器极品属性修改 ────────────────────────────────────────────
  // WeaponRefinery <dc> <mc> <sc> <accuracy> : 修改武器极品属性
  //    desc[0]=DC, desc[1]=MC, desc[2]=SC, desc[5]=Accuracy
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

  // ── 武器耐久修改 ────────────────────────────────────────────────
  // ChangeWeaponDura <dura_scale> : 修改武器耐久度（0-65 缩放为 0-65000）
  //    实际耐久 = dura * 1000
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

  // ── 未识别的命令 ──────────────────────────────────────────────
  result.handled = false;
  return result;
}
