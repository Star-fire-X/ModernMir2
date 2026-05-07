#pragma once

// Implementation detail for map_actor.cpp: monster, slave, reward, and AI members.
namespace {

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

std::pair<std::int32_t, std::int32_t> legacy_slave_back_position(const Player& master) {
  const auto back_dir =
      static_cast<std::uint8_t>((master.character().dir + 4) % 8);
  const auto [dx, dy] = direction_delta(back_dir);
  return {master.x() + dx, master.y() + dy};
}

}  // namespace

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
    monster.mark_legacy_death(now_ms);
    refresh_moving_object_state(monster, now_ms);
  }
  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (!is_legacy_visible_to(watcher, monster)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 target_died
                     ? make_death_packet(watcher.session_id(), monster)
                     : make_struck_packet(watcher.session_id(), monster, source_actor_id,
                                          tick_result.damage, true));
  });

  if (!target_died) {
    return false;
  }

  finalize_monster_death(monster.id(), source_actor_id, dispatch, current_tick);
  return true;
}

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

void MapActor::refresh_moving_object_state(const GameObject& object, std::uint64_t now_ms) {
  static_cast<void>(environment_.delete_from_map(object.x(), object.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 object.id()));
  static_cast<void>(environment_.add_moving_object(object.x(), object.y(), object.id(),
                                                   now_ms, moving_state_for(object)));
}

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

CharacterRecord MapActor::snapshot_player_with_slaves(Player& player, std::uint64_t now_ms) {
  auto snapshot = player.snapshot();
  snapshot.slaves = snapshot_owned_slaves(player, now_ms);
  return snapshot;
}

void MapActor::queue_save_player_character(RuntimeDispatch& dispatch, Player& player,
                                           std::uint64_t now_ms) {
  queue_save_character(dispatch, snapshot_player_with_slaves(player, now_ms));
}

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

void MapActor::restore_saved_slaves(Player& player, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  for (const auto& record : player.character().slaves) {
    if (record.name.empty() || record.remain_royalty_sec <= 0 || record.hp <= 0) {
      continue;
    }
    const auto royalty_time_ms =
        now_ms + static_cast<std::uint64_t>(record.remain_royalty_sec) * 1000ULL;
    auto spawn = build_slave_spawn_mail(record.name, player, player.x(), player.y(),
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
    handle_mail(*spawn, dispatch, current_tick, now_ms);
    if (objects_.contains(spawn->actor_id)) {
      player.add_slave_actor_id(spawn->actor_id);
      add_legacy_trace(dispatch, "LegacySlave", "restore", *spawn, current_tick,
                       now_ms, true, static_cast<std::int32_t>(spawn->actor_id & 0x7fffffff),
                       record.slave_exp_level, record.name);
    }
  }
}

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
      sync_visibility_after_actor_move(*slave, old_x, old_y, target_x, target_y, dispatch);
    }
  }
}

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

void MapActor::remove_slave_from_master(Monster& slave) {
  if (slave.master_actor_id() == 0) {
    return;
  }
  if (auto* master = find_player(slave.master_actor_id()); master != nullptr) {
    master->remove_slave_actor_id(slave.id());
  }
}

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
    monster.mark_legacy_death(now_ms);
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
    monster.mark_legacy_death(now_ms);
    refresh_moving_object_state(monster, now_ms);
    return true;
  }
  return false;
}

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
    monster->mark_legacy_death(now_ms);
  } else if (monster->death_time_ms() == 0) {
    monster->mark_legacy_death(now_ms);
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

  std::uint64_t reward_actor_id = 0;
  std::uint64_t drop_owner_actor_id = 0;
  if (monster->exp_hitter_id() != 0) {
    const auto exp_it = objects_.find(monster->exp_hitter_id());
    if (exp_it != objects_.end()) {
      if (auto* player_hitter = as_player(exp_it->second.get()); player_hitter != nullptr) {
        reward_actor_id = player_hitter->id();
        drop_owner_actor_id = player_hitter->id();
      } else if (auto* slave_hitter = as_monster(exp_it->second.get());
                 slave_hitter != nullptr && slave_hitter->master_actor_id() != 0) {
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
    const auto last_it = objects_.find(monster->last_hitter_id());
    if (last_it != objects_.end()) {
      if (auto* player_hitter = as_player(last_it->second.get()); player_hitter != nullptr) {
        reward_actor_id = player_hitter->id();
      }
    }
  }
  static_cast<void>(killer_actor_id);

  if (reward_actor_id != 0) {
    const auto attacker_it = objects_.find(reward_actor_id);
    if (attacker_it != objects_.end()) {
      if (auto* attacker = as_player(attacker_it->second.get()); attacker != nullptr) {
        award_monster_kill(*attacker, *monster, dispatch);
      }
    }
  }

  auto drop_position = [&](std::int32_t wide) -> std::pair<std::int32_t, std::int32_t> {
    std::optional<std::pair<std::int32_t, std::int32_t>> best;
    std::size_t best_count = 999;
    for (std::int32_t k = 1; k <= wide; ++k) {
      for (std::int32_t dy = -k; dy <= k; ++dy) {
        for (std::int32_t dx = -k; dx <= k; ++dx) {
          const auto try_x = monster->x() + dx;
          const auto try_y = monster->y() + dy;
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
    return {monster->x(), monster->y()};
  };

  auto prepare_death_drop = [&](GroundItem& ground_item) {
    ground_item.owner_actor_id = drop_owner_actor_id;
    ground_item.drop_time_ms = now_ms;
    if (ground_item.owner_actor_id != 0) {
      ground_item.ownership_expire_ms = now_ms + kLegacyDropOwnerMs;
    }
    ground_item.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
    ground_item.dropper_actor_id = monster->id();
    ground_item.dropper_name = monster->name();
    ground_item.death_drop = true;
  };

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
        existing->second.owner_actor_id = 0;
        existing->second.ownership_expire_ms = 0;
      }
      sync_visibility_after_item_change(existing->second.x, existing->second.y, dispatch,
                                        existing->second.id);
    } else {
      const auto item_id = ground_item.id;
      const auto item_x = ground_item.x;
      const auto item_y = ground_item.y;
      ++next_ground_item_id_;
      ground_items_[ground_item.id] = std::move(ground_item);
      sync_visibility_after_item_change(item_x, item_y, dispatch, item_id);
    }
    return true;
  };

  auto remaining_gold = monster->drop_gold();
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

  for (const auto& item : monster->drop_items()) {
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

  if (reward_actor_id != 0) {
    const auto attacker_it = objects_.find(reward_actor_id);
    if (attacker_it != objects_.end()) {
      if (auto* attacker = as_player(attacker_it->second.get()); attacker != nullptr) {
        static_cast<void>(trigger_map_quest(*attacker, monster->name(), {}, false, "monster_die",
                                            dispatch, current_tick, now_ms));
      }
    }
  }

}

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
  sync_all_player_visibility(dispatch);
}

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

bool MapActor::legacy_try_monster_walk(Monster& monster, std::uint8_t dir,
                                       RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick,
                                       std::uint64_t now_ms) {
  const auto [dx, dy] = direction_delta(dir);
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
  move_mail.dir = static_cast<std::uint8_t>(dir % 8);
  monster.set_dir(move_mail.dir);

  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;
  monster.on_mail(move_mail, context);

  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (!is_legacy_visible_to(watcher, monster)) {
      return;
    }
  queue_packet(dispatch, watcher.session_id(),
                 make_turn_like_packet(watcher.session_id(), kSmWalk, monster, false));
  });
  sync_visibility_after_actor_move(monster, old_x, old_y, monster.x(), monster.y(), dispatch);
  return true;
}

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
      const auto invalid_player =
          player_target != nullptr &&
          (player_target->is_dead() ||
           is_safe_zone(config_, player_target->x(), player_target->y()) ||
           player_target->legacy_transparent_active(current_tick));
      const auto invalid_monster =
          monster_target != nullptr &&
          (monster_target->is_dead() || monster_target->legacy_ghosted());
      if (target == nullptr || focus_expired || target_too_far ||
          invalid_player || invalid_monster) {
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

void MapActor::legacy_active_search(Monster& monster, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick, std::uint64_t now_ms) {
  if (!legacy_monster_has_pre_run_search(monster)) {
    return;
  }

  const auto elapsed =
      static_cast<std::int64_t>(now_ms) -
      static_cast<std::int64_t>(monster.search_enemy_time_ms());
  if (elapsed <= 8000 && (monster.target_actor_id() != 0 || elapsed <= 1000)) {
    return;
  }

  monster.mark_search_enemy_time(now_ms);
  static_cast<void>(legacy_monster_normal_attack(monster, dispatch, current_tick, now_ms));
}

bool MapActor::legacy_monster_normal_attack(Monster& monster, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick,
                                            std::uint64_t now_ms) {
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

    const auto distance = std::abs(player->x() - monster.x()) +
                          std::abs(player->y() - monster.y());
    if (distance >= best_distance) {
      continue;
    }
    nearest = player;
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
  if (target == nullptr || !is_attackable_target(*target)) {
    monster.lose_target();
    return false;
  }
  if (player_target != nullptr &&
      (player_target->is_dead() || is_safe_zone(config_, player_target->x(), player_target->y()) ||
       player_target->legacy_transparent_active(current_tick))) {
    monster.lose_target();
    return false;
  }
  if (monster_target != nullptr &&
      (monster_target->is_dead() || monster_target->legacy_ghosted() ||
       monster_target->id() == monster.id() ||
       monster_target->master_actor_id() == monster.master_actor_id() ||
       monster_target->id() == monster.master_actor_id())) {
    monster.lose_target();
    return false;
  }
  if (monster.is_slave() && player_target != nullptr &&
      player_target->id() == monster.master_actor_id()) {
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
    }
    return true;
  }

  monster.set_target_xy(target->x(), target->y());
  return false;
}

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

void MapActor::legacy_monster_temp_attack(Monster& monster, Player& target,
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
                          std::max(legacy_speed_point(target), 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  if (monster.accuracy_point() <= hit_roll) {
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

  const auto died = target.is_dead();
  if (died) {
    target.mark_dead(now_ms);
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
                          std::max(legacy_speed_point(target), 1), monster.id(),
                          target.id(), "Attack", now_ms, current_tick);
  if (monster.accuracy_point() <= hit_roll) {
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
      apply_legacy_monster_damage(objects_, target, damage, monster.id(), now_ms);
  if (applied_damage <= 0) {
    add_legacy_trace(dispatch, "MonsterCombat", "absorbed", trace_mail,
                     current_tick, now_ms, false, 0, 0, "StruckDamage");
    return;
  }

  const auto died = target.is_dead();
  for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
      return;
    }
    queue_packet(dispatch, watcher.session_id(),
                 died ? make_death_packet(watcher.session_id(), target, false)
                      : make_struck_packet(watcher.session_id(), target, monster.id(),
                                           applied_damage, false));
  });
  add_legacy_trace(dispatch, "MonsterCombat", died ? "death" : "struck",
                   trace_mail, current_tick, now_ms, true, 0, applied_damage,
                   died ? "SM_DEATH" : "SM_STRUCK");
  if (died) {
    finalize_monster_death(target.id(), monster.id(), dispatch, current_tick);
  }
}

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

  auto raw_dc = [&](std::string action) {
    const auto dc_min = std::max(monster.dc_min(), 0);
    const auto dc_max = std::max(dc_min, monster.dc_max());
    const auto roll = legacy_random_value(dispatch, "MonsterSpecial", std::move(action),
                                          std::max(1, dc_max - dc_min + 1), monster.id(),
                                          target->id(), {}, now_ms, current_tick);
    return dc_min + std::clamp(roll, 0, dc_max - dc_min);
  };

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

  auto apply_player_damage = [&](Player& player, std::int32_t damage,
                                 std::uint32_t delay_ms, bool magical,
                                 std::string label) {
    if (damage <= 0) {
      return 0;
    }
    const auto damage_result = player.apply_damage(damage, current_tick);
    const auto applied_damage = damage_result.hp_damage;
    if (player.is_dead()) {
      player.mark_dead(now_ms);
    }
    schedule_struck(player, applied_damage, delay_ms, magical, std::move(label));
    return applied_damage;
  };

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
      if (map_x < 0 || map_x >= 5 || map_y < 0 || map_y >= 5 ||
          kLegacySpitMap[dir % 8][map_y][map_x] == 0) {
        continue;
      }
      const auto hit_roll = legacy_random_value(dispatch, "MonsterSpecial", "spit_hit",
                                                std::max(legacy_speed_point(*player), 1),
                                                monster.id(), player->id(), {}, now_ms,
                                                current_tick);
      if (monster.accuracy_point() <= hit_roll) {
        continue;
      }
      LegacyRandom fallback_random;
      auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
      const auto damage = legacy_magic_defense_damage(*player, pwr, random, current_tick,
                                                      budgets_.tick_ms);
      const auto applied = apply_player_damage(*player, damage, 300, true, "RM_STRUCK");
      if (applied > 0 && monster.race_server() != kRcHighRiskSpider) {
        apply_poison(*player, kPoisonDecHealth, 30, 1, "spit_poison");
      }
    }
    return true;
  }

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
      const auto anti_roll = legacy_random_value(dispatch, "MonsterSpecial", "anti_magic",
                                                10, monster.id(), target->id(), {}, now_ms,
                                                current_tick);
      if (!legacy_anti_magic_pass(legacy_actor_anti_magic(*target), anti_roll)) {
        return true;
      }
    } else {
      const auto hit_roll = legacy_random_value(dispatch, "MonsterSpecial", "gas_hit",
                                                std::max(legacy_speed_point(*target), 1),
                                                monster.id(), target->id(), {}, now_ms,
                                                current_tick);
      if (monster.accuracy_point() <= hit_roll) {
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
        apply_poison(*target, kPoisonDecHealth, 30, 1, "toxic_poison");
      } else {
        apply_poison(*target, kLegacyPoisonStone, 5, 0, "gas_poison");
      }
    }
    return true;
  }

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
      if (monster.chain_shot_count() > 0 &&
          monster.chain_shot() >= monster.chain_shot_count() - 1) {
        if (legacy_random_value(dispatch, "MonsterSpecial", "chain_reset", 5,
                                monster.id(), target->id(), {}, now_ms, current_tick) == 0) {
          monster.set_chain_shot(0);
        }
        return true;
      }
      monster.increment_chain_shot();
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
    const auto delay = static_cast<std::uint32_t>(600 + cheb * 50);
    static_cast<void>(apply_player_damage(*target, damage, delay, false, "RM_FLYAXE"));
    return true;
  }

  return false;
}

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

  if (behavior == LegacyMonsterRaceBehavior::structure) {
    monster.lose_target();
    return true;
  }

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

  if (monster.hide_mode()) {
    const auto range = behavior == LegacyMonsterRaceBehavior::sculture_king ? 2
                       : monster.dig_up_range() > 0 ? monster.dig_up_range()
                                                     : 4;
    if (behavior == LegacyMonsterRaceBehavior::centipede &&
        static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(monster.appear_time_ms()) <=
            10000) {
      return true;
    }
    if (auto* target = legacy_nearest_player_target(monster, current_tick, range, false);
        target != nullptr) {
      monster.set_hide_mode(false);
      monster.set_appear_time_ms(now_ms);
      monster.select_target(target->id(), now_ms);
      refresh_moving_object_state(monster, now_ms);
      sync_all_player_visibility(dispatch);
      ActorMail trace_mail;
      trace_mail.kind = ActorMailKind::turn;
      trace_mail.map_id = config_.id;
      trace_mail.actor_id = monster.id();
      add_legacy_trace(dispatch, "MonsterSpecial", "dig_up", trace_mail,
                       current_tick, now_ms, true, monster.race_server(), 0, "RM_DIGUP");
    }
    return true;
  }

  if (behavior == LegacyMonsterRaceBehavior::stick_hide ||
      behavior == LegacyMonsterRaceBehavior::sculture_king) {
    auto* target = find_player(monster.target_actor_id());
    const auto down_range = monster.dig_down_range() > 0 ? monster.dig_down_range() : 4;
    if (target == nullptr || target->is_dead() ||
        std::abs(target->x() - monster.x()) > down_range ||
        std::abs(target->y() - monster.y()) > down_range) {
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

  if (behavior == LegacyMonsterRaceBehavior::centipede) {
    if (static_cast<std::int64_t>(now_ms) - static_cast<std::int64_t>(monster.appear_time_ms()) <=
        3000) {
      return true;
    }
    if (auto* any_target = legacy_nearest_player_target(monster, current_tick, 6, false);
        any_target == nullptr) {
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
        player->mark_dead(now_ms);
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

  if (legacy_monster_special_attack_target(monster, dispatch, current_tick, now_ms)) {
    return true;
  }
  return false;
}

void MapActor::handle_monster_ai(Monster& monster, RuntimeDispatch& dispatch,
                                 std::uint64_t current_tick, std::uint64_t now_ms) {
  if (monster.is_dead()) {
    return;
  }

  if (!monster.is_slave()) {
    legacy_active_search(monster, dispatch, current_tick, now_ms);
  }
  if (legacy_monster_think(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  if (monster.walk_wait_mode() && monster.legacy_walk_wait_elapsed(now_ms)) {
    monster.set_walk_wait_mode(false);
  }
  if (monster.walk_wait_mode() || !monster.legacy_walk_due_by_walk_time(now_ms)) {
    return;
  }

  monster.mark_legacy_walk_time(now_ms);
  monster.increment_walk_cur_step();
  if (monster.walk_cur_step() > monster.walk_step()) {
    monster.reset_walk_cur_step();
    monster.begin_walk_wait(now_ms);
  }

  if (legacy_monster_has_special_behavior(monster.race_server()) &&
      legacy_monster_special_run(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  if (monster.ai_profile() == MonsterAiProfile::passive_animal) {
    monster.lose_target();
  }

  if (monster.target_actor_id() != 0 && legacy_attack_target(monster, dispatch,
                                                             current_tick, now_ms)) {
    return;
  }

  if (handle_slave_follow(monster, dispatch, current_tick, now_ms)) {
    return;
  }

  if (monster.target_actor_id() == 0) {
    monster.clear_target_xy();
  }

  if (monster.has_target_xy()) {
    static_cast<void>(legacy_goto_target_xy(monster, dispatch, current_tick, now_ms));
    return;
  }

  legacy_wondering(monster, dispatch, current_tick, now_ms);
}

