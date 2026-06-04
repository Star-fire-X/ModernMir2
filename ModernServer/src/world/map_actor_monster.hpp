#pragma once

/**
 * @file map_actor_monster.hpp
 * @brief MapActor 类的怪物、宠物、奖励和 AI 成员函数实现细节
 *
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含在 mir2 命名空间内。
 *          涵盖以下功能模块：
 *          - 怪物 AI 行为决策（预运行搜索、各种族特殊行为）
 *          - 怪物攻击和目标选择逻辑
 *          - 宠物/奴隶管理与生命周期
 *          - 怪物死亡、尸体、鬼魂处理
 *          - 掉落物/散落逻辑
 *          - 重生管理
 *          - 经验和奖励分配
 *
 *          该文件中的所有函数都位于匿名命名空间中（工具函数）或作为 MapActor
 *          的成员函数。代码遵循"遗留兼容优先"原则，精确复刻了 Delphi 版
 *          传奇服务端的行为。
 */

// Implementation detail for map_actor.cpp: monster, slave, reward, and AI members.
namespace {

/**
 * @brief 判断怪物是否需要执行预运行搜索
 *
 * @details 预运行搜索是指怪物在 Think 周期之前主动搜寻敌人的行为。
 *          某些种族（狼、半兽人、骷髅等）天生具有主动搜索能力，
 *          而普通怪物（kRcMonster）不进行预搜索。如果怪物具有特殊行为
 *          （如钻地、飞行等），也不进行预搜索。
 *
 * @param monster 要检查的怪物对象
 * @return true  该怪物需要在 AI 决策前进行主动搜索
 * @return false 该怪物不进行预运行搜索
 */
bool legacy_monster_has_pre_run_search(const Monster& monster) {
  if (legacy_monster_has_special_behavior(monster.race_server())) {
    return false;
  }
  switch (monster.race_server()) {
    case kRcWolf:
    case kRcOma:
    case kRcSlowMonster:
    case kRcSkeleton:
    case kRcHeavyAxeSkeleton:
    case kRcKnightSkeleton:
    case kRcNoblePigKing:
      return true;
    case kRcMonster:
      return false;
    default:
      break;
  }
  return monster.race_server() == 0 &&
         (monster.ai_profile() == MonsterAiProfile::aggressive ||
          monster.ai_profile() == MonsterAiProfile::ranged ||
          monster.ai_profile() == MonsterAiProfile::stationary);
}

/**
 * @brief 普通怪物的视野范围（切比雪夫距离）
 *
 * @details 普通怪物在视线范围内的 5x5 区域内搜寻目标。
 *          这是传奇经典设定的标准视野范围。
 */
constexpr std::int32_t kLegacyOrdinaryMonsterViewRange = 5;

/**
 * @brief 计算宠物应该站在主人身后的位置
 *
 * @details 宠物跟随主人时，应站在主人朝向的反方向一格位置。
 *          当主人朝某个方向移动时，宠物通常跟随在身后。
 *          方向计算为主人当前方向 + 4（即反方向），然后取余 8。
 *
 * @param master 宠物主人（玩家对象）
 * @return std::pair<std::int32_t, std::int32_t> 宠物应该站立的坐标 (x, y)
 */
std::pair<std::int32_t, std::int32_t> legacy_slave_back_position(const Player& master) {
  const auto back_dir =
      static_cast<std::uint8_t>((master.character().dir + 4) % 8);
  const auto [dx, dy] = direction_delta(back_dir);
  return {master.x() + dx, master.y() + dy};
}

}  // namespace

/**
 * @brief 处理怪物的状态效果（中毒、灼烧等持续伤害）
 *
 * @details 每个 Tick 检查怪物身上的持续状态效果。如果状态效果造成伤害，
 *          则将伤害来源记录为最后一次攻击者。如果怪物因此死亡，
 *          则执行死亡流程；否则向周围玩家广播受击消息。
 *
 * @param monster    正在处理状态效果的怪物
 * @param dispatch   运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms     当前时间（毫秒）
 * @return true  怪物因状态效果死亡，已执行死亡处理
 * @return false 怪物未死亡，或状态效果未造成伤害
 */
bool MapActor::handle_monster_status_effects(Monster& monster, RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  const auto tick_result = monster.tick_status_effects(current_tick);
  if (tick_result.damage <= 0) {
    return false;
  }

  const auto source_actor_id =
      tick_result.source_actor_id != 0 ? tick_result.source_actor_id : monster.last_hitter_id();
  const auto target_died = monster.is_dead();
  if (target_died && monster.death_time_ms() == 0) {
    static_cast<void>(monster.mark_legacy_death(now_ms));
    refresh_moving_object_state(monster, now_ms);
  }
  if (!target_died) {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!is_legacy_visible_to(watcher, monster)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_struck_packet(watcher.session_id(), monster, source_actor_id,
                                      tick_result.damage, true));
    });
    return false;
  }

  auto pending_death_packets = collect_legacy_death_packets(objects_, monster);
  finalize_monster_death(monster.id(), source_actor_id, dispatch, current_tick);
  queue_legacy_packets(dispatch, std::move(pending_death_packets));
  return true;
}

/**
 * @brief 向击杀怪物的玩家发放经验奖励
 *
 * @details 计算玩家应得的经验值，发送经验获得包。如果玩家因此升级，
 *          则发送升级包并恢复满状态（HP/MP）。经验值计算公式为：
 *          若怪物配置了经验奖励则使用配置值，否则使用 level * 10 + max_hp
 *          作为基础战斗经验值，再通过 calc_get_exp 根据等级差调整。
 *
 * @param attacker  击杀怪物的玩家
 * @param monster   被击杀的怪物（用于获取经验值和等级信息）
 * @param dispatch  运行时消息分发器
 *
 * @see calc_get_exp
 * @see make_win_exp_packet
 * @see make_level_up_packet
 */
void MapActor::award_monster_kill(Player& attacker, const Monster& monster, RuntimeDispatch& dispatch) {
  const auto fight_exp = monster.exp_reward() > 0 ? monster.exp_reward()
                                                  : monster.level() * 10 + monster.max_hp();
  const auto exp_reward = calc_get_exp(attacker.character().ability.level, monster.level(),
                                       fight_exp);
  const auto exp_result = attacker.gain_experience(exp_reward);
  queue_packet(dispatch, attacker.session_id(),
               make_win_exp_packet(attacker.session_id(), exp_result.display_exp,
                                   exp_result.gained));
  if (exp_result.leveled_up) {
    attacker.refresh_derived_state(item_configs_);
    attacker.restore_full_vitals();
    queue_packet(dispatch, attacker.session_id(),
                 make_level_up_packet(attacker.session_id(), attacker));
    queue_packet(dispatch, attacker.session_id(),
                 make_ability_packet(attacker.session_id(), attacker.character()));
    queue_packet(dispatch, attacker.session_id(),
                 make_sub_ability_packet(attacker.session_id(), attacker));
    queue_packet(dispatch, attacker.session_id(),
                 make_health_spell_changed_packet(attacker.session_id(), attacker));
  }
}

/**
 * @brief 安排怪物重生
 *
 * @details 查询该怪物的出生模板（spawn template），如果存在则根据配置的
 *          重生时间（毫秒）计算需要延迟的 Tick 数，然后通过延迟邮件轮
 *          （delayed_mail_wheel_）安排重生事件。
 *
 * @param monster_id    需要重生的怪物 ID
 * @param current_tick  当前逻辑 Tick 数
 *
 * @see delayed_mail_wheel_
 * @see ActorMailKind::spawn_monster
 */
void MapActor::schedule_monster_respawn(std::uint64_t monster_id, std::uint64_t current_tick) {
  if (const auto spawn_it = monster_spawn_templates_.find(monster_id);
      spawn_it != monster_spawn_templates_.end()) {
    const auto respawn_ticks = std::max<std::uint64_t>(
        1, (static_cast<std::uint64_t>(spawn_it->second.mail.respawn_ms) +
            static_cast<std::uint64_t>(budgets_.tick_ms) - 1) /
               static_cast<std::uint64_t>(budgets_.tick_ms));
    delayed_mail_wheel_.schedule(current_tick, respawn_ticks, spawn_it->second.mail);
  }
}

/**
 * @brief 刷新地图上移动对象的状态
 *
 * @details 先从地图上删除该对象的旧状态，然后使用当前状态重新添加。
 *          这用于更新对象的死亡、隐藏、幽灵等状态变更，使周围玩家能
 *          看到最新的对象外观。
 *
 * @param object  要刷新状态的地图对象
 * @param now_ms  当前时间（毫秒）
 *
 * @see environment_.delete_from_map
 * @see environment_.add_moving_object
 * @see moving_state_for
 */
void MapActor::refresh_moving_object_state(const GameObject& object, std::uint64_t now_ms) {
  static_cast<void>(environment_.delete_from_map(object.x(), object.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 object.id()));
  static_cast<void>(environment_.add_moving_object(object.x(), object.y(), object.id(),
                                                   now_ms, moving_state_for(object)));
}

/**
 * @brief 安排玩家技能升级经验的延迟通知
 *
 * @details 当玩家因击杀怪物获得技能经验时，延迟发送技能经验变化包。
 *          升级和未升级的延迟时间不同（升级 800ms，未升级 1000ms）。
 *          这是为了与传奇客户端的技能经验动画播放时序保持一致。
 *
 * @param player       获得技能经验的玩家
 * @param training     技能训练结果（是否升级、技能ID等）
 * @param dispatch     运行时消息分发器
 * @param source_mail  触发该行为的源邮件
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 */
void MapActor::schedule_legacy_magic_lvexp(Player& player,
                                           const LegacyMagicTrainResult& training,
                                           RuntimeDispatch& dispatch,
                                           const ActorMail& source_mail,
                                           std::uint64_t current_tick,
                                           std::uint64_t now_ms) {
  if (!training.trained || training.magic_id <= 0) {
    return;
  }

  ActorMail lvexp_mail;
  lvexp_mail.kind = ActorMailKind::legacy_magic_lvexp;
  lvexp_mail.map_id = config_.id;
  lvexp_mail.actor_id = player.id();
  lvexp_mail.session_id = player.session_id();
  lvexp_mail.magic_id = training.magic_id;
  lvexp_mail.magic_level = training.level;
  lvexp_mail.magic_train = training.cur_train;

  const auto delay_ms = training.leveled_up ? 800u : 1000u;
  if (training.leveled_up) {
    lvexp_mail.magic_lvexp_generation =
        player.advance_legacy_magic_lvexp_generation(training.magic_id);
  } else {
    lvexp_mail.magic_lvexp_generation =
        player.legacy_magic_lvexp_generation(training.magic_id);
  }
  delayed_mail_wheel_.schedule(current_tick, legacy_delay_ms_to_ticks(delay_ms, budgets_.tick_ms),
                               lvexp_mail);
  add_legacy_trace(dispatch, "LegacySkill", "magic_lvexp_queued", source_mail, current_tick,
                   now_ms, true, training.magic_id, training.cur_train,
                   training.leveled_up ? "800ms" : "1000ms");
}

// ---------------------------------------------------------------------------
// 宠物生成与管理（Slave Spawn & Management）
// ---------------------------------------------------------------------------

/**
 * @brief 构建宠物出生邮件（ActorMail）
 *
 * @details 根据怪物名称查询配置，在主人附近的可行走位置生成一只宠物。
 *          如果初始位置不可行走，则在半径 1~3 范围内搜索可用位置。
 *          邮件的各项属性从怪物模板中复制，部分属性（如攻击力、防御等）
 *          会取最大值以兼容不同类型的数据格式。宠物的 AI 固定为 aggressive，
 *          不会掉落物品（monster_no_item = true）。
 *
 * @param monster_name    怪物模板名称
 * @param master          宠物主人（玩家）
 * @param x               期望生成的 x 坐标
 * @param y               期望生成的 y 坐标
 * @param make_level      宠物制作等级（影响属性）
 * @param exp_level       宠物经验等级（0-6）
 * @param slave_exp       宠物当前经验
 * @param royalty_time_ms 忠诚到期时间戳（毫秒，0 表示永久）
 * @param life_time_ms    宠物生命时间（毫秒，0 表示无限制）
 * @param now_ms          当前时间戳（毫秒）
 * @param restored        如果是从存档恢复的宠物，则传入存档记录
 * @return std::optional<ActorMail> 构造成功返回邮件，构造失败（模板不存在或无可放置位置）返回空
 */
std::optional<ActorMail> MapActor::build_slave_spawn_mail(
    const std::string& monster_name, Player& master, std::int32_t x, std::int32_t y,
    std::int32_t make_level, std::int32_t exp_level, std::int32_t slave_exp,
    std::uint64_t royalty_time_ms, std::uint64_t life_time_ms,
    std::uint64_t now_ms, std::optional<CharacterSlaveRecord> restored) {
  const auto key = util::lower_copy(util::trim(monster_name));
  const auto def_it = monster_defs_.find(key);
  if (def_it == monster_defs_.end()) {
    return std::nullopt;
  }
  const auto& def = def_it->second;
  if (!environment_.can_walk(x, y, true)) {
    bool placed = false;
    for (std::int32_t radius = 1; radius <= 3 && !placed; ++radius) {
      for (std::int32_t dy = -radius; dy <= radius && !placed; ++dy) {
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
          const auto try_x = master.x() + dx;
          const auto try_y = master.y() + dy;
          if (environment_.can_walk(try_x, try_y, true)) {
            x = try_x;
            y = try_y;
            placed = true;
            break;
          }
        }
      }
    }
    if (!placed) {
      return std::nullopt;
    }
  }

  ActorMail spawn;
  spawn.kind = ActorMailKind::spawn_monster;
  spawn.map_id = config_.id;
  spawn.actor_id = next_script_monster_id_++;
  spawn.name = def.name;
  spawn.x = x;
  spawn.y = y;
  spawn.level = std::max(def.level, 1);
  spawn.max_hp = std::max(def.hp, 1);
  spawn.max_mp = std::max(def.mp, 0);
  spawn.current_hp = restored.has_value() && restored->hp > 0 ? restored->hp : spawn.max_hp;
  spawn.current_mp = restored.has_value() && restored->mp >= 0 ? restored->mp : spawn.max_mp;
  spawn.attack_power = std::max(def.dc_max > 0 ? def.dc_max : def.dc, 1);
  spawn.dc_min = std::max(def.dc, 0);
  spawn.dc_max = std::max({def.dc_max, def.dc, spawn.attack_power, 1});
  spawn.defense = std::max(def.ac, 0);
  spawn.magic_defense = std::max(def.mac, 0);
  spawn.mc = std::max(def.mc, 0);
  spawn.sc = std::max(def.sc, 0);
  spawn.exp_reward = std::max(def.exp, 1);
  spawn.life_attrib = def.undead ? 1 : 0;
  spawn.race_server = def.race_server;
  spawn.race_image = def.race_image;
  spawn.appearance = def.appearance;
  spawn.cool_eye = def.cool_eye;
  spawn.speed = def.agility;
  spawn.accuracy = def.accurate;
  spawn.walk_speed_ms = std::max(def.walk_speed_ms, 1);
  spawn.walk_step = std::max(def.walk_step, 1);
  spawn.walk_wait_ms = std::max(def.walk_wait_ms, 0);
  spawn.attack_speed_ms = std::max(def.attack_speed_ms, 1);
  spawn.home_x = master.x();
  spawn.home_y = master.y();
  spawn.home_area = 8;
  spawn.legacy_spawn_group = true;
  spawn.monster_ai_profile = MonsterAiProfile::aggressive;
  spawn.master_actor_id = master.id();
  spawn.monster_is_slave = true;
  spawn.monster_no_item = true;
  spawn.slave_exp = std::max(slave_exp, 0);
  spawn.slave_make_level = std::max(make_level, 0);
  spawn.slave_exp_level = std::clamp(exp_level, 0, 6);
  spawn.master_royalty_time_ms = royalty_time_ms;
  spawn.slave_life_time_ms = life_time_ms;
  static_cast<void>(now_ms);
  return spawn;
}

/**
 * @brief 快照当前所有存活宠物的状态
 *
 * @details 遍历玩家的宠物 ID 列表，收集所有存活（未死亡、未鬼魂化）
 *          的宠物状态。同时清理玩家宠物列表中的无效记录。
 *          返回的数组大小固定为 kMaxLegacySlaves。
 *
 * @param player  要快照的玩家
 * @param now_ms  当前时间（毫秒）
 * @return std::array<CharacterSlaveRecord, kMaxLegacySlaves> 宠物状态数组
 *
 * @see CharacterSlaveRecord
 * @see kMaxLegacySlaves
 */
std::array<CharacterSlaveRecord, kMaxLegacySlaves> MapActor::snapshot_owned_slaves(
    Player& player, std::uint64_t now_ms) {
  std::array<CharacterSlaveRecord, kMaxLegacySlaves> records{};
  std::unordered_set<std::uint64_t> live_slaves;
  std::size_t slot = 0;
  for (const auto slave_id : player.slave_actor_ids()) {
    if (slot >= records.size()) {
      break;
    }
    const auto it = objects_.find(slave_id);
    auto* slave = it != objects_.end() ? as_monster(it->second.get()) : nullptr;
    if (slave == nullptr || slave->is_dead() || slave->legacy_ghosted() ||
        slave->master_actor_id() != player.id()) {
      continue;
    }
    live_slaves.insert(slave_id);
    auto& record = records[slot++];
    record.name = slave->name();
    record.slave_exp = slave->slave_exp();
    record.slave_exp_level =
        static_cast<std::uint8_t>(std::clamp(slave->slave_exp_level(), 0, 255));
    record.slave_make_level =
        static_cast<std::uint8_t>(std::clamp(slave->slave_make_level(), 0, 255));
    if (slave->master_royalty_time_ms() > now_ms) {
      record.remain_royalty_sec = static_cast<std::int32_t>(
          std::min<std::uint64_t>((slave->master_royalty_time_ms() - now_ms) / 1000ULL,
                                  static_cast<std::uint64_t>(
                                      std::numeric_limits<std::int32_t>::max())));
    }
    record.hp = slave->hp();
    record.mp = slave->mp();
  }
  player.prune_slave_actor_ids(live_slaves);
  return records;
}

/**
 * @brief 创建带宠物状态的玩家存档记录
 *
 * @details 先生成玩家的基础存档快照，再附加宠物的状态快照，
 *          最终生成完整的玩家存档数据。
 *
 * @param player  要存档的玩家
 * @param now_ms  当前时间（毫秒）
 * @return CharacterRecord 包含宠物状态的完整玩家存档
 */
CharacterRecord MapActor::snapshot_player_with_slaves(Player& player, std::uint64_t now_ms) {
  auto snapshot = player.persistent_snapshot();
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);
  return snapshot;
}

/**
 * @brief 触发玩家数据延迟保存，包含宠物状态
 *
 * @details 将玩家状态（包括宠物状态）通过存档分发器排队，等待异步写入数据库。
 *
 * @param dispatch  运行时消息分发器
 * @param player    要存档的玩家
 * @param now_ms    当前时间（毫秒）
 */
void MapActor::queue_save_player_character(RuntimeDispatch& dispatch, Player& player,
                                           std::uint64_t now_ms) {
  queue_save_character(dispatch, snapshot_player_with_slaves(player, now_ms));
}

/**
 * @brief 召唤一只宠物（通过技能如召唤骷髅、召唤神兽等）
 *
 * @details 先快照当前宠物列表以清理无效记录，然后检查宠物数量是否已达上限。
 *          在玩家面对方向前方一格生成宠物，如果生成成功则补满一半的 HP 差额
 *         （这是传奇的经典设定，让新召唤的宠物拥有更高 HP）。
 *
 * @param master         宠物主人
 * @param monster_name   要召唤的怪物模板名
 * @param make_level     制作等级
 * @param max_slaves     最大宠物数量上限
 * @param royalty_seconds 忠诚时间（秒）
 * @param dispatch       运行时消息分发器
 * @param current_tick   当前逻辑 Tick 数
 * @param now_ms         当前时间（毫秒）
 * @param source_mail    触发该行为的源邮件
 * @return true  召唤成功
 * @return false 召唤失败（数量已达上限、模板不存在或生成失败）
 */
bool MapActor::summon_player_slave(Player& master, const std::string& monster_name,
                                   std::int32_t make_level, std::int32_t max_slaves,
                                   std::uint64_t royalty_seconds,
                                   RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick, std::uint64_t now_ms,
                                   const ActorMail& source_mail) {
  static_cast<void>(snapshot_owned_slaves(master, now_ms));
  if (static_cast<std::int32_t>(master.slave_actor_ids().size()) >= std::max(max_slaves, 0)) {
    add_legacy_trace(dispatch, "LegacySlave", "summon_reject", source_mail,
                     current_tick, now_ms, false, max_slaves, 0, "max_slave");
    return false;
  }
  const auto [dx, dy] = direction_delta(actor_dir(master));
  const auto royalty_time_ms = now_ms + royalty_seconds * 1000ULL;
  auto spawn = build_slave_spawn_mail(monster_name, master, master.x() + dx, master.y() + dy,
                                      make_level, make_level, 0, royalty_time_ms, 0, now_ms);
  if (!spawn.has_value()) {
    add_legacy_trace(dispatch, "LegacySlave", "summon_reject", source_mail,
                     current_tick, now_ms, false, 0, 0, monster_name);
    return false;
  }
  handle_mail(*spawn, dispatch, current_tick, now_ms);
  auto* slave = objects_.contains(spawn->actor_id)
                    ? as_monster(objects_.at(spawn->actor_id).get())
                    : nullptr;
  if (slave == nullptr) {
    add_legacy_trace(dispatch, "LegacySlave", "summon_reject", source_mail,
                     current_tick, now_ms, false, 0, 0, "spawn_failed");
    return false;
  }
  const auto missing_hp = std::max(slave->max_hp() - slave->hp(), 0);
  slave->set_hp_mp(slave->hp() + missing_hp / 2, slave->mp());
  master.add_slave_actor_id(slave->id());
  add_legacy_trace(dispatch, "LegacySlave", "summon", source_mail, current_tick,
                   now_ms, true, static_cast<std::int32_t>(slave->id() & 0x7fffffff),
                   slave->slave_exp_level(), monster_name);
  return true;
}

/**
 * @brief 驯服一只野生怪物作为宠物
 *
 * @details 尝试将一只符合条件的野生怪物转化为玩家的宠物。
 *          驯服条件：目标存活、无主人、可驯服、非不死系、玩家宠物数量未达上限。
 *          驯服后设置忠诚时间（基础 20 分钟 + make_level * 20 分钟），
 *          并清除目标当前的攻击目标。
 *
 * @param master       宠物主人
 * @param target       要驯服的怪物目标
 * @param make_level   制作等级
 * @param max_slaves   最大宠物数量上限
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @param source_mail  触发该行为的源邮件
 * @return true  驯服成功
 * @return false 驯服失败（条件不满足）
 */
bool MapActor::tame_player_slave(Player& master, Monster& target,
                                 std::int32_t make_level, std::int32_t max_slaves,
                                 RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms,
                                 const ActorMail& source_mail) {
  static_cast<void>(snapshot_owned_slaves(master, now_ms));
  if (target.is_dead() || target.master_actor_id() != 0 || !target.tameable() ||
      target.life_attrib() != 0 ||
      static_cast<std::int32_t>(master.slave_actor_ids().size()) >= std::max(max_slaves, 0)) {
    add_legacy_trace(dispatch, "LegacySlave", "tame_reject", source_mail, current_tick,
                     now_ms, false, max_slaves, 0, "target_or_count");
    return false;
  }
  target.configure_slave(master.id(), 0, make_level, 0,
                         now_ms + (20ULL + static_cast<std::uint64_t>(make_level) * 20ULL) *
                                      60ULL * 1000ULL,
                         now_ms, true);
  target.lose_target();
  master.add_slave_actor_id(target.id());
  add_legacy_trace(dispatch, "LegacySlave", "tame", source_mail, current_tick,
                   now_ms, true, static_cast<std::int32_t>(target.id() & 0x7fffffff),
                   make_level, target.name());
  return true;
}

/**
 * @brief 从存档中恢复玩家之前拥有的宠物
 *
 * @details 当玩家重新登录时，从存档记录中遍历所有宠物数据，
 *          尝试重新生成每只宠物。只有名称非空、忠诚时间 > 0 且 HP > 0
 *          的宠物会恢复。每只宠物生成后会随机偏移 AI 计时器（0-3000ms），
 *          以避免所有宠物同时行动。
 *
 * @param player       要恢复宠物的玩家
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 *
 * @note 恢复过程中如果玩家对象被销毁（如掉线），则立即停止恢复。
 * @warning 此函数可能一次性生成多只宠物，需要确保生成位置不重叠。
 */
void MapActor::restore_saved_slaves(Player& player, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto master_id = player.id();
  const auto records = player.character().slaves;
  for (const auto& record : records) {
    if (record.name.empty() || record.remain_royalty_sec <= 0 || record.hp <= 0) {
      continue;
    }
    auto* master = find_player(master_id);
    if (master == nullptr) {
      return;
    }
    const auto royalty_time_ms =
        now_ms + static_cast<std::uint64_t>(record.remain_royalty_sec) * 1000ULL;
    auto spawn = build_slave_spawn_mail(record.name, *master, master->x(), master->y(),
                                        record.slave_make_level, record.slave_exp_level,
                                        record.slave_exp, royalty_time_ms, 0, now_ms,
                                        record);
    ActorMail trace_mail;
    trace_mail.kind = ActorMailKind::spawn_monster;
    trace_mail.map_id = config_.id;
    trace_mail.actor_id = player.id();
    trace_mail.name = record.name;
    if (!spawn.has_value()) {
      add_legacy_trace(dispatch, "LegacySlave", "restore_skip", trace_mail,
                       current_tick, now_ms, false, 0, 0, "template_missing");
      continue;
    }
    auto object = make_object(*spawn);
    if (auto* slave = as_monster(object.get()); slave != nullptr) {
      slave->set_dir(spawn->dir);
      if (spawn->current_hp > 0 || spawn->current_mp > 0) {
        slave->set_hp_mp(spawn->current_hp > 0 ? spawn->current_hp : slave->hp(),
                         spawn->current_mp >= 0 ? spawn->current_mp : slave->mp());
      }
      const auto walk_offset =
          legacy_random_ != nullptr ? static_cast<std::uint64_t>(legacy_random_->random(3000))
                                    : 0ULL;
      const auto hit_offset =
          legacy_random_ != nullptr ? static_cast<std::uint64_t>(legacy_random_->random(3000))
                                    : 0ULL;
      slave->initialize_legacy_ai_timers(now_ms, walk_offset, hit_offset);
    }
    if (environment_.add_moving_object(object->x(), object->y(), object->id(), now_ms,
                                       moving_state_for(*object))) {
      schedule_actor(current_tick, *object);
      const auto slave_id = spawn->actor_id;
      objects_[slave_id] = std::move(object);
      if (auto* refreshed_master = find_player(master_id); refreshed_master != nullptr) {
        refreshed_master->add_slave_actor_id(slave_id);
      }
      sync_all_player_visibility(dispatch, now_ms);
      add_legacy_trace(dispatch, "LegacySlave", "restore", *spawn, current_tick,
                       now_ms, true, static_cast<std::int32_t>(spawn->actor_id & 0x7fffffff),
                       record.slave_exp_level, record.name);
    }
  }
}

/**
 * @brief 解除玩家所有宠物的绑定
 *
 * @details 遍历玩家所有宠物，从地图上移除并清除可见性。
 *          如果 erase_objects 为 true，则直接从对象容器中删除宠物；
 *          否则将宠物主人的 ID 置零，将其 HP 降至忠诚破裂线，
 *          使其变为野生怪物。
 *
 * @param player        宠物主人
 * @param dispatch      运行时消息分发器
 * @param now_ms        当前时间（毫秒）
 * @param erase_objects true=直接删除宠物对象，false=释放为野生怪物
 *
 * @warning 此函数通常用于玩家下线时，需要在玩家对象存活时调用。
 */
void MapActor::detach_owned_slaves(Player& player, RuntimeDispatch& dispatch,
                                   std::uint64_t now_ms, bool erase_objects) {
  std::vector<std::uint64_t> ids(player.slave_actor_ids().begin(), player.slave_actor_ids().end());
  for (const auto slave_id : ids) {
    const auto it = objects_.find(slave_id);
    auto* slave = it != objects_.end() ? as_monster(it->second.get()) : nullptr;
    if (slave == nullptr || slave->master_actor_id() != player.id()) {
      player.remove_slave_actor_id(slave_id);
      continue;
    }
    static_cast<void>(environment_.delete_from_map(slave->x(), slave->y(),
                                                   LegacyMapObjectShape::moving_object,
                                                   slave->id()));
    remove_actor_from_visibility(slave->id(), dispatch);
    if (erase_objects) {
      objects_.erase(slave_id);
    } else {
      slave->set_master_actor_id(0);
      slave->reduce_hp_to_loyalty_break_floor();
      refresh_moving_object_state(*slave, now_ms);
    }
    player.remove_slave_actor_id(slave_id);
  }
}

/**
 * @brief 召回所有宠物到主人身边
 *
 * @details 当宠物距离主人太远时，将其瞬间传送到主人身边最近的可行走位置。
 *          搜索半径从 1 递增到 3，找到第一个可行走位置。
 *          传送后更新所有玩家的可见性状态。
 *
 * @param player        宠物主人
 * @param dispatch      运行时消息分发器
 * @param current_tick  当前逻辑 Tick 数
 * @param now_ms        当前时间（毫秒）
 *
 * @note 只召回存活的宠物，死亡的宠物会被跳过。
 */
void MapActor::recall_owned_slaves_to_master(Player& player, RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  for (const auto slave_id : player.slave_actor_ids()) {
    const auto it = objects_.find(slave_id);
    auto* slave = it != objects_.end() ? as_monster(it->second.get()) : nullptr;
    if (slave == nullptr || slave->is_dead() || slave->master_actor_id() != player.id()) {
      continue;
    }
    auto target_x = player.x();
    auto target_y = player.y();
    bool placed = false;
    for (std::int32_t radius = 1; radius <= 3 && !placed; ++radius) {
      for (std::int32_t dy = -radius; dy <= radius && !placed; ++dy) {
        for (std::int32_t dx = -radius; dx <= radius; ++dx) {
          const auto try_x = player.x() + dx;
          const auto try_y = player.y() + dy;
          if (environment_.can_walk(try_x, try_y, true)) {
            target_x = try_x;
            target_y = try_y;
            placed = true;
            break;
          }
        }
      }
    }
    if (!placed) {
      continue;
    }
    const auto old_x = slave->x();
    const auto old_y = slave->y();
    if (environment_.move_to_moving_object(old_x, old_y, slave->id(), target_x, target_y,
                                           true, now_ms, moving_state_for(*slave)) == 1) {
      ActorMail move_mail;
      move_mail.kind = ActorMailKind::move;
      move_mail.map_id = config_.id;
      move_mail.actor_id = slave->id();
      move_mail.x = target_x;
      move_mail.y = target_y;
      MapContext context;
      context.tick = current_tick;
      context.map_id = config_.id;
      context.dispatch = &dispatch;
      context.items = &item_configs_;
      context.magics = &magic_configs_;
      slave->on_mail(move_mail, context);
      sync_visibility_after_actor_move(*slave, old_x, old_y, target_x, target_y, dispatch,
                                       now_ms);
    }
  }
}

/**
 * @brief 通知主人的所有宠物攻击指定目标
 *
 * @details 当玩家攻击某个目标时，同步通知该玩家的所有存活宠物
 *          将攻击目标切换为同一目标。实现宠物协同攻击机制。
 *
 * @param player          宠物主人
 * @param target_actor_id 目标角色 ID
 * @param now_ms          当前时间（毫秒）
 */
void MapActor::notify_owned_slaves_target(Player& player, std::uint64_t target_actor_id,
                                          std::uint64_t now_ms) {
  if (target_actor_id == 0) {
    return;
  }
  for (const auto slave_id : player.slave_actor_ids()) {
    const auto it = objects_.find(slave_id);
    auto* slave = it != objects_.end() ? as_monster(it->second.get()) : nullptr;
    if (slave == nullptr || slave->is_dead() || slave->master_actor_id() != player.id() ||
        slave->id() == target_actor_id) {
      continue;
    }
    slave->select_target(target_actor_id, now_ms);
  }
}

/**
 * @brief 从主人的宠物列表中移除指定宠物
 *
 * @details 当宠物死亡或脱离控制时，将宠物 ID 从主人的宠物列表中移除。
 *          如果主人不存在（已下线），则不做任何操作。
 *
 * @param slave 要移除的宠物怪物
 */
void MapActor::remove_slave_from_master(Monster& slave) {
  if (slave.master_actor_id() == 0) {
    return;
  }
  if (auto* master = find_player(slave.master_actor_id()); master != nullptr) {
    master->remove_slave_actor_id(slave.id());
  }
}

/**
 * @brief 处理宠物的生命周期检查
 *
 * @details 每个 Tick 检查宠物的存活条件：
 *          1. 主人死亡或进入幽灵状态超过 1 秒 -> 宠物死亡
 *          2. 忠诚时间到期 -> 宠物恢复野生状态（HP 降至忠诚破裂线）
 *          3. 宠物生命时间到期（12 小时后）-> 宠物死亡
 *
 * @param monster      要检查的怪物（如果是宠物则执行检查）
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  宠物因条件不满足已死亡或脱离
 * @return false 宠物存活且条件正常
 *
 * @note 宠物每次 Tick 都会清理过期的攻击者记录。
 * @warning 忠诚到期时宠物不会死亡，而是变为野生怪物（不执行死亡流程）。
 */
bool MapActor::handle_slave_lifecycle(Monster& monster, RuntimeDispatch& dispatch,
                                      std::uint64_t current_tick,
                                      std::uint64_t now_ms) {
  monster.expire_legacy_hitters(now_ms);
  if (!monster.is_slave() || monster.master_actor_id() == 0) {
    return false;
  }
  monster.set_no_item(true);
  auto* master = find_player(monster.master_actor_id());
  if (master == nullptr || master->is_dead() ||
    (master->legacy_ghost() && now_ms > master->legacy_ghost_time_ms() + 1000ULL)) {
    remove_slave_from_master(monster);
    static_cast<void>(monster.mark_legacy_death(now_ms));
    refresh_moving_object_state(monster, now_ms);
    return true;
  }
  if (monster.master_royalty_time_ms() != 0 &&
      now_ms > monster.master_royalty_time_ms()) {
    master->remove_slave_actor_id(monster.id());
    monster.set_master_actor_id(0);
    monster.reduce_hp_to_loyalty_break_floor();
    monster.lose_target();
    refresh_moving_object_state(monster, now_ms);
    add_legacy_trace(dispatch, "LegacySlave", "royalty_expired", ActorMail{},
                     current_tick, now_ms, true,
                     static_cast<std::int32_t>(monster.id() & 0x7fffffff), monster.hp(),
                     monster.name());
    return false;
  }
  if (monster.slave_life_time_ms() != 0 &&
      now_ms > monster.slave_life_time_ms() + 12ULL * 60ULL * 60ULL * 1000ULL) {
    remove_slave_from_master(monster);
    static_cast<void>(monster.mark_legacy_death(now_ms));
    refresh_moving_object_state(monster, now_ms);
    return true;
  }
  return false;
}

/**
 * @brief 处理宠物的跟随逻辑
 *
 * @details 当宠物没有攻击目标时，自动跟随主人。
 *          计算宠物应该站立的"主人身后位置"，如果宠物距离主人
 *          超过 20 格则直接召回；如果宠物距离主人较近但目标位置
 *          不可行走则原地等待。
 *          如果主人开启了"宠物休息"模式（slave_relax），宠物不会跟随。
 *
 * @param monster      宠物怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  已执行跟随逻辑（或处于休息模式）
 * @return false 无需跟随（不是宠物、有攻击目标、或已在主人身边）
 *
 * @see legacy_slave_back_position
 * @see recall_owned_slaves_to_master
 */
bool MapActor::handle_slave_follow(Monster& monster, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick,
                                   std::uint64_t now_ms) {
  if (!monster.is_slave() || monster.master_actor_id() == 0 || monster.target_actor_id() != 0) {
    return false;
  }
  auto* master = find_player(monster.master_actor_id());
  if (master == nullptr) {
    return false;
  }
  if (master->legacy_slave_relax()) {
    return true;
  }
  const auto [back_x, back_y] = legacy_slave_back_position(*master);
  const auto dx = back_x - monster.x();
  const auto dy = back_y - monster.y();
  const auto cheb = std::max(std::abs(dx), std::abs(dy));
  if (cheb <= 1) {
    return false;
  }
  if (std::max(std::abs(master->x() - monster.x()),
               std::abs(master->y() - monster.y())) > 20) {
    recall_owned_slaves_to_master(*master, dispatch, current_tick, now_ms);
    return true;
  }
  if (std::max(std::abs(master->x() - monster.x()),
               std::abs(master->y() - monster.y())) <= 2 &&
      !environment_.can_walk(back_x, back_y, true)) {
    monster.set_target_xy(monster.x(), monster.y());
    return false;
  }
  monster.set_target_xy(back_x, back_y);
  return legacy_goto_target_xy(monster, dispatch, current_tick, now_ms);
}

// ---------------------------------------------------------------------------
// 怪物死亡处理（Monster Death & Loot）
// ---------------------------------------------------------------------------

/**
 * @brief 最终处理怪物死亡（掉落、经验、任务触发）
 *
 * @details 完整的怪物死亡处理流程：
 *          1. 标记死亡时间和状态
 *          2. 如果是宠物或设置了不掉落物品，则跳过掉落处理
 *          3. 查找击杀者（经验打击者 -> 最后一击者 -> killer_actor_id 回退）
 *          4. 如果击杀者是玩家的宠物，则奖励经验给宠物主人
 *          5. 发放经验奖励
 *          6. 触发地图任务（"monster_die" 事件），范围内玩家都触发
 *          7. 处理金币掉落（分块掉落，每块不超过 kLegacyMonsterGoldDropChunk）
 *          8. 处理物品掉落（搜索最佳落地位置，优先堆叠到已有物品上）
 *
 * @param monster_id      被击杀怪物的 ID
 * @param killer_actor_id 击杀者的角色 ID（可能为 0）
 * @param dispatch        运行时消息分发器
 * @param current_tick    当前逻辑 Tick 数
 *
 * @note 掉落物品会分配所有权（owner_actor_id），过期时间默认为 kLegacyDropOwnerMs。
 * @warning 此函数只应该被调用一次，通过 death_settled() 防止重复处理。
 */
void MapActor::finalize_monster_death(std::uint64_t monster_id, std::uint64_t killer_actor_id,
                                      RuntimeDispatch& dispatch, std::uint64_t current_tick) {
  const auto monster_it = objects_.find(monster_id);
  if (monster_it == objects_.end()) {
    return;
  }

  auto* monster = as_monster(monster_it->second.get());
  if (monster == nullptr) {
    return;
  }

  const auto now_ms = current_tick * static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1));
  if (!monster->is_dead()) {
    static_cast<void>(monster->mark_legacy_death(now_ms));
  } else if (monster->death_time_ms() == 0) {
    static_cast<void>(monster->mark_legacy_death(now_ms));
  }
  refresh_moving_object_state(*monster, now_ms);

  if (monster->death_settled()) {
    return;
  }
  monster->mark_death_settled();

  if (monster->is_slave() || monster->no_item()) {
    remove_slave_from_master(*monster);
    monster->clear_legacy_hitters();
    add_legacy_trace(dispatch, "LegacyReward", "slave_no_drop", ActorMail{},
                     current_tick, now_ms, true,
                     static_cast<std::int32_t>(monster->id() & 0x7fffffff), 0,
                     monster->name());
    return;
  }

  // --- 确定击杀奖励和掉落归属 ---
  std::uint64_t reward_actor_id = 0;
  std::uint64_t drop_owner_actor_id = 0;
  // 优先使用经验打击者（造成最多伤害的人）
  if (monster->exp_hitter_id() != 0) {
    const auto exp_it = objects_.find(monster->exp_hitter_id());
    if (exp_it != objects_.end()) {
      if (auto* player_hitter = as_player(exp_it->second.get()); player_hitter != nullptr) {
        reward_actor_id = player_hitter->id();
        drop_owner_actor_id = player_hitter->id();
      } else if (auto* slave_hitter = as_monster(exp_it->second.get());
                 slave_hitter != nullptr && slave_hitter->master_actor_id() != 0) {
        // 经验打击者是宠物 -> 奖励归主人
        const auto master_it = objects_.find(slave_hitter->master_actor_id());
        auto* master = master_it != objects_.end() ? as_player(master_it->second.get()) : nullptr;
        if (master != nullptr) {
          reward_actor_id = master->id();
          drop_owner_actor_id = master->id();
          const auto leveled = slave_hitter->gain_slave_exp(monster->level());
          add_legacy_trace(dispatch, "LegacyReward", "slave_exp", ActorMail{},
                           current_tick, now_ms, true, monster->level(),
                           slave_hitter->slave_exp_level(),
                           leveled ? "GainSlaveExpLevel" : "GainSlaveExp");
        }
      }
    }
  } else if (monster->last_hitter_id() != 0) {
    // 没有经验打击者时使用最后一击者
    const auto last_it = objects_.find(monster->last_hitter_id());
    if (last_it != objects_.end()) {
      if (auto* player_hitter = as_player(last_it->second.get()); player_hitter != nullptr) {
        reward_actor_id = player_hitter->id();
        drop_owner_actor_id = player_hitter->id();
      }
    }
  }
  // 最终回退：使用传入的 killer_actor_id
  if (reward_actor_id == 0 && killer_actor_id != 0) {
    const auto killer_it = objects_.find(killer_actor_id);
    if (killer_it != objects_.end()) {
      if (auto* player_killer = as_player(killer_it->second.get()); player_killer != nullptr) {
        reward_actor_id = player_killer->id();
        drop_owner_actor_id = player_killer->id();
      }
    }
  }

  // --- 发放经验奖励 ---
  if (reward_actor_id != 0) {
    const auto attacker_it = objects_.find(reward_actor_id);
    if (attacker_it != objects_.end()) {
      if (auto* attacker = as_player(attacker_it->second.get()); attacker != nullptr) {
        award_monster_kill(*attacker, *monster, dispatch);
      }
    }
  }

  // --- 缓存掉落数据（可能在触发任务时怪物被修改） ---
  const auto death_dropper_id = monster->id();
  const auto death_dropper_name = monster->name();
  const auto death_x = monster->x();
  const auto death_y = monster->y();
  const auto death_drop_gold = monster->drop_gold();
  const auto death_drop_items = monster->drop_items();

  // --- 触发地图任务 ---
  if (reward_actor_id != 0) {
    const auto attacker_it = objects_.find(reward_actor_id);
    if (attacker_it != objects_.end()) {
      if (auto* attacker = as_player(attacker_it->second.get()); attacker != nullptr) {
        static_cast<void>(trigger_map_quest(*attacker, death_dropper_name, {}, false,
                                            "monster_die", dispatch, current_tick, now_ms));
        for (auto& [_, member_object] : objects_) {
          auto* member = as_player(member_object.get());
          if (member == nullptr || member->is_dead()) {
            continue;
          }
          if (std::abs(member->x() - attacker->x()) > 12 ||
              std::abs(member->y() - attacker->y()) > 12) {
            continue;
          }
          static_cast<void>(trigger_map_quest(*member, death_dropper_name, {}, true,
                                              "monster_die", dispatch, current_tick, now_ms));
        }
      }
    }
  }

  // --- 寻找最佳掉落位置 ---
  auto drop_position = [&](std::int32_t wide) -> std::pair<std::int32_t, std::int32_t> {
    std::optional<std::pair<std::int32_t, std::int32_t>> best;
    std::size_t best_count = 999;
    // 从内到外扫描，优先选择无物品的位置
    for (std::int32_t k = 1; k <= wide; ++k) {
      for (std::int32_t dy = -k; dy <= k; ++dy) {
        for (std::int32_t dx = -k; dx <= k; ++dx) {
          const auto try_x = death_x + dx;
          const auto try_y = death_y + dy;
          const auto item_count = environment_.item_object_count(try_x, try_y);
          if (!item_count.has_value()) {
            continue;
          }
          if (*item_count == 0) {
            return {try_x, try_y};
          }
          if (*item_count < best_count) {
            best_count = *item_count;
            best = std::pair{try_x, try_y};
          }
        }
      }
    }
    if (best.has_value() && best_count < 8) {
      return *best;
    }
    // 所有位置都堆满物品（>=8件）时，强行掉在怪物死亡位置
    return {death_x, death_y};
  };

  // --- 初始化地面物品属性 ---
  auto prepare_death_drop = [&](GroundItem& ground_item) {
    ground_item.owner_actor_id = drop_owner_actor_id;
    ground_item.drop_time_ms = now_ms;
    if (ground_item.owner_actor_id != 0) {
      ground_item.ownership_expire_ms = now_ms + kLegacyDropOwnerMs;
    }
    ground_item.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
    ground_item.dropper_actor_id = death_dropper_id;
    ground_item.dropper_name = death_dropper_name;
    ground_item.death_drop = true;
  };

  // --- 放置一个地面物品 ---
  auto place_ground_item = [&](GroundItem ground_item, LegacyMapItemState state,
                               std::int32_t scatter_range) -> bool {
    const auto [drop_x, drop_y] = drop_position(scatter_range);
    ground_item.x = drop_x;
    ground_item.y = drop_y;
    const auto add_result =
        environment_.add_item_object(ground_item.x, ground_item.y, ground_item.id, state, now_ms);
    if (!add_result.ok) {
      return false;
    }
    if (add_result.merged) {
      // 与地上已有的物品堆叠
      auto existing = ground_items_.find(add_result.object_id);
      if (existing == ground_items_.end()) {
        return false;
      }
      refresh_ground_item_ownership(existing->second, now_ms);
      const auto same_owner = existing->second.owner_actor_id == ground_item.owner_actor_id;
      existing->second.gold_amount += ground_item.gold_amount;
      existing->second.count = existing->second.gold_amount;
      existing->second.looks = gold_looks(existing->second.gold_amount);
      existing->second.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
      if (!same_owner) {
        // 不同所有者的物品堆叠在一起时，清除所有权（所有人都可捡取）
        existing->second.owner_actor_id = 0;
        existing->second.ownership_expire_ms = 0;
      }
      sync_visibility_after_item_change(existing->second.x, existing->second.y, dispatch, now_ms,
                                        existing->second.id);
    } else {
      const auto item_id = ground_item.id;
      const auto item_x = ground_item.x;
      const auto item_y = ground_item.y;
      ++next_ground_item_id_;
      ground_items_[ground_item.id] = std::move(ground_item);
      sync_visibility_after_item_change(item_x, item_y, dispatch, now_ms, item_id);
    }
    return true;
  };

  // --- 掉落金币（分块，避免单堆过大） ---
  auto remaining_gold = death_drop_gold;
  for (std::int32_t index = 0;
       index < kLegacyMonsterGoldDropMaxChunks && remaining_gold > 0; ++index) {
    const auto gold_amount = std::min(remaining_gold, kLegacyMonsterGoldDropChunk);
    remaining_gold -= gold_amount;
    GroundItem ground_item;
    ground_item.id = next_ground_item_id_;
    ground_item.is_gold = true;
    ground_item.gold_amount = gold_amount;
    ground_item.name = "Gold";
    ground_item.count = gold_amount;
    ground_item.looks = gold_looks(ground_item.gold_amount);
    prepare_death_drop(ground_item);
    static_cast<void>(
        place_ground_item(ground_item, LegacyMapItemState{true, ground_item.gold_amount}, 3));
  }

  // --- 掉落物品 ---
  for (const auto& item : death_drop_items) {
    if (is_empty(item)) {
      continue;
    }
    GroundItem ground_item;
    ground_item.id = next_ground_item_id_;
    ground_item.item = item;
    ground_item.name = item_name(item, item_configs_);
    ground_item.count = 1;
    ground_item.looks = item_looks(item, item_configs_);
    if (const auto* config = find_item_config(item_configs_, item.index); config != nullptr) {
      ground_item.ani_count = config->ani_count;
    }
    prepare_death_drop(ground_item);
    static_cast<void>(place_ground_item(ground_item, LegacyMapItemState{}, 3));
  }

}

/**
 * @brief 最终处理怪物鬼魂化（从地图上清除）
 *
 * @details 怪物死亡后经过一段时间会进入鬼魂状态，此函数执行：
 *          1. 如果死亡尚未结算，先执行死亡结算
 *          2. 标记鬼魂化时间
 *          3. 从地图和可见性系统中移除
 *          4. 如果不是宠物，安排重生
 *          5. 从对象容器中删除
 *
 * @param monster_id    要鬼魂化的怪物 ID
 * @param dispatch      运行时消息分发器
 * @param current_tick  当前逻辑 Tick 数
 * @param now_ms        当前时间（毫秒）
 *
 * @note 宠物和不掉落物品的怪物也会被安排重生（宠物除外）。
 */
void MapActor::finalize_monster_ghost(std::uint64_t monster_id, RuntimeDispatch& dispatch,
                                      std::uint64_t current_tick, std::uint64_t now_ms) {
  const auto monster_it = objects_.find(monster_id);
  if (monster_it == objects_.end()) {
    return;
  }

  auto* monster = as_monster(monster_it->second.get());
  if (monster == nullptr) {
    return;
  }
  if (!monster->death_settled()) {
    finalize_monster_death(monster_id, monster->last_hitter_id(), dispatch, current_tick);
  }

  monster->mark_legacy_ghost(now_ms);
  static_cast<void>(environment_.delete_from_map(monster->x(), monster->y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 monster->id()));
  if (!monster->is_slave() && !monster->no_item()) {
    schedule_monster_respawn(monster_id, current_tick);
  }
  remove_actor_from_visibility(monster_id, dispatch);
  objects_.erase(monster_id);
  sync_all_player_visibility(dispatch, now_ms);
}

// ---------------------------------------------------------------------------
// 怪物移动与碰撞检测（Monster Movement & Collision）
// ---------------------------------------------------------------------------

/**
 * @brief 统计指定坐标上存活对象的数量（用于判断"堆叠"）
 *
 * @details 检查地图单元格内所有移动对象，统计其中存活的个数。
 *          当同一位置有多个怪物/角色时，用于触发"去堆叠"逻辑。
 *
 * @param x 地图 x 坐标
 * @param y 地图 y 坐标
 * @return std::size_t 该坐标上存活的移动对象数量
 *
 * @see legacy_monster_think 中使用该值判断是否进入 dup_mode
 */
std::size_t MapActor::legacy_dup_count(std::int32_t x, std::int32_t y) const {
  const auto* cell = environment_.cell(x, y);
  if (cell == nullptr) {
    return 0;
  }

  std::size_t count = 0;
  for (const auto& entry : cell->obj_list) {
    if (entry.shape != LegacyMapObjectShape::moving_object) {
      continue;
    }
    const auto object_it = objects_.find(entry.object_id);
    if (object_it == objects_.end() || !is_alive(*object_it->second)) {
      continue;
    }
    ++count;
  }
  return count;
}

/**
 * @brief 尝试让怪物朝指定方向移动一步
 *
 * @details 检查目标位置是否可通行，如果可通行则移动怪物并广播行走动画。
 *          这是怪物最基本的移动操作，被 AI 各模块调用。
 *
 * @param monster      要移动的怪物
 * @param dir          移动方向（0-7）
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  移动成功
 * @return false 移动失败（目标位置不可通行）
 */
bool MapActor::legacy_try_monster_walk(Monster& monster, std::uint8_t dir,
                                       RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick,
                                       std::uint64_t now_ms) {
  const auto walk_dir = static_cast<std::uint8_t>(dir % 8);
  monster.set_dir(walk_dir);
  const auto [dx, dy] = direction_delta(walk_dir);
  const auto old_x = monster.x();
  const auto old_y = monster.y();
  const auto next_x = monster.x() + dx;
  const auto next_y = monster.y() + dy;
  if (environment_.move_to_moving_object(monster.x(), monster.y(), monster.id(), next_x,
                                         next_y, false, now_ms,
                                         moving_state_for(monster)) != 1) {
    return false;
  }

  ActorMail move_mail;
  move_mail.kind = ActorMailKind::move;
  move_mail.actor_id = monster.id();
  move_mail.x = next_x;
  move_mail.y = next_y;
  move_mail.dir = walk_dir;

  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;
  monster.on_mail(move_mail, context);

  for (const auto watcher_id : legacy_ref_target_player_ids(monster, now_ms)) {
    const auto* watcher = find_player(watcher_id);
    if (watcher == nullptr || watcher->id() == monster.id()) {
      continue;
    }
    queue_packet(dispatch, watcher->session_id(),
                 make_turn_like_packet(watcher->session_id(), kSmWalk, monster, false));
  }
  sync_visibility_after_actor_move(monster, old_x, old_y, monster.x(), monster.y(), dispatch,
                                   now_ms);
  return true;
}

// ---------------------------------------------------------------------------
// 怪物 AI 核心逻辑（Monster AI Core）
// ---------------------------------------------------------------------------

/**
 * @brief 怪物的 Think 逻辑（AI 决策核心之一）
 *
 * @details 每 3 秒执行一次的目标有效性检查：
 *          1. 检查当前目标是否有效（存在、未过期、距离未过远）
 *          2. 如果当前格子有 >=2 个存活对象，进入 dup_mode（去堆叠模式）
 *
 *          dup_mode 下怪物会尝试随机方向行走，以分散聚集的怪物群。
 *          这是为了防止大量怪物堆叠在同一格上的视觉异常。
 *
 * @param monster      要执行思考的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  怪物已执行操作（处于 dup_mode）
 * @return false 无操作或不在 dup_mode
 *
 * @see legacy_dup_count
 */
bool MapActor::legacy_monster_think(Monster& monster, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick,
                                    std::uint64_t now_ms) {
  if (static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(monster.think_time_ms()) >
      3000) {
    monster.mark_think_time(now_ms);
    if (monster.target_actor_id() != 0) {
      const auto target_it = objects_.find(monster.target_actor_id());
      auto* target = target_it != objects_.end() ? target_it->second.get() : nullptr;
      auto* player_target = as_player(target);
      auto* monster_target = as_monster(target);
      const auto focus_expired =
          monster.target_focus_time_ms() != 0 && now_ms > monster.target_focus_time_ms() + 30000ULL;
      const auto target_too_far =
          target != nullptr &&
          (std::abs(target->x() - monster.x()) > 15 ||
           std::abs(target->y() - monster.y()) > 15);
      static_cast<void>(player_target);
      static_cast<void>(monster_target);
      if (target == nullptr || focus_expired || target_too_far ||
          !legacy_monster_valid_target(monster, *target, current_tick)) {
        monster.lose_target();
      }
    }
    if (legacy_dup_count(monster.x(), monster.y()) >= 2) {
      monster.set_dup_mode(true);
    }
  }

  if (!monster.dup_mode()) {
    return false;
  }

  const auto dir = static_cast<std::uint8_t>(
      legacy_random_value(dispatch, "MonsterAI", "dup_walk", 8, monster.id(),
                          monster.target_actor_id(), {}, now_ms, current_tick));
  if (!legacy_try_monster_walk(monster, dir, dispatch, current_tick, now_ms)) {
    return false;
  }
  monster.set_dup_mode(false);
  return true;
}

/**
 * @brief 刷新怪物的可见角色列表
 *
 * @details 在怪物视野范围（默认 5x5）内扫描所有非隐藏、非死亡的
 *          移动对象，构建可见角色 ID 列表。这为怪物的目标搜索提供基础。
 *          扫描过滤条件：
 *          - 非移动对象、鬼魂、死亡、隐身或管理员模式的对象被排除
 *          - 玩家类型的对象如果死亡或鬼魂化也被排除
 *          - 怪物类型的对象如果死亡、鬼魂化或隐身也被排除
 *
 * @param monster 要刷新可见列表的怪物
 */
void MapActor::legacy_refresh_monster_visible_actors(Monster& monster) {
  std::vector<std::uint64_t> scanned_actor_ids;
  for (std::int32_t x = monster.x() - kLegacyOrdinaryMonsterViewRange;
       x <= monster.x() + kLegacyOrdinaryMonsterViewRange; ++x) {
    for (std::int32_t y = monster.y() - kLegacyOrdinaryMonsterViewRange;
         y <= monster.y() + kLegacyOrdinaryMonsterViewRange; ++y) {
      const auto* cell = environment_.cell(x, y);
      if (cell == nullptr) {
        continue;
      }
      for (const auto& object : cell->obj_list) {
        if (object.shape != LegacyMapObjectShape::moving_object ||
            object.object_id == monster.id() || object.moving.ghost ||
            object.moving.death || object.moving.hide_mode ||
            object.moving.supervisor_mode) {
          continue;
        }
        if (std::find(scanned_actor_ids.begin(), scanned_actor_ids.end(),
                      object.object_id) != scanned_actor_ids.end()) {
          continue;
        }
        const auto object_it = objects_.find(object.object_id);
        if (object_it == objects_.end()) {
          continue;
        }
        if (const auto* player = as_player(object_it->second.get()); player != nullptr) {
          if (player->is_dead() || player->legacy_ghost()) {
            continue;
          }
        } else if (const auto* candidate_monster = as_monster(object_it->second.get());
                   candidate_monster != nullptr) {
          if (candidate_monster->is_dead() || candidate_monster->legacy_ghosted() ||
              candidate_monster->hide_mode()) {
            continue;
          }
        } else {
          continue;
        }
        scanned_actor_ids.push_back(object.object_id);
      }
    }
  }
  monster.refresh_legacy_visible_actor_ids(scanned_actor_ids);
}

/**
 * @brief 检查怪物目标是否有效
 *
 * @details 判断一个目标是否是怪物的合法攻击目标：
 *          - 玩家目标：不能死亡、鬼魂化、在安全区或透明状态
 *            （宠物的主人不是有效目标）
 *          - 怪物目标：不能死亡、鬼魂化或隐藏
 *            并且不能是宠物本人的"友军"（同一主人的其他宠物）
 *
 * @param monster      攻击者怪物
 * @param target       待检查的目标
 * @param current_tick 当前 Tick（用于检查透明状态）
 * @return true  目标是合法攻击目标
 * @return false 目标无效
 */
bool MapActor::legacy_monster_valid_target(const Monster& monster, const GameObject& target,
                                           std::uint64_t current_tick) const {
  if (target.id() == monster.id()) {
    return false;
  }
  if (const auto* player = as_player(&target); player != nullptr) {
    if (player->is_dead() || player->legacy_ghost() ||
        is_safe_zone(config_, player->x(), player->y()) ||
        player->legacy_transparent_active(current_tick)) {
      return false;
    }
    return !monster.is_slave() || player->id() != monster.master_actor_id();
  }
  const auto* target_monster = as_monster(&target);
  if (target_monster == nullptr || target_monster->is_dead() ||
      target_monster->legacy_ghosted() || target_monster->hide_mode()) {
    return false;
  }
  if (target_monster->id() == monster.master_actor_id()) {
    return false;
  }
  if (target_monster->master_actor_id() == monster.master_actor_id()) {
    return false;
  }
  return true;
}

/**
 * @brief 判断怪物搜索候选目标的资格
 *
 * @details 在 legacy_monster_valid_target 基础上增加：对于怪物目标，
 *          只有被玩家驯服的宠物（master_actor_id != 0）才被视为合法目标。
 *          野生怪物之间不会互相攻击（除非特殊逻辑处理）。
 *
 * @param monster      搜索者怪物
 * @param target       候选目标
 * @param current_tick 当前 Tick
 * @return true  该候选者可以成为攻击目标
 * @return false 该候选者不可作为攻击目标
 */
bool MapActor::legacy_monster_search_candidate(const Monster& monster,
                                               const GameObject& target,
                                               std::uint64_t current_tick) const {
  if (!legacy_monster_valid_target(monster, target, current_tick)) {
    return false;
  }
  if (as_player(&target) != nullptr) {
    return true;
  }
  const auto* target_monster = as_monster(&target);
  return target_monster != nullptr && target_monster->master_actor_id() != 0;
}

/**
 * @brief 怪物的主动搜索行为
 *
 * @details 在预搜索频率内查找最近的合法目标，如果找到则设置为当前目标。
 *          只有具有预搜索资格的怪物才会执行此函数
 *          （参见 legacy_monster_has_pre_run_search）。
 *          搜索频率由怪物的 legacy_search_rate_ms() 控制。
 *
 * @param monster      执行搜索的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 *
 * @see legacy_monster_has_pre_run_search
 * @see legacy_monster_normal_attack
 */
void MapActor::legacy_active_search(Monster& monster, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  if (!legacy_monster_has_pre_run_search(monster)) {
    return;
  }

  if (now_ms <= monster.search_enemy_time_ms() + monster.legacy_search_rate_ms()) {
    return;
  }

  monster.mark_search_enemy_time(now_ms);
  static_cast<void>(legacy_monster_normal_attack(monster, dispatch, current_tick, now_ms));
}

/**
 * @brief 怪物的普通攻击搜索
 *
 * @details 遍历可见角色列表，找到距离最近的合法目标，设为当前攻击目标。
 *          距离计算使用曼哈顿距离，优先选择更近的目标。
 *
 * @param monster      执行搜索的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  找到了目标并已设置
 * @return false 未找到合适目标
 */
bool MapActor::legacy_monster_normal_attack(Monster& monster, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick,
                                            std::uint64_t now_ms) {
  GameObject* nearest = nullptr;
  auto best_distance = std::numeric_limits<std::int32_t>::max();
  for (const auto actor_id : monster.legacy_visible_actor_ids()) {
    const auto object_it = objects_.find(actor_id);
    if (object_it == objects_.end()) {
      continue;
    }
    auto* target = object_it->second.get();
    if (!legacy_monster_search_candidate(monster, *target, current_tick)) {
      continue;
    }

    const auto distance = std::abs(target->x() - monster.x()) +
                          std::abs(target->y() - monster.y());
    if (distance >= best_distance) {
      continue;
    }
    nearest = target;
    best_distance = distance;
  }

  if (nearest == nullptr) {
    return false;
  }

  monster.select_target(nearest->id(), now_ms);
  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::attack;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = monster.id();
  trace_mail.target_actor_id = nearest->id();
  add_legacy_trace(dispatch, "MonsterAI", "MonsterNormalAttack", trace_mail,
                   current_tick, now_ms, true, best_distance);
  return true;
}

/**
 * @brief 执行怪物对当前目标的攻击
 *
 * @details 检查目标是否在攻击范围内（近战 1 格，远程 4 格，固定 3 格）。
 *          如果在范围内且攻击 CD 已到，则执行攻击并重置攻击计时器。
 *          如果目标超出攻击范围，则设置目标坐标为移动目的地。
 *
 * @param monster      攻击者怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  目标在攻击范围内（无论是否实际造成伤害）
 * @return false 目标不在攻击范围内
 *
 * @see legacy_monster_temp_attack    （对玩家攻击）
 * @see legacy_monster_attack_monster（对怪物攻击）
 */
bool MapActor::legacy_attack_target(Monster& monster, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick,
                                    std::uint64_t now_ms) {
  if (monster.target_actor_id() == 0) {
    return false;
  }

  auto target_it = objects_.find(monster.target_actor_id());
  auto* target = target_it != objects_.end() ? target_it->second.get() : nullptr;
  auto* player_target = as_player(target);
  auto* monster_target = as_monster(target);
  if (target == nullptr || !legacy_monster_valid_target(monster, *target, current_tick)) {
    monster.lose_target();
    return false;
  }

  const auto dx = target->x() - monster.x();
  const auto dy = target->y() - monster.y();
  const auto distance = std::max(std::abs(dx), std::abs(dy));
  const auto profile = monster.ai_profile();
  const auto attack_range =
      profile == MonsterAiProfile::ranged ? 4 : profile == MonsterAiProfile::stationary ? 3 : 1;
  if (distance <= attack_range) {
    if (monster.legacy_attack_due_by_hit_time(now_ms)) {
      monster.set_dir(legacy::next_direction(monster.x(), monster.y(), target->x(), target->y()));
      monster.mark_legacy_hit_time(now_ms);
      monster.select_target(target->id(), now_ms);
      if (player_target != nullptr) {
        legacy_monster_temp_attack(monster, *player_target, dispatch, current_tick, now_ms);
      } else if (monster_target != nullptr) {
        legacy_monster_attack_monster(monster, *monster_target, dispatch, current_tick, now_ms);
      }
      monster.break_legacy_holy_seize();
    }
    return true;
  }

  monster.set_target_xy(target->x(), target->y());
  return false;
}

/**
 * @brief 使怪物向目标坐标移动
 *
 * @details 使用寻路算法向目标坐标前进。首先尝试朝向目标方向的直线移动，
 *          如果直线方向被阻挡，则依次尝试左右偏转（顺时针和逆时针交替），
 *          最多尝试 7 个方向。
 *
 * @param monster      要移动的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  成功向目标方向移动（至少一步）
 * @return false 无法向目标方向移动或已在目标位置
 */
bool MapActor::legacy_goto_target_xy(Monster& monster, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms) {
  if (!monster.has_target_xy()) {
    return false;
  }
  if (monster.x() == monster.target_x() && monster.y() == monster.target_y()) {
    monster.clear_target_xy();
    return false;
  }

  auto dir = legacy::next_direction(monster.x(), monster.y(),
                                    monster.target_x(), monster.target_y());
  if (legacy_try_monster_walk(monster, dir, dispatch, current_tick, now_ms)) {
    return true;
  }

  const auto random_dir =
      legacy_random_value(dispatch, "MonsterAI", "GotoTargetXY", 3, monster.id(),
                          monster.target_actor_id(), {}, now_ms, current_tick);
  for (std::int32_t retry = 0; retry < 7; ++retry) {
    dir = static_cast<std::uint8_t>(random_dir == 0 ? (dir + 7) % 8 : (dir + 1) % 8);
    if (legacy_try_monster_walk(monster, dir, dispatch, current_tick, now_ms)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 怪物闲逛行为（无目标时的随机移动）
 *
 * @details 当怪物没有目标且不在 dup_mode 时，以 1/20 的概率触发闲逛。
 *          闲逛时有两种行为：
 *          - 1/4 概率转身（仅改变方向，不移动）
 *          - 3/4 概率向前走一步
 *          固定型（stationary）怪物不会闲逛。
 *
 * @param monster      闲逛的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 */
void MapActor::legacy_wondering(Monster& monster, RuntimeDispatch& dispatch,
                                std::uint64_t current_tick, std::uint64_t now_ms) {
  if (monster.ai_profile() == MonsterAiProfile::stationary) {
    return;
  }
  if (legacy_random_value(dispatch, "MonsterAI", "Wondering20", 20, monster.id(),
                          monster.target_actor_id(), {}, now_ms, current_tick) != 0) {
    return;
  }

  if (legacy_random_value(dispatch, "MonsterAI", "Wondering4", 4, monster.id(),
                          monster.target_actor_id(), {}, now_ms, current_tick) == 1) {
    const auto dir = static_cast<std::uint8_t>(
        legacy_random_value(dispatch, "MonsterAI", "WonderingTurn", 8, monster.id(),
                            monster.target_actor_id(), {}, now_ms, current_tick));
    monster.set_dir(dir);
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!is_legacy_visible_to(watcher, monster)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_turn_like_packet(watcher.session_id(), kSmTurn, monster, false));
    });
    return;
  }

  static_cast<void>(
      legacy_try_monster_walk(monster, monster.dir(), dispatch, current_tick, now_ms));
}

// ---------------------------------------------------------------------------
// 怪物战斗系统（Monster Combat）
// ---------------------------------------------------------------------------

/**
 * @brief 怪物对玩家的攻击（临时攻击/普通近战）
 *
 * @details 完整的物理攻击流程：
 *          1. PK 限制检查（宠物不能攻击主人不允许攻击的玩家）
 *          2. 广播攻击动画（kCmHit）
 *          3. 命中判定：accuracy_point 与随机速率的比较
 *          4. 伤害计算：攻击力（DC 区间随机）- 防御力（AC 区间随机）
 *          5. 应用伤害并处理吸收/护盾
 *          6. 处理装备耐久损耗
 *          7. 如果目标死亡，尝试复活 -> 执行死亡结算
 *          8. 广播受击/死亡通知
 *          9. 通知宠物的主人被攻击
 *
 * @param monster      攻击者怪物
 * @param target       被攻击的玩家
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 *
 * @note 这是"临时攻击"（temp_attack），与"特殊攻击"（special_attack）相对，
 *       指的是最基础的普通物理攻击。
 */
void MapActor::legacy_monster_temp_attack(Monster& monster, Player& target,
                                          RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms) {
  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::attack;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = monster.id();
  trace_mail.target_actor_id = target.id();
  if (monster.is_slave() && monster.master_actor_id() != 0) {
    if (auto master_it = objects_.find(monster.master_actor_id());
        master_it != objects_.end()) {
      if (auto* master = as_player(master_it->second.get()); master != nullptr) {
        const auto block_reason = resolve_pk_block_reason(config_, *master, target, now_ms);
        if (!block_reason.empty()) {
          add_legacy_trace(dispatch, "MonsterCombat", "pk_block", trace_mail,
                           current_tick, now_ms, false, 0, 0, block_reason);
          return;
        }
      }
    }
  }
  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (!is_legacy_visible_to(watcher, monster)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 make_hit_packet(watcher.session_id(), monster, kCmHit));
  });
  add_legacy_trace(dispatch, "MonsterCombat", "attack_broadcast", trace_mail,
                   current_tick, now_ms, true, 0, 0, "SM_HIT");

  // --- 命中判定 ---
  const auto hit_roll =
      legacy_random_value(dispatch, "MonsterCombat", "hit_check",
                          legacy_speed_point(target), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  if (!legacy_hit_roll_succeeds(monster.accuracy_point(), hit_roll)) {
    add_legacy_trace(dispatch, "MonsterCombat", "miss", trace_mail, current_tick,
                     now_ms, false, hit_roll, 0,
                     "AccuracyPoint<=Random(SpeedPoint)");
    return;
  }

  // --- 攻击力和防御力随机 ---
  const auto dc_min = std::max(monster.dc_min(), 0);
  const auto dc_max = std::max(dc_min, monster.dc_max());
  const auto attack_roll =
      legacy_random_value(dispatch, "MonsterCombat", "attack_power_roll",
                          std::max(1, dc_max - dc_min + 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  const auto attack_power = dc_min + std::clamp(attack_roll, 0, dc_max - dc_min);
  const auto [ac_min, ac_max] = actor_physical_defense_range(target);
  const auto armor_roll =
      legacy_random_value(dispatch, "MonsterCombat", "armor_roll",
                          std::max(1, ac_max - ac_min + 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  const auto damage = legacy_physical_struck_damage(target, attack_power, armor_roll);
  add_legacy_trace(dispatch, "MonsterCombat", "damage", trace_mail, current_tick,
                   now_ms, true, attack_power, damage,
                   "GetAttackPower/GetHitStruckDamage");

  const auto damage_result = target.apply_damage(damage, current_tick);
  const auto applied_damage = damage_result.hp_damage;
  const auto absorbed_damage = damage_result.absorbed_damage;
  if (applied_damage <= 0) {
    add_legacy_trace(dispatch, "MonsterCombat", "absorbed", trace_mail,
                     current_tick, now_ms, absorbed_damage > 0, absorbed_damage,
                     0, "StruckDamage");
    if (absorbed_damage > 0) {
      queue_packet(dispatch, target.session_id(),
                   make_health_spell_changed_packet(target.session_id(), target));
      if (damage_result.shield_broken) {
        notify_player_and_watchers(dispatch, target,
                                   make_shield_break_self_notice(damage_result.shield_name),
                                   make_shield_break_watcher_notice(target,
                                                                    damage_result.shield_name));
      }
    }
    return;
  }

  static_cast<void>(apply_legacy_struck_equipment_durability(
      target, monster.id(), dispatch, current_tick, now_ms, "MonsterCombat"));

  auto died = target.is_dead();
  if (died && try_legacy_revival(target, dispatch, current_tick, now_ms)) {
    died = false;
  }
  if (died) {
    const auto death_clear = target.mark_dead(now_ms);
    dispatch_player_status_tick_result(target, death_clear, dispatch, false);
    static_cast<void>(settle_player_death(target, dispatch, current_tick, now_ms));
  }
  if (absorbed_damage > 0) {
    queue_packet(dispatch, target.session_id(),
                 make_health_spell_changed_packet(target.session_id(), target));
  }
  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 died ? make_death_packet(watcher.session_id(), target,
                                          watcher.id() == target.id())
                      : make_struck_packet(watcher.session_id(), target, monster.id(),
                                           applied_damage, false));
  });
  add_legacy_trace(dispatch, "MonsterCombat", died ? "death" : "struck",
                   trace_mail, current_tick, now_ms, true, 0, applied_damage,
                   died ? "SM_DEATH" : "SM_STRUCK");
  if (applied_damage > 0) {
    notify_owned_slaves_target(target, monster.id(), now_ms);
  }
}

/**
 * @brief 怪物对怪物的攻击
 *
 * @details 与怪物对玩家的攻击流程基本一致，但不需要处理 PK 限制、复活和
 *          护盾吸收。击中后直接计算物理伤害，如果目标死亡则执行死亡结算。
 *          用于宠物攻击其他怪物、或特殊怪物之间的攻击。
 *
 * @param monster      攻击者怪物
 * @param target       被攻击的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 */
void MapActor::legacy_monster_attack_monster(Monster& monster, Monster& target,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms) {
  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (!is_legacy_visible_to(watcher, monster)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 make_hit_packet(watcher.session_id(), monster, kCmHit));
  });
  ActorMail trace_mail;
  trace_mail.kind = ActorMailKind::attack;
  trace_mail.map_id = config_.id;
  trace_mail.actor_id = monster.id();
  trace_mail.target_actor_id = target.id();
  add_legacy_trace(dispatch, "MonsterCombat", "attack_broadcast", trace_mail,
                   current_tick, now_ms, true, 0, 0, "SM_HIT");

  const auto hit_roll =
      legacy_random_value(dispatch, "MonsterCombat", "hit_check",
                          legacy_speed_point(target), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  if (!legacy_hit_roll_succeeds(monster.accuracy_point(), hit_roll)) {
    add_legacy_trace(dispatch, "MonsterCombat", "miss", trace_mail, current_tick,
                     now_ms, false, hit_roll, 0,
                     "AccuracyPoint<=Random(SpeedPoint)");
    return;
  }

  const auto dc_min = std::max(monster.dc_min(), 0);
  const auto dc_max = std::max(dc_min, monster.dc_max());
  const auto attack_roll =
      legacy_random_value(dispatch, "MonsterCombat", "attack_power_roll",
                          std::max(1, dc_max - dc_min + 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  const auto attack_power = dc_min + std::clamp(attack_roll, 0, dc_max - dc_min);
  const auto [ac_min, ac_max] = actor_physical_defense_range(target);
  const auto armor_roll =
      legacy_random_value(dispatch, "MonsterCombat", "armor_roll",
                          std::max(1, ac_max - ac_min + 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  const auto damage = legacy_physical_struck_damage(target, attack_power, armor_roll);
  add_legacy_trace(dispatch, "MonsterCombat", "damage", trace_mail, current_tick,
                   now_ms, true, attack_power, damage,
                   "GetAttackPower/GetHitStruckDamage");

  const auto applied_damage =
      apply_legacy_monster_damage(objects_, target, damage, monster.id(),
                                  config_, current_tick, now_ms);
  if (applied_damage <= 0) {
    add_legacy_trace(dispatch, "MonsterCombat", "absorbed", trace_mail,
                     current_tick, now_ms, false, 0, 0, "StruckDamage");
    return;
  }

  const auto died = target.is_dead();
  auto pending_death_packets =
      died ? collect_legacy_death_packets(objects_, target)
           : std::vector<PendingLegacyPacket>{};
  if (died) {
    finalize_monster_death(target.id(), monster.id(), dispatch, current_tick);
    queue_legacy_packets(dispatch, std::move(pending_death_packets));
  } else {
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_struck_packet(watcher.session_id(), target, monster.id(),
                                      applied_damage, false));
    });
  }
  add_legacy_trace(dispatch, "MonsterCombat", died ? "death" : "struck",
                   trace_mail, current_tick, now_ms, true, 0, applied_damage,
                   died ? "SM_DEATH" : "SM_STRUCK");
}

/**
 * @brief 查找距离怪物最近的玩家目标
 *
 * @details 在怪物视野范围内查找符合条件的玩家：
 *          - 存活且不在安全区
 *          - 未处于透明（隐身）状态
 *          - 如果 max_range > 0，则距离不能超过该值
 *          - 如果 guard_rules 为 true，只追击攻击过自己的 PK 值 >= 2 的玩家
 *
 * @param monster      搜索者怪物
 * @param current_tick 当前 Tick
 * @param max_range    最大搜索范围（0 表示不限制）
 * @param guard_rules  是否启用"守卫规则"（只反击攻击者）
 * @return Player* 找到的最近的玩家，未找到返回 nullptr
 */
Player* MapActor::legacy_nearest_player_target(const Monster& monster,
                                               std::uint64_t current_tick,
                                               std::int32_t max_range,
                                               bool guard_rules) {
  Player* nearest = nullptr;
  auto best_distance = std::numeric_limits<std::int32_t>::max();
  for (auto& [actor_id, object] : objects_) {
    static_cast<void>(actor_id);
    auto* player = as_player(object.get());
    if (player == nullptr || player->is_dead()) {
      continue;
    }
    if (!in_legacy_view_range(monster, *player) ||
        is_safe_zone(config_, player->x(), player->y()) ||
        player->legacy_transparent_active(current_tick)) {
      continue;
    }
    if (max_range > 0 && (std::abs(player->x() - monster.x()) > max_range ||
                          std::abs(player->y() - monster.y()) > max_range)) {
      continue;
    }
    if (guard_rules && monster.last_hitter_id() != player->id() &&
        player->pk_level() < 2) {
      continue;
    }
    const auto distance = std::abs(player->x() - monster.x()) +
                          std::abs(player->y() - monster.y());
    if (distance >= best_distance) {
      continue;
    }
    nearest = player;
    best_distance = distance;
  }
  return nearest;
}

// ---------------------------------------------------------------------------
// 怪物特殊能力（Monster Special Abilities）
// ---------------------------------------------------------------------------

/**
 * @brief 怪物召唤子怪物（如蜂群、蜘蛛）
 *
 * @details 怪物可以召唤子怪物协助作战。功能要点：
 *          1. 首先清理已死亡或鬼魂化的子怪物记录
 *          2. 如果子怪物数量已达上限或召唤名称未配置，跳过
 *          3. 生成子怪物邮件，通过延迟邮件轮调度产生
 *          4. 子怪物的属性基于母体按比例缩减（HP 1/4、攻击 1/2、等级 -1）
 *          5. 蜘蛛类怪物（kRcSpiderHouse）会在 y+1 位置生成，
 *             且需要目标位置可行走
 *
 * @param monster      母体怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 *
 * @note 召唤的子怪物 AI 为 aggressive（主动攻击），母体死亡后子怪物
 *       的行为不受影响。
 */
void MapActor::legacy_monster_summon_child(Monster& monster, RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick,
                                           std::uint64_t now_ms) {
  std::unordered_set<std::uint64_t> live_children;
  for (const auto child_id : monster.child_actor_ids()) {
    const auto child_it = objects_.find(child_id);
    const auto* child = child_it != objects_.end() ? as_monster(child_it->second.get()) : nullptr;
    if (child != nullptr && !child->is_dead() && !child->legacy_ghosted()) {
      live_children.insert(child_id);
    }
  }
  monster.prune_child_actor_ids(live_children);
  if (monster.summon_monster_name().empty() ||
      static_cast<std::int32_t>(monster.child_actor_ids().size()) >= monster.summon_limit()) {
    return;
  }

  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (!is_legacy_visible_to(watcher, monster)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 make_hit_packet(watcher.session_id(), monster, kCmHit));
  });

  ActorMail spawn;
  spawn.kind = ActorMailKind::spawn_monster;
  spawn.map_id = config_.id;
  spawn.actor_id = next_script_monster_id_++;
  spawn.name = monster.summon_monster_name();
  spawn.x = monster.x();
  spawn.y = monster.race_server() == kRcSpiderHouse ? monster.y() + 1 : monster.y();
  spawn.level = std::max(monster.level() - 1, 1);
  spawn.max_hp = std::max(monster.max_hp() / 4, 1);
  spawn.attack_power = std::max(monster.attack_power() / 2, 1);
  spawn.dc_min = std::max(monster.dc_min() / 2, 0);
  spawn.dc_max = std::max(monster.dc_max() / 2, 1);
  spawn.defense = monster.physical_defense();
  spawn.magic_defense = monster.magical_defense();
  spawn.exp_reward = 1;
  spawn.race_server = 81;
  spawn.walk_speed_ms = monster.walk_speed_ms();
  spawn.attack_speed_ms = monster.attack_speed_ms();
  spawn.home_x = spawn.x;
  spawn.home_y = spawn.y;
  spawn.home_area = 6;
  spawn.legacy_spawn_group = true;
  spawn.master_actor_id = monster.id();
  spawn.monster_is_slave = true;
  spawn.target_actor_id = monster.target_actor_id();
  spawn.monster_ai_profile = MonsterAiProfile::aggressive;
  if (monster.race_server() == kRcSpiderHouse && !environment_.can_walk(spawn.x, spawn.y, true)) {
    add_legacy_trace(dispatch, "MonsterSpecial", "summon_reject", spawn, current_tick,
                     now_ms, false, 0, 0, "CanWalk");
    return;
  }
  delayed_mail_wheel_.schedule(
      current_tick, legacy_delay_ms_to_ticks(
                        static_cast<std::uint32_t>(std::max<std::uint64_t>(monster.summon_delay_ms(), 1)),
                        budgets_.tick_ms),
      spawn);
  monster.add_child_actor_id(spawn.actor_id);
  add_legacy_trace(dispatch, "MonsterSpecial", "summon_child", spawn, current_tick,
                   now_ms, true, static_cast<std::int32_t>(spawn.actor_id & 0x7fffffff),
                   0, monster.race_server() == kRcSpiderHouse ? "__Spider" : "__Bee");
}

/**
 * @brief 怪物对当前目标的特殊攻击（种族技能）
 *
 * @details 根据怪物的种族行为执行不同的特殊攻击：
 *
 *          - spit（吐液）：5x5 范围攻击，利用 kLegacySpitMap 方向模板判定
 *            被击中的玩家可能中毒（高危险蜘蛛除外）
 *
 *          - front_gas（前方毒气）：近身范围魔法攻击，附加石化或中毒效果
 *          - front_magic（前方魔法）：近身范围魔法攻击，有魔法躲避判定
 *
 *          - fly_axe（飞斧）：远程物理攻击，支持连击（chain_shot），
 *            需要飞行轨迹检测（can_fly_line），不可穿越障碍物
 *          - guard（守卫）：远程物理攻击，距离超过 12 格丢失目标
 *
 * @param monster      使用特殊攻击的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  已执行特殊攻击或正在接近目标
 * @return false 目标不在范围内或该怪物没有特殊攻击
 *
 * @note 伤害计算支持延迟发送（delay_ms），以匹配客户端动画播放时序。
 * @see legacy_monster_race_behavior
 * @see LegacyMonsterRaceBehavior
 */
bool MapActor::legacy_monster_special_attack_target(Monster& monster,
                                                    RuntimeDispatch& dispatch,
                                                    std::uint64_t current_tick,
                                                    std::uint64_t now_ms) {
  if (monster.target_actor_id() == 0) {
    return false;
  }
  auto* target = find_player(monster.target_actor_id());
  if (target == nullptr || target->is_dead() ||
      is_safe_zone(config_, target->x(), target->y()) ||
      target->legacy_transparent_active(current_tick)) {
    monster.lose_target();
    return false;
  }

  const auto behavior = legacy_monster_race_behavior(monster.race_server());
  const auto dx = target->x() - monster.x();
  const auto dy = target->y() - monster.y();
  const auto cheb = std::max(std::abs(dx), std::abs(dy));
  auto dir = legacy::next_direction(monster.x(), monster.y(), target->x(), target->y());

  // --- 辅助 lambda：计算原始 DC 伤害 ---
  auto raw_dc = [&](std::string action) {
    const auto dc_min = std::max(monster.dc_min(), 0);
    const auto dc_max = std::max(dc_min, monster.dc_max());
    const auto roll = legacy_random_value(dispatch, "MonsterSpecial", std::move(action),
                                          std::max(1, dc_max - dc_min + 1), monster.id(),
                                          target->id(), {}, now_ms, current_tick);
    return dc_min + std::clamp(roll, 0, dc_max - dc_min);
  };

  // --- 辅助 lambda：广播攻击动画 ---
  auto queue_hit = [&](std::string label) {
    monster.set_dir(dir);
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!is_legacy_visible_to(watcher, monster)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_hit_packet(watcher.session_id(), monster, kCmHit));
    });
    ActorMail trace_mail;
    trace_mail.kind = ActorMailKind::attack;
    trace_mail.map_id = config_.id;
    trace_mail.actor_id = monster.id();
    trace_mail.target_actor_id = target->id();
    add_legacy_trace(dispatch, "MonsterSpecial", "attack_broadcast", trace_mail,
                     current_tick, now_ms, true, monster.race_server(), 0, std::move(label));
  };

  // --- 辅助 lambda：安排延迟受击事件 ---
  auto schedule_struck = [&](GameObject& struck_target, std::int32_t applied_damage,
                             std::uint32_t delay_ms, bool magical, std::string label) {
    if (applied_damage <= 0) {
      return;
    }
    ActorMail delayed;
    delayed.kind = ActorMailKind::legacy_delayed_effect;
    delayed.map_id = config_.id;
    delayed.actor_id = monster.id();
    delayed.target_actor_id = struck_target.id();
    delayed.delayed_effect_kind = LegacyDelayedEffectKind::monster_struck;
    delayed.power = applied_damage;
    delayed.magic_id = magical ? 1 : 0;
    delayed.payload = std::move(label);
    delayed_mail_wheel_.schedule(current_tick,
                                 legacy_delay_ms_to_ticks(delay_ms, budgets_.tick_ms),
                                 delayed);
  };

  // --- 辅助 lambda：对玩家应用伤害（含死亡处理） ---
  auto apply_player_damage = [&](Player& player, std::int32_t damage,
                                 std::uint32_t delay_ms, bool magical,
                                 std::string label) {
    if (damage <= 0) {
      return 0;
    }
    const auto damage_result = player.apply_damage(damage, current_tick);
    const auto applied_damage = damage_result.hp_damage;
    if (player.is_dead()) {
      if (!try_legacy_revival(player, dispatch, current_tick, now_ms)) {
        const auto death_clear = player.mark_dead(now_ms);
        dispatch_player_status_tick_result(player, death_clear, dispatch, false);
        static_cast<void>(settle_player_death(player, dispatch, current_tick, now_ms));
      }
    }
    if (damage_result.absorbed_damage > 0) {
      queue_packet(dispatch, player.session_id(),
                   make_health_spell_changed_packet(player.session_id(), player));
    }
    schedule_struck(player, applied_damage, delay_ms, magical, std::move(label));
    return applied_damage;
  };

  // --- 辅助 lambda：对目标施加中毒状态 ---
  auto apply_poison = [&](GameObject& poisoned, std::int32_t poison_kind,
                          std::uint64_t duration_seconds, std::int32_t poison_level,
                          std::string action) {
    const auto anti_poison = legacy_actor_anti_poison(poisoned);
    const auto gate_range = 20 + std::max(anti_poison, 0);
    const auto gate = legacy_random_value(dispatch, "MonsterSpecial", std::move(action),
                                          gate_range, monster.id(), poisoned.id(), {},
                                          now_ms, current_tick);
    ActorMail trace_mail;
    trace_mail.kind = ActorMailKind::legacy_delayed_effect;
    trace_mail.map_id = config_.id;
    trace_mail.actor_id = monster.id();
    trace_mail.target_actor_id = poisoned.id();
    trace_mail.poison_kind = poison_kind;
    trace_mail.poison_level = poison_level;
    trace_mail.duration_ticks = legacy_delay_ms_to_ticks(
        static_cast<std::uint32_t>(duration_seconds * 1000), budgets_.tick_ms);
    if (gate != 0) {
      add_legacy_trace(dispatch, "MonsterSpecial", "poison_gate", trace_mail,
                       current_tick, now_ms, false, gate, 0, "Random(20+AntiPoison)");
      return;
    }
    const auto poison_tick_interval = legacy_delay_ms_to_ticks(2500, budgets_.tick_ms);
    auto applied = false;
    if (auto* player_target = as_player(&poisoned); player_target != nullptr) {
      applied = player_target->apply_legacy_poison(poison_kind, trace_mail.duration_ticks,
                                                   poison_level, poison_tick_interval,
                                                   monster.id(), current_tick);
      if (applied) {
        broadcast_legacy_char_status_changed(dispatch, *player_target);
      }
    } else if (auto* monster_target = as_monster(&poisoned); monster_target != nullptr) {
      applied = monster_target->apply_legacy_poison(poison_kind, trace_mail.duration_ticks,
                                                    poison_level, poison_tick_interval,
                                                    monster.id(), current_tick);
    }
    add_legacy_trace(dispatch, "MonsterSpecial", "poison_apply", trace_mail,
                     current_tick, now_ms, applied, poison_kind, poison_level,
                     "RM_MAKEPOISON");
  };

  // ========== 吐液攻击（Spit）==========
  if (behavior == LegacyMonsterRaceBehavior::spit) {
    if (std::abs(dx) > 2 || std::abs(dy) > 2) {
      monster.set_target_xy(target->x(), target->y());
      return false;
    }
    if (!monster.legacy_attack_due_by_hit_time(now_ms)) {
      return true;
    }
    monster.mark_legacy_hit_time(now_ms);
    monster.select_target(target->id(), now_ms);
    queue_hit("RM_HIT");
    const auto pwr = raw_dc("spit_power");
    for (auto& [_, object] : objects_) {
      auto* player = as_player(object.get());
      if (player == nullptr || player->is_dead()) {
        continue;
      }
      const auto map_x = player->x() - monster.x() + 2;
      const auto map_y = player->y() - monster.y() + 2;
      // 使用 kLegacySpitMap 方向模板判定是否在喷射范围内
      if (map_x < 0 || map_x >= 5 || map_y < 0 || map_y >= 5 ||
          kLegacySpitMap[dir % 8][map_y][map_x] == 0) {
        continue;
      }
      const auto hit_roll = legacy_random_value(dispatch, "MonsterSpecial", "spit_hit",
                                                legacy_speed_point(*player),
                                                monster.id(), player->id(), {}, now_ms,
                                                current_tick);
      if (!legacy_hit_roll_succeeds(monster.accuracy_point(), hit_roll)) {
        continue;
      }
      LegacyRandom fallback_random;
      auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
      const auto damage = legacy_magic_defense_damage(*player, pwr, random, current_tick,
                                                      budgets_.tick_ms);
      const auto applied = apply_player_damage(*player, damage, 300, true, "RM_STRUCK");
      if (applied > 0 && monster.race_server() != kRcHighRiskSpider) {
        // 非高危蜘蛛的吐液攻击附带 30 秒掉血毒
        apply_poison(*player, kPoisonDecHealth, 30, 1, "spit_poison");
      }
    }
    return true;
  }

  // ========== 前方毒气/前方魔法（Front Gas / Front Magic）==========
  if (behavior == LegacyMonsterRaceBehavior::front_gas ||
      behavior == LegacyMonsterRaceBehavior::front_magic) {
    if (cheb > 1) {
      monster.set_target_xy(target->x(), target->y());
      return false;
    }
    if (!monster.legacy_attack_due_by_hit_time(now_ms)) {
      return true;
    }
    monster.mark_legacy_hit_time(now_ms);
    monster.select_target(target->id(), now_ms);
    queue_hit("RM_HIT");
    if (behavior == LegacyMonsterRaceBehavior::front_magic) {
      // 前方魔法：需要过魔法躲避判定
      const auto anti_roll = legacy_random_value(dispatch, "MonsterSpecial", "anti_magic",
                                                10, monster.id(), target->id(), {}, now_ms,
                                                current_tick);
      if (!legacy_anti_magic_pass(legacy_actor_anti_magic(*target), anti_roll)) {
        return true;
      }
    } else {
      // 前方毒气：命中判定
      const auto hit_roll = legacy_random_value(dispatch, "MonsterSpecial", "gas_hit",
                                                legacy_speed_point(*target),
                                                monster.id(), target->id(), {}, now_ms,
                                                current_tick);
      if (!legacy_hit_roll_succeeds(monster.accuracy_point(), hit_roll)) {
        return true;
      }
    }
    LegacyRandom fallback_random;
    auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
    const auto damage = legacy_magic_defense_damage(*target, raw_dc("front_magic_power"),
                                                    random, current_tick, budgets_.tick_ms);
    const auto applied = apply_player_damage(*target, damage, 300, true, "RM_STRUCK");
    if (applied > 0 && behavior == LegacyMonsterRaceBehavior::front_gas) {
      if (monster.race_server() == kRcToxicGhost) {
        // 剧毒怨灵：30 秒掉血毒
        apply_poison(*target, kPoisonDecHealth, 30, 1, "toxic_poison");
      } else {
        // 普通毒气：5 秒石化
        apply_poison(*target, kLegacyPoisonStone, 5, 0, "gas_poison");
      }
    }
    return true;
  }

  // ========== 飞斧 / 守卫（Fly Axe / Guard）==========
  if (behavior == LegacyMonsterRaceBehavior::fly_axe ||
      behavior == LegacyMonsterRaceBehavior::guard) {
    const auto range = behavior == LegacyMonsterRaceBehavior::guard ? 12 : 7;
    if (std::abs(dx) > range || std::abs(dy) > range) {
      if (behavior == LegacyMonsterRaceBehavior::guard) {
        monster.lose_target();
        return true;
      }
      if (std::abs(dx) <= 11 && std::abs(dy) <= 11) {
        monster.set_target_xy(target->x(), target->y());
      }
      return false;
    }
    if (!monster.legacy_attack_due_by_hit_time(now_ms)) {
      return true;
    }
    monster.mark_legacy_hit_time(now_ms);
    if (behavior == LegacyMonsterRaceBehavior::fly_axe) {
      // 飞斧连击判定：如果连击次数达到上限，不再攻击（有 1/5 概率重置）
      if (monster.chain_shot_count() > 0 &&
          monster.chain_shot() >= monster.chain_shot_count() - 1) {
        if (legacy_random_value(dispatch, "MonsterSpecial", "chain_reset", 5,
                                monster.id(), target->id(), {}, now_ms, current_tick) == 0) {
          monster.set_chain_shot(0);
        }
        return true;
      }
      monster.increment_chain_shot();
      // 检查飞斧飞行路径是否被阻挡
      if (!environment_.can_fly_line(monster.x(), monster.y(), target->x(), target->y())) {
        add_legacy_trace(dispatch, "MonsterSpecial", "fly_reject", ActorMail{},
                         current_tick, now_ms, false, monster.race_server(), 0, "CanFly");
        return true;
      }
    }
    queue_hit(behavior == LegacyMonsterRaceBehavior::guard ? "RM_FLYAXE" : "RM_FLYAXE");
    const auto attack_power = raw_dc("flyaxe_power");
    const auto [ac_min, ac_max] = actor_physical_defense_range(*target);
    const auto armor_roll = legacy_random_value(dispatch, "MonsterSpecial", "flyaxe_armor",
                                                std::max(1, ac_max - ac_min + 1),
                                                monster.id(), target->id(), {}, now_ms,
                                                current_tick);
    const auto damage = legacy_physical_struck_damage(*target, attack_power, armor_roll);
    // 延迟 = 600ms + 距离 * 50ms（距离越远飞行时间越长）
    const auto delay = static_cast<std::uint32_t>(600 + cheb * 50);
    static_cast<void>(apply_player_damage(*target, damage, delay, false, "RM_FLYAXE"));
    return true;
  }

  return false;
}

/**
 * @brief 怪物特殊行为主调度器
 *
 * @details 根据怪物的种族行为类型，调度不同的 AI 逻辑：
 *
 *          - normal：无特殊行为，返回 false 交由普通 AI 处理
 *          - structure：地图装饰物，丢失目标并保持不动
 *          - guard：守卫模式，在 12 格范围内巡逻并攻击
 *          - centipede：蜈蚣/钻地模式，先隐藏后出现，AOE 范围攻击
 *          - sculture_king / stick_hide：钻地隐藏/出现
 *          - digout_zombi：僵尸出土（出土时生成地图事件）
 *          - summoner：召唤者，定期召唤子怪物
 *          - spit / fly_axe 等：搜索目标后执行特殊攻击
 *
 * @param monster      执行特殊行为的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 * @return true  特殊行为已处理完毕
 * @return false 不是特殊行为或未命中，需要交回普通 AI 处理
 */
bool MapActor::legacy_monster_special_run(Monster& monster, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms) {
  const auto behavior = legacy_monster_race_behavior(monster.race_server());
  if (behavior == LegacyMonsterRaceBehavior::normal) {
    return false;
  }

  if (monster.appear_time_ms() == 0) {
    monster.set_appear_time_ms(now_ms);
  }

  // --- 地图装饰物（结构） ---
  if (behavior == LegacyMonsterRaceBehavior::structure) {
    monster.lose_target();
    return true;
  }

  // --- 守卫（只反击 PK 值高的玩家） ---
  if (behavior == LegacyMonsterRaceBehavior::guard) {
    if (auto* target = legacy_nearest_player_target(monster, current_tick, 12, true);
        target != nullptr) {
      monster.select_target(target->id(), now_ms);
    } else {
      monster.lose_target();
      return true;
    }
    return legacy_monster_special_attack_target(monster, dispatch, current_tick, now_ms);
  }

  // --- 隐藏模式（钻地/潜伏） ---
  if (monster.hide_mode()) {
    const auto range = behavior == LegacyMonsterRaceBehavior::sculture_king ? 2
                       : monster.dig_up_range() > 0 ? monster.dig_up_range()
                                                     : 4;
    if (behavior == LegacyMonsterRaceBehavior::centipede &&
        static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(monster.appear_time_ms()) <=
            10000) {
      // 蜈蚣类隐藏后 10 秒内不出土
      return true;
    }
    if (auto* target = legacy_nearest_player_target(monster, current_tick, range, false);
        target != nullptr) {
      // 玩家进入范围 -> 出土
      monster.set_hide_mode(false);
      monster.set_appear_time_ms(now_ms);
      monster.select_target(target->id(), now_ms);
      refresh_moving_object_state(monster, now_ms);
      sync_all_player_visibility(dispatch, now_ms);
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::turn;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = monster.id();
      add_legacy_trace(dispatch, "MonsterSpecial", "dig_up", trace_mail,
                       current_tick, now_ms, true, monster.race_server(), 0, "RM_DIGUP");
      if (behavior == LegacyMonsterRaceBehavior::digout_zombi) {
        // 僵尸出土时生成地图事件（5 分钟内持续触发）
        LegacyEventRecord event;
        event.map_id = config_.id;
        event.x = monster.x();
        event.y = monster.y();
        event.type = LegacyEventType::digout_zombi;
        event.open_start_ms = now_ms;
        event.continue_ms = 5ULL * 60ULL * 1000ULL;
        event.run_start_ms = now_ms;
        event.run_tick_ms = 500;
        event.blocks_walk = false;
        dispatch.legacy_event_creates.push_back(event);
      }
    }
    return true;
  }

  // --- 隐藏模式保持（stick_hide / sculture_king） ---
  if (behavior == LegacyMonsterRaceBehavior::stick_hide ||
      behavior == LegacyMonsterRaceBehavior::sculture_king) {
    auto* target = find_player(monster.target_actor_id());
    const auto down_range = monster.dig_down_range() > 0 ? monster.dig_down_range() : 4;
    if (target == nullptr || target->is_dead() ||
        std::abs(target->x() - monster.x()) > down_range ||
        std::abs(target->y() - monster.y()) > down_range) {
      // 目标离开范围或死亡 -> 钻回地下
      monster.lose_target();
      monster.set_hide_mode(true);
      monster.set_appear_time_ms(now_ms);
      refresh_moving_object_state(monster, now_ms);
      remove_actor_from_visibility(monster.id(), dispatch);
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::turn;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = monster.id();
      add_legacy_trace(dispatch, "MonsterSpecial", "dig_down", trace_mail,
                       current_tick, now_ms, true, monster.race_server(), 0, "RM_DIGDOWN");
      return true;
    }
    return legacy_attack_target(monster, dispatch, current_tick, now_ms);
  }

  // --- 蜈蚣类（出土后 AOE 攻击） ---
  if (behavior == LegacyMonsterRaceBehavior::centipede) {
    if (static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(monster.appear_time_ms()) <=
        3000) {
      // 出土后 3 秒硬直
      return true;
    }
    if (auto* any_target = legacy_nearest_player_target(monster, current_tick, 6, false);
        any_target == nullptr) {
      // 10 秒内没有目标则钻回地下
      if (static_cast<std::int64_t>(now_ms) -
              static_cast<std::int64_t>(monster.appear_time_ms()) >
          10000) {
        monster.set_hide_mode(true);
        monster.set_appear_time_ms(now_ms);
        refresh_moving_object_state(monster, now_ms);
        remove_actor_from_visibility(monster.id(), dispatch);
      }
      return true;
    }
    if (!monster.legacy_attack_due_by_hit_time(now_ms)) {
      return true;
    }
    monster.mark_legacy_hit_time(now_ms);
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (!is_legacy_visible_to(watcher, monster)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   make_hit_packet(watcher.session_id(), monster, kCmHit));
    });
    // 6 格范围内 ALL AOE 攻击
    const auto dc_min = std::max(monster.dc_min(), 0);
    const auto dc_max = std::max(dc_min, monster.dc_max());
    const auto power = dc_min + legacy_random_value(dispatch, "MonsterSpecial",
                                                    "centipede_power",
                                                    std::max(1, dc_max - dc_min + 1),
                                                    monster.id(), 0, {}, now_ms,
                                                    current_tick);
    for (auto& [_, object] : objects_) {
      auto* player = as_player(object.get());
      if (player == nullptr || player->is_dead() ||
          std::abs(player->x() - monster.x()) > 6 ||
          std::abs(player->y() - monster.y()) > 6) {
        continue;
      }
      LegacyRandom fallback_random;
      auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
      const auto damage = legacy_magic_defense_damage(*player, power, random, current_tick,
                                                      budgets_.tick_ms);
      const auto damage_result = player->apply_damage(damage, current_tick);
      if (player->is_dead()) {
        if (!try_legacy_revival(*player, dispatch, current_tick, now_ms)) {
          const auto death_clear = player->mark_dead(now_ms);
          dispatch_player_status_tick_result(*player, death_clear, dispatch, false);
          static_cast<void>(settle_player_death(*player, dispatch, current_tick, now_ms));
        }
      }
      if (damage_result.absorbed_damage > 0) {
        queue_packet(dispatch, player->session_id(),
                     make_health_spell_changed_packet(player->session_id(), *player));
      }
      ActorMail delayed;
      delayed.kind = ActorMailKind::legacy_delayed_effect;
      delayed.map_id = config_.id;
      delayed.actor_id = monster.id();
      delayed.target_actor_id = player->id();
      delayed.delayed_effect_kind = LegacyDelayedEffectKind::monster_struck;
      delayed.power = damage_result.hp_damage;
      delayed.magic_id = 1;
      delayed.payload = "RM_DELAYMAGIC";
      delayed_mail_wheel_.schedule(current_tick, legacy_delay_ms_to_ticks(600, budgets_.tick_ms),
                                   delayed);
      // 1/4 概率附加中毒（掉血毒 60s 或石化 5s）
      if (legacy_random_value(dispatch, "MonsterSpecial", "centipede_poison_gate", 4,
                              monster.id(), player->id(), {}, now_ms, current_tick) == 0) {
        const auto poison_kind =
            legacy_random_value(dispatch, "MonsterSpecial", "centipede_poison_kind", 3,
                monster.id(), player->id(), {}, now_ms, current_tick) != 0
                ? kPoisonDecHealth
                : kLegacyPoisonStone;
        const auto poison_ticks = legacy_delay_ms_to_ticks(
            poison_kind == kPoisonDecHealth ? 60000 : 5000, budgets_.tick_ms);
        static_cast<void>(player->apply_legacy_poison(
            poison_kind, poison_ticks, poison_kind == kPoisonDecHealth ? 3 : 0,
            legacy_delay_ms_to_ticks(2500, budgets_.tick_ms), monster.id(), current_tick));
        broadcast_legacy_char_status_changed(dispatch, *player);
      }
    }
    return true;
  }

  // --- 召唤者（Summoner） ---
  if (behavior == LegacyMonsterRaceBehavior::summoner) {
    if (auto* target = legacy_nearest_player_target(monster, current_tick, 9, false);
        target != nullptr) {
      monster.select_target(target->id(), now_ms);
    }
    if (monster.target_actor_id() != 0 && monster.legacy_attack_due_by_hit_time(now_ms)) {
      monster.mark_legacy_hit_time(now_ms);
      legacy_monster_summon_child(monster, dispatch, current_tick, now_ms);
    }
    return true;
  }

  // --- 其他特殊攻击类型：搜索目标 ---
  if (monster.target_actor_id() == 0) {
    const auto elapsed =
        static_cast<std::int64_t>(now_ms) -
        static_cast<std::int64_t>(monster.search_enemy_time_ms());
    if (elapsed > 1000) {
      monster.mark_search_enemy_time(now_ms);
      const auto search_range = behavior == LegacyMonsterRaceBehavior::spit ? 5
                                : behavior == LegacyMonsterRaceBehavior::fly_axe ? 11
                                                                                  : 7;
      if (auto* target = legacy_nearest_player_target(monster, current_tick, search_range, false);
          target != nullptr) {
        monster.select_target(target->id(), now_ms);
      }
    }
  }

  if (legacy_monster_special_attack_target(monster, dispatch, current_tick, now_ms)) {
    return true;
  }
  return false;
}

/**
 * @brief 怪物 AI 主调度函数（每 Tick 调用）
 *
 * @details 这是怪物 AI 的最高层决策入口，按优先级顺序执行：
 *
 *          1. 死亡检查：如果怪物已死亡，跳过所有 AI
 *          2. 神圣捕获检查（Holy Seize）：被捕获时失去目标
 *          3. 疯狂状态检查（Crazy）：疯狂时丢失目标
 *          4. 归巢判定：非宠物怪物超出活动范围时返回刷新点
 *          5. 主动搜索：非宠物怪物进行预搜索
 *          6. Think 逻辑：3 秒间隔的目标有效性检查和堆叠处理
 *          7. 行走 CD 和行走步数限制管理
 *          8. 特殊行为调度（如果怪物有特殊种族行为）
 *          9. 被动动物：丢失目标
 *          10. 攻击当前目标（如果目标在攻击范围内）
 *          11. 宠物跟随逻辑
 *          12. 归巢移动
 *          13. 目标追踪移动
 *          14. 闲逛（无目标时随机移动）
 *
 * @param monster      执行 AI 的怪物
 * @param dispatch     运行时消息分发器
 * @param current_tick 当前逻辑 Tick 数
 * @param now_ms       当前时间（毫秒）
 *
 * @note 这是一个深度优先的决策链，每个步骤返回 true 时后续步骤被跳过。
 */
void MapActor::handle_monster_ai(Monster& monster, RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms) {
  if (monster.is_dead()) {
    return;
  }
  // 神圣捕获状态：怪物被玩家用捕获技能定身
  if (monster.legacy_holy_seize_active(now_ms)) {
    monster.lose_target();
    monster.clear_target_xy();
    return;
  }
  // 疯狂状态：怪物发狂时丢失目标（可能会随机攻击）
  if (monster.legacy_crazy_active(now_ms)) {
    monster.lose_target();
  }

  // 归巢判定：非宠物、有活动范围限制且当前不在范围内
  const auto returning_home =
      !monster.is_slave() && !legacy_monster_has_special_behavior(monster.race_server()) &&
      monster.home_area() > 0 && !monster.inside_home_area();
  if (returning_home) {
    monster.lose_target();
    monster.set_target_xy(monster.home_x(), monster.home_y());
  } else if (!monster.is_slave()) {
    legacy_active_search(monster, dispatch, current_tick, now_ms);
  }
  if (legacy_monster_think(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  // 行走等待/CD 管理
  if (monster.walk_wait_mode() && monster.legacy_walk_wait_elapsed(now_ms)) {
    monster.set_walk_wait_mode(false);
  }
  if (monster.walk_wait_mode() || !monster.legacy_walk_due_by_walk_time(now_ms)) {
    return;
  }

  monster.mark_legacy_walk_time(now_ms);
  monster.increment_walk_cur_step();
  // 行走步数限制：走满 walk_step 后进入等待状态
  if (monster.walk_cur_step() > monster.walk_step()) {
    monster.reset_walk_cur_step();
    monster.begin_walk_wait(now_ms);
  }

  // 特殊行为怪物优先走特殊 AI
  if (legacy_monster_has_special_behavior(monster.race_server()) &&
      legacy_monster_special_run(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  // 被动动物：始终丢失目标（不会主动攻击）
  if (monster.ai_profile() == MonsterAiProfile::passive_animal) {
    monster.lose_target();
  }

  // 攻击当前目标
  if (monster.target_actor_id() != 0 && legacy_attack_target(monster, dispatch,
                                                             current_tick, now_ms)) {
    return;
  }

  // 宠物跟随
  if (handle_slave_follow(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  // 归巢移动
  if (returning_home) {
    static_cast<void>(legacy_goto_target_xy(monster, dispatch, current_tick, now_ms));
    return;
  }

  // 清除无效目标坐标
  if (monster.target_actor_id() == 0) {
    monster.clear_target_xy();
  }

  // 向目标坐标移动
  if (monster.has_target_xy()) {
    static_cast<void>(legacy_goto_target_xy(monster, dispatch, current_tick, now_ms));
    return;
  }

  // 闲逛
  legacy_wondering(monster, dispatch, current_tick, now_ms);
}
