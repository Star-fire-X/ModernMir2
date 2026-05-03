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
  queue_save_player_character(dispatch, player, now_ms);
  detach_owned_slaves(player, dispatch, now_ms, true);
  queue_force_disconnect(dispatch, player.session_id(), "legacy_player_closed");
  static_cast<void>(environment_.delete_from_map(player.x(), player.y(),
                                                 LegacyMapObjectShape::moving_object,
                                                 player.id()));
  player.mark_legacy_closed();
  objects_.erase(actor_id);
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
    const auto died = player.is_dead();
    if (died) {
      player.mark_dead(current_tick * static_cast<std::uint64_t>(std::max<std::uint32_t>(budgets_.tick_ms, 1)));
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

  queue_packet(dispatch, player.session_id(),
               make_health_spell_changed_packet(player.session_id(), player));
  if (tick_result.legacy_status_changed) {
    broadcast_legacy_char_status_changed(dispatch, player);
  }
  if (tick_result.ability_changed) {
    queue_packet(dispatch, player.session_id(),
                 make_ability_packet(player.session_id(), player.character()));
    queue_packet(dispatch, player.session_id(),
                 make_sub_ability_packet(player.session_id(), player));
  }
  if (tick_result.shield_broken) {
    notify_player_and_watchers(dispatch, player, make_shield_break_self_notice(tick_result.shield_name),
                               make_shield_break_watcher_notice(player, tick_result.shield_name));
  }
  if (tick_result.shield_expired) {
    notify_player_and_watchers(dispatch, player, make_shield_fade_self_notice(tick_result.shield_name),
                               make_shield_fade_watcher_notice(player, tick_result.shield_name));
  }
}

