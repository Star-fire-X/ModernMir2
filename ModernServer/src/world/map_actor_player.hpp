#pragma once

// Implementation detail for map_actor.cpp: player lifecycle and status members.
void MapActor::dispatch_legacy_run_notice(Player& player, RuntimeDispatch& dispatch,
                                          std::uint64_t now_ms) {
  if (player.legacy_state() != LegacyPlayerState::notice_pending) {
    return;
  }
  dispatch.audit_events.push_back(
      AuditEvent{"world.run_notice", player.character().account_id + ":" +
                                         player.character().character_name,
                 config_.id});
  player.mark_legacy_notice_done(now_ms);
}

void MapActor::dispatch_legacy_initialize(Player& player, RuntimeDispatch& dispatch,
                                          std::uint64_t now_ms) {
  if (player.legacy_state() != LegacyPlayerState::initialize_pending) {
    return;
  }

  player.refresh_derived_state(item_configs_);
  player.set_in_safe_zone(is_safe_zone(config_, player.x(), player.y()));

  if (!player.character().birth_items_granted) {
    bool starter_items_changed = false;
    bool bag_empty = true;
    for (const auto& item : player.character().bag_items) {
      if (item.index != 0) {
        bag_empty = false;
        break;
      }
    }
    bool equipped_empty = true;
    for (const auto& item : player.character().equipped_items) {
      if (item.index != 0) {
        equipped_empty = false;
        break;
      }
    }
    if (player.character().ability.exp == 0 && bag_empty && equipped_empty) {
      const auto grant_item = [&](std::string_view item_name) {
        const auto* item_config = find_item_config_by_name_or_id(item_configs_, item_name);
        if (item_config == nullptr) {
          return;
        }
        LegacyUserItem item;
        item.make_index = allocate_make_index();
        item.index = static_cast<std::uint16_t>(std::clamp(item_config->id, 0, 65535));
        item.dura_max = static_cast<std::uint16_t>(
            std::clamp(item_config->dura_max > 0 ? item_config->dura_max : 1000, 0, 65535));
        item.dura = item.dura_max;
        if (player.can_add_bag_item(item, item_configs_) && player.has_free_bag_slot()) {
          static_cast<void>(player.add_bag_item(item));
          starter_items_changed = true;
          queue_packet(dispatch, player.session_id(),
                       make_add_item_packet(player.session_id(), player.id(), item, item_configs_));
        }
      };
      grant_item("蜡烛");
      grant_item("金创药(小量)");
      grant_item("木剑");
      if (player.character().sex == 0) {
        grant_item("布衣(男)");
      } else {
        grant_item("布衣(女)");
      }
    }
    if (starter_items_changed) {
      player.refresh_derived_state(item_configs_);
    }
    player.mark_birth_items_granted();
    queue_save_character(dispatch, player);
  }

  dispatch_login_sequence(dispatch, player, config_, item_configs_, magic_configs_,
                          area_state_mask(config_, player.x(), player.y()));

  sync_player_visibility(player, dispatch, true);
  sync_all_player_visibility(dispatch);

  dispatch.audit_events.push_back(
      AuditEvent{"world.initialize", player.character().account_id + ":" +
                                         player.character().character_name,
                 config_.id});
  player.mark_legacy_initialize_done(now_ms);
}

void MapActor::dispatch_legacy_close(Player& player, RuntimeDispatch& dispatch) {
  const auto actor_id = player.id();
  const auto now_ms = player.legacy_ghost_time_ms() != 0 ? player.legacy_ghost_time_ms()
                                                         : static_cast<std::uint64_t>(tick_count_ms());
  cancel_trade_for(actor_id, dispatch, true);
  queue_save_player_character(dispatch, player, now_ms);
  detach_owned_slaves(player, dispatch, now_ms, true);
  queue_force_disconnect(dispatch, player.session_id(), "legacy_player_closed");
  static_cast<void>(environment_.delete_from_map(player.x(), player.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player.id()));
  player.mark_legacy_closed();
  objects_.erase(actor_id);
}

void MapActor::trace_player_operate(RuntimeDispatch& dispatch, const Player& player,
                                    std::string action, std::uint64_t current_tick,
                                    std::uint64_t now_ms, bool success,
                                    std::int32_t value, std::string label) const {
  LegacyRuntimeTrace trace;
  trace.stage = "PlayerOperate";
  trace.action = std::move(action);
  trace.map_id = config_.id;
  trace.object_name = player.name();
  trace.actor_id = player.id();
  trace.now_ms = now_ms;
  trace.current_tick = current_tick;
  trace.label = std::move(label);
  trace.value = value;
  trace.success = success;
  dispatch.legacy_traces.push_back(std::move(trace));
}

void MapActor::handle_player_health_spell_tick(Player& player, RuntimeDispatch& dispatch,
                                               std::uint64_t current_tick) {
  const auto tick_result = player.tick_legacy_health_spell(current_tick);
  if (!tick_result.changed) {
    return;
  }
  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
}

void MapActor::handle_player_status_effects(Player& player, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick) {
  const auto tick_result = player.tick_status_effects(current_tick);
  if (tick_result.damage <= 0 && tick_result.heal <= 0 && tick_result.absorbed_damage <= 0 &&
      !tick_result.shield_expired && !tick_result.ability_changed &&
      !tick_result.legacy_status_changed) {
    return;
  }

  if (tick_result.damage > 0) {
    auto died = player.is_dead();
    if (died) {
      died = !try_legacy_revival(
          player, dispatch, current_tick,
          current_tick * static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1)));
    }
    if (died) {
      const auto now_ms = current_tick *
                          static_cast<std::uint64_t>(
                              std::max<std::uint32_t>(budgets_.tick_ms, 1));
      const auto death_clear = player.mark_dead(now_ms);
      dispatch_player_status_tick_result(player, death_clear, dispatch, false);
      static_cast<void>(settle_player_death(player, dispatch, current_tick, now_ms));
    }
    for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
      if (watcher.id() != player.id() && !is_legacy_visible_to(watcher, player)) {
        return;
      }
      queue_packet(dispatch, watcher.session_id(),
                   died ? make_death_packet(watcher.session_id(), player, watcher.id() == player.id())
                        : make_struck_packet(watcher.session_id(), player, tick_result.source_actor_id,
                                             tick_result.damage, true));
    });
  }

  dispatch_player_status_tick_result(player, tick_result, dispatch, true);
  if (tick_result.shield_broken) {
    notify_player_and_watchers(dispatch, player, make_shield_break_self_notice(tick_result.shield_name),
                               make_shield_break_watcher_notice(player, tick_result.shield_name));
  }
  if (tick_result.shield_expired) {
    notify_player_and_watchers(dispatch, player, make_shield_fade_self_notice(tick_result.shield_name),
                               make_shield_fade_watcher_notice(player, tick_result.shield_name));
  }
}

void MapActor::legacy_operate_player_running(std::uint64_t actor_id, Player& player,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms,
                                             bool persistence_overloaded,
                                             std::size_t player_input_budget_per_tick) {
  if (!player.legacy_due(now_ms)) {
    return;
  }

  trace_player_operate(dispatch, player, "pre_periodic", current_tick, now_ms);
  sync_area_state(dispatch, config_, player);
  sync_player_visibility(player, dispatch, false);

  trace_player_operate(dispatch, player, "operate_timers", current_tick, now_ms);

  trace_player_operate(dispatch, player, "health_spell", current_tick, now_ms);
  handle_player_health_spell_tick(player, dispatch, current_tick);

  trace_player_operate(dispatch, player, "status", current_tick, now_ms);
  handle_player_status_effects(player, dispatch, current_tick);

  trace_player_operate(dispatch, player, "messages", current_tick, now_ms,
                       player.legacy_has_commands(),
                       static_cast<std::int32_t>(player.legacy_inbox_size()));
  std::size_t processed_messages = 0;
  while (player_input_budget_per_tick == 0 ||
         processed_messages < player_input_budget_per_tick) {
    auto command = player.pop_legacy_command();
    if (!command.has_value()) {
      break;
    }
    ++processed_messages;
    handle_mail(command->mail, dispatch, current_tick, now_ms, true);
    auto* current_player = find_player(actor_id);
    if (current_player == nullptr) {
      return;
    }
  }

  auto* current_player = find_player(actor_id);
  if (current_player == nullptr) {
    return;
  }
  if (current_player->legacy_has_commands()) {
    trace_player_operate(dispatch, *current_player, "messages_budget_exhausted", current_tick,
                         now_ms, false,
                         static_cast<std::int32_t>(current_player->legacy_inbox_size()));
  }
  trace_player_operate(dispatch, *current_player, "post_operate", current_tick, now_ms);
  sync_player_visibility(*current_player, dispatch, false);

  if (current_player->is_dead() && current_player->death_time_ms() != 0 &&
      now_ms > current_player->death_time_ms() + kPlayerCorpseMs) {
    static_cast<void>(environment_.delete_from_map(current_player->x(), current_player->y(),
                                                   LegacyMapObjectShape::moving_object,
                                                   current_player->id()));
    current_player->mark_legacy_ghost(now_ms);
    queue_save_player_character(dispatch, *current_player, now_ms);
    return;
  }

  constexpr std::uint64_t kLegacyAutoSaveMs = 15ULL * 60ULL * 1000ULL;
  if (!persistence_overloaded &&
      now_ms > current_player->legacy_last_save_time_ms() + kLegacyAutoSaveMs) {
    queue_save_player_character(dispatch, *current_player, now_ms);
    current_player->mark_legacy_autosaved(now_ms);
  }
  current_player->mark_legacy_running_time(now_ms);
}
