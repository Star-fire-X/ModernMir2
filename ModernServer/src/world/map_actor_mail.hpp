#pragma once

// Implementation detail for map_actor.cpp: ActorMail dispatch member.
void MapActor::handle_mail(const ActorMail& mail, RuntimeDispatch& dispatch,
                           std::uint64_t current_tick, std::uint64_t now_ms,
                           bool from_legacy_operate) {
  MapContext context;
  context.tick = current_tick;
  context.map_id = config_.id;
  context.dispatch = &dispatch;
  context.items = &item_configs_;
  context.magics = &magic_configs_;

  auto reject_trade_locked_item_change = [&](Player* player) {
    if (player == nullptr || trade_session_for(player->id()) == nullptr) {
      return false;
    }
    queue_system_notice(dispatch, *player, "Finish trade first.");
    return true;
  };
  auto reject_npc_modal_item_change = [&](Player* player) {
    if (player == nullptr || player->legacy_npc_item_mode() == LegacyNpcItemMode::none) {
      return false;
    }
    queue_system_notice(dispatch, *player, "Close NPC dialog first.");
    return true;
  };
  auto npc_mode_allows = [](Player* player, LegacyNpcItemMode mode, std::uint64_t actor_id) {
    return player == nullptr || player->legacy_npc_item_mode() == LegacyNpcItemMode::none ||
           (player->legacy_npc_item_mode() == mode &&
            player->legacy_npc_item_actor_id() == actor_id);
  };

  if (!from_legacy_operate && is_legacy_player_command(mail.kind)) {
    static_cast<void>(enqueue_legacy_player_command(mail, now_ms));
    return;
  }

  switch (mail.kind) {
    case ActorMailKind::spawn_player:
    case ActorMailKind::spawn_monster:
    case ActorMailKind::spawn_npc: {
      if (mail.kind == ActorMailKind::spawn_monster && !mail.legacy_spawn_group) {
        monster_spawn_templates_[mail.actor_id] = MonsterSpawnTemplate{mail};
      }
      auto object = make_object(mail);
      if (mail.kind == ActorMailKind::spawn_monster) {
        if (auto* monster = as_monster(object.get()); monster != nullptr) {
          monster->set_dir(mail.dir);
          if (mail.current_hp > 0 || mail.current_mp > 0) {
            monster->set_hp_mp(mail.current_hp > 0 ? mail.current_hp : monster->hp(),
                               mail.current_mp >= 0 ? mail.current_mp : monster->mp());
          }
          const auto walk_offset =
              legacy_random_ != nullptr ? static_cast<std::uint64_t>(legacy_random_->random(3000))
                                        : 0ULL;
          const auto hit_offset =
              legacy_random_ != nullptr ? static_cast<std::uint64_t>(legacy_random_->random(3000))
                                        : 0ULL;
          monster->initialize_legacy_ai_timers(now_ms, walk_offset, hit_offset);
          if (mail.target_actor_id != 0) {
            monster->select_target(mail.target_actor_id, now_ms);
          }
          if (mail.monster_has_target_xy) {
            monster->set_target_xy(mail.monster_target_x, mail.monster_target_y);
          }
        }
      }
      if (!environment_.add_moving_object(object->x(), object->y(), object->id(), now_ms,
                                          moving_state_for(*object))) {
        if (mail.kind == ActorMailKind::spawn_monster && !mail.legacy_spawn_group) {
          monster_spawn_templates_.erase(mail.actor_id);
        }
        break;
      }
      schedule_actor(current_tick, *object);
      objects_[mail.actor_id] = std::move(object);
      if (mail.kind == ActorMailKind::spawn_player) {
        auto* player = find_player(mail.actor_id);
        if (player != nullptr) {
          player->set_legacy_name_color(mail.legacy_name_color);
          player->set_legacy_group_id(mail.legacy_group_id);
          player->restore_legacy_buffs_from_transfer(mail.legacy_buffs, current_tick);
          player->refresh_derived_state(item_configs_);
          player->set_in_safe_zone(is_safe_zone(config_, player->x(), player->y()));
          restore_saved_slaves(*player, dispatch, current_tick, now_ms);
        }
      } else if (mail.kind == ActorMailKind::spawn_monster) {
        if (mail.master_actor_id != 0 && mail.monster_is_slave) {
          if (auto* master = find_player(mail.master_actor_id); master != nullptr) {
            master->add_slave_actor_id(mail.actor_id);
          }
        }
        sync_all_player_visibility(dispatch, now_ms);
      }
      break;
    }
    case ActorMailKind::system_notice: {
      if (auto* player = find_player(mail.actor_id); player != nullptr && !mail.payload.empty()) {
        queue_system_notice(dispatch, *player, mail.payload);
      } else if (player == nullptr && mail.retry_count < kCrossMapSyncRetryLimit &&
                 !mail.payload.empty()) {
        auto retry_mail = mail;
        ++retry_mail.retry_count;
        delayed_mail_wheel_.schedule(current_tick, 1, std::move(retry_mail));
      }
      break;
    }
    case ActorMailKind::guild_membership_sync: {
      if (auto* player = find_player(mail.actor_id); player != nullptr) {
        if (mail.character.guild_name.empty()) {
          player->clear_guild_membership();
        } else {
          player->set_guild_membership(mail.character.guild_name, mail.character.guild_title);
        }
        queue_save_character(dispatch, *player);
        if (!mail.payload.empty()) {
          queue_system_notice(dispatch, *player, mail.payload);
        }
      } else if (player == nullptr && mail.retry_count < kCrossMapSyncRetryLimit) {
        auto retry_mail = mail;
        ++retry_mail.retry_count;
        delayed_mail_wheel_.schedule(current_tick, 1, std::move(retry_mail));
      } else if (player == nullptr && !mail.character.account_id.empty()) {
        queue_save_character(dispatch, mail.character);
      }
      break;
    }
    case ActorMailKind::group_membership_sync: {
      if (auto* player = find_player(mail.actor_id); player != nullptr) {
        player->set_legacy_group_id(mail.legacy_group_id);
      } else if (player == nullptr && mail.retry_count < kCrossMapSyncRetryLimit) {
        auto retry_mail = mail;
        ++retry_mail.retry_count;
        delayed_mail_wheel_.schedule(current_tick, 1, std::move(retry_mail));
      }
      break;
    }
    case ActorMailKind::despawn: {
      auto it = objects_.find(mail.actor_id);
      if (it != objects_.end()) {
        if (auto* player = as_player(it->second.get()); player != nullptr) {
          cancel_trade_for(mail.actor_id, dispatch, true);
          PersistRequest request;
          request.kind = PersistRequestKind::save_character;
          request.account_id = player->character().account_id;
          request.character_name = player->character().character_name;
          request.character = player->snapshot();
          dispatch.persist_requests.push_back(std::move(request));
        }
        static_cast<void>(environment_.delete_from_map(
            it->second->x(), it->second->y(), LegacyMapObjectShape::moving_object,
            it->second->id()));
      }
      remove_actor_from_visibility(mail.actor_id, dispatch);
      objects_.erase(mail.actor_id);
      break;
    }
    case ActorMailKind::transfer: {
      if (const auto it = objects_.find(mail.actor_id); it != objects_.end()) {
        if (as_player(it->second.get()) != nullptr) {
          cancel_trade_for(mail.actor_id, dispatch, true);
        }
        static_cast<void>(environment_.delete_from_map(
            it->second->x(), it->second->y(), LegacyMapObjectShape::moving_object,
            it->second->id()));
        remove_actor_from_visibility(mail.actor_id, dispatch);
        objects_.erase(it);
      }
      dispatch.cross_map_mails.push_back(mail);
      break;
    }
    case ActorMailKind::query_username: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it != objects_.end() && target_it != objects_.end()) {
        if (const auto* requester = as_player(requester_it->second.get()); requester != nullptr) {
          queue_packet(dispatch, requester->session_id(),
                       make_username_packet(requester->session_id(), target_it->second->id(),
                                            actor_name(*target_it->second)));
        }
      }
      break;
    }
    case ActorMailKind::click_npc: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (trade_session_for(requester->id()) != nullptr) {
        break;
      }
      if (legacy_execute_npc_script(*requester, *merchant, "@main", dispatch, current_tick,
                                    now_ms)) {
        break;
      }
      if (merchant->supports_buy()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::buy, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_goods_list_packet(requester->session_id(), target_it->second->id(),
                                                 *merchant, item_configs_));
      } else if (merchant->supports_storage()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::storage, target_it->second->id());
        const auto storage_count = static_cast<std::uint16_t>(std::count_if(
            requester->character().storage_items.begin(), requester->character().storage_items.end(),
            [](const LegacyUserItem& item) { return !is_empty(item); }));
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_storage_packet(requester->session_id(), target_it->second->id(),
                                                   storage_count));
        queue_packet(dispatch, requester->session_id(),
                     make_save_item_list_packet(requester->session_id(), target_it->second->id(),
                                                *requester, item_configs_));
      } else if (merchant->supports_sell()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::sell, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_sell_packet(requester->session_id(), target_it->second->id()));
      } else if (merchant->supports_repair()) {
        requester->set_legacy_repair_mode(LegacyRepairMode::normal);
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::repair, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_repair_packet(requester->session_id(), target_it->second->id()));
      }
      break;
    }
    case ActorMailKind::merchant_select: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (trade_session_for(requester->id()) != nullptr) {
        break;
      }

      const auto lowered_payload = util::lower_copy(mail.payload);
      const auto script_handled = legacy_execute_npc_script(
          *requester, *merchant, mail.payload, dispatch, current_tick, now_ms);
      const auto uses_existing_business =
          legacy_script_action_uses_existing_business(lowered_payload, *merchant);
      if (script_handled && !uses_existing_business) {
        break;
      }
      if (mail.payload == "@guild_menu" && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                 build_guild_service_dialog_text(
                                                     *requester, guild_castle_snapshot_),
                                                 item_configs_)));
        break;
      }
      if (mail.payload == "@guild_info" && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                build_guild_info_dialog_text(
                                                    *requester, guild_castle_snapshot_),
                                                item_configs_)));
        break;
      }
      if (mail.payload == "@guild_create_menu" && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                build_guild_create_menu_dialog_text(
                                                    *requester, guild_castle_snapshot_),
                                                item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_create_confirm" ||
           util::starts_with(lowered_payload, "@guild_create_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_create_confirm_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 util::trim(mail.payload.substr(
                                     std::string("@guild_create_confirm").size()))),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@guild_directory") && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_directory_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_dialog_page(mail.payload, "@guild_directory")),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_browse" ||
           util::starts_with(lowered_payload, "@guild_browse ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_browse_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_guild_browse_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_roster" ||
           util::starts_with(lowered_payload, "@guild_roster ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_roster_dialog_text(
                                 guild_castle_snapshot_,
                                 parse_guild_browse_list_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_applicant_roster" ||
           util::starts_with(lowered_payload, "@guild_applicant_roster ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_applicant_roster_dialog_text(
                                 guild_castle_snapshot_,
                                 parse_guild_browse_list_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@guild_my_applications") &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_my_applications_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_dialog_page(mail.payload, "@guild_my_applications")),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_apply_status" ||
           util::starts_with(lowered_payload, "@guild_apply_status ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_apply_status_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 util::trim(mail.payload.substr(std::string("@guild_apply_status").size()))),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_apply_confirm" ||
           util::starts_with(lowered_payload, "@guild_apply_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_apply_confirm_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 util::trim(mail.payload.substr(std::string("@guild_apply_confirm").size()))),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_withdraw_confirm" ||
           util::starts_with(lowered_payload, "@guild_withdraw_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_withdraw_confirm_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 util::trim(mail.payload.substr(std::string("@guild_withdraw_confirm").size()))),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@guild_members") && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_members_dialog_text(*requester, guild_castle_snapshot_,
                                                             parse_dialog_page(mail.payload,
                                                                               "@guild_members")),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@guild_applicants") && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_applicants_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_dialog_page(mail.payload, "@guild_applicants")),
                               item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_applicant" ||
           util::starts_with(lowered_payload, "@guild_applicant ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_applicant_review_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_applicant_dialog_target(mail.payload)),
                              item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_approve_confirm" ||
           util::starts_with(lowered_payload, "@guild_approve_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_approve_confirm_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_applicant_dialog_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_reject_confirm" ||
           util::starts_with(lowered_payload, "@guild_reject_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_reject_confirm_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_applicant_dialog_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_member" ||
           util::starts_with(lowered_payload, "@guild_member ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_member_manage_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_member_dialog_target(mail.payload)),
                               item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_kick_confirm" ||
           util::starts_with(lowered_payload, "@guild_kick_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_kick_confirm_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_member_dialog_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_transfer_confirm" ||
           util::starts_with(lowered_payload, "@guild_transfer_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_transfer_confirm_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_member_dialog_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_member_titles" ||
           util::starts_with(lowered_payload, "@guild_member_titles ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                              build_guild_member_titles_dialog_text(
                                  *requester, objects_, guild_castle_snapshot_,
                                  parse_guild_member_title_dialog_target(mail.payload)),
                              item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_title_confirm" ||
           util::starts_with(lowered_payload, "@guild_title_confirm ")) &&
          merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_title_confirm_dialog_text(
                                 *requester, objects_, guild_castle_snapshot_,
                                 parse_guild_title_confirm_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if (mail.payload == "@guild_leave_confirm" && merchant->supports_guild()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                build_guild_leave_confirm_dialog_text(
                                                    *requester, guild_castle_snapshot_),
                                                item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_approve_exec" ||
           util::starts_with(lowered_payload, "@guild_approve_exec ")) &&
          merchant->supports_guild()) {
        const auto target = parse_guild_applicant_dialog_target(mail.payload);
        const auto result = execute_guild_approve_action(*requester, objects_, guild_castle_snapshot_,
                                                         dispatch, target.applicant_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Approval Result", result,
                                 "@guild_applicants " + std::to_string(static_cast<int>(target.page))),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_create_exec" ||
           util::starts_with(lowered_payload, "@guild_create_exec ")) &&
          merchant->supports_guild()) {
        const auto result =
            execute_guild_create_action(*requester, guild_castle_snapshot_, dispatch,
                                        util::trim(mail.payload.substr(
                                            std::string("@guild_create_exec").size())));
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Creation Result", result, "@guild_create_menu",
                                 "@guild_menu"),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_apply_exec" ||
           util::starts_with(lowered_payload, "@guild_apply_exec ")) &&
          merchant->supports_guild()) {
        const auto result =
            execute_guild_apply_action(*requester, objects_, guild_castle_snapshot_, dispatch,
                                       util::trim(mail.payload.substr(std::string("@guild_apply_exec").size())));
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text("Guild Application Result", result,
                                                                   "@guild_menu", "@guild_menu"),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_withdraw_exec" ||
           util::starts_with(lowered_payload, "@guild_withdraw_exec ")) &&
          merchant->supports_guild()) {
        const auto guild_name =
            util::trim(mail.payload.substr(std::string("@guild_withdraw_exec").size()));
        const auto result =
            execute_guild_withdraw_action(*requester, objects_, guild_castle_snapshot_, dispatch,
                                          guild_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Withdrawal Result", result, "@guild_menu", "@guild_menu"),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_reject_exec" ||
           util::starts_with(lowered_payload, "@guild_reject_exec ")) &&
          merchant->supports_guild()) {
        const auto target = parse_guild_applicant_dialog_target(mail.payload);
        const auto result = execute_guild_reject_action(*requester, objects_, guild_castle_snapshot_,
                                                        dispatch, target.applicant_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Rejection Result", result,
                                 "@guild_applicants " + std::to_string(static_cast<int>(target.page))),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_kick_exec" ||
           util::starts_with(lowered_payload, "@guild_kick_exec ")) &&
          merchant->supports_guild()) {
        const auto target = parse_guild_member_dialog_target(mail.payload);
        const auto result = execute_guild_kick_action(*requester, objects_, guild_castle_snapshot_,
                                                      dispatch, target.member_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Kick Result", result,
                                 "@guild_members " + std::to_string(static_cast<int>(target.page))),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_title_exec" ||
           util::starts_with(lowered_payload, "@guild_title_exec ")) &&
          merchant->supports_guild()) {
        const auto target = parse_guild_title_confirm_target(mail.payload);
        const auto result = execute_guild_title_action(*requester, objects_, guild_castle_snapshot_,
                                                       dispatch, target.member_name,
                                                       target.title_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Title Result", result,
                                 "@guild_member_titles " +
                                     std::to_string(static_cast<int>(target.member_page)) + " " +
                                     std::to_string(static_cast<int>(target.title_page)) + " " +
                                     target.member_name),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@guild_transfer_exec" ||
           util::starts_with(lowered_payload, "@guild_transfer_exec ")) &&
          merchant->supports_guild()) {
        const auto target = parse_guild_member_dialog_target(mail.payload);
        const auto result =
            execute_guild_transfer_action(*requester, objects_, guild_castle_snapshot_, dispatch,
                                          target.member_name);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text(
                                 "Guild Transfer Result", result,
                                 "@guild_members " + std::to_string(static_cast<int>(target.page))),
                             item_configs_)));
        break;
      }
      if (mail.payload == "@guild_leave_exec" && merchant->supports_guild()) {
        const auto result =
            execute_guild_leave_action(*requester, objects_, guild_castle_snapshot_, dispatch);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_guild_action_result_dialog_text("Guild Leave Result", result,
                                                                   "@guild_menu", "@guild_menu"),
                             item_configs_)));
        break;
      }
      if (mail.payload == "@castle_menu" && merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                 build_castle_service_dialog_text(
                                                     *requester, guild_castle_snapshot_),
                                                 item_configs_)));
        break;
      }
      if (mail.payload == "@castle_claim_confirm" && merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                build_castle_claim_confirm_dialog_text(
                                                    *requester, guild_castle_snapshot_),
                                                item_configs_)));
        break;
      }
      if (mail.payload == "@castle_show" && merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(*merchant, *requester, config_,
                                                castle_dialog_context_,
                                                build_castle_show_dialog_text(
                                                    *requester, guild_castle_snapshot_),
                                                item_configs_)));
        break;
      }
      if ((lowered_payload == "@castle_guild_browse" ||
           util::starts_with(lowered_payload, "@castle_guild_browse ")) &&
          merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_guild_browse_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_castle_guild_browse_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@castle_war_confirm" ||
           util::starts_with(lowered_payload, "@castle_war_confirm ")) &&
          merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_war_confirm_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_castle_war_confirm_target(mail.payload)),
                             item_configs_)));
        break;
      }
      if (mail.payload == "@castle_claim" && merchant->supports_castle()) {
        const auto result = execute_castle_claim(*requester, guild_castle_snapshot_, dispatch);
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_action_result_dialog_text("Castle Claim Result", result,
                                                                   "@castle_menu"),
                             item_configs_)));
        break;
      }
      if ((lowered_payload == "@castle_war" ||
           util::starts_with(lowered_payload, "@castle_war ")) &&
          merchant->supports_castle()) {
        const auto result =
            execute_castle_war(*requester, guild_castle_snapshot_, dispatch,
                               util::trim(mail.payload.substr(std::string("@castle_war").size())));
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_action_result_dialog_text(
                                 "Castle War Result", result, "@castle_menu"),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@castle_wars") && merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_wars_dialog_text(
                                 guild_castle_snapshot_,
                                 parse_dialog_page(mail.payload, "@castle_wars")),
                             item_configs_)));
        break;
      }
      if (util::starts_with(lowered_payload, "@castle_war_targets") &&
          merchant->supports_castle()) {
        queue_packet(dispatch, requester->session_id(),
                     make_merchant_say_packet(
                         requester->session_id(), target_it->second->id(), *merchant,
                         render_npc_dialog_text(
                             *merchant, *requester, config_, castle_dialog_context_,
                             build_castle_war_targets_dialog_text(
                                 *requester, guild_castle_snapshot_,
                                 parse_dialog_page(mail.payload, "@castle_war_targets")),
                             item_configs_)));
        break;
      }

      if (handle_guild_castle_business_command(*requester, objects_, mail.payload,
                                               guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }

      if (handle_castle_admin_command(*requester, mail.payload, guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }

      if (!script_handled) {
        if (const auto* dialog = find_npc_dialog_text(*merchant, mail.payload); dialog != nullptr) {
          queue_packet(dispatch, requester->session_id(),
                       make_merchant_say_packet(requester->session_id(), target_it->second->id(),
                                                *merchant,
                                                render_npc_dialog_text(*merchant, *requester,
                                                                       config_,
                                                                       castle_dialog_context_,
                                                                       *dialog,
                                                                       item_configs_)));
        } else if (mail.payload == "@main" && should_open_merchant_dialog(*merchant)) {
          queue_packet(dispatch, requester->session_id(),
                       make_merchant_say_packet(requester->session_id(), target_it->second->id(),
                                                *merchant,
                                              render_npc_dialog_text(*merchant, *requester,
                                                                     config_,
                                                                     castle_dialog_context_,
                                                                     build_merchant_dialog_text(*merchant),
                                                                     item_configs_)));
        }
      }

      if (lowered_payload == "@buy" && merchant->supports_buy()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::buy, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_goods_list_packet(requester->session_id(), target_it->second->id(),
                                                 *merchant, item_configs_));
      } else if (lowered_payload == "@sell" && merchant->supports_sell()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::sell, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_sell_packet(requester->session_id(), target_it->second->id()));
      } else if ((lowered_payload == "@repair" || lowered_payload == "@s_repair") &&
                 merchant->supports_repair()) {
        requester->set_legacy_repair_mode(lowered_payload == "@s_repair"
                                              ? LegacyRepairMode::special
                                              : LegacyRepairMode::normal);
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::repair, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_repair_packet(requester->session_id(), target_it->second->id()));
      } else if (lowered_payload == "@upgradenow" && merchant->supports_weapon_upgrade()) {
        if (!reject_trade_locked_item_change(requester)) {
          static_cast<void>(handle_weapon_upgrade_start(*requester, *merchant, dispatch,
                                                        current_tick, now_ms));
        }
      } else if (lowered_payload == "@getbackupgnow" && merchant->supports_weapon_upgrade()) {
        if (!reject_trade_locked_item_change(requester)) {
          static_cast<void>(handle_weapon_upgrade_get_back(*requester, *merchant, dispatch,
                                                           current_tick, now_ms));
        }
      } else if (lowered_payload == "@storage" && merchant->supports_storage()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::storage, target_it->second->id());
        const auto storage_count = static_cast<std::uint16_t>(std::count_if(
            requester->character().storage_items.begin(), requester->character().storage_items.end(),
            [](const LegacyUserItem& item) { return !is_empty(item); }));
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_storage_packet(requester->session_id(), target_it->second->id(),
                                                   storage_count));
      } else if (lowered_payload == "@getback" && merchant->supports_storage()) {
        requester->set_legacy_npc_item_mode(LegacyNpcItemMode::storage, target_it->second->id());
        queue_packet(dispatch, requester->session_id(),
                     make_save_item_list_packet(requester->session_id(), target_it->second->id(),
                                                *requester, item_configs_));
      }
      break;
    }
    case ActorMailKind::query_bag_items: {
      auto* requester = find_player(mail.actor_id);
      if (requester != nullptr) {
        requester->refresh_derived_state(item_configs_);
        queue_packet(dispatch, requester->session_id(),
                     make_bag_items_packet(requester->session_id(), *requester, item_configs_));
      }
      break;
    }
    case ActorMailKind::query_storage_items: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      const auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_storage() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      queue_packet(dispatch, requester->session_id(),
                   make_save_item_list_packet(requester->session_id(), target_it->second->id(),
                                              *requester, item_configs_));
      break;
    }
    case ActorMailKind::query_detail_goods: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      const auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_buy() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto detail_items =
          collect_detail_goods(*merchant, mail.payload, mail.item_make_index, item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_send_detail_goods_list_packet(requester->session_id(), target_it->second->id(),
                                                      mail.item_make_index, detail_items,
                                                      item_configs_, *merchant));
      break;
    }
    case ActorMailKind::query_repair_cost: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_repair() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto* item = requester->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      const auto cost = item != nullptr
                            ? compute_repair_cost(*item, item_configs_, *merchant,
                                                  requester->legacy_repair_mode())
                            : -1;
      queue_packet(dispatch, requester->session_id(),
                   make_send_repair_cost_packet(requester->session_id(), cost));
      break;
    }
    case ActorMailKind::query_sell_price: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_sell() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto* item = requester->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      const auto price =
          item != nullptr && can_sell_item(*merchant, *item, item_configs_)
              ? std::max(compute_buy_price(*item, item_configs_, merchant->merchant_price(item->index)),
                         0)
              : 0;
      queue_packet(dispatch, requester->session_id(),
                   make_send_buy_price_packet(requester->session_id(), price));
      break;
    }
    case ActorMailKind::drop_item: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead()) {
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                             mail.payload));
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                             mail.payload));
        break;
      }
      if (!player->legacy_item_change_ready(now_ms)) {
        add_legacy_trace(dispatch, "LegacyItem", "item_change_throttle", mail, current_tick,
                         now_ms, false, mail.item_make_index, 0, "drop_item");
        queue_packet(dispatch, player->session_id(),
                     make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                             mail.payload));
        break;
      }

      const auto* bag_item =
          player->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      if (bag_item == nullptr) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         mail.item_make_index, 0, "drop_item");
        queue_packet(dispatch, player->session_id(),
                     make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                             mail.payload));
        break;
      }
      auto dropped_item = *bag_item;
      if (const auto* config = find_item_config(item_configs_, bag_item->index); config != nullptr) {
        if (config->std_mode == 51) {
          add_legacy_trace(dispatch, "LegacyItem", "stdmode51_reject", mail, current_tick,
                           now_ms, false, mail.item_make_index, 0, "drop_item");
          queue_packet(dispatch, player->session_id(),
                       make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                               mail.payload));
          break;
        }
        if (config->std_mode == 40) {
          dropped_item.dura =
              clamp_dura_value(static_cast<std::int32_t>(dropped_item.dura) - 2000);
        }
      }

      GroundItem ground_item;
      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "drop_item");
      ground_item.id = next_ground_item_id_;
      ground_item.item = dropped_item;
      ground_item.name = item_name(*bag_item, item_configs_);
      ground_item.count = 1;
      ground_item.looks = item_looks(*bag_item, item_configs_);
      if (const auto* config = find_item_config(item_configs_, bag_item->index); config != nullptr) {
        ground_item.ani_count = config->ani_count;
      }
      ground_item.x = player->x();
      ground_item.y = player->y();
      ground_item.drop_time_ms = now_ms;
      ground_item.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
      ground_item.dropper_actor_id = player->id();
      ground_item.dropper_name = player->name();
      const auto add_result = environment_.add_item_object(
          ground_item.x, ground_item.y, ground_item.id, LegacyMapItemState{}, now_ms);
      if (!add_result.ok) {
        add_legacy_trace(dispatch, "LegacyItem", "map_reject", mail, current_tick, now_ms, false,
                         0, 0, "drop_item");
        queue_packet(dispatch, player->session_id(),
                     make_drop_result_packet(player->session_id(), false, mail.item_make_index,
                                             mail.payload));
        break;
      }
      ++next_ground_item_id_;

      const auto removed =
          player->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!removed.has_value()) {
        add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                         false, 0, 0, "drop_item");
        static_cast<void>(environment_.delete_from_map(
            ground_item.x, ground_item.y, LegacyMapObjectShape::item_object, ground_item.id));
        break;
      }
      ground_item.item = dropped_item;
      ground_items_[ground_item.id] = ground_item;
      player->refresh_derived_state(item_configs_);

      sync_visibility_after_item_change(ground_item.x, ground_item.y, dispatch, now_ms,
                                        ground_item.id);
      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), ground_item.item,
                                        item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(),
                   make_drop_result_packet(player->session_id(), true, removed->make_index,
                                           ground_item.name));
      queue_save_character(dispatch, *player);
      player->mark_legacy_item_change(now_ms);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       static_cast<std::int32_t>(ground_item.id), 0, "drop_item");
      break;
    }
    case ActorMailKind::drop_gold: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead() || !player->can_spend_gold(mail.amount)) {
        if (player != nullptr) {
          add_legacy_trace(dispatch, "LegacyItem", "gold_reject", mail, current_tick, now_ms,
                           false, mail.amount, 0, "drop_gold");
        }
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        add_legacy_trace(dispatch, "LegacyItem", "trade_locked", mail, current_tick, now_ms,
                         false, mail.amount, 0, "drop_gold");
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        add_legacy_trace(dispatch, "LegacyItem", "npc_modal_locked", mail, current_tick, now_ms,
                         false, mail.amount, 0, "drop_gold");
        break;
      }

      GroundItem ground_item;
      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true,
                       mail.amount, 0, "drop_gold");
      ground_item.id = next_ground_item_id_;
      ground_item.is_gold = true;
      ground_item.gold_amount = mail.amount;
      ground_item.name = "Gold";
      ground_item.count = mail.amount;
      ground_item.looks = gold_looks(mail.amount);
      ground_item.x = player->x();
      ground_item.y = player->y();
      ground_item.drop_time_ms = now_ms;
      ground_item.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
      ground_item.dropper_actor_id = player->id();
      ground_item.dropper_name = player->name();

      const auto add_result = environment_.add_item_object(
          ground_item.x, ground_item.y, ground_item.id,
          LegacyMapItemState{true, ground_item.gold_amount}, now_ms);
      if (!add_result.ok) {
        add_legacy_trace(dispatch, "LegacyItem", "map_reject", mail, current_tick, now_ms, false,
                         mail.amount, 0, "drop_gold");
        break;
      }

      if (add_result.merged) {
        auto existing = ground_items_.find(add_result.object_id);
        if (existing == ground_items_.end()) {
          GroundItem recovered_item = ground_item;
          recovered_item.id = add_result.object_id;
          recovered_item.gold_amount = add_result.merged_gold_amount;
          recovered_item.count = recovered_item.gold_amount;
          recovered_item.looks = gold_looks(recovered_item.gold_amount);
          recovered_item.owner_actor_id = 0;
          recovered_item.ownership_expire_ms = 0;
          recovered_item.dropper_actor_id = 0;
          recovered_item.dropper_name.clear();
          auto [recovered_it, _] =
              ground_items_.insert_or_assign(recovered_item.id, std::move(recovered_item));
          existing = recovered_it;
          add_legacy_trace(dispatch, "LegacyItem", "merge_state_repair", mail, current_tick,
                           now_ms, true, existing->second.gold_amount, 0, "drop_gold");
        }
        player->spend_gold(mail.amount);
        refresh_ground_item_ownership(existing->second, now_ms);
        const auto same_owner = existing->second.owner_actor_id == ground_item.owner_actor_id;
        const auto merged_total =
            add_result.merged_gold_amount > 0
                ? add_result.merged_gold_amount
                : existing->second.gold_amount + mail.amount;
        existing->second.gold_amount = merged_total;
        existing->second.count = existing->second.gold_amount;
        existing->second.looks = gold_looks(existing->second.gold_amount);
        existing->second.expire_time_ms = now_ms + kLegacyGroundItemExpireMs;
        if (!same_owner) {
          existing->second.owner_actor_id = 0;
          existing->second.ownership_expire_ms = 0;
        }
        ground_item = existing->second;
      } else {
        player->spend_gold(mail.amount);
        ++next_ground_item_id_;
        ground_items_[ground_item.id] = ground_item;
      }

      sync_visibility_after_item_change(ground_item.x, ground_item.y, dispatch, now_ms,
                                        ground_item.id);
      queue_packet(dispatch, player->session_id(),
                   make_gold_changed_packet(player->session_id(), player->character().gold));
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", add_result.merged ? "merged" : "success", mail,
                       current_tick, now_ms, true, ground_item.gold_amount, 0, "drop_gold");
      break;
    }
    case ActorMailKind::repair_item: {
      auto requester_it = objects_.find(mail.actor_id);
      if (requester_it == objects_.end()) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      if (requester == nullptr || requester->is_dead()) {
        break;
      }
      auto fail_repair = [&] {
        queue_packet(dispatch, requester->session_id(),
                     make_user_repair_result_packet(requester->session_id(), false, 0, 0, 0));
      };
      auto target_it = objects_.find(mail.target_actor_id);
      if (target_it == objects_.end() || target_it->second->kind() != GameObjectKind::npc) {
        fail_repair();
        break;
      }
      const auto* merchant = as_npc(target_it->second.get());
      if (merchant == nullptr || !merchant->supports_repair() ||
          !in_interaction_range(*requester, *target_it->second)) {
        fail_repair();
        break;
      }
      if (reject_trade_locked_item_change(requester)) {
        fail_repair();
        break;
      }
      if (!npc_mode_allows(requester, LegacyNpcItemMode::repair, target_it->second->id())) {
        fail_repair();
        break;
      }
      auto* item = requester->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      if (item == nullptr) {
        fail_repair();
        break;
      }
      const auto repair_mode = requester->legacy_repair_mode();
      const auto cost = compute_repair_cost(*item, item_configs_, *merchant, repair_mode);
      if (cost < 0 || (cost > 0 && !requester->can_spend_gold(cost))) {
        fail_repair();
        break;
      }
      requester->spend_gold(cost);
      if (repair_mode == LegacyRepairMode::normal) {
        const auto dura_gap =
            static_cast<std::int32_t>(item->dura_max) - static_cast<std::int32_t>(item->dura);
        if (dura_gap > 0) {
          item->dura_max = static_cast<std::uint16_t>(
              std::max(0, static_cast<std::int32_t>(item->dura_max) - dura_gap / 30));
        }
      }
      item->dura = item->dura_max;
      queue_packet(dispatch, requester->session_id(),
                   make_user_repair_result_packet(requester->session_id(), true,
                                                  requester->character().gold, item->dura,
                                                  item->dura_max));
      queue_packet(dispatch, requester->session_id(),
                   make_gold_changed_packet(requester->session_id(), requester->character().gold));
      queue_save_character(dispatch, *requester);
      break;
    }
    case ActorMailKind::sell_item: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_sell() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (reject_trade_locked_item_change(requester)) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      if (!npc_mode_allows(requester, LegacyNpcItemMode::sell, target_it->second->id())) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      const auto* sell_item = requester->bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (sell_item == nullptr || !can_sell_item(*merchant, *sell_item, item_configs_)) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      const auto price =
          compute_buy_price(*sell_item, item_configs_, merchant->merchant_price(sell_item->index));
      const auto new_gold = static_cast<std::int64_t>(requester->character().gold) + price;
      if (price < 0 || new_gold > kLegacyBagGold) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      const auto item = requester->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!item.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      requester->add_gold(price);
      add_merchant_goods(*merchant, *item, item_configs_);
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_user_sell_result_packet(requester->session_id(), true,
                                                requester->character().gold));
      queue_packet(dispatch, requester->session_id(),
                   make_weight_changed_packet(requester->session_id(), requester->character()));
      queue_save_character(dispatch, *requester);
      dispatch.persist_requests.push_back(make_save_merchant_state_request(*merchant));
      break;
    }
    case ActorMailKind::buy_item: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_buy() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (reject_trade_locked_item_change(requester)) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      if (!npc_mode_allows(requester, LegacyNpcItemMode::buy, target_it->second->id())) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      const auto merchant_item_index =
          find_merchant_item_index(*merchant, mail.payload, mail.item_make_index, item_configs_);
      if (!merchant_item_index.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 1, 0));
        break;
      }
      const auto item = merchant->merchant_items()[*merchant_item_index];
      const auto price = compute_merchant_sell_price(*merchant, item, item_configs_);
      if (!requester->can_add_bag_item(item, item_configs_) || !requester->has_free_bag_slot()) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      if (price <= 0 || !requester->can_spend_gold(price)) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 3, 0));
        break;
      }
      auto removed_item = take_merchant_item(*merchant, mail.payload, mail.item_make_index, item_configs_);
      if (!removed_item.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 1, 0));
        break;
      }
      requester->spend_gold(price);
      if (!requester->add_bag_item(*removed_item)) {
        requester->add_gold(price);
        auto& goods = merchant->merchant_items_mutable();
        const auto insert_at = std::min(*merchant_item_index, goods.size());
        goods.insert(goods.begin() + static_cast<std::ptrdiff_t>(insert_at), *removed_item);
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_add_item_packet(requester->session_id(), requester->id(), *removed_item, item_configs_));
      queue_packet(dispatch, requester->session_id(),
                   make_weight_changed_packet(requester->session_id(), requester->character()));
      queue_packet(dispatch, requester->session_id(),
                   make_buy_item_result_packet(requester->session_id(), true,
                                               requester->character().gold, removed_item->make_index));
      queue_save_character(dispatch, *requester);
      dispatch.persist_requests.push_back(make_save_merchant_state_request(*merchant));
      break;
    }
    case ActorMailKind::trade_try: {
      auto* requester = find_player(mail.actor_id);
      Player* target = nullptr;
      if (requester != nullptr) {
        for (auto& [object_id, object] : objects_) {
          if (object_id == requester->id()) {
            continue;
          }
          auto* candidate = as_player(object.get());
          if (candidate != nullptr && is_directly_in_front_of(*requester, *candidate)) {
            target = candidate;
            break;
          }
        }
      }
      if (requester == nullptr || target == nullptr || requester == target ||
          requester->is_dead() || target->is_dead()) {
        if (requester != nullptr) {
          queue_packet(dispatch, requester->session_id(),
                       make_deal_simple_packet(requester->session_id(), kSmDealTryFail));
        }
        break;
      }
      if (!mutually_facing(*requester, *target) ||
          trade_session_for(requester->id()) != nullptr ||
          trade_session_for(target->id()) != nullptr) {
        queue_packet(dispatch, requester->session_id(),
                     make_deal_simple_packet(requester->session_id(), kSmDealTryFail));
        break;
      }
      if (reject_npc_modal_item_change(requester)) {
        queue_packet(dispatch, requester->session_id(),
                     make_deal_simple_packet(requester->session_id(), kSmDealTryFail));
        break;
      }
      if (reject_npc_modal_item_change(target)) {
        queue_packet(dispatch, requester->session_id(),
                     make_deal_simple_packet(requester->session_id(), kSmDealTryFail));
        break;
      }
      TradeSession session;
      session.id = next_trade_session_id_++;
      session.first_actor_id = requester->id();
      session.second_actor_id = target->id();
      session.first.last_change_time_ms = now_ms;
      session.second.last_change_time_ms = now_ms;
      trade_session_by_actor_[session.first_actor_id] = session.id;
      trade_session_by_actor_[session.second_actor_id] = session.id;
      trade_sessions_[session.id] = std::move(session);
      queue_system_notice(dispatch, *requester, "Trade started with " +
                                               target->character().character_name + ".");
      queue_system_notice(dispatch, *target, "Trade started with " +
                                            requester->character().character_name + ".");
      queue_packet(dispatch, requester->session_id(),
                   make_deal_menu_packet(requester->session_id(),
                                         target->character().character_name));
      queue_packet(dispatch, target->session_id(),
                   make_deal_menu_packet(target->session_id(),
                                         requester->character().character_name));
      break;
    }
    case ActorMailKind::trade_cancel: {
      cancel_trade_for(mail.actor_id, dispatch, true);
      break;
    }
    case ActorMailKind::trade_add_item: {
      auto* player = find_player(mail.actor_id);
      auto* session = trade_session_for(mail.actor_id);
      if (player == nullptr || session == nullptr) {
        break;
      }
      auto* offer = trade_offer_for(*session, mail.actor_id);
      auto* peer_offer = trade_peer_offer_for(*session, mail.actor_id);
      auto* peer = session->first_actor_id == mail.actor_id ? find_player(session->second_actor_id)
                                                            : find_player(session->first_actor_id);
      if (offer == nullptr || peer_offer == nullptr || offer->accepted) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealAddItemFail));
        break;
      }
      const auto already_offered =
          std::any_of(offer->items.begin(), offer->items.end(), [&](const LegacyUserItem& item) {
            return !is_empty(item) && item.make_index == mail.item_make_index;
          });
      if (already_offered) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealAddItemFail));
        break;
      }
      const auto* bag_item = player->bag_item(mail.item_make_index, mail.payload, item_configs_);
      const auto* item_config =
          bag_item != nullptr ? find_item_config(item_configs_, bag_item->index) : nullptr;
      if (bag_item == nullptr || (item_config != nullptr && item_config->std_mode == 51)) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealAddItemFail));
        break;
      }
      const auto item = player->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!item.has_value()) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealAddItemFail));
        break;
      }
      offer->items.push_back(*item);
      offer->accepted = false;
      peer_offer->accepted = false;
      offer->last_change_time_ms = now_ms;
      peer_offer->last_change_time_ms = now_ms;
      player->refresh_derived_state(item_configs_);
      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *item, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(),
                   make_deal_simple_packet(player->session_id(), kSmDealAddItemOk));
      if (peer != nullptr) {
        queue_packet(dispatch, peer->session_id(),
                     make_deal_remote_add_item_packet(peer->session_id(), player->id(), *item,
                                                      item_configs_));
      }
      break;
    }
    case ActorMailKind::trade_remove_item: {
      auto* player = find_player(mail.actor_id);
      auto* session = trade_session_for(mail.actor_id);
      if (player == nullptr || session == nullptr) {
        break;
      }
      auto* offer = trade_offer_for(*session, mail.actor_id);
      auto* peer_offer = trade_peer_offer_for(*session, mail.actor_id);
      auto* peer = session->first_actor_id == mail.actor_id ? find_player(session->second_actor_id)
                                                            : find_player(session->first_actor_id);
      if (offer == nullptr || peer_offer == nullptr || offer->accepted) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealDelItemFail));
        break;
      }
      const auto item_it = std::find_if(
          offer->items.begin(), offer->items.end(), [&](const LegacyUserItem& item) {
            return !is_empty(item) && item.make_index == mail.item_make_index &&
                   (mail.payload.empty() || item_name(item, item_configs_) == mail.payload);
          });
      if (item_it == offer->items.end() ||
          !player->can_add_bag_item(*item_it, item_configs_)) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealDelItemFail));
        break;
      }
      const auto item = *item_it;
      offer->items.erase(item_it);
      if (!player->add_bag_item(item)) {
        offer->items.push_back(item);
        queue_packet(dispatch, player->session_id(),
                     make_deal_simple_packet(player->session_id(), kSmDealDelItemFail));
        break;
      }
      offer->accepted = false;
      peer_offer->accepted = false;
      offer->last_change_time_ms = now_ms;
      peer_offer->last_change_time_ms = now_ms;
      player->refresh_derived_state(item_configs_);
      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), player->id(), item, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(),
                   make_deal_simple_packet(player->session_id(), kSmDealDelItemOk));
      if (peer != nullptr) {
        queue_packet(dispatch, peer->session_id(),
                     make_deal_remote_del_item_packet(peer->session_id(), player->id(),
                                                      item, item_configs_));
      }
      break;
    }
    case ActorMailKind::trade_set_gold: {
      auto* player = find_player(mail.actor_id);
      auto* session = trade_session_for(mail.actor_id);
      if (player == nullptr || session == nullptr) {
        break;
      }
      auto* offer = trade_offer_for(*session, mail.actor_id);
      auto* peer_offer = trade_peer_offer_for(*session, mail.actor_id);
      if (offer == nullptr || peer_offer == nullptr || mail.amount < 0 ||
          offer->accepted ||
          static_cast<std::int64_t>(player->character().gold) + offer->gold < mail.amount) {
        queue_packet(dispatch, player->session_id(),
                     make_deal_change_gold_packet(player->session_id(), kSmDealChangeGoldFail,
                                                  offer != nullptr ? offer->gold : 0,
                                                  player->character().gold));
        break;
      }
      if (offer->gold > 0) {
        player->add_gold(offer->gold);
      }
      if (mail.amount > 0) {
        player->spend_gold(mail.amount);
      }
      offer->gold = mail.amount;
      offer->accepted = false;
      peer_offer->accepted = false;
      offer->last_change_time_ms = now_ms;
      peer_offer->last_change_time_ms = now_ms;
      queue_packet(dispatch, player->session_id(),
                   make_deal_change_gold_packet(player->session_id(), kSmDealChangeGoldOk,
                                                offer->gold, player->character().gold));
      auto* peer = session->first_actor_id == mail.actor_id ? find_player(session->second_actor_id)
                                                            : find_player(session->first_actor_id);
      if (peer != nullptr) {
        queue_packet(dispatch, peer->session_id(),
                     make_deal_remote_change_gold_packet(peer->session_id(), offer->gold));
      }
      break;
    }
    case ActorMailKind::trade_accept: {
      auto* player = find_player(mail.actor_id);
      auto* session = trade_session_for(mail.actor_id);
      if (player == nullptr || session == nullptr) {
        break;
      }
      auto* offer = trade_offer_for(*session, mail.actor_id);
      auto* peer_offer = trade_peer_offer_for(*session, mail.actor_id);
      if (offer == nullptr || peer_offer == nullptr) {
        break;
      }
      constexpr std::uint64_t kLegacyTradeStableMs = 1000;
      const auto offer_stable = offer->last_change_time_ms == 0 ||
                                now_ms >= offer->last_change_time_ms + kLegacyTradeStableMs;
      const auto peer_stable = peer_offer->last_change_time_ms == 0 ||
                               now_ms >= peer_offer->last_change_time_ms + kLegacyTradeStableMs;
      if (!offer_stable || !peer_stable) {
        cancel_trade_for(mail.actor_id, dispatch, true);
        break;
      }
      offer->accepted = true;
      if (session->first.accepted && session->second.accepted) {
        static_cast<void>(commit_trade(*session, dispatch));
      } else {
        queue_system_notice(dispatch, *player, "Trade accepted.");
      }
      break;
    }
    case ActorMailKind::storage_item: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_storage() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (reject_trade_locked_item_change(requester)) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      if (!npc_mode_allows(requester, LegacyNpcItemMode::storage, target_it->second->id())) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      if (legacy_approval_mode_) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      const auto storage_count = static_cast<std::size_t>(std::count_if(
          requester->character().storage_items.begin(), requester->character().storage_items.end(),
          [](const LegacyUserItem& item) { return !is_empty(item); }));
      if (storage_count >= kRuntimeMaxStorageItems) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFull));
        break;
      }
      if (mail.payload.empty()) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      const auto* bag_item = requester->bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (bag_item == nullptr) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      const auto* item_config = find_item_config(item_configs_, bag_item->index);
      if (item_config != nullptr && item_config->std_mode == 51) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      if (!requester->has_free_storage_slot()) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFull));
        break;
      }
      const auto item = requester->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!item.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFail));
        break;
      }
      if (!requester->add_storage_item(*item)) {
        static_cast<void>(requester->add_bag_item(*item));
        queue_packet(dispatch, requester->session_id(),
                     make_storage_result_packet(requester->session_id(), kSmStorageFull));
        break;
      }
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_del_item_packet(requester->session_id(), requester->id(), *item,
                                        item_configs_));
      queue_packet(dispatch, requester->session_id(),
                   make_storage_result_packet(requester->session_id(), kSmStorageOk));
      queue_packet(dispatch, requester->session_id(),
                   make_weight_changed_packet(requester->session_id(), requester->character()));
      queue_save_character(dispatch, *requester);
      break;
    }
    case ActorMailKind::take_back_storage_item: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || requester->is_dead() || merchant == nullptr ||
          !merchant->supports_storage() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (reject_trade_locked_item_change(requester)) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (!npc_mode_allows(requester, LegacyNpcItemMode::storage, target_it->second->id())) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (legacy_approval_mode_) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (mail.payload.empty()) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      const auto* storage_item =
          requester->storage_item(mail.item_make_index, mail.payload, item_configs_);
      if (storage_item == nullptr) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (!requester->has_free_bag_slot()) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFullBag, 0));
        break;
      }
      if (!requester->can_add_bag_item(*storage_item, item_configs_)) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      const auto removed =
          requester->remove_storage_item(mail.item_make_index, mail.payload, item_configs_);
      if (!removed.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (!requester->add_bag_item(*removed)) {
        static_cast<void>(requester->add_storage_item(*removed));
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFullBag, 0));
        break;
      }
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_add_item_packet(requester->session_id(), requester->id(), *removed, item_configs_));
      queue_packet(dispatch, requester->session_id(),
                   make_take_back_storage_result_packet(requester->session_id(),
                                                        kSmTakeBackStorageItemOk,
                                                        removed->make_index));
      queue_packet(dispatch, requester->session_id(),
                   make_weight_changed_packet(requester->session_id(), requester->character()));
      queue_save_character(dispatch, *requester);
      break;
    }
    case ActorMailKind::pickup_item: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead() || player->x() != mail.x || player->y() != mail.y) {
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        add_legacy_trace(dispatch, "LegacyItem", "trade_locked", mail, current_tick, now_ms,
                         false, 0, 0, "pickup_item");
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        add_legacy_trace(dispatch, "LegacyItem", "npc_modal_locked", mail, current_tick, now_ms,
                         false, 0, 0, "pickup_item");
        break;
      }

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "pickup_item");
      auto ground_it = ground_items_.end();
      while (true) {
        const auto first_item_id = environment_.first_item_object_id(player->x(), player->y());
        if (!first_item_id.has_value()) {
          break;
        }
        ground_it = ground_items_.find(*first_item_id);
        if (ground_it != ground_items_.end()) {
          break;
        }
        static_cast<void>(environment_.delete_from_map(
            player->x(), player->y(), LegacyMapObjectShape::item_object, *first_item_id));
        add_legacy_trace(dispatch, "LegacyItem", "orphan_map_item_repair", mail, current_tick,
                         now_ms, true, static_cast<std::int32_t>(*first_item_id), 0, "pickup_item");
      }
      if (ground_it == ground_items_.end()) {
        add_legacy_trace(dispatch, "LegacyItem", "empty_cell", mail, current_tick, now_ms, false,
                         0, 0, "pickup_item");
        break;
      }

      auto& ground_item_ref = ground_it->second;
      refresh_ground_item_ownership(ground_item_ref, now_ms);
      if (ground_item_ref.owner_actor_id != 0 && ground_item_ref.owner_actor_id != player->id()) {
        add_legacy_trace(dispatch, "LegacyItem", "owner_reject", mail, current_tick, now_ms,
                         false, static_cast<std::int32_t>(ground_item_ref.owner_actor_id), 0,
                         "pickup_item");
        break;
      }

      if (ground_it->second.is_gold) {
        const auto new_gold = static_cast<std::int64_t>(player->character().gold) +
                              static_cast<std::int64_t>(ground_it->second.gold_amount);
        if (new_gold > kLegacyBagGold) {
          add_legacy_trace(dispatch, "LegacyItem", "gold_cap_reject", mail, current_tick, now_ms,
                           false, ground_it->second.gold_amount, 0, "pickup_gold");
          break;
        }
        player->add_gold(ground_it->second.gold_amount);
        const auto ground_item = ground_it->second;
        static_cast<void>(environment_.delete_from_map(
            ground_item.x, ground_item.y, LegacyMapObjectShape::item_object, ground_item.id));
        remove_item_from_visibility(ground_item.id, dispatch, now_ms,
                                    ItemVisibilityRemovalMode::immediate_all);
        ground_items_.erase(ground_it);

        queue_packet(dispatch, player->session_id(),
                     make_gold_changed_packet(player->session_id(), player->character().gold));
        queue_save_character(dispatch, *player);
        add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                         ground_item.gold_amount, 0, "pickup_gold");
        break;
      }

      if (!player->can_add_bag_item(ground_it->second.item, item_configs_)) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         0, 0, "pickup_item");
        break;
      }

      const auto ground_item = ground_it->second;
      if (!environment_.delete_from_map(ground_item.x, ground_item.y,
                                        LegacyMapObjectShape::item_object,
                                        ground_item.id)) {
        remove_item_from_visibility(ground_item.id, dispatch, now_ms,
                                    ItemVisibilityRemovalMode::delayed_all);
        ground_items_.erase(ground_it);
        add_legacy_trace(dispatch, "LegacyItem", "orphan_ground_item_repair", mail, current_tick,
                         now_ms, true, static_cast<std::int32_t>(ground_item.id), 0, "pickup_item");
        add_legacy_trace(dispatch, "LegacyItem", "map_reject", mail, current_tick, now_ms,
                         false, static_cast<std::int32_t>(ground_item.id), 0, "pickup_item");
        break;
      }
      if (!player->add_bag_item(ground_item.item)) {
        const auto rollback_result = environment_.add_item_object(
            ground_item.x, ground_item.y, ground_item.id, LegacyMapItemState{}, now_ms);
        if (!rollback_result.ok) {
          remove_item_from_visibility(ground_item.id, dispatch, now_ms,
                                      ItemVisibilityRemovalMode::delayed_all);
          ground_items_.erase(ground_it);
          add_legacy_trace(dispatch, "LegacyItem", "rollback_repair", mail, current_tick, now_ms,
                           false, static_cast<std::int32_t>(ground_item.id), 0, "pickup_item");
        }
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         0, 0, "pickup_item");
        break;
      }
      player->refresh_derived_state(item_configs_);
      remove_item_from_visibility(ground_item.id, dispatch, now_ms,
                                  ItemVisibilityRemovalMode::immediate_single_session,
                                  player->session_id());
      ground_items_.erase(ground_it);

      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), player->id(), ground_item.item, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       ground_item.item.make_index, 0, "pickup_item");
      static_cast<void>(trigger_map_quest(*player, ground_item.dropper_name, ground_item.name,
                                          false, "pickup", dispatch, current_tick, now_ms));
      break;
    }
    case ActorMailKind::take_on_item: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead() || mail.item_slot < 0 ||
          mail.item_slot >= static_cast<std::int32_t>(kMaxEquipSlots)) {
        if (player != nullptr) {
          add_legacy_trace(dispatch, "LegacyItem", "slot_reject", mail, current_tick, now_ms,
                           false, mail.item_slot, 0, "take_on_item");
        }
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true,
                       mail.item_slot, 0, "take_on_item");
      player->refresh_derived_state(item_configs_);
      const auto previous_feature = player->character().feature;
      const auto bag_slot =
          player->bag_item_index(mail.item_make_index, mail.payload, item_configs_);
      if (!bag_slot.has_value()) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }
      const auto target_item = player->character().bag_items[*bag_slot];

      const auto* item_config = find_item_config(item_configs_, target_item.index);
      if (item_config == nullptr || !item_fits_slot(*item_config, mail.item_slot)) {
        add_legacy_trace(dispatch, "LegacyItem", "slot_reject", mail, current_tick, now_ms, false,
                         mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      const auto* current_equipped =
          player->equipped_item(static_cast<std::size_t>(mail.item_slot));
      const auto* current_config =
          current_equipped != nullptr ? find_item_config(item_configs_, current_equipped->index)
                                      : nullptr;
      if (current_equipped != nullptr && !is_empty(*current_equipped) &&
          !legacy_item_can_take_off(current_config, *current_equipped)) {
        add_legacy_trace(dispatch, "LegacyItem", "takeoff_locked", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      std::string reject_reason;
      const auto old_slot_weight = current_equipped != nullptr && current_equipped->dura > 0
                                       ? item_weight(*current_equipped, item_configs_)
                                       : 0;
      if (!legacy_can_take_on_item(player->character(), *item_config, target_item, mail.item_slot,
                                   player->character().ability.wear_weight,
                                   player->character().ability.hand_weight, old_slot_weight,
                                   &reject_reason)) {
        add_legacy_trace(dispatch, "LegacyItem", reject_reason, mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      std::optional<LegacyUserItem> swapped_item;
      const auto previous_status = player->character().status;
      if (const auto* equipped = player->equipped_item(static_cast<std::size_t>(mail.item_slot));
          equipped != nullptr && !is_empty(*equipped)) {
        swapped_item = *equipped;
      }
      if (swapped_item.has_value()) {
        const auto bag_weight_after_target_remove =
            static_cast<std::int32_t>(player->character().ability.weight) -
            item_weight(target_item, item_configs_);
        if (bag_weight_after_target_remove + item_weight(*swapped_item, item_configs_) >
            std::max<std::int32_t>(player->character().ability.max_weight, 0)) {
          add_legacy_trace(dispatch, "LegacyItem", "swap_bag_weight", mail, current_tick, now_ms,
                           false, mail.item_slot, 0, "take_on_item");
          queue_packet(dispatch, player->session_id(),
                       make_take_on_result_packet(player->session_id(), false, 0));
          break;
        }
      }

      player->equip_item(static_cast<std::size_t>(mail.item_slot), target_item);
      const auto removed = player->remove_bag_item_at(*bag_slot);
      if (!removed.has_value()) {
        if (swapped_item.has_value()) {
          player->equip_item(static_cast<std::size_t>(mail.item_slot), *swapped_item);
        } else {
          static_cast<void>(player->remove_equipped_item(
              static_cast<std::size_t>(mail.item_slot), target_item.make_index,
              item_name(target_item, item_configs_), item_configs_));
        }
        add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }
      if (swapped_item.has_value()) {
        if (!player->add_bag_item(*swapped_item)) {
          static_cast<void>(player->add_bag_item(*removed));
          player->equip_item(static_cast<std::size_t>(mail.item_slot), *swapped_item);
          add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                           false, mail.item_slot, 0, "take_on_item");
          queue_packet(dispatch, player->session_id(),
                       make_take_on_result_packet(player->session_id(), false, 0));
          break;
        }
      }
      player->refresh_derived_state(item_configs_);

      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed, item_configs_));
      if (swapped_item.has_value()) {
        queue_packet(dispatch, player->session_id(),
                     make_add_item_packet(player->session_id(), player->id(), *swapped_item, item_configs_));
      }
      queue_packet(dispatch, player->session_id(),
                   make_update_item_packet(player->session_id(), player->id(),
                                           player->character()
                                               .equipped_items[static_cast<std::size_t>(mail.item_slot)],
                                           item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_use_items_packet(player->session_id(), *player, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_ability_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(), make_sub_ability_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_take_on_result_packet(player->session_id(), true, player->character().feature));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));

      if (player->character().feature != previous_feature) {
        for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
          if (!is_legacy_visible_to(watcher, *player)) {
            return;
          }
          queue_packet(dispatch, watcher.session_id(),
                       make_feature_changed_packet(watcher.session_id(), player->id(),
                                                  player->character().feature));
        });
      }
      if (player->character().status != previous_status) {
        broadcast_legacy_char_status_changed(dispatch, *player);
      }
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       player->character().feature, 0, "take_on_item");
      break;
    }
    case ActorMailKind::take_off_item: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead() || mail.item_slot < 0 ||
          mail.item_slot >= static_cast<std::int32_t>(kMaxEquipSlots) || !player->has_free_bag_slot()) {
        if (player != nullptr) {
          add_legacy_trace(dispatch, "LegacyItem", "slot_reject", mail, current_tick, now_ms,
                           false, mail.item_slot, 0, "take_off_item");
          queue_packet(dispatch, player->session_id(),
                       make_take_off_result_packet(player->session_id(), false, 0));
        }
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true,
                       mail.item_slot, 0, "take_off_item");
      player->refresh_derived_state(item_configs_);
      const auto* current_equipped =
          player->equipped_item(static_cast<std::size_t>(mail.item_slot));
      const auto* current_config =
          current_equipped != nullptr ? find_item_config(item_configs_, current_equipped->index)
                                      : nullptr;
      if (current_equipped == nullptr || is_empty(*current_equipped) ||
          !legacy_item_can_take_off(current_config, *current_equipped) ||
          current_equipped->make_index != mail.item_make_index ||
          (!mail.payload.empty() &&
           util::lower_copy(item_name(*current_equipped, item_configs_)) !=
               util::lower_copy(mail.payload)) ||
          !player->can_add_bag_item(*current_equipped, item_configs_)) {
        add_legacy_trace(dispatch, "LegacyItem", "takeoff_reject", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_off_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }
      const auto previous_feature = player->character().feature;
      const auto previous_status = player->character().status;
      const auto item_to_take_off = *current_equipped;
      if (!player->add_bag_item(item_to_take_off)) {
        add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_off_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }
      const auto removed = player->remove_equipped_item(static_cast<std::size_t>(mail.item_slot),
                                                        mail.item_make_index, mail.payload,
                                                        item_configs_);
      if (!removed.has_value()) {
        static_cast<void>(player->remove_bag_item(item_to_take_off.make_index,
                                                  item_name(item_to_take_off, item_configs_),
                                                  item_configs_));
        add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_off_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }

      player->refresh_derived_state(item_configs_);
      queue_packet(dispatch, player->session_id(),
                   make_take_off_result_packet(player->session_id(), true, player->character().feature));
      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), player->id(), *removed, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_use_items_packet(player->session_id(), *player, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_ability_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(), make_sub_ability_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));

      if (player->character().feature != previous_feature) {
        for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
          if (!is_legacy_visible_to(watcher, *player)) {
            return;
          }
          queue_packet(dispatch, watcher.session_id(),
                       make_feature_changed_packet(watcher.session_id(), player->id(),
                                                  player->character().feature));
        });
      }
      if (player->character().status != previous_status) {
        broadcast_legacy_char_status_changed(dispatch, *player);
      }
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       player->character().feature, 0, "take_off_item");
      break;
    }
    case ActorMailKind::eat_item: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr || player->is_dead()) {
        break;
      }
      if (reject_trade_locked_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }
      if (reject_npc_modal_item_change(player)) {
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "eat_item");
      const auto bag_slot =
          mail.item_make_index != 0
              ? player->bag_item_index(mail.item_make_index, mail.payload, item_configs_)
              : (mail.item_slot >= 0 &&
                         static_cast<std::size_t>(mail.item_slot) <
                             player->character().bag_items.size() &&
                         !is_empty(player->character()
                                       .bag_items[static_cast<std::size_t>(mail.item_slot)])
                     ? std::optional<std::size_t>{static_cast<std::size_t>(mail.item_slot)}
                     : std::nullopt);
      if (!bag_slot.has_value()) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         0, 0, "eat_item");
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }
      const auto target_item = player->character().bag_items[*bag_slot];

      const auto* item_config = find_item_config(item_configs_, target_item.index);
      if (item_config != nullptr && config_.no_drug && item_config->std_mode >= 0 &&
          item_config->std_mode <= 3) {
        add_legacy_trace(dispatch, "LegacyItem", "nodrug_reject", mail, current_tick, now_ms,
                         false, target_item.index, 0, "eat_item");
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }
      if (item_config != nullptr && legacy_item_is_unbind_bundle(*item_config)) {
        const auto* target_config = find_item_config_by_name_or_id(item_configs_, item_config->unbind_item);
        if (target_config == nullptr || item_config->unbind_count <= 0) {
          add_legacy_trace(dispatch, "LegacyItem", "unbind_config_reject", mail, current_tick,
                           now_ms, false, target_item.index, 0, "eat_item");
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        player->refresh_derived_state(item_configs_);
        const auto free_slots = std::count_if(
            player->character().bag_items.begin(), player->character().bag_items.end(),
            [](const LegacyUserItem& item) { return is_empty(item); });
        const auto current_weight =
            static_cast<std::int32_t>(player->character().ability.weight) -
            item_weight(target_item, item_configs_);
        const auto target_weight = std::max(target_config->weight, 0) * item_config->unbind_count;
        if (free_slots + 1 < item_config->unbind_count ||
            current_weight + target_weight >
                static_cast<std::int32_t>(player->character().ability.max_weight)) {
          add_legacy_trace(dispatch, "LegacyItem", "unbind_bag_reject", mail, current_tick,
                           now_ms, false, item_config->unbind_count, 0, "eat_item");
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }

        const auto removed = player->remove_bag_item_at(*bag_slot);
        if (!removed.has_value()) {
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        std::vector<LegacyUserItem> unbound_items;
        unbound_items.reserve(static_cast<std::size_t>(item_config->unbind_count));
        for (std::int32_t index = 0; index < item_config->unbind_count; ++index) {
          LegacyUserItem item;
          item.index = static_cast<std::uint16_t>(std::clamp(target_config->id, 0, 65535));
          item.make_index = allocate_make_index();
          item.dura_max = static_cast<std::uint16_t>(
              std::clamp(target_config->dura_max > 0 ? target_config->dura_max : 1, 1, 65535));
          item.dura = item.dura_max;
          if (!player->add_bag_item(item)) {
            for (const auto& rollback : unbound_items) {
              static_cast<void>(player->remove_bag_item(
                  rollback.make_index, item_name(rollback, item_configs_), item_configs_));
            }
            static_cast<void>(player->add_bag_item(*removed));
            queue_packet(dispatch, player->session_id(),
                         make_eat_result_packet(player->session_id(), false));
            break;
          }
          unbound_items.push_back(item);
        }
        if (static_cast<std::int32_t>(unbound_items.size()) != item_config->unbind_count) {
          break;
        }
        player->refresh_derived_state(item_configs_);
        queue_packet(dispatch, player->session_id(),
                     make_del_item_packet(player->session_id(), player->id(), *removed,
                                          item_configs_));
        for (const auto& item : unbound_items) {
          queue_packet(dispatch, player->session_id(),
                       make_add_item_packet(player->session_id(), player->id(), item, item_configs_));
        }
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), true));
        queue_packet(dispatch, player->session_id(),
                     make_weight_changed_packet(player->session_id(), player->character()));
        queue_save_character(dispatch, *player);
        add_legacy_trace(dispatch, "LegacyItem", "unbind_success", mail, current_tick, now_ms,
                         true, item_config->unbind_count, 0, item_config->unbind_item);
        break;
      }

      if (item_config != nullptr && legacy_is_blessed_oil(*item_config)) {
        if (!apply_legacy_weapon_good_luck(*player, dispatch, current_tick, now_ms)) {
          add_legacy_trace(dispatch, "LegacyWeaponLuck", "blessed_oil_reject", mail,
                           current_tick, now_ms, false, target_item.index, 0,
                           "MakeWeaponGoodLock");
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        const auto removed = player->remove_bag_item_at(*bag_slot);
        if (!removed.has_value()) {
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        player->refresh_derived_state(item_configs_);
        queue_packet(dispatch, player->session_id(),
                     make_del_item_packet(player->session_id(), player->id(), *removed,
                                          item_configs_));
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), true));
        queue_packet(dispatch, player->session_id(),
                     make_weight_changed_packet(player->session_id(), player->character()));
        queue_save_character(dispatch, *player);
        add_legacy_trace(dispatch, "LegacyWeaponLuck", "blessed_oil_success", mail,
                         current_tick, now_ms, true, removed->index, 0,
                         "MakeWeaponGoodLock");
        break;
      }

      if (item_config != nullptr && legacy_item_is_magic_book(*item_config)) {
        const auto book_result = legacy_read_magic_book(*player, *item_config, magic_configs_);
        if (book_result.status != LegacyReadBookStatus::learned) {
          add_legacy_trace(dispatch, "LegacySkill", "book_reject", mail, current_tick, now_ms,
                           false, book_result.magic_id, 0,
                           legacy_read_book_status_name(book_result.status));
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }

        const auto* learned_magic = player->learned_magic(book_result.magic_id);
        if (learned_magic == nullptr) {
          add_legacy_trace(dispatch, "LegacySkill", "book_reject", mail, current_tick, now_ms,
                           false, book_result.magic_id, 0, "slot_missing");
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }

        const auto removed = player->remove_bag_item_at(*bag_slot);
        if (!removed.has_value()) {
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        player->refresh_derived_state(item_configs_);
        queue_packet(dispatch, player->session_id(),
                     make_add_magic_packet(player->session_id(), *learned_magic, magic_configs_));
        queue_packet(dispatch, player->session_id(),
                     make_del_item_packet(player->session_id(), player->id(), *removed,
                                          item_configs_));
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), true));
        queue_packet(dispatch, player->session_id(),
                     make_weight_changed_packet(player->session_id(), player->character()));
        queue_save_character(dispatch, *player);
        add_legacy_trace(dispatch, "LegacySkill", "book_success", mail, current_tick, now_ms,
                         true, book_result.magic_id, 0, "ReadBook");
        break;
      }

      if (item_config != nullptr && legacy_item_is_scroll(*item_config)) {
        const auto kind = legacy_scroll_kind(*item_config);
        std::string target_map = config_.id;
        std::int32_t target_x = config_.home_x;
        std::int32_t target_y = config_.home_y;
        bool blocked = false;
        if (kind == "random") {
          blocked = config_.no_random_move || config_.no_position_move;
          if (!blocked) {
            const auto target = random_item_scroll_target(dispatch, *player, current_tick, now_ms);
            if (target.has_value()) {
              target_x = target->first;
              target_y = target->second;
            } else {
              blocked = true;
            }
          }
        } else {
          blocked = config_.no_recall || config_.no_position_move;
          if (!config_.back_map.empty()) {
            target_map = config_.back_map;
          }
        }
        if (!blocked && target_map == config_.id &&
            (!environment_.in_bounds(target_x, target_y) ||
             !environment_.can_walk(target_x, target_y, true))) {
          blocked = true;
        }
        if (blocked) {
          add_legacy_trace(dispatch, "LegacyItem", "scroll_blocked", mail, current_tick,
                           now_ms, false, target_item.index, 0, kind);
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        const auto session_id = player->session_id();
        const auto actor_id = player->id();
        const auto same_map_transfer = target_map == config_.id;
        std::optional<LegacyUserItem> removed;
        if (!same_map_transfer) {
          removed = player->remove_bag_item_at(*bag_slot);
          if (!removed.has_value()) {
            queue_packet(dispatch, session_id, make_eat_result_packet(session_id, false));
            break;
          }
        }
        player->refresh_derived_state(item_configs_);
        const auto character_after_use = player->character();
        if (!try_item_map_move(*player, target_map, target_x, target_y, dispatch, current_tick,
                               now_ms)) {
          add_legacy_trace(dispatch, "LegacyItem", "scroll_transfer_reject", mail,
                           current_tick, now_ms, false, target_item.index, 0, kind);
          if (auto* rollback_player = find_player(actor_id); rollback_player != nullptr) {
            if (removed.has_value()) {
              static_cast<void>(rollback_player->add_bag_item(*removed));
            }
            rollback_player->refresh_derived_state(item_configs_);
          }
          queue_packet(dispatch, session_id, make_eat_result_packet(session_id, false));
        } else {
          if (!removed.has_value()) {
            if (auto* moved_player = find_player(actor_id); moved_player != nullptr) {
              removed = moved_player->remove_bag_item_at(*bag_slot);
              if (removed.has_value()) {
                moved_player->refresh_derived_state(item_configs_);
              }
            }
          }
          if (!removed.has_value()) {
            queue_packet(dispatch, session_id, make_eat_result_packet(session_id, false));
            break;
          }
          queue_packet(dispatch, session_id,
                       make_del_item_packet(session_id, actor_id, *removed, item_configs_));
          queue_packet(dispatch, session_id, make_eat_result_packet(session_id, true));
          const auto* weight_player = same_map_transfer ? find_player(actor_id) : nullptr;
          queue_packet(dispatch, session_id,
                       make_weight_changed_packet(session_id,
                                                  weight_player != nullptr
                                                      ? weight_player->character()
                                                      : character_after_use));
          if (auto* moved_player = find_player(actor_id); moved_player != nullptr) {
            queue_save_character(dispatch, *moved_player);
          }
          add_legacy_trace(dispatch, "LegacyItem", "scroll_success", mail, current_tick,
                           now_ms, true, removed->index, 0, kind);
        }
        break;
      }

      if (item_config == nullptr || !is_consumable(*item_config)) {
        add_legacy_trace(dispatch, "LegacyItem", "type_reject", mail, current_tick, now_ms, false,
                         target_item.index, 0, "eat_item");
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }

      player->apply_consumable(*item_config);
      const auto removed = player->remove_bag_item_at(*bag_slot);
      if (!removed.has_value()) {
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }
      player->refresh_derived_state(item_configs_);
      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed,
                                        item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_health_spell_changed_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(),
                   make_eat_result_packet(player->session_id(), true));
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       removed->index, 0, "eat_item");
      break;
    }
    case ActorMailKind::attack: {
      constexpr std::int32_t kLegacyMainStruckDelayMs = 200;
      constexpr std::int32_t kLegacyDirectStruckDelayMs = 500;
      auto attacker_it = objects_.find(mail.actor_id);
      if (attacker_it == objects_.end()) {
        break;
      }
      auto* attacker = as_player(attacker_it->second.get());
      if (attacker == nullptr || attacker->is_dead()) {
        break;
      }
      cancel_trade_for(attacker->id(), dispatch, true);

      ActorMail effective_mail = mail;
      auto effective_ident = mail.game_message.ident;
      auto sword_magic_id = legacy_sword_skill_for_attack_ident(effective_ident);
      auto prepared_sword_magic_id = 0;
      const auto has_power_hit_magic = [&]() {
        const auto magic_it = magic_configs_.find(7);
        return magic_it != magic_configs_.end() &&
               magic_it->second.legacy.legacy_present &&
               magic_it->second.legacy.is_sword_skill &&
               legacy_p14_sword_skill(7) &&
               attacker->learned_magic(7) != nullptr;
      };
      const auto pending_magic_id = attacker->pending_legacy_sword_skill(current_tick);
      const auto pending_ident = legacy_attack_ident_for_sword_skill(pending_magic_id);
      if (pending_magic_id == 26 &&
          (effective_ident == kCmHit || effective_ident == pending_ident)) {
        prepared_sword_magic_id = pending_magic_id;
        sword_magic_id = pending_magic_id;
        effective_ident = pending_ident;
      } else if (effective_ident == kCmPowerHit) {
        if (!has_power_hit_magic() || !attacker->legacy_power_hit_ready()) {
          effective_ident = kCmHit;
          sword_magic_id = attacker->learned_magic(3) != nullptr ? 3 : 0;
        }
      } else if (effective_ident == kCmFireHit) {
        effective_ident = kCmHit;
        sword_magic_id = attacker->learned_magic(3) != nullptr ? 3 : 0;
      } else if (effective_ident == kCmHit && attacker->learned_magic(3) != nullptr) {
        sword_magic_id = 3;
      }
      effective_mail.game_message.ident = effective_ident;

      if (sword_magic_id != 0) {
        const auto magic_it = magic_configs_.find(sword_magic_id);
        auto* user_magic = attacker->learned_magic_mutable(sword_magic_id);
        if (magic_it == magic_configs_.end() || user_magic == nullptr ||
            !magic_it->second.legacy.legacy_present ||
            !magic_it->second.legacy.is_sword_skill ||
            !legacy_p14_sword_skill(sword_magic_id)) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_reject", effective_mail,
                           current_tick, now_ms, false, sword_magic_id, 0, "GetMagic");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
        if (effective_ident == kCmLongHit && !attacker->legacy_long_hit_enabled()) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_reject", effective_mail,
                           current_tick, now_ms, false, sword_magic_id, 0, "LongDisabled");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
        if (effective_ident == kCmWideHit && !attacker->legacy_wide_hit_enabled()) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_reject", effective_mail,
                           current_tick, now_ms, false, sword_magic_id, 0, "WideDisabled");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
        if (effective_ident == kCmCrossHit && !attacker->legacy_cross_hit_enabled()) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_reject", effective_mail,
                           current_tick, now_ms, false, sword_magic_id, 0, "CrossDisabled");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
      }

      const auto attack_throttle = attacker->begin_attack_attempt(now_ms);
      if (!attack_throttle.allowed) {
        add_legacy_trace(dispatch, "LegacyCombat", "attack_cooldown_reject", effective_mail,
                         current_tick, now_ms, false, attack_throttle.over_count, 0,
                         "LatestHitTime");
        queue_packet(dispatch, attacker->session_id(),
                     make_ack_packet(attacker->session_id(), false));
        if (attack_throttle.disconnect) {
          queue_force_disconnect(dispatch, attacker->session_id(), "speed_hack_attack");
        }
        break;
      }

      auto power_hit_active = false;
      if (effective_ident == kCmPowerHit && sword_magic_id == 7) {
        power_hit_active = attacker->consume_legacy_power_hit();
        if (!power_hit_active) {
          effective_ident = kCmHit;
          effective_mail.game_message.ident = effective_ident;
          sword_magic_id = attacker->learned_magic(3) != nullptr ? 3 : 0;
        }
      }

      attacker->on_mail(effective_mail, context);

      const auto attack_range = resolve_attack_range(effective_ident);
      GameObject* target = nullptr;
      std::vector<GameObject*> direct_attack_targets;
      if (effective_ident == kCmLongHit) {
        const auto [dx, dy] = direction_delta(actor_dir(*attacker));
        if (auto* long_target = find_attack_target_by_position(
                objects_, *attacker, attacker->x() + dx * 2, attacker->y() + dy * 2,
                attack_range);
            long_target != nullptr) {
          direct_attack_targets.push_back(long_target);
        }
        target = find_attack_target_in_front(objects_, *attacker, 1);
      } else {
        target =
            find_attack_target_by_actor_id(objects_, *attacker, mail.target_actor_id, attack_range);
        if (target == nullptr && (mail.x != 0 || mail.y != 0)) {
          target = find_attack_target_by_position(objects_, *attacker, mail.x, mail.y, attack_range);
        }
        if (target == nullptr) {
          target = find_attack_target_in_front(objects_, *attacker, attack_range);
        }
      }
      auto wide_targets =
          effective_ident == kCmWideHit
              ? collect_wide_hit_targets(objects_, *attacker, config_, now_ms)
              : std::vector<GameObject*>{};
      auto cross_targets =
          effective_ident == kCmCrossHit
              ? collect_cross_hit_targets(objects_, *attacker, config_, now_ms)
              : std::vector<GameObject*>{};
      if (effective_ident == kCmWideHit) {
        target = find_attack_target_in_front(objects_, *attacker, 1);
        direct_attack_targets = wide_targets;
      }
      auto direct_only_primary = false;
      if (effective_ident == kCmCrossHit) {
        target = find_attack_target_in_front(objects_, *attacker, 1);
        direct_attack_targets = cross_targets;
      }
      if (target == nullptr && !direct_attack_targets.empty()) {
        target = direct_attack_targets.front();
        direct_attack_targets.erase(direct_attack_targets.begin());
        direct_only_primary = true;
      }
      if (target != nullptr) {
        direct_attack_targets.erase(
            std::remove(direct_attack_targets.begin(), direct_attack_targets.end(), target),
            direct_attack_targets.end());
      }
      if (target == nullptr && prepared_sword_magic_id != 0) {
        effective_ident = kCmHit;
        effective_mail.game_message.ident = effective_ident;
        sword_magic_id = attacker->learned_magic(3) != nullptr ? 3 : 0;
        prepared_sword_magic_id = 0;
      }
      if (auto* player_target = as_player(target); player_target != nullptr) {
        const auto block_reason = resolve_pk_block_reason(config_, *attacker, *player_target, now_ms);
        if (!block_reason.empty()) {
          add_legacy_trace(dispatch, "LegacyCombat", "pk_block", effective_mail, current_tick, now_ms,
                           false, 0, 0, block_reason);
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          queue_packet(dispatch, attacker->session_id(),
                       make_system_notice_packet(attacker->session_id(), block_reason));
          break;
        }
      } else if (auto* monster_target = as_monster(target);
                 monster_target != nullptr && monster_target->is_slave() &&
                 monster_target->master_actor_id() != 0) {
        if (auto master_it = objects_.find(monster_target->master_actor_id());
            master_it != objects_.end()) {
          if (auto* master = as_player(master_it->second.get()); master != nullptr) {
            const auto block_reason =
                resolve_pk_block_reason(config_, *attacker, *master, now_ms);
            if (!block_reason.empty()) {
              add_legacy_trace(dispatch, "LegacyCombat", "pk_block", effective_mail,
                               current_tick, now_ms, false, 0, 0, block_reason);
              queue_packet(dispatch, attacker->session_id(),
                           make_ack_packet(attacker->session_id(), false));
              queue_packet(dispatch, attacker->session_id(),
                           make_system_notice_packet(attacker->session_id(), block_reason));
              break;
            }
          }
        }
      }

      if (prepared_sword_magic_id != 0) {
        const auto consumed = attacker->consume_legacy_sword_skill(current_tick);
        if (consumed != prepared_sword_magic_id) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_reject", effective_mail,
                           current_tick, now_ms, false, prepared_sword_magic_id, 0,
                           "PreparedExpired");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
      }

      queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), true));
      add_legacy_trace(dispatch, "LegacyCombat", "ack", effective_mail, current_tick, now_ms, true, 0, 0,
                       "attack");

      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (watcher.id() != attacker->id() && !is_legacy_visible_to(watcher, *attacker)) {
          return;
        }
        queue_packet(dispatch, watcher.session_id(),
                     make_hit_packet(watcher.session_id(), *attacker, effective_ident));
      });
      add_legacy_trace(dispatch, "LegacyCombat", "attack_broadcast", effective_mail, current_tick, now_ms,
                       true, 0, 0, "SM_HIT");

      auto advance_power_hit_proc = [&]() {
        if (!has_power_hit_magic()) {
          return;
        }
        if (attacker->legacy_power_hit_ready()) {
          return;
        }
        auto* power_magic = attacker->learned_magic_mutable(7);
        const auto* weapon = attacker->equipped_item(kEquipWeapon);
        if (power_magic == nullptr || weapon == nullptr || is_empty(*weapon)) {
          return;
        }
        const auto level = static_cast<std::int32_t>(power_magic->level);
        const auto counter_range = std::max(1, 7 - level);
        auto roll_power_point = [&]() {
          return legacy_random_value(dispatch, "LegacyCombat", "power_hit_point",
                                     counter_range, attacker->id(), 0,
                                     "AttackSkillPointCount", now_ms, current_tick);
        };
        if (!attacker->legacy_power_hit_counter_matches(level)) {
          attacker->reset_legacy_power_hit_counter(level, roll_power_point());
        }
        const auto became_ready = attacker->advance_legacy_power_hit_counter();
        if (became_ready) {
          queue_packet(dispatch, attacker->session_id(),
                       make_sword_state_packet(attacker->session_id(), "+PWR"));
          add_legacy_trace(dispatch, "LegacySkill", "power_hit_ready", effective_mail,
                           current_tick, now_ms, true, 7, level, "+PWR");
        }
        if (attacker->legacy_power_hit_counter_expired()) {
          attacker->reset_legacy_power_hit_counter(level, roll_power_point());
        }
      };
      advance_power_hit_proc();

      if (target == nullptr && direct_attack_targets.empty()) {
        add_legacy_trace(dispatch, "LegacyCombat", "no_target", effective_mail, current_tick, now_ms,
                         false, 0, 0, "attack");
        break;
      }
      static_cast<void>(apply_pending_weapon_upgrade_result(*attacker, dispatch,
                                                            current_tick, now_ms));

      const auto attack_roll_ident =
          sword_magic_id == 4 || sword_magic_id == 7 || sword_magic_id == 12 ||
                  sword_magic_id == 25 || sword_magic_id == 26 || sword_magic_id == 34
              ? kCmHit
              : effective_ident;
      auto attack_power =
          roll_legacy_player_attack_power(*attacker, *target, attack_roll_ident, dispatch,
                                          "LegacyCombat", "attack", current_tick, now_ms);
      auto direct_attack_power = attack_power;
      if (sword_magic_id == 7 && power_hit_active) {
        const auto* power_magic = attacker->learned_magic(7);
        const auto power_level =
            power_magic != nullptr ? static_cast<std::int32_t>(power_magic->level) : 0;
        const auto power_bonus = 5 + power_level;
        attack_power += power_bonus;
        add_legacy_trace(dispatch, "LegacyCombat", "power_hit_bonus", effective_mail,
                         current_tick, now_ms, true, power_bonus, attack_power,
                         "HitPowerPlus");
      }
      if (sword_magic_id == 12) {
        const auto* long_magic = attacker->learned_magic(12);
        const auto long_level =
            long_magic != nullptr ? static_cast<std::int32_t>(long_magic->level) : 0;
        auto max_train_level = 3;
        if (const auto magic_it = magic_configs_.find(12); magic_it != magic_configs_.end()) {
          max_train_level = std::max(0, magic_it->second.legacy.max_train_level);
        }
        direct_attack_power = delphi_round(static_cast<double>(attack_power) /
                                           static_cast<double>(max_train_level + 2) *
                                           static_cast<double>(long_level + 2));
        add_legacy_trace(dispatch, "LegacyCombat", "long_hit_power", effective_mail,
                         current_tick, now_ms, true, long_level, direct_attack_power,
                         "PLongHitSkill");
        if (direct_only_primary) {
          attack_power = direct_attack_power;
        }
      }
      if (sword_magic_id == 25) {
        const auto* wide_magic = attacker->learned_magic(25);
        const auto wide_level =
            wide_magic != nullptr ? static_cast<std::int32_t>(wide_magic->level) : 0;
        auto max_train_level = 3;
        if (const auto magic_it = magic_configs_.find(25); magic_it != magic_configs_.end()) {
          max_train_level = std::max(0, magic_it->second.legacy.max_train_level);
        }
        direct_attack_power = delphi_round(static_cast<double>(attack_power) /
                                           static_cast<double>(max_train_level + 10) *
                                           static_cast<double>(wide_level + 2));
        add_legacy_trace(dispatch, "LegacyCombat", "wide_hit_power", effective_mail,
                         current_tick, now_ms, true, wide_level, direct_attack_power,
                         "PWideHitSkill");
        if (direct_only_primary) {
          attack_power = direct_attack_power;
        }
      }
      auto cross_attack_power = 0;
      if (sword_magic_id == 34) {
        const auto* cross_magic = attacker->learned_magic(34);
        const auto cross_level =
            cross_magic != nullptr ? static_cast<std::int32_t>(cross_magic->level) : 0;
        auto cross_max_train_level = 3;
        if (const auto cross_it = magic_configs_.find(34); cross_it != magic_configs_.end()) {
          cross_max_train_level = std::max(0, cross_it->second.legacy.max_train_level);
        }
        cross_attack_power =
            delphi_round(static_cast<double>(attack_power) /
                         static_cast<double>(cross_max_train_level + 11) *
                         static_cast<double>(cross_level + 3));
        direct_attack_power = cross_attack_power;
        if (direct_only_primary) {
          attack_power = cross_attack_power;
          if (as_player(target) != nullptr) {
            attack_power = delphi_round(static_cast<double>(attack_power) * 0.8);
          }
        }
      }
      if (sword_magic_id == 26) {
        const auto* fire_magic = attacker->learned_magic(26);
        const auto fire_level = fire_magic != nullptr ? static_cast<std::int32_t>(fire_magic->level) : 0;
        const auto hit_double = 4 + fire_level * 4;
        const auto fire_bonus =
            delphi_round(static_cast<double>(attack_power) / 100.0 *
                         static_cast<double>(hit_double * 10));
        attack_power += fire_bonus;
        add_legacy_trace(dispatch, "LegacyCombat", "fire_hit_bonus", effective_mail,
                         current_tick, now_ms, true, fire_bonus, attack_power, "HitDouble");
      }
      bool monster_damaged = false;
      auto apply_direct_attack_target = [&](GameObject& direct_target,
                                            std::int32_t direct_damage,
                                            std::string_view miss_label) {
        if (!is_attackable_target(direct_target)) {
          return;
        }
        if (auto* player_target = as_player(&direct_target); player_target != nullptr) {
          const auto block_reason =
              resolve_pk_block_reason(config_, *attacker, *player_target, now_ms);
          if (!block_reason.empty()) {
            add_legacy_trace(dispatch, "LegacyCombat", "pk_block", effective_mail,
                             current_tick, now_ms, false, 0, 0, block_reason);
            return;
          }
        } else if (auto* monster_target = as_monster(&direct_target);
                   monster_target != nullptr && monster_target->is_slave() &&
                   monster_target->master_actor_id() != 0) {
          if (auto master_it = objects_.find(monster_target->master_actor_id());
              master_it != objects_.end()) {
            if (auto* master = as_player(master_it->second.get()); master != nullptr) {
              const auto block_reason =
                  resolve_pk_block_reason(config_, *attacker, *master, now_ms);
              if (!block_reason.empty()) {
                add_legacy_trace(dispatch, "LegacyCombat", "pk_block", effective_mail,
                                 current_tick, now_ms, false, 0, 0, block_reason);
                return;
              }
            }
          }
        }
        const auto direct_hit_roll =
            legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                                legacy_speed_point(direct_target), attacker->id(),
                                direct_target.id(), "direct_attack", now_ms, current_tick);
        if (legacy_accuracy_point(*attacker) <= direct_hit_roll) {
          add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick,
                           now_ms, false, direct_hit_roll, 0, std::string(miss_label));
          return;
        }

        const auto direct_applied_damage_value = std::max(0, direct_damage);
        std::int32_t direct_applied_damage = 0;
        bool direct_target_died = false;
        Monster* direct_slain_monster = nullptr;
        if (auto* player_target = as_player(&direct_target); player_target != nullptr) {
          if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
            player_target->record_pk_hiter(attacker->id(), now_ms);
          }
          if (direct_applied_damage_value > 0) {
            static_cast<void>(apply_legacy_struck_equipment_durability(
                *player_target, attacker->id(), dispatch, current_tick, now_ms,
                "LegacyCombat"));
          }
          const auto damage_result =
              player_target->apply_damage(direct_applied_damage_value, current_tick);
          direct_applied_damage = damage_result.hp_damage;
          direct_target_died = player_target->is_dead();
          if (direct_target_died &&
              try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
            direct_target_died = false;
          }
          if (direct_target_died) {
            const auto death_clear = player_target->mark_dead(now_ms);
            dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
            apply_bad_kill_penalty(*attacker, *player_target, dispatch, current_tick,
                                   now_ms, "LegacyCombat");
            static_cast<void>(
                settle_player_death(*player_target, dispatch, current_tick, now_ms));
          }
        } else if (auto* monster_target = as_monster(&direct_target);
                   monster_target != nullptr) {
          direct_applied_damage = apply_legacy_monster_damage(
              objects_, *monster_target, direct_applied_damage_value, attacker->id(),
              config_, current_tick, now_ms);
          if (direct_applied_damage > 0) {
            monster_damaged = true;
            notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
          }
          direct_target_died = monster_target->is_dead();
          direct_slain_monster = direct_target_died ? monster_target : nullptr;
        }
        if (direct_applied_damage <= 0) {
          return;
        }
        auto pending_direct_death_packets =
            direct_target_died && direct_slain_monster != nullptr
                ? collect_legacy_death_packets(objects_, direct_target)
                : std::vector<PendingLegacyPacket>{};
        if (!direct_target_died || direct_slain_monster == nullptr) {
          for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
            if (watcher.id() != direct_target.id() &&
                !is_legacy_visible_to(watcher, direct_target)) {
              return;
            }
            queue_packet(dispatch, watcher.session_id(),
                         direct_target_died
                             ? make_death_packet(watcher.session_id(), direct_target,
                                                 watcher.id() == direct_target.id())
                             : make_struck_packet(watcher.session_id(), direct_target,
                                                  attacker->id(), direct_applied_damage, false),
                         direct_target_died ? 0 : kLegacyDirectStruckDelayMs);
          });
        }
        if (direct_slain_monster != nullptr) {
          finalize_monster_death(direct_slain_monster->id(), attacker->id(), dispatch,
                                 current_tick);
          add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick,
                           now_ms, true, sword_magic_id, direct_applied_damage, "WinExp");
          queue_legacy_packets(dispatch, std::move(pending_direct_death_packets));
        }
        add_legacy_trace(dispatch, "LegacyCombat",
                         direct_target_died ? "death" : "struck", effective_mail,
                         current_tick, now_ms, true, sword_magic_id, direct_applied_damage,
                         direct_target_died ? "SM_DEATH" : "SM_STRUCK");
      };
      if (effective_ident == kCmLongHit) {
        for (auto* direct_target : direct_attack_targets) {
          if (direct_target != nullptr && direct_target != target) {
            apply_direct_attack_target(*direct_target, direct_attack_power,
                                       "LongHit AccuracyPoint<=Random(SpeedPoint)");
          }
        }
        direct_attack_targets.clear();
      } else if (effective_ident == kCmWideHit) {
        for (auto* direct_target : direct_attack_targets) {
          if (direct_target != nullptr && direct_target != target) {
            apply_direct_attack_target(*direct_target, direct_attack_power,
                                       "WideHit AccuracyPoint<=Random(SpeedPoint)");
          }
        }
        direct_attack_targets.clear();
        wide_targets.clear();
      } else if (effective_ident == kCmCrossHit) {
        for (auto* direct_target : direct_attack_targets) {
          if (direct_target == nullptr || direct_target == target) {
            continue;
          }
          auto direct_damage = direct_attack_power;
          if (as_player(direct_target) != nullptr) {
            direct_damage = delphi_round(static_cast<double>(direct_damage) * 0.8);
          }
          apply_direct_attack_target(*direct_target, direct_damage,
                                     "CrossHit AccuracyPoint<=Random(SpeedPoint)");
        }
        direct_attack_targets.clear();
        cross_targets.clear();
      }
      const auto undead_power = legacy_player_undead_power(*attacker, item_configs_);
      const auto hit_roll =
          legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                              legacy_speed_point(*target), attacker->id(),
                              target->id(), "attack", now_ms, current_tick);
      if (!legacy_hit_roll_succeeds(legacy_accuracy_point(*attacker), hit_roll)) {
        add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick, now_ms, false,
                         hit_roll, 0, "AccuracyPoint<=Random(SpeedPoint)");
        break;
      }

      const auto direct_primary_hit = direct_only_primary;
      auto damage = std::max(0, attack_power);
      if (!direct_primary_hit) {
        const auto [ac_min, ac_max] = actor_physical_defense_range(*target);
        const auto armor_roll =
            legacy_random_value(dispatch, "LegacyCombat", "armor_roll",
                                std::max(1, ac_max - ac_min + 1), attacker->id(),
                                target->id(), "attack", now_ms, current_tick);
        damage = legacy_physical_struck_damage(*target, attack_power, armor_roll,
                                               undead_power);
      }
      add_legacy_trace(dispatch, "LegacyCombat", "damage", effective_mail, current_tick, now_ms, true,
                       attack_power, damage, "GetAttackPower/GetHitStruckDamage");
      std::int32_t applied_damage = 0;
      std::int32_t absorbed_damage = 0;
      bool shield_broken = false;
      std::string shield_name{};
      bool target_died = false;
      Monster* slain_monster = nullptr;
      std::int32_t weapon_durability_loss = 0;

      if (auto* player_target = as_player(target); player_target != nullptr) {
        if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
          player_target->record_pk_hiter(attacker->id(), now_ms);
        }
        if (damage > 0) {
          static_cast<void>(apply_legacy_struck_equipment_durability(
              *player_target, attacker->id(), dispatch, current_tick, now_ms,
              "LegacyCombat"));
        }
        const auto damage_result = player_target->apply_damage(damage, current_tick);
        applied_damage = damage_result.hp_damage;
        absorbed_damage = damage_result.absorbed_damage;
        shield_broken = damage_result.shield_broken;
        shield_name = damage_result.shield_name;
        target_died = player_target->is_dead();
        if (target_died && try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
          target_died = false;
        }
        if (damage > 0 && !direct_primary_hit) {
          weapon_durability_loss = roll_legacy_weapon_durability_loss(
              *attacker, *target, dispatch, current_tick, now_ms);
          static_cast<void>(apply_legacy_physical_equipment_specials(
              *attacker, *target, damage, applied_damage, dispatch, "LegacyCombat",
              current_tick, now_ms));
        }
        if (target_died) {
          const auto death_clear = player_target->mark_dead(now_ms);
          dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
          apply_bad_kill_penalty(*attacker, *player_target, dispatch, current_tick,
                                 now_ms, "LegacyCombat");
          static_cast<void>(settle_player_death(*player_target, dispatch, current_tick,
                                                now_ms));
        }
      } else if (auto* monster_target = as_monster(target); monster_target != nullptr) {
        applied_damage = apply_legacy_monster_damage(
            objects_, *monster_target, damage, attacker->id(), config_, current_tick, now_ms);
        if (applied_damage > 0) {
          monster_damaged = true;
          if (!direct_primary_hit) {
            weapon_durability_loss = roll_legacy_weapon_durability_loss(
                *attacker, *target, dispatch, current_tick, now_ms);
          }
          notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
          if (!direct_primary_hit) {
            static_cast<void>(apply_legacy_physical_equipment_specials(
                *attacker, *target, applied_damage, applied_damage, dispatch,
                "LegacyCombat", current_tick, now_ms));
          }
        }
        target_died = monster_target->is_dead();
        slain_monster = target_died ? monster_target : nullptr;
      }

      static_cast<void>(
          apply_legacy_weapon_durability_loss(*attacker, weapon_durability_loss, dispatch));

      if (applied_damage <= 0) {
        add_legacy_trace(dispatch, "LegacyCombat", "absorbed", effective_mail, current_tick, now_ms,
                         absorbed_damage > 0, absorbed_damage, 0, "StruckDamage");
        if (absorbed_damage > 0) {
          if (const auto* player_target = as_player(target); player_target != nullptr) {
            queue_packet(dispatch, player_target->session_id(),
                         make_health_spell_changed_packet(player_target->session_id(), *player_target));
            if (shield_broken) {
              notify_player_and_watchers(
                  dispatch, *player_target, make_shield_break_self_notice(shield_name),
                  make_shield_break_watcher_notice(*player_target, shield_name));
            }
          }
        }
        break;
      }

      if (const auto* player_target = as_player(target);
          player_target != nullptr && absorbed_damage > 0) {
        queue_packet(dispatch, player_target->session_id(),
                     make_health_spell_changed_packet(player_target->session_id(),
                                                      *player_target));
      }

      auto pending_death_packets =
          target_died && slain_monster != nullptr
              ? collect_legacy_death_packets(objects_, *target)
              : std::vector<PendingLegacyPacket>{};
      if (!target_died || slain_monster == nullptr) {
        for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
          if (watcher.id() != target->id() && !is_legacy_visible_to(watcher, *target)) {
            return;
          }
          if (target_died) {
            queue_packet(dispatch, watcher.session_id(),
                         make_death_packet(watcher.session_id(), *target,
                                           watcher.id() == target->id()));
          } else {
            queue_packet(dispatch, watcher.session_id(),
                         make_struck_packet(watcher.session_id(), *target, attacker->id(),
                                            applied_damage, false),
                         direct_primary_hit ? kLegacyDirectStruckDelayMs
                                            : kLegacyMainStruckDelayMs);
          }
        });
      }

      if (slain_monster != nullptr) {
        finalize_monster_death(slain_monster->id(), attacker->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick, now_ms, true, 0,
                         applied_damage, "WinExp");
        queue_legacy_packets(dispatch, std::move(pending_death_packets));
      }
      add_legacy_trace(dispatch, "LegacyCombat", target_died ? "death" : "struck", effective_mail,
                       current_tick, now_ms, true, 0, applied_damage,
                       target_died ? "SM_DEATH" : "SM_STRUCK");
      if (effective_ident == kCmWideHit) {
        for (auto* extra_target : wide_targets) {
          if (extra_target == nullptr || extra_target == target ||
              objects_.find(extra_target->id()) == objects_.end() ||
              !is_attackable_target(*extra_target)) {
            continue;
          }
          const auto extra_hit_roll =
              legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                                  legacy_speed_point(*extra_target), attacker->id(),
                                  extra_target->id(), "wide_hit", now_ms, current_tick);
          if (!legacy_hit_roll_succeeds(legacy_accuracy_point(*attacker), extra_hit_roll)) {
            add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick,
                             now_ms, false, extra_hit_roll, 0,
                             "WideHit AccuracyPoint<=Random(SpeedPoint)");
            continue;
          }
          const auto extra_attack_power = attack_power;
          const auto extra_damage = std::max(0, extra_attack_power);
          std::int32_t extra_applied_damage = 0;
          bool extra_target_died = false;
          Monster* extra_slain_monster = nullptr;
          if (auto* player_target = as_player(extra_target); player_target != nullptr) {
            if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
              player_target->record_pk_hiter(attacker->id(), now_ms);
            }
            if (extra_damage > 0) {
              static_cast<void>(apply_legacy_struck_equipment_durability(
                  *player_target, attacker->id(), dispatch, current_tick, now_ms,
                  "LegacyCombat"));
            }
            const auto damage_result = player_target->apply_damage(extra_damage, current_tick);
            extra_applied_damage = damage_result.hp_damage;
            extra_target_died = player_target->is_dead();
            if (extra_target_died &&
                try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
              extra_target_died = false;
            }
            if (extra_target_died) {
              const auto death_clear = player_target->mark_dead(now_ms);
              dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
              apply_bad_kill_penalty(*attacker, *player_target, dispatch,
                                     current_tick, now_ms, "LegacyCombat");
              static_cast<void>(settle_player_death(*player_target, dispatch,
                                                    current_tick, now_ms));
            }
          } else if (auto* monster_target = as_monster(extra_target); monster_target != nullptr) {
            extra_applied_damage = apply_legacy_monster_damage(
                objects_, *monster_target, extra_damage, attacker->id(),
                config_, current_tick, now_ms);
            if (extra_applied_damage > 0) {
              monster_damaged = true;
              notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
            }
            extra_target_died = monster_target->is_dead();
            extra_slain_monster = extra_target_died ? monster_target : nullptr;
          }
          if (extra_applied_damage <= 0) {
            continue;
          }
          auto pending_extra_death_packets =
              extra_target_died && extra_slain_monster != nullptr
                  ? collect_legacy_death_packets(objects_, *extra_target)
                  : std::vector<PendingLegacyPacket>{};
          if (!extra_target_died || extra_slain_monster == nullptr) {
            for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
              if (watcher.id() != extra_target->id() &&
                  !is_legacy_visible_to(watcher, *extra_target)) {
                return;
              }
              queue_packet(dispatch, watcher.session_id(),
                           extra_target_died
                               ? make_death_packet(watcher.session_id(), *extra_target,
                                                   watcher.id() == extra_target->id())
                               : make_struck_packet(watcher.session_id(), *extra_target,
                                                    attacker->id(), extra_applied_damage, false),
                           extra_target_died ? 0 : kLegacyDirectStruckDelayMs);
            });
          }
          if (extra_slain_monster != nullptr) {
            finalize_monster_death(extra_slain_monster->id(), attacker->id(), dispatch,
                                   current_tick);
            add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick,
                             now_ms, true, sword_magic_id, extra_applied_damage, "WinExp");
            queue_legacy_packets(dispatch, std::move(pending_extra_death_packets));
          }
          add_legacy_trace(dispatch, "LegacyCombat",
                           extra_target_died ? "death" : "struck", effective_mail,
                           current_tick, now_ms, true, sword_magic_id, extra_applied_damage,
                           extra_target_died ? "SM_DEATH" : "SM_STRUCK");
        }
      }
      if (effective_ident == kCmCrossHit) {
        for (auto* extra_target : cross_targets) {
          if (extra_target == nullptr || extra_target == target ||
              objects_.find(extra_target->id()) == objects_.end() ||
              !is_attackable_target(*extra_target)) {
            continue;
          }
          const auto extra_hit_roll =
              legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                                  legacy_speed_point(*extra_target), attacker->id(),
                                  extra_target->id(), "cross_hit", now_ms, current_tick);
          if (!legacy_hit_roll_succeeds(legacy_accuracy_point(*attacker), extra_hit_roll)) {
            add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick,
                             now_ms, false, extra_hit_roll, 0,
                             "CrossHit AccuracyPoint<=Random(SpeedPoint)");
            continue;
          }
          auto extra_attack_power = cross_attack_power;
          if (as_player(extra_target) != nullptr) {
            extra_attack_power =
                delphi_round(static_cast<double>(extra_attack_power) * 0.8);
          }
          const auto extra_damage = std::max(0, extra_attack_power);
          std::int32_t extra_applied_damage = 0;
          bool extra_target_died = false;
          Monster* extra_slain_monster = nullptr;
          if (auto* player_target = as_player(extra_target); player_target != nullptr) {
            if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
              player_target->record_pk_hiter(attacker->id(), now_ms);
            }
            if (extra_damage > 0) {
              static_cast<void>(apply_legacy_struck_equipment_durability(
                  *player_target, attacker->id(), dispatch, current_tick, now_ms,
                  "LegacyCombat"));
            }
            const auto damage_result = player_target->apply_damage(extra_damage, current_tick);
            extra_applied_damage = damage_result.hp_damage;
            extra_target_died = player_target->is_dead();
            if (extra_target_died &&
                try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
              extra_target_died = false;
            }
            if (extra_target_died) {
              const auto death_clear = player_target->mark_dead(now_ms);
              dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
              apply_bad_kill_penalty(*attacker, *player_target, dispatch,
                                     current_tick, now_ms, "LegacyCombat");
              static_cast<void>(settle_player_death(*player_target, dispatch,
                                                    current_tick, now_ms));
            }
          } else if (auto* monster_target = as_monster(extra_target); monster_target != nullptr) {
            extra_applied_damage = apply_legacy_monster_damage(
                objects_, *monster_target, extra_damage, attacker->id(),
                config_, current_tick, now_ms);
            if (extra_applied_damage > 0) {
              monster_damaged = true;
              notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
            }
            extra_target_died = monster_target->is_dead();
            extra_slain_monster = extra_target_died ? monster_target : nullptr;
          }
          if (extra_applied_damage <= 0) {
            continue;
          }
          auto pending_extra_death_packets =
              extra_target_died && extra_slain_monster != nullptr
                  ? collect_legacy_death_packets(objects_, *extra_target)
                  : std::vector<PendingLegacyPacket>{};
          if (!extra_target_died || extra_slain_monster == nullptr) {
            for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
              if (watcher.id() != extra_target->id() &&
                  !is_legacy_visible_to(watcher, *extra_target)) {
                return;
              }
              queue_packet(dispatch, watcher.session_id(),
                           extra_target_died
                               ? make_death_packet(watcher.session_id(), *extra_target,
                                                   watcher.id() == extra_target->id())
                               : make_struck_packet(watcher.session_id(), *extra_target,
                                                    attacker->id(), extra_applied_damage, false),
                           extra_target_died ? 0 : kLegacyDirectStruckDelayMs);
            });
          }
          if (extra_slain_monster != nullptr) {
            finalize_monster_death(extra_slain_monster->id(), attacker->id(), dispatch,
                                   current_tick);
            add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick,
                             now_ms, true, sword_magic_id, extra_applied_damage, "WinExp");
            queue_legacy_packets(dispatch, std::move(pending_extra_death_packets));
          }
          add_legacy_trace(dispatch, "LegacyCombat",
                           extra_target_died ? "death" : "struck", effective_mail,
                           current_tick, now_ms, true, sword_magic_id, extra_applied_damage,
                           extra_target_died ? "SM_DEATH" : "SM_STRUCK");
        }
      }
      if (sword_magic_id != 0 && monster_damaged) {
        if (auto* user_magic = attacker->learned_magic_mutable(sword_magic_id);
            user_magic != nullptr) {
          const auto magic_it = magic_configs_.find(sword_magic_id);
          if (magic_it != magic_configs_.end()) {
            LegacyRandom fallback_random;
            auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
            const auto fixed_train_amount =
                (sword_magic_id == 12 || sword_magic_id == 25 || sword_magic_id == 26 ||
                 sword_magic_id == 34)
                    ? 1
                    : 0;
            const auto training =
                legacy_train_magic(*attacker, *user_magic, magic_it->second, random,
                                   fixed_train_amount);
            if (training.trained) {
              add_legacy_trace(dispatch, "LegacySkill", "train_skill", effective_mail,
                               current_tick, now_ms, true, training.train_amount,
                               training.cur_train,
                               training.leveled_up ? "sword_level_up" : "sword_train");
              schedule_legacy_magic_lvexp(*attacker, training, dispatch, effective_mail,
                                           current_tick, now_ms);
            }
          }
        }
      }
      break;
    }
    case ActorMailKind::spell: {
      auto attacker_it = objects_.find(mail.actor_id);
      if (attacker_it == objects_.end()) {
        break;
      }
      auto* attacker = as_player(attacker_it->second.get());
      if (attacker == nullptr || attacker->is_dead()) {
        break;
      }
      if (attacker->legacy_poison_stone_active(current_tick)) {
        add_legacy_trace(dispatch, "LegacySpell", "poison_stone_reject", mail,
                         current_tick, now_ms, false, 0, 0, "POISON_STONE");
        queue_packet(dispatch, attacker->session_id(),
                     make_ack_packet(attacker->session_id(), false));
        break;
      }

      const auto magic_id = static_cast<std::int32_t>(mail.game_message.tag);
      const auto magic_it = magic_configs_.find(magic_id);
      if (magic_it == magic_configs_.end()) {
        add_legacy_trace(dispatch, "LegacySpell", "magic_missing", mail, current_tick, now_ms,
                         false, magic_id, 0, "spell");
        queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), false));
        break;
      }
      cancel_trade_for(attacker->id(), dispatch, true);

      if (magic_it->second.legacy.legacy_present && magic_it->second.legacy.is_sword_skill) {
        if (!legacy_p14_sword_skill(magic_id)) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_unsupported", mail, current_tick,
                           now_ms, false, magic_id, 0, "P2");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }

        auto* user_magic = attacker->learned_magic_mutable(magic_id);
        if (user_magic == nullptr) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_unlearned", mail, current_tick,
                           now_ms, false, magic_id, 0, "GetMagic");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }

        if (magic_id == 27) {
          if (!attacker->legacy_rush_ready(now_ms)) {
            add_legacy_trace(dispatch, "LegacySkill", "sword_cooldown_reject", mail,
                             current_tick, now_ms, false, magic_id, 0,
                             "LatestRushRushTime");
            queue_packet(dispatch, attacker->session_id(),
                         make_ack_packet(attacker->session_id(), false));
            break;
          }
          const auto spell_point =
              legacy_spell_point(magic_it->second.legacy, static_cast<std::int32_t>(user_magic->level));
          if (spell_point > 0 && !attacker->spend_mp(spell_point)) {
            add_legacy_trace(dispatch, "LegacySkill", "sword_mp_reject", mail, current_tick,
                             now_ms, false, spell_point, 0, "GetSpellPoint");
            queue_packet(dispatch, attacker->session_id(),
                         make_ack_packet(attacker->session_id(), false));
            break;
          }
          const auto cleared_transparent = attacker->clear_legacy_transparent(current_tick);
          if (spell_point > 0 || cleared_transparent) {
            queue_packet(dispatch, attacker->session_id(),
                         make_health_spell_changed_packet(attacker->session_id(), *attacker));
          }
          if (cleared_transparent) {
            broadcast_legacy_char_status_changed(dispatch, *attacker);
          }
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), true));
          const auto rushed = handle_legacy_rush_rush(*attacker, *user_magic, magic_it->second,
                                                      mail, dispatch, current_tick, now_ms);
          if (rushed) {
            attacker->mark_legacy_rush(now_ms);
            LegacyRandom fallback_random;
            auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
            const auto training =
                legacy_train_magic(*attacker, *user_magic, magic_it->second, random);
            if (training.trained) {
              add_legacy_trace(dispatch, "LegacySkill", "train_skill", mail,
                               current_tick, now_ms, true, training.train_amount,
                               training.cur_train,
                               training.leveled_up ? "sword_level_up" : "sword_train");
              schedule_legacy_magic_lvexp(*attacker, training, dispatch, mail,
                                           current_tick, now_ms);
            }
          }
          break;
        }

        const auto throttle =
            attacker->begin_spell_attempt(now_ms, magic_it->second.legacy.delay_time, true);
        if (!throttle.allowed) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_cooldown_reject", mail,
                           current_tick, now_ms, false, throttle.over_count, 0,
                           "LatestSpellDelay");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }

        if (magic_id == 3 || magic_id == 7) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_passive", mail,
                           current_tick, now_ms, false, magic_id, 0, "CM_SPELL");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }

        if (magic_id == 26 && !attacker->legacy_fire_hit_ready(now_ms)) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_cooldown_reject", mail,
                           current_tick, now_ms, false, magic_id, 0, "SetAllowFireHit");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }

        if (magic_id == 12) {
          const auto enabled = !attacker->legacy_long_hit_enabled();
          attacker->set_legacy_long_hit_enabled(enabled);
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), true));
          queue_packet(dispatch, attacker->session_id(),
                       make_sword_state_packet(attacker->session_id(),
                                               enabled ? "+LNG" : "+ULNG"));
          add_legacy_trace(dispatch, "LegacySkill", "sword_toggle", mail, current_tick,
                           now_ms, true, magic_id, enabled ? 1 : 0, "CM_SPELL");
          break;
        }

        if (magic_id == 25) {
          const auto enabled = !attacker->legacy_wide_hit_enabled();
          attacker->set_legacy_wide_hit_enabled(enabled);
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), true));
          queue_packet(dispatch, attacker->session_id(),
                       make_sword_state_packet(attacker->session_id(),
                                               enabled ? "+WID" : "+UWID"));
          add_legacy_trace(dispatch, "LegacySkill", "sword_toggle", mail, current_tick,
                           now_ms, true, magic_id, enabled ? 1 : 0, "CM_SPELL");
          break;
        }

        if (magic_id == 34) {
          const auto enabled = !attacker->legacy_cross_hit_enabled();
          attacker->set_legacy_cross_hit_enabled(enabled);
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), true));
          queue_packet(dispatch, attacker->session_id(),
                       make_sword_state_packet(attacker->session_id(),
                                               enabled ? "+CRS" : "+UCRS"));
          add_legacy_trace(dispatch, "LegacySkill", "sword_toggle", mail, current_tick,
                           now_ms, true, magic_id, enabled ? 1 : 0, "CM_SPELL");
          break;
        }

        const auto spell_point =
            legacy_spell_point(magic_it->second.legacy, static_cast<std::int32_t>(user_magic->level));
        if (spell_point > 0 && !attacker->spend_mp(spell_point)) {
          add_legacy_trace(dispatch, "LegacySkill", "sword_mp_reject", mail, current_tick,
                           now_ms, false, spell_point, 0, "GetSpellPoint");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          break;
        }
        const auto cleared_transparent =
            magic_id != 18 && magic_id != 19 &&
            attacker->clear_legacy_transparent(current_tick);
        if (spell_point > 0 || cleared_transparent) {
          queue_packet(dispatch, attacker->session_id(),
                       make_health_spell_changed_packet(attacker->session_id(), *attacker));
        }
        if (cleared_transparent) {
          broadcast_legacy_char_status_changed(dispatch, *attacker);
        }

        const auto expire_tick =
            current_tick + legacy_delay_ms_to_ticks(20000, budgets_.tick_ms);
        attacker->prepare_legacy_sword_skill(magic_id, expire_tick);
        if (magic_id == 26) {
          attacker->mark_legacy_fire_hit(now_ms);
        }
        queue_packet(dispatch, attacker->session_id(),
                     make_ack_packet(attacker->session_id(), true));
        if (magic_id == 26) {
          queue_packet(dispatch, attacker->session_id(),
                       make_sword_state_packet(attacker->session_id(), "+FIR"));
        }
        add_legacy_trace(dispatch, "LegacySkill", "sword_prepare", mail, current_tick,
                         now_ms, true, magic_id,
                         static_cast<std::int32_t>(legacy_attack_ident_for_sword_skill(magic_id)),
                         "CM_SPELL");
        break;
      }

      if (legacy_spell_supported(magic_id, magic_it->second)) {
        auto* user_magic = attacker->learned_magic_mutable(magic_id);
        if (user_magic == nullptr) {
          add_legacy_trace(dispatch, "LegacySpell", "magic_unlearned", mail, current_tick,
                           now_ms, false, magic_id, 0, "GetMagic");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          queue_packet(dispatch, attacker->session_id(),
                       make_magic_fire_fail_packet(attacker->session_id(), *attacker));
          break;
        }

        const auto throttle =
            attacker->begin_spell_attempt(now_ms, magic_it->second.legacy.delay_time, false);
        if (!throttle.allowed) {
          add_legacy_trace(dispatch, "LegacySpell", "cooldown_reject", mail, current_tick,
                           now_ms, false, throttle.over_count, 0, "LatestSpellDelay");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          if (throttle.disconnect) {
            queue_force_disconnect(dispatch, attacker->session_id(), "speed_hack_spell");
          }
          break;
        }

        LegacyRandom fallback_random;
        auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;

        auto fail_magic_after_spell = [&](std::string label) {
          add_legacy_trace(dispatch, "LegacySpell", "spell_fail", mail, current_tick, now_ms,
                           false, magic_id, 0, std::move(label));
          queue_actor_origin_packet(objects_, dispatch, *attacker, true, [&](const Player& watcher) {
            queue_packet(dispatch, watcher.session_id(),
                         make_magic_fire_fail_packet(watcher.session_id(), *attacker));
          });
        };

        auto harmful_target_ok = [&](GameObject* candidate, std::string& reason) {
          if (candidate == nullptr || candidate->id() == attacker->id() ||
              !is_attackable_target(*candidate)) {
            reason = "target_missing";
            return false;
          }
          if (auto* player_target = as_player(candidate); player_target != nullptr) {
            reason = resolve_pk_block_reason(config_, *attacker, *player_target, now_ms);
            if (!reason.empty()) {
              return false;
            }
          }
          reason.clear();
          return true;
        };

        GameObject* target = nullptr;
        if (mail.target_actor_id != 0) {
          const auto target_it = objects_.find(mail.target_actor_id);
          if (target_it != objects_.end() && is_attackable_target(*target_it->second)) {
            target = target_it->second.get();
          }
        }
        if (target == nullptr && (mail.x != 0 || mail.y != 0)) {
          target = find_attack_target_by_position(objects_, *attacker, mail.x, mail.y);
        }
        if (target == nullptr && magic_id == 2) {
          target = attacker;
        }
        if (magic_id == 31) {
          target = attacker;
        }

        auto fire_x = target != nullptr ? target->x() : mail.x;
        auto fire_y = target != nullptr ? target->y() : mail.y;
        if (mail.x != 0 || mail.y != 0) {
          fire_x = mail.x;
          fire_y = mail.y;
        }
        if (fire_x == 0 && fire_y == 0 &&
            (magic_id == 8 || magic_id == 37 || magic_id == 14 || magic_id == 15 || magic_id == 18 ||
             magic_id == 19 || magic_id == 21 || magic_id == 22 || magic_id == 24 ||
             magic_id == 31 || magic_id == 36 || magic_id == 16 || magic_id == 17 ||
             magic_id == 30)) {
          fire_x = attacker->x();
          fire_y = attacker->y();
        }

        const auto spell_point =
            legacy_spell_point(magic_it->second.legacy, static_cast<std::int32_t>(user_magic->level));
        if (spell_point > 0 && !attacker->spend_mp(spell_point)) {
          add_legacy_trace(dispatch, "LegacySpell", "mp_reject", mail, current_tick, now_ms,
                           false, spell_point, 0, "GetSpellPoint");
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          queue_actor_origin_packet(objects_, dispatch, *attacker, true, [&](const Player& watcher) {
            queue_packet(dispatch, watcher.session_id(),
                         make_magic_fire_fail_packet(watcher.session_id(), *attacker));
          });
          break;
        }
        const auto cleared_transparent = attacker->clear_legacy_transparent(current_tick);
        if (spell_point > 0 || cleared_transparent) {
          queue_packet(dispatch, attacker->session_id(),
                       make_health_spell_changed_packet(attacker->session_id(), *attacker));
        }
        if (cleared_transparent) {
          broadcast_legacy_char_status_changed(dispatch, *attacker);
        }

        queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), true));
        add_legacy_trace(dispatch, "LegacySpell", "ack", mail, current_tick, now_ms, true,
                         magic_id, 0, "DoSpell");

        queue_actor_origin_packet(
            objects_, dispatch, *attacker, false, [&](const Player& watcher) {
          queue_packet(dispatch, watcher.session_id(),
                       make_spell_packet(watcher.session_id(), *attacker, mail, magic_configs_));
        });
        add_legacy_trace(dispatch, "LegacySpell", "spell_broadcast", mail, current_tick, now_ms,
                         true, magic_id, 0, "RM_SPELL");

        auto make_delayed = [&]() {
          ActorMail delayed;
          delayed.kind = ActorMailKind::legacy_delayed_effect;
          delayed.map_id = config_.id;
          delayed.actor_id = attacker->id();
          delayed.magic_id = magic_id;
          delayed.x = fire_x;
          delayed.y = fire_y;
          return delayed;
        };

        auto queue_delayed_hit = [&](GameObject& hit_target, std::int32_t power,
                                     std::uint32_t delay_ms, std::int32_t range,
                                     LegacyDelayedEffectKind kind) {
          auto delayed = make_delayed();
          delayed.delayed_effect_kind = kind;
          delayed.target_actor_id = hit_target.id();
          delayed.power = power;
          delayed.undead_power = legacy_player_undead_power(*attacker, item_configs_);
          delayed.range = range;
          delayed_mail_wheel_.schedule(current_tick,
                                       legacy_delay_ms_to_ticks(delay_ms, budgets_.tick_ms),
                                       delayed);
          add_legacy_trace(dispatch, "LegacySpell",
                           kind == LegacyDelayedEffectKind::delay_magic
                               ? "delay_magic_queued"
                               : "mag_struck_queued",
                           mail, current_tick, now_ms, true, magic_id, power,
                           std::to_string(delay_ms) + "ms");
        };

        auto queue_delayed_poison = [&](GameObject& poison_target, std::int32_t poison_kind,
                                        std::int32_t poison_seconds,
                                        std::int32_t poison_level) {
          auto delayed = make_delayed();
          delayed.delayed_effect_kind = LegacyDelayedEffectKind::make_poison;
          delayed.target_actor_id = poison_target.id();
          delayed.poison_kind = poison_kind;
          delayed.poison_level = poison_level;
          delayed.duration_ticks = legacy_delay_ms_to_ticks(
              static_cast<std::uint32_t>(std::max(poison_seconds, 1) * 1000), budgets_.tick_ms);
          delayed_mail_wheel_.schedule(current_tick,
                                       legacy_delay_ms_to_ticks(1000, budgets_.tick_ms),
                                       delayed);
          add_legacy_trace(dispatch, "LegacySpell", "poison_queued", mail, current_tick,
                           now_ms, true, poison_kind, poison_seconds, "RM_MAKEPOISON");
        };

        auto queue_delayed_transparent = [&](Player& transparent_target,
                                             std::int32_t transparent_seconds) {
          auto delayed = make_delayed();
          delayed.delayed_effect_kind = LegacyDelayedEffectKind::transparent;
          delayed.target_actor_id = transparent_target.id();
          delayed.duration_ticks = legacy_delay_ms_to_ticks(
              static_cast<std::uint32_t>(std::max(transparent_seconds, 1) * 1000),
              budgets_.tick_ms);
          delayed_mail_wheel_.schedule(current_tick,
                                       legacy_delay_ms_to_ticks(800, budgets_.tick_ms),
                                       delayed);
          add_legacy_trace(dispatch, "LegacySpell", "transparent_queued", mail,
                           current_tick, now_ms, true, magic_id, transparent_seconds,
                           "RM_TRANSPARENT");
        };

        auto apply_direct_magic = [&](GameObject& hit_target, std::int32_t raw_power,
                                      std::string_view label) {
          const auto damage = legacy_magic_defense_damage(hit_target, raw_power, random,
                                                          current_tick, budgets_.tick_ms);
          const auto result = apply_legacy_magic_damage(objects_, item_configs_, dispatch, *attacker,
                                                        hit_target, config_, damage,
                                                        current_tick, now_ms);
          if (result.applied_damage > 0 && as_monster(&hit_target) != nullptr) {
            notify_owned_slaves_target(*attacker, hit_target.id(), now_ms);
          }
          if (result.target_died) {
            if (auto* player_target = as_player(&hit_target); player_target != nullptr) {
              apply_bad_kill_penalty(*attacker, *player_target, dispatch, current_tick,
                                     now_ms, "LegacySpell");
              static_cast<void>(settle_player_death(*player_target, dispatch, current_tick,
                                                    now_ms));
            }
          }
          auto pending_death_packets =
              result.slain_monster_id != 0
                  ? collect_legacy_death_packets(objects_, hit_target)
                  : std::vector<PendingLegacyPacket>{};
          if (result.slain_monster_id != 0) {
            finalize_monster_death(result.slain_monster_id, attacker->id(), dispatch,
                                   current_tick);
          }
          if (result.target_died) {
            if (result.slain_monster_id != 0) {
              queue_legacy_packets(dispatch, std::move(pending_death_packets));
            } else {
              queue_legacy_death_packet(objects_, dispatch, hit_target);
            }
          }
          add_legacy_trace(dispatch, "LegacySpell",
                           result.target_died ? "death" : "mag_struck", mail,
                           current_tick, now_ms, result.applied_damage > 0, magic_id,
                           result.applied_damage, std::string(label));
          if (result.slain_monster_id != 0) {
            add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms,
                             true, magic_id, result.applied_damage, "WinExp");
          }
          return result.applied_damage > 0;
        };

        bool train = false;
        bool send_magic_fire = true;
        bool spell_branch_aborted = false;
        std::uint64_t fire_target_id = target != nullptr ? target->id() : 0;
        auto send_magic_fire_now = [&]() {
          queue_actor_origin_packet(objects_, dispatch, *attacker, true, [&](const Player& watcher) {
            queue_packet(dispatch, watcher.session_id(),
                         make_magic_fire_packet(watcher.session_id(), *attacker, fire_x, fire_y,
                                                magic_it->second, fire_target_id));
          });
          add_legacy_trace(dispatch, "LegacySpell", "magic_fire", mail, current_tick, now_ms,
                           true, magic_id, 0, "SM_MAGICFIRE");
        };
        switch (magic_id) {
          case 1:
          case 5: {
            std::string reason;
            if (!harmful_target_ok(target, reason)) {
              fire_target_id = 0;
              add_legacy_trace(dispatch, "LegacySpell", "target_reject", mail,
                               current_tick, now_ms, false, magic_id, 0, reason);
              break;
            }
            if (!legacy_mag_can_hit_target(attacker->x(), attacker->y(), target) ||
                std::abs(target->x() - fire_x) > 1 || std::abs(target->y() - fire_y) > 1) {
              fire_target_id = 0;
              add_legacy_trace(dispatch, "LegacySpell", "target_reject", mail,
                               current_tick, now_ms, false, magic_id, 0, "MagCanHitTarget");
              break;
            }
            const auto anti_roll = random.random(10);
            const auto anti_magic = legacy_actor_anti_magic(*target);
            const auto anti_pass = legacy_anti_magic_pass(anti_magic, anti_roll);
            add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail, current_tick,
                             now_ms, anti_pass, anti_roll,
                             anti_magic,
                             "AntiMagic");
            if (anti_pass) {
              const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                       user_magic->level, random);
              queue_delayed_hit(*target, power, 600, 2, LegacyDelayedEffectKind::delay_magic);
              train = as_monster(target) != nullptr;
            } else {
              fire_target_id = 0;
            }
            break;
          }
          case 2: {
            if (auto* player_target = as_player(target); player_target != nullptr) {
              const auto power =
                  legacy_heal_power(*attacker, magic_it->second.legacy, user_magic->level, random);
              if (player_target->character().ability.hp < player_target->character().ability.max_hp) {
                auto delayed = make_delayed();
                delayed.delayed_effect_kind = LegacyDelayedEffectKind::mag_healing;
                delayed.target_actor_id = player_target->id();
                delayed.power = power;
                delayed_mail_wheel_.schedule(
                    current_tick, legacy_delay_ms_to_ticks(800, budgets_.tick_ms), delayed);
                add_legacy_trace(dispatch, "LegacySpell", "healing_queued", mail,
                                 current_tick, now_ms, true, magic_id, power, "800ms");
                train = true;
              }
            }
            break;
          }
          case 6: {
            send_magic_fire = false;
            std::string reason;
            if (!harmful_target_ok(target, reason)) {
              fail_magic_after_spell(reason.empty() ? "target_missing" : reason);
              spell_branch_aborted = true;
              break;
            }
            auto poison_slot = find_legacy_poison_powder_slot(*attacker, item_configs_);
            if (!poison_slot.has_value()) {
              fail_magic_after_spell("poison_powder_missing");
              spell_branch_aborted = true;
              break;
            }
            const auto poison_shape = poison_slot->config != nullptr ? poison_slot->config->shape : 0;
            consume_legacy_bujuk_slot(*poison_slot, 1);
            queue_packet(dispatch, attacker->session_id(),
                         make_dura_change_packet(attacker->session_id(), poison_slot->slot,
                                                 *poison_slot->item, item_configs_));
            if (auto removed = clear_legacy_bujuk_slot_if_spent(*poison_slot);
                removed.has_value()) {
              queue_packet(dispatch, attacker->session_id(),
                           make_del_item_packet(attacker->session_id(), attacker->id(),
                                                *removed, item_configs_));
            }
            add_legacy_trace(dispatch, "LegacySpell", "poison_powder_used", mail,
                             current_tick, now_ms, true, poison_shape,
                             poison_slot->item != nullptr ? poison_slot->item->dura : 0,
                             poison_slot->slot == kEquipBujuk ? "U_BUJUK" : "U_ARMRINGL");
            const auto anti_poison = target != nullptr ? legacy_actor_anti_poison(*target) : 0;
            const auto gate_roll = random.random(7 + std::max(anti_poison, 0));
            const auto gate_pass = 6 >= gate_roll;
            add_legacy_trace(dispatch, "LegacySpell", "poison_gate", mail, current_tick,
                             now_ms, gate_pass, gate_roll, anti_poison,
                             "Random(7+AntiPoison)");
            if (gate_pass && target != nullptr) {
              if (poison_shape == 1) {
                const auto seconds = legacy_poison_seconds(*attacker, magic_it->second.legacy,
                                                           user_magic->level, 30, random);
                queue_delayed_poison(*target, kLegacyPoisonDecHealth, seconds,
                                     user_magic->level);
                train = true;
              } else if (poison_shape == 2) {
                const auto seconds = legacy_poison_seconds(*attacker, magic_it->second.legacy,
                                                           user_magic->level, 40, random);
                queue_delayed_poison(*target, kLegacyPoisonDamageArmor, seconds,
                                     user_magic->level);
                train = true;
              }
            }
            send_magic_fire = true;
            break;
          }
          case 8:
          case 37: {
            auto pushed = 0;
            const auto push_level = static_cast<std::int32_t>(user_magic->level);
            auto targets = collect_legacy_area_targets(objects_, *attacker, config_,
                                                       attacker->x(), attacker->y(), 1, false);
            for (auto* push_target : targets) {
              if (actor_level(*attacker) <= actor_level(*push_target)) {
                continue;
              }
              const auto level_gap = actor_level(*attacker) - actor_level(*push_target);
              const auto gate_roll = random.random(20);
              add_legacy_trace(dispatch, "LegacySpell", "push_gate", mail, current_tick,
                               now_ms, gate_roll < 6 + push_level * 3 + level_gap,
                               gate_roll, 0, "Random(20)");
              if (gate_roll >= 6 + push_level * 3 + level_gap) {
                continue;
              }
              const auto push_count = 1 + std::max(0, push_level - 1) + random.random(2);
              const auto dir = next_direction(attacker->x(), attacker->y(), push_target->x(),
                                              push_target->y());
              const auto [dx, dy] = direction_delta(dir);
              auto moved = 0;
              for (std::int32_t step = 0; step < push_count; ++step) {
                const auto nx = push_target->x() + dx;
                const auto ny = push_target->y() + dy;
                if (!environment_.can_walk(nx, ny, false)) {
                  break;
                }
                if (environment_.move_to_moving_object(
                        push_target->x(), push_target->y(), push_target->id(), nx, ny,
                        false, now_ms, moving_state_for(*push_target)) != 1) {
                  break;
                }
                ActorMail move_mail;
                move_mail.kind = ActorMailKind::move;
                move_mail.map_id = config_.id;
                move_mail.actor_id = push_target->id();
                move_mail.x = nx;
                move_mail.y = ny;
                move_mail.dir = static_cast<std::uint8_t>((dir + 4) % 8);
                if (auto* pushed_monster = as_monster(push_target); pushed_monster != nullptr) {
                  pushed_monster->set_dir(move_mail.dir);
                }
                push_target->on_mail(move_mail, context);
                for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
                  if (watcher.id() != push_target->id() &&
                      !is_legacy_visible_to(watcher, *push_target)) {
                    return;
                  }
                  queue_packet(dispatch, watcher.session_id(),
                               make_turn_like_packet(watcher.session_id(), kSmWalk,
                                                     *push_target, false));
                });
                ++moved;
              }
              if (moved > 0) {
                ++pushed;
                add_legacy_trace(dispatch, "LegacySpell", "push", mail, current_tick,
                                 now_ms, true, moved, 0, "RM_PUSH");
              }
            }
            train = pushed > 0;
            break;
          }
          case 9:
          case 10: {
            const auto dir = next_direction(attacker->x(), attacker->y(), fire_x, fire_y);
            const auto [dx, dy] = direction_delta(dir);
            const auto distance = magic_id == 9 ? 5 : 8;
            const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                     user_magic->level, random);
            auto hit_count = 0;
            for (std::int32_t step = 1; step <= distance; ++step) {
              const auto sx = attacker->x() + dx * step;
              const auto sy = attacker->y() + dy * step;
              if (!environment_.in_bounds(sx, sy)) {
                break;
              }
              if (!environment_.can_fire_fly_line(attacker->x(), attacker->y(), sx, sy)) {
                add_legacy_trace(dispatch, "LegacySpell", "line_blocked", mail,
                                 current_tick, now_ms, false, magic_id, step,
                                 "CanFireFly");
                break;
              }
              auto* line_target = find_legacy_line_target(objects_, *attacker, sx, sy);
              std::string reason;
              if (line_target == nullptr || !harmful_target_ok(line_target, reason)) {
                continue;
              }
              const auto anti_roll = random.random(10);
              const auto anti_magic = legacy_actor_anti_magic(*line_target);
              add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail, current_tick,
                               now_ms, legacy_anti_magic_pass(anti_magic, anti_roll), anti_roll,
                               anti_magic, "PassThrough");
              if (!legacy_anti_magic_pass(anti_magic, anti_roll)) {
                continue;
              }
              const auto line_power =
                  magic_id == 10 && actor_undead(*line_target)
                      ? delphi_round(static_cast<double>(power) * 1.5)
                      : power;
              queue_delayed_hit(*line_target, line_power, 600, 0,
                                LegacyDelayedEffectKind::mag_struck);
              ++hit_count;
            }
            train = hit_count > 0;
            break;
          }
          case 11:
          case 35: {
            std::string reason;
            if (!harmful_target_ok(target, reason)) {
              fire_target_id = 0;
              add_legacy_trace(dispatch, "LegacySpell", "target_reject", mail,
                               current_tick, now_ms, false, magic_id, 0, reason);
              break;
            }
            const auto anti_roll = random.random(10);
            const auto anti_magic = legacy_actor_anti_magic(*target);
            const auto anti_pass = legacy_anti_magic_pass(anti_magic, anti_roll);
            add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail, current_tick,
                             now_ms, anti_pass, anti_roll,
                             anti_magic,
                             "AntiMagic");
            if (anti_pass) {
              auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                 user_magic->level, random);
              if (magic_id == 11 && actor_undead(*target)) {
                power = delphi_round(static_cast<double>(power) * 1.5);
              } else if (magic_id == 35 && as_monster(target) != nullptr &&
                         !actor_undead(*target)) {
                power = delphi_round(static_cast<double>(power) * 1.2);
              }
              queue_delayed_hit(*target, power, 600, 2, LegacyDelayedEffectKind::mag_struck);
              train = as_monster(target) != nullptr;
            } else {
              fire_target_id = 0;
            }
            break;
          }
          case 13:
          case 17:
          case 14:
          case 15:
          case 16:
          case 30:
          case 36:
          case 18:
          case 19: {
            const auto bujuk_count = magic_id == 30 ? 5 : 1;
            auto bujuk_slot = find_legacy_bujuk_slot(*attacker, item_configs_, bujuk_count);
            if (!bujuk_slot.has_value()) {
              fail_magic_after_spell("bujuk_missing");
              spell_branch_aborted = true;
              break;
            }
            consume_legacy_bujuk_slot(*bujuk_slot, bujuk_count);
            queue_packet(dispatch, attacker->session_id(),
                         make_dura_change_packet(attacker->session_id(), bujuk_slot->slot,
                                                 *bujuk_slot->item, item_configs_));
            if (bujuk_count == 1) {
              if (auto removed = clear_legacy_bujuk_slot_if_spent(*bujuk_slot);
                  removed.has_value()) {
                queue_packet(dispatch, attacker->session_id(),
                             make_del_item_packet(attacker->session_id(), attacker->id(),
                                                  *removed, item_configs_));
              }
            }
            add_legacy_trace(dispatch, "LegacySpell", "bujuk_used", mail, current_tick,
                             now_ms, true, magic_id,
                             bujuk_slot->item != nullptr ? bujuk_slot->item->dura : 0,
                             bujuk_slot->slot == kEquipBujuk ? "U_BUJUK" : "U_ARMRINGL");

            if (magic_id == 16) {
              if (!environment_.can_walk(fire_x, fire_y, true)) {
                add_legacy_trace(dispatch, "LegacySpell", "holy_curtain_center_reject",
                                 mail, current_tick, now_ms, false, magic_id, 0,
                                 "CanWalk");
                break;
              }

              const auto seconds = legacy_holy_curtain_seconds(
                  *attacker, magic_it->second.legacy, user_magic->level, random);
              const auto duration_ms =
                  static_cast<std::uint64_t>(std::max(seconds, 1)) * 1000ULL;
              std::vector<std::uint64_t> seized_ids;
              bool holy_ok = true;
              for (std::int32_t hx = fire_x - 1; hx <= fire_x + 1 && holy_ok; ++hx) {
                for (std::int32_t hy = fire_y - 1; hy <= fire_y + 1 && holy_ok; ++hy) {
                  std::vector<std::uint64_t> ids_at_cell;
                  for (const auto& [actor_id, object] : objects_) {
                    if (object->x() == hx && object->y() == hy &&
                        as_monster(object.get()) != nullptr) {
                      ids_at_cell.push_back(actor_id);
                    }
                  }
                  std::sort(ids_at_cell.begin(), ids_at_cell.end(), std::greater<>());
                  for (const auto actor_id : ids_at_cell) {
                    auto* monster = as_monster(objects_.at(actor_id).get());
                    const auto level_roll = random.random(4);
                    const auto pass =
                        monster != nullptr && monster->master_actor_id() == 0 &&
                        monster->level() <
                            static_cast<std::int32_t>(attacker->character().ability.level) - 1 +
                                level_roll &&
                        monster->level() < 50;
                    add_legacy_trace(dispatch, "LegacySpell", "holy_curtain_gate",
                                     mail, current_tick, now_ms, pass, level_roll,
                                     monster != nullptr ? monster->level() : 0,
                                     "Random(4)");
                    if (!pass) {
                      holy_ok = false;
                      break;
                    }
                    monster->make_legacy_holy_seize(duration_ms, now_ms);
                    seized_ids.push_back(monster->id());
                  }
                }
              }

              if (holy_ok && !seized_ids.empty()) {
                const auto group_id =
                    (attacker->id() << 32U) ^ static_cast<std::uint64_t>(now_ms) ^
                    current_tick;
                constexpr std::array<std::pair<std::int32_t, std::int32_t>, 8> kOffsets{{
                    {-1, -2},
                    {1, -2},
                    {-2, -1},
                    {2, -1},
                    {-2, 1},
                    {2, 1},
                    {-1, 2},
                    {1, 2},
                }};
                for (const auto& [dx, dy] : kOffsets) {
                  LegacyEventRecord event;
                  event.map_id = config_.id;
                  event.x = fire_x + dx;
                  event.y = fire_y + dy;
                  event.type = LegacyEventType::holy_curtain;
                  event.open_start_ms = now_ms;
                  event.continue_ms = duration_ms;
                  event.run_start_ms = now_ms;
                  event.run_tick_ms = 500;
                  event.owner_actor_id = attacker->id();
                  event.holy_group_id = group_id;
                  event.blocks_walk = true;
                  dispatch.legacy_event_creates.push_back(event);
                }
                LegacyHolyCurtainGroup group;
                group.id = group_id;
                group.map_id = config_.id;
                group.open_start_ms = now_ms;
                group.seize_ms = duration_ms;
                group.seized_actor_ids = seized_ids;
                dispatch.legacy_holy_curtain_groups.push_back(std::move(group));
                train = true;
              }
              add_legacy_trace(dispatch, "LegacySpell", "holy_curtain", mail,
                               current_tick, now_ms, train,
                               static_cast<std::int32_t>(seized_ids.size()), seconds,
                               "MagMakeHolyCurtain");
              break;
            }

            if (magic_id == 17) {
              train = summon_player_slave(*attacker, "__WhiteSkeleton", user_magic->level, 1,
                                          10ULL * 24ULL * 60ULL * 60ULL, dispatch,
                                          current_tick, now_ms, mail);
              break;
            }

            if (magic_id == 30) {
              train = summon_player_slave(*attacker, "__ShinSu", user_magic->level, 1,
                                          10ULL * 24ULL * 60ULL * 60ULL, dispatch,
                                          current_tick, now_ms, mail);
              break;
            }

            if (magic_id == 13) {
              std::string reason;
              if (harmful_target_ok(target, reason) &&
                  legacy_mag_can_hit_target(attacker->x(), attacker->y(), target) &&
                  std::abs(target->x() - fire_x) <= 1 &&
                  std::abs(target->y() - fire_y) <= 1) {
                const auto anti_roll = random.random(10);
                const auto anti_magic = target != nullptr ? legacy_actor_anti_magic(*target) : 0;
                add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail,
                                 current_tick, now_ms,
                                 legacy_anti_magic_pass(anti_magic, anti_roll),
                                 anti_roll, anti_magic, "AntiMagic");
                if (legacy_anti_magic_pass(anti_magic, anti_roll)) {
                  const auto power = legacy_soul_fire_power(*attacker, magic_it->second.legacy,
                                                            user_magic->level, random);
                  queue_delayed_hit(*target, power, 1200, 2,
                                    LegacyDelayedEffectKind::delay_magic);
                  train = as_monster(target) != nullptr;
                }
              } else {
                add_legacy_trace(dispatch, "LegacySpell", "soul_fire_target_reject",
                                 mail, current_tick, now_ms, false, magic_id, 0,
                                 reason.empty() ? "MagCanHitTarget" : reason);
              }
              break;
            }

            if (magic_id == 36) {
              const auto seconds = legacy_defence_status_seconds(
                  *attacker, magic_it->second.legacy, user_magic->level, random);
              const auto duration_ticks = legacy_delay_ms_to_ticks(
                  static_cast<std::uint32_t>(std::max(seconds, 1) * 1000),
                  budgets_.tick_ms);
              const auto sc_max = packed_max(attacker->character().ability.sc);
              const auto dc_up = std::min(8, ((sc_max - 1) / 5) + 1);
              auto applied = 0;
              if (attacker->activate_legacy_dc_up(duration_ticks, current_tick, dc_up)) {
                ++applied;
                queue_packet(dispatch, attacker->session_id(),
                             make_ability_packet(attacker->session_id(),
                                                 attacker->character()));
                queue_packet(dispatch, attacker->session_id(),
                             make_sub_ability_packet(attacker->session_id(), *attacker));
              }
              for (const auto slave_id : attacker->slave_actor_ids()) {
                const auto slave_it = objects_.find(slave_id);
                auto* slave = slave_it != objects_.end() ? as_monster(slave_it->second.get())
                                                         : nullptr;
                if (slave == nullptr || slave->master_actor_id() != attacker->id()) {
                  continue;
                }
                if (slave->activate_legacy_dc_up(duration_ticks, current_tick, dc_up)) {
                  ++applied;
                }
              }
              add_legacy_trace(dispatch, "LegacySpell", "dc_up", mail,
                               current_tick, now_ms, applied > 0, applied, dc_up,
                               "MagDcUp");
              train = applied > 0;
              break;
            }

            if (magic_id == 14 || magic_id == 15) {
              const auto seconds = legacy_defence_status_seconds(
                  *attacker, magic_it->second.legacy, user_magic->level, random);
              auto targets = collect_legacy_area_targets(objects_, *attacker, config_,
                                                         fire_x, fire_y, 3, true);
              auto applied = 0;
              for (auto* friend_target : targets) {
                auto* player_target = as_player(friend_target);
                const auto duration_ticks = legacy_delay_ms_to_ticks(
                    static_cast<std::uint32_t>(std::max(seconds, 1) * 1000),
                    budgets_.tick_ms);
                auto changed = false;
                if (player_target != nullptr) {
                  changed = magic_id == 14
                                ? player_target->activate_legacy_magic_defence_up(
                                      duration_ticks, current_tick)
                                : player_target->activate_legacy_defence_up(
                                      duration_ticks, current_tick);
                } else if (auto* monster_target = as_monster(friend_target);
                           monster_target != nullptr &&
                           monster_target->master_actor_id() == attacker->id()) {
                  changed = magic_id == 14
                                ? monster_target->activate_legacy_magic_defence_up(
                                      duration_ticks, current_tick)
                                : monster_target->activate_legacy_defence_up(
                                      duration_ticks, current_tick);
                }
                if (changed) {
                  ++applied;
                  if (player_target != nullptr) {
                    broadcast_legacy_char_status_changed(dispatch, *player_target);
                    queue_packet(dispatch, player_target->session_id(),
                                 make_ability_packet(player_target->session_id(),
                                                     player_target->character()));
                    queue_packet(dispatch, player_target->session_id(),
                                 make_sub_ability_packet(player_target->session_id(),
                                                         *player_target));
                  }
                }
              }
              add_legacy_trace(dispatch, "LegacySpell", "defence_area", mail,
                               current_tick, now_ms, applied > 0, applied, seconds,
                               magic_id == 14 ? "MagMagDefenceUp" : "MagDefenceUp");
              train = applied > 0;
              break;
            }

            const auto transparent_seconds =
                legacy_transparent_seconds(*attacker, magic_it->second.legacy,
                                           user_magic->level, random);
            if (magic_id == 18) {
              train = attacker->activate_legacy_transparent(
                  legacy_delay_ms_to_ticks(
                      static_cast<std::uint32_t>(std::max(transparent_seconds, 1) * 1000),
                      budgets_.tick_ms),
                  current_tick);
              if (train) {
                broadcast_legacy_char_status_changed(dispatch, *attacker);
              }
              auto cleared_targets = 0;
              for (auto& [actor_id, object] : objects_) {
                auto* monster = as_monster(object.get());
                if (monster == nullptr || monster->aggro_target_id() != attacker->id()) {
                  continue;
                }
                const auto far = std::abs(monster->x() - attacker->x()) > 1 ||
                                 std::abs(monster->y() - attacker->y()) > 1;
                auto clear_target = far;
                if (!far) {
                  const auto hide_roll = random.random(2);
                  clear_target = hide_roll == 0;
                  add_legacy_trace(dispatch, "LegacySpell", "transparent_hide_gate",
                                   mail, current_tick, now_ms, clear_target, hide_roll, 0,
                                   "Random(2)");
                }
                if (clear_target) {
                  monster->clear_aggro_target();
                  ++cleared_targets;
                }
              }
              add_legacy_trace(dispatch, "LegacySpell", "transparent_apply", mail,
                               current_tick, now_ms, train, magic_id,
                               transparent_seconds, "MagMakePrivateTransparent");
              if (cleared_targets > 0) {
                add_legacy_trace(dispatch, "LegacySpell", "transparent_clear_target",
                                 mail, current_tick, now_ms, true, cleared_targets, 0,
                                 "TargetCret");
              }
              break;
            }

            auto targets = collect_legacy_area_targets(objects_, *attacker, config_, fire_x,
                                                       fire_y, 1, true);
            auto queued = 0;
            for (auto* friend_target : targets) {
              auto* player_target = as_player(friend_target);
              if (player_target == nullptr ||
                  player_target->legacy_transparent_active(current_tick)) {
                continue;
              }
              queue_delayed_transparent(*player_target, transparent_seconds);
              ++queued;
            }
            train = queued > 0;
            add_legacy_trace(dispatch, "LegacySpell", "group_transparent", mail,
                             current_tick, now_ms, train, queued, transparent_seconds,
                             "MagMakeGroupTransparent");
            break;
          }
          case 20: {
            auto* monster_target = as_monster(target);
            std::string reason;
            if (target == nullptr || !harmful_target_ok(target, reason)) {
              add_legacy_trace(dispatch, "LegacySlave", "lighting_shock_reject", mail,
                               current_tick, now_ms, false, magic_id, 0,
                               reason.empty() ? "target_missing" : reason);
              break;
            }

            const auto shock_level = static_cast<std::int32_t>(user_magic->level);
            const auto top_roll = monster_target != nullptr
                                      ? random.random(std::max(4 - shock_level, 1))
                                      : 1;
            if (monster_target == nullptr || top_roll != 0) {
              const auto train_roll = random.random(2);
              train = train_roll == 0;
              add_legacy_trace(dispatch, "LegacySlave", "lighting_shock_no_effect",
                               mail, current_tick, now_ms, train, train_roll, top_roll,
                               "Random(2)");
              break;
            }

            monster_target->lose_target();
            monster_target->clear_target_xy();
            if (monster_target->master_actor_id() == attacker->id()) {
              monster_target->make_legacy_holy_seize(
                  static_cast<std::uint64_t>(10 + shock_level * 5) * 1000ULL, now_ms);
              add_legacy_trace(dispatch, "LegacySlave", "holy_seize", mail,
                               current_tick, now_ms, true, magic_id, shock_level,
                               "MakeHolySeize");
              train = true;
              break;
            }

            const auto action_roll = random.random(2);
            if (action_roll == 0 &&
                monster_target->level() <=
                    static_cast<std::int32_t>(attacker->character().ability.level) + 2) {
              const auto branch_roll = random.random(3);
              if (branch_roll == 0) {
                const auto power_roll =
                    random.random(20 +
                                  static_cast<std::int32_t>(attacker->character().ability.level) +
                                  shock_level * 5);
                if (10 + monster_target->level() < power_roll) {
                  const auto max_slaves = 2 + shock_level;
                  const auto can_tame =
                      monster_target->tameable() && monster_target->life_attrib() == 0 &&
                      monster_target->level() < 50 &&
                      static_cast<std::int32_t>(attacker->slave_actor_ids().size()) < max_slaves;
                  if (can_tame) {
                    auto tame_roll_range = monster_target->max_hp() / 100;
                    tame_roll_range = tame_roll_range <= 2 ? 2 : tame_roll_range * 2;
                    const auto tame_roll = random.random(tame_roll_range);
                    if (monster_target->master_actor_id() != attacker->id() && tame_roll == 0) {
                      monster_target->break_legacy_crazy();
                      monster_target->break_legacy_holy_seize();
                      train = tame_player_slave(*attacker, *monster_target, shock_level,
                                                max_slaves, dispatch, current_tick,
                                                now_ms, mail);
                      break;
                    }
                    const auto death_roll = random.random(20);
                    if (death_roll == 0) {
                      const auto result = apply_legacy_magic_damage(
                          objects_, item_configs_, dispatch, *attacker, *monster_target,
                          config_, monster_target->hp(), current_tick, now_ms);
                      if (result.slain_monster_id != 0) {
                        auto pending_death_packets =
                            collect_legacy_death_packets(objects_, *monster_target);
                        finalize_monster_death(result.slain_monster_id, attacker->id(),
                                               dispatch, current_tick);
                        queue_legacy_packets(dispatch, std::move(pending_death_packets));
                      }
                      add_legacy_trace(dispatch, "LegacySlave", "lighting_shock_death",
                                       mail, current_tick, now_ms, result.target_died,
                                       magic_id, death_roll, "WAbil.HP:=0");
                    }
                  } else if (monster_target->legacy_undead()) {
                    const auto undead_roll = random.random(2);
                    if (undead_roll == 0) {
                      const auto result = apply_legacy_magic_damage(
                          objects_, item_configs_, dispatch, *attacker, *monster_target,
                          config_, monster_target->hp(), current_tick, now_ms);
                      if (result.slain_monster_id != 0) {
                        auto pending_death_packets =
                            collect_legacy_death_packets(objects_, *monster_target);
                        finalize_monster_death(result.slain_monster_id, attacker->id(),
                                               dispatch, current_tick);
                        queue_legacy_packets(dispatch, std::move(pending_death_packets));
                      }
                      add_legacy_trace(dispatch, "LegacySlave", "lighting_shock_death",
                                       mail, current_tick, now_ms, result.target_died,
                                       magic_id, undead_roll, "LA_UNDEAD");
                    }
                  }
                } else if (!monster_target->legacy_undead()) {
                  const auto crazy_roll = random.random(2);
                  if (crazy_roll == 0) {
                    const auto crazy_seconds = 10 + random.random(20);
                    monster_target->make_legacy_crazy(
                        static_cast<std::uint64_t>(crazy_seconds) * 1000ULL, now_ms);
                    add_legacy_trace(dispatch, "LegacySlave", "crazy", mail,
                                     current_tick, now_ms, true, magic_id,
                                     crazy_seconds, "MakeCrazyMode");
                  }
                }
              } else if (!monster_target->legacy_undead()) {
                const auto crazy_seconds = 10 + random.random(20);
                monster_target->make_legacy_crazy(
                    static_cast<std::uint64_t>(crazy_seconds) * 1000ULL, now_ms);
                add_legacy_trace(dispatch, "LegacySlave", "crazy", mail,
                                 current_tick, now_ms, true, magic_id, crazy_seconds,
                                 "MakeCrazyMode");
              }
            } else if (action_roll != 0) {
              monster_target->make_legacy_holy_seize(
                  static_cast<std::uint64_t>(10 + shock_level * 5) * 1000ULL, now_ms);
              add_legacy_trace(dispatch, "LegacySlave", "holy_seize", mail,
                               current_tick, now_ms, true, magic_id, shock_level,
                               "MakeHolySeize");
            }
            train = true;
            break;
          }
          case 21: {
            send_magic_fire_now();
            send_magic_fire = false;
            const auto space_roll = random.random(11);
            train = space_roll < 4 + static_cast<std::int32_t>(user_magic->level) * 2;
            add_legacy_trace(dispatch, "LegacySpell", "space_move_gate", mail,
                             current_tick, now_ms, train, space_roll,
                             static_cast<std::int32_t>(user_magic->level),
                             "Random(11)");
            if (train) {
              dispatch.legacy_random_space_moves.push_back(LegacyRandomSpaceMoveRequest{
                  config_.id,
                  {},
                  attacker->id(),
                  magic_id});
            }
            break;
          }
          case 22: {
            const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                     user_magic->level, random);
            const auto seconds = legacy_fire_wall_seconds(*attacker, magic_it->second.legacy,
                                                          user_magic->level, random);
            constexpr std::array<std::pair<std::int32_t, std::int32_t>, 5> kOffsets{{
                {0, -1},
                {-1, 0},
                {0, 0},
                {1, 0},
                {0, 1},
            }};
            for (const auto& [dx, dy] : kOffsets) {
              LegacyEventRecord event;
              event.map_id = config_.id;
              event.x = fire_x + dx;
              event.y = fire_y + dy;
              event.type = LegacyEventType::fire_burn;
              event.open_start_ms = now_ms;
              event.continue_ms =
                  static_cast<std::uint64_t>(std::max(seconds, 1)) * 1000ULL;
              event.run_start_ms = now_ms;
              event.run_tick_ms = 500;
              event.owner_actor_id = attacker->id();
              event.damage = power;
              event.blocks_walk = false;
              event.skip_if_occupied = true;
              dispatch.legacy_event_creates.push_back(event);
            }
            train = true;
            add_legacy_trace(dispatch, "LegacySpell", "fire_wall", mail,
                             current_tick, now_ms, true, power, seconds,
                             "MagMakeFireCross");
            break;
          }
          case 23:
          case 33: {
            const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                     user_magic->level, random);
            auto targets = collect_legacy_area_targets(objects_, *attacker, config_,
                                                       fire_x, fire_y, 1, false);
            add_legacy_trace(dispatch, "LegacySpell", "target_collect", mail, current_tick,
                             now_ms, !targets.empty(), static_cast<std::int32_t>(targets.size()),
                             0, "GetMapCreatures");
            for (auto* area_target : targets) {
              train = true;
              apply_direct_magic(*area_target, power, "RM_MAGSTRUCK");
            }
            break;
          }
          case 24: {
            const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                     user_magic->level, random);
            auto targets = collect_legacy_area_targets(objects_, *attacker, config_,
                                                       attacker->x(), attacker->y(), 2, false);
            add_legacy_trace(dispatch, "LegacySpell", "target_collect", mail, current_tick,
                             now_ms, !targets.empty(), static_cast<std::int32_t>(targets.size()),
                             0, "GetMapCreatures");
            for (auto* area_target : targets) {
              train = true;
              const auto area_power = actor_undead(*area_target) ? power : power / 10;
              apply_direct_magic(*area_target, area_power, "RM_MAGSTRUCK");
            }
            break;
          }
          case 28: {
            const auto already_open =
                (as_player(target) != nullptr &&
                 as_player(target)->legacy_open_health_active(current_tick)) ||
                (as_monster(target) != nullptr &&
                 as_monster(target)->legacy_open_health_active(current_tick));
            if (!already_open && target != nullptr) {
              const auto gate_roll = random.random(6);
              const auto success = gate_roll <= 3 + static_cast<std::int32_t>(user_magic->level);
              add_legacy_trace(dispatch, "LegacySpell", "open_health_gate", mail,
                               current_tick, now_ms, success, gate_roll, 0, "Random(6)");
              if (success) {
                auto delayed = make_delayed();
                delayed.delayed_effect_kind = LegacyDelayedEffectKind::open_health;
                delayed.target_actor_id = target->id();
                delayed.power = static_cast<std::int32_t>(legacy_delay_ms_to_ticks(
                    static_cast<std::uint32_t>(std::max(
                        legacy_open_health_power(*attacker, magic_it->second.legacy,
                                                 user_magic->level, random),
                        1)) *
                        1000u,
                    budgets_.tick_ms));
                delayed_mail_wheel_.schedule(
                    current_tick, legacy_delay_ms_to_ticks(1500, budgets_.tick_ms), delayed);
                add_legacy_trace(dispatch, "LegacySpell", "open_health_queued", mail,
                                 current_tick, now_ms, true, magic_id, delayed.power, "1500ms");
                train = true;
              }
            }
            break;
          }
          case 29: {
            const auto power =
                legacy_heal_power(*attacker, magic_it->second.legacy, user_magic->level, random);
            auto targets = collect_legacy_area_targets(objects_, *attacker, config_, fire_x,
                                                       fire_y, 1, true);
            add_legacy_trace(dispatch, "LegacySpell", "target_collect", mail, current_tick,
                             now_ms, !targets.empty(), static_cast<std::int32_t>(targets.size()),
                             0, "GetMapCreatures");
            for (auto* heal_target : targets) {
              auto* player_target = as_player(heal_target);
              if (player_target == nullptr ||
                  player_target->character().ability.hp >= player_target->character().ability.max_hp) {
                continue;
              }
              auto delayed = make_delayed();
              delayed.delayed_effect_kind = LegacyDelayedEffectKind::mag_healing;
              delayed.target_actor_id = player_target->id();
              delayed.power = power;
              delayed_mail_wheel_.schedule(
                  current_tick, legacy_delay_ms_to_ticks(800, budgets_.tick_ms), delayed);
              add_legacy_trace(dispatch, "LegacySpell", "healing_queued", mail,
                               current_tick, now_ms, true, magic_id, power, "800ms");
              train = true;
            }
            break;
          }
          case 31: {
            const auto seconds = legacy_magic_bubble_seconds(*attacker, magic_it->second.legacy,
                                                             user_magic->level, random);
            const auto expire_tick = current_tick + legacy_delay_ms_to_ticks(
                                                        static_cast<std::uint32_t>(
                                                            std::max(seconds, 1) * 1000),
                                                        budgets_.tick_ms);
            train = attacker->activate_legacy_magic_bubble(user_magic->level, current_tick,
                                                           expire_tick);
            if (train) {
              broadcast_legacy_char_status_changed(dispatch, *attacker);
            }
            add_legacy_trace(dispatch, "LegacySpell", "magic_bubble", mail, current_tick,
                             now_ms, train, seconds, 0, "MagBubbleDefenceUp");
            break;
          }
          case 32: {
            std::string reason;
            auto* monster_target =
                harmful_target_ok(target, reason) ? as_monster(target) : nullptr;
            if (monster_target != nullptr && monster_target->legacy_undead()) {
              const auto level_roll = random.random(4);
              const auto level_gate =
                  monster_target->level() <
                      (attacker->character().ability.level - 1 + level_roll) &&
                  monster_target->level() < 60;
              add_legacy_trace(dispatch, "LegacySpell", "turn_undead_level", mail,
                               current_tick, now_ms, level_gate, level_roll, 0,
                               "Random(4)");
              if (level_gate) {
                const auto chance_roll = random.random(100);
                const auto level_gap =
                    static_cast<std::int32_t>(attacker->character().ability.level) -
                    monster_target->level();
                const auto success =
                    chance_roll < 15 + static_cast<std::int32_t>(user_magic->level) * 7 +
                                      level_gap;
                add_legacy_trace(dispatch, "LegacySpell", "turn_undead_gate", mail,
                                 current_tick, now_ms, success, chance_roll, 0,
                                 "Random(100)");
                if (success) {
                  const auto damage = monster_target->hp();
                  const auto result = apply_legacy_magic_damage(
                      objects_, item_configs_, dispatch, *attacker, *monster_target, config_, damage,
                      current_tick, now_ms);
                  if (result.applied_damage > 0) {
                    notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
                  }
                  add_legacy_trace(dispatch, "LegacySpell",
                                   result.target_died ? "death" : "mag_struck", mail,
                                   current_tick, now_ms, result.applied_damage > 0,
                                   magic_id, result.applied_damage,
                                   result.target_died ? "SM_DEATH" : "SM_STRUCK");
                  if (result.slain_monster_id != 0) {
                    auto pending_death_packets =
                        collect_legacy_death_packets(objects_, *monster_target);
                    finalize_monster_death(result.slain_monster_id, attacker->id(), dispatch,
                                           current_tick);
                    add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick,
                                     now_ms, true, magic_id, damage, "WinExp");
                    queue_legacy_packets(dispatch, std::move(pending_death_packets));
                  }
                  train = true;
                }
              }
            }
            break;
          }
          default:
            break;
        }

        if (spell_branch_aborted) {
          break;
        }
        if (send_magic_fire) {
          send_magic_fire_now();
        }
        if (train) {
          const auto training =
              legacy_train_magic(*attacker, *user_magic, magic_it->second, random);
          if (training.trained) {
            add_legacy_trace(dispatch, "LegacySkill", "train_skill", mail, current_tick, now_ms,
                             true, training.train_amount, training.cur_train,
                             training.leveled_up ? "level_up" : "train");
            schedule_legacy_magic_lvexp(*attacker, training, dispatch, mail, current_tick, now_ms);
          }
        }
        break;
      }

      const auto harmful_spell = magic_is_harmful(magic_it->second);
      const auto beneficial_spell = magic_is_beneficial(magic_it->second);
      const auto allow_self = beneficial_spell && magic_it->second.affect_players;

      GameObject* target = nullptr;
      if (mail.target_actor_id != 0) {
        if (allow_self && mail.target_actor_id == attacker->id()) {
          target = attacker;
        } else {
          const auto target_it = objects_.find(mail.target_actor_id);
          if (target_it != objects_.end() && is_attackable_target(*target_it->second) &&
              magic_can_hit_target(magic_it->second, *target_it->second)) {
            target = target_it->second.get();
          }
        }
      }
      if (target == nullptr && (mail.x != 0 || mail.y != 0)) {
        target = find_attack_target_by_position(objects_, *attacker, mail.x, mail.y);
      }
      if (target == nullptr && allow_self && mail.target_actor_id == 0 && mail.x == 0 && mail.y == 0) {
        target = attacker;
      }

      if (magic_it->second.radius <= 0 && harmful_spell) {
        if (auto* player_target = as_player(target); player_target != nullptr) {
          const auto block_reason = resolve_pk_block_reason(config_, *attacker, *player_target, now_ms);
          if (!block_reason.empty()) {
            add_legacy_trace(dispatch, "LegacySpell", "pk_block", mail, current_tick, now_ms,
                             false, magic_id, 0, block_reason);
            queue_packet(dispatch, attacker->session_id(),
                         make_ack_packet(attacker->session_id(), false));
            queue_packet(dispatch, attacker->session_id(),
                         make_system_notice_packet(attacker->session_id(), block_reason));
            break;
          }
        }
      }

      if (!attacker->spend_mp(std::max(magic_it->second.mp_cost, 0))) {
        add_legacy_trace(dispatch, "LegacySpell", "mp_reject", mail, current_tick, now_ms,
                         false, magic_it->second.mp_cost, 0, "spell");
        queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), false));
        break;
      }

      attacker->on_mail(mail, context);
      queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), true));
      add_legacy_trace(dispatch, "LegacySpell", "ack", mail, current_tick, now_ms, true,
                       magic_id, 0, "RM_SPELL");
      queue_actor_origin_packet(objects_, dispatch, *attacker, false, [&](const Player& watcher) {
        queue_packet(dispatch, watcher.session_id(),
                     make_spell_packet(watcher.session_id(), *attacker, mail, magic_configs_));
      });
      add_legacy_trace(dispatch, "LegacySpell", "spell_broadcast", mail, current_tick, now_ms,
                       true, magic_id, 0, "RM_SPELL");

      const auto has_center = target != nullptr || mail.x != 0 || mail.y != 0;
      const auto center_x = target != nullptr ? target->x() : mail.x;
      const auto center_y = target != nullptr ? target->y() : mail.y;
      const auto target_ids = collect_spell_target_ids(objects_, *attacker, magic_it->second, config_,
                                                       target, center_x, center_y, has_center,
                                                       allow_self);
      add_legacy_trace(dispatch, "LegacySpell", "target_collect", mail, current_tick, now_ms,
                       !target_ids.empty(), static_cast<std::int32_t>(target_ids.size()), 0,
                       "stable_actor_id_order");

      if (target_ids.empty()) {
        queue_packet(dispatch, attacker->session_id(),
                     make_health_spell_changed_packet(attacker->session_id(), *attacker));
        break;
      }

      struct SlainMonsterDeath {
        std::uint64_t monster_id{0};
        std::int32_t applied_damage{0};
        std::vector<PendingLegacyPacket> pending_death_packets;
      };
      std::vector<SlainMonsterDeath> slain_monster_deaths;
      for (const auto target_id : target_ids) {
        const auto target_it = objects_.find(target_id);
        if (target_it == objects_.end()) {
          continue;
        }

        auto& resolved_target = *target_it->second;
        const auto mc_min = packed_min(attacker->character().ability.mc);
        const auto mc_max = std::max(mc_min, packed_max(attacker->character().ability.mc));
        const auto magic_roll =
            legacy_random_value(dispatch, "LegacySpell", "magic_power_roll",
                                std::max(1, mc_max - mc_min + 1), attacker->id(),
                                resolved_target.id(), "spell", now_ms, current_tick);
        const auto [mac_min, mac_max] = actor_magic_defense_range(resolved_target);
        const auto defense_roll =
            legacy_random_value(dispatch, "LegacySpell", "magic_defense_roll",
                                std::max(1, mac_max - mac_min + 1), attacker->id(),
                                resolved_target.id(), "spell", now_ms, current_tick);
        const auto damage =
            compute_spell_damage(*attacker, resolved_target, magic_it->second,
                                 magic_roll, defense_roll);
        add_legacy_trace(dispatch, "LegacySpell", "damage", mail, current_tick, now_ms, true,
                         magic_id, damage, "SpellNow");
        std::int32_t applied_damage = 0;
        std::int32_t absorbed_damage = 0;
        std::int32_t applied_heal = 0;
        bool shield_broken = false;
        std::string shield_name{};
        bool target_died = false;
        Monster* slain_monster = nullptr;
        bool applied_player_effect = false;

        if (auto* player_target = as_player(&resolved_target); player_target != nullptr) {
          if (harmful_spell) {
            if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
              player_target->record_pk_hiter(attacker->id(), now_ms);
            }
            const auto damage_result = player_target->apply_damage(damage, current_tick);
            applied_damage = damage_result.hp_damage;
            absorbed_damage = damage_result.absorbed_damage;
            shield_broken = damage_result.shield_broken;
            shield_name = damage_result.shield_name;
          }
          if (beneficial_spell) {
            applied_heal = player_target->apply_heal(std::max(magic_it->second.instant_heal, 0));
            if (magic_it->second.dispel_negative) {
              const auto modern_cleared =
                  player_target->clear_negative_status_effects(current_tick);
              const auto legacy_cleared =
                  player_target->clear_negative_legacy_buffs(current_tick);
              if (legacy_cleared > 0) {
                broadcast_legacy_char_status_changed(dispatch, *player_target);
              }
              if (modern_cleared > 0 || legacy_cleared > 0) {
                applied_player_effect = true;
              }
            }
          }
          if ((magic_it->second.dot_damage > 0 || magic_it->second.heal_per_tick > 0 ||
               magic_it->second.slow_percent > 0 || magic_it->second.shield_amount > 0) &&
              magic_it->second.effect_duration_ms > 0) {
            const auto duration_ticks =
                ms_to_logic_ticks(magic_it->second.effect_duration_ms, budgets_.tick_ms);
            const auto tick_interval =
                ms_to_logic_ticks(magic_it->second.effect_tick_ms, budgets_.tick_ms);
            if (duration_ticks > 0) {
              player_target->add_status_effect(TimedStatusEffect{
                  attacker->id(),
                  current_tick + duration_ticks,
                  current_tick + std::max<std::uint64_t>(tick_interval, 1),
                  std::max<std::uint64_t>(tick_interval, 1),
                  magic_it->second.name,
                  magic_it->second.dot_damage,
                  magic_it->second.slow_percent,
                  magic_it->second.heal_per_tick,
                  magic_it->second.shield_amount});
              applied_player_effect = true;
              if (magic_it->second.shield_amount > 0) {
                notify_player_and_watchers(
                    dispatch, *player_target, make_shield_apply_self_notice(magic_it->second.name),
                    make_shield_apply_watcher_notice(*player_target, magic_it->second.name));
              }
            }
          }
          target_died = player_target->is_dead();
          if (target_died &&
              try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
            target_died = false;
          }
          if (target_died) {
            const auto death_clear = player_target->mark_dead(now_ms);
            dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
            apply_bad_kill_penalty(*attacker, *player_target, dispatch, current_tick,
                                   now_ms, "LegacySpell");
            static_cast<void>(settle_player_death(*player_target, dispatch, current_tick,
                                                  now_ms));
          }
        } else if (auto* monster_target = as_monster(&resolved_target); monster_target != nullptr) {
          if (harmful_spell) {
            applied_damage = apply_legacy_monster_damage(
                objects_, *monster_target, damage, attacker->id(), config_, current_tick, now_ms);
            if (applied_damage > 0) {
              notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
            }
          }
          target_died = monster_target->is_dead();
          slain_monster = target_died ? monster_target : nullptr;
          if (!target_died &&
              (magic_it->second.dot_damage > 0 || magic_it->second.slow_percent > 0)) {
            const auto duration_ticks =
                ms_to_logic_ticks(magic_it->second.effect_duration_ms, budgets_.tick_ms);
            const auto tick_interval =
                ms_to_logic_ticks(magic_it->second.effect_tick_ms, budgets_.tick_ms);
            if (duration_ticks > 0) {
              monster_target->add_status_effect(TimedStatusEffect{
                  attacker->id(),
                  current_tick + duration_ticks,
                  current_tick + std::max<std::uint64_t>(tick_interval, 1),
                  std::max<std::uint64_t>(tick_interval, 1),
                  magic_it->second.name,
                  magic_it->second.dot_damage,
                  magic_it->second.slow_percent,
                  0});
              monster_target->schedule_next_ai_tick(current_tick);
            }
          }
        }

        const auto monster_death = target_died && slain_monster != nullptr;
        auto pending_death_packets =
            monster_death ? collect_legacy_death_packets(objects_, resolved_target)
                          : std::vector<PendingLegacyPacket>{};
        if (applied_damage > 0) {
          if (!monster_death) {
            for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
              if (watcher.id() != resolved_target.id() &&
                  !is_legacy_visible_to(watcher, resolved_target)) {
                return;
              }
              queue_packet(dispatch, watcher.session_id(),
                           target_died
                               ? make_death_packet(watcher.session_id(), resolved_target,
                                                   watcher.id() == resolved_target.id())
                               : make_struck_packet(watcher.session_id(), resolved_target,
                                                    attacker->id(), applied_damage, true));
            });
          }
        }

        if (auto* player_target = as_player(&resolved_target);
            player_target != nullptr &&
            (applied_heal > 0 || applied_player_effect || absorbed_damage > 0)) {
          queue_packet(dispatch, player_target->session_id(),
                       make_health_spell_changed_packet(player_target->session_id(), *player_target));
          if (shield_broken) {
            notify_player_and_watchers(
                dispatch, *player_target, make_shield_break_self_notice(shield_name),
                make_shield_break_watcher_notice(*player_target, shield_name));
          }
        }

        if (slain_monster != nullptr) {
          const auto slain_id = slain_monster->id();
          const auto existing = std::find_if(
              slain_monster_deaths.begin(), slain_monster_deaths.end(),
              [&](const auto& entry) { return entry.monster_id == slain_id; });
          if (existing == slain_monster_deaths.end()) {
            slain_monster_deaths.push_back(
                {slain_id, applied_damage, std::move(pending_death_packets)});
          }
        }
        if (applied_damage > 0 && !monster_death) {
          add_legacy_trace(dispatch, "LegacySpell", target_died ? "death" : "magic_fire", mail,
                           current_tick, now_ms, true, magic_id, applied_damage,
                           target_died ? "SM_DEATH" : "RM_MAGICFIRE");
        }
      }

      std::sort(slain_monster_deaths.begin(), slain_monster_deaths.end(),
                [](const auto& left, const auto& right) {
                  return left.monster_id < right.monster_id;
                });
      for (auto& death : slain_monster_deaths) {
        finalize_monster_death(death.monster_id, attacker->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms, true,
                         static_cast<std::int32_t>(death.monster_id), 0, "WinExp");
        queue_legacy_packets(dispatch, std::move(death.pending_death_packets));
        add_legacy_trace(dispatch, "LegacySpell", "death", mail, current_tick, now_ms,
                         true, magic_id, death.applied_damage, "SM_DEATH");
      }

      queue_packet(dispatch, attacker->session_id(),
                   make_health_spell_changed_packet(attacker->session_id(), *attacker));
      add_legacy_trace(dispatch, "LegacySpell", "self_refresh", mail, current_tick, now_ms, true,
                       magic_id, 0, "SM_HEALTHSPELLCHANGED");
      break;
    }
    case ActorMailKind::revive: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr) {
        break;
      }
      if (!player->is_dead()) {
        queue_packet(dispatch, player->session_id(), make_ack_packet(player->session_id(), false));
        queue_system_notice(dispatch, *player, "You are not dead.");
        break;
      }

      const auto target_x = config_.home_x > 0 ? config_.home_x : player->x();
      const auto target_y = config_.home_y > 0 ? config_.home_y : player->y();
      static_cast<void>(environment_.delete_from_map(player->x(), player->y(),
                                                     LegacyMapObjectShape::moving_object,
                                                     player->id()));
      const auto added = environment_.add_moving_object(target_x, target_y, player->id(), now_ms,
                                                        moving_state_for(*player));
      if (!added) {
        static_cast<void>(environment_.add_moving_object(player->x(), player->y(), player->id(),
                                                        now_ms, moving_state_for(*player)));
        queue_packet(dispatch, player->session_id(), make_ack_packet(player->session_id(), false));
        queue_system_notice(dispatch, *player, "Revive point is blocked.");
        break;
      }

      player->revive_at(config_.id, target_x, target_y,
                        static_cast<std::uint16_t>(std::max<std::int32_t>(
                            1, player->character().ability.max_hp / 2)),
                        static_cast<std::uint16_t>(std::max<std::int32_t>(
                            0, player->character().ability.max_mp / 2)));
      player->refresh_derived_state(item_configs_);
      static_cast<void>(environment_.delete_from_map(target_x, target_y,
                                                     LegacyMapObjectShape::moving_object,
                                                     player->id()));
      static_cast<void>(environment_.add_moving_object(target_x, target_y, player->id(),
                                                       now_ms, moving_state_for(*player)));
      queue_packet(dispatch, player->session_id(), make_ack_packet(player->session_id(), true));
      queue_packet(dispatch, player->session_id(), make_alive_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_health_spell_changed_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_ability_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(),
                   make_sub_ability_packet(player->session_id(), *player));
      queue_save_character(dispatch, *player);
      sync_player_visibility(*player, dispatch, true, now_ms);
      sync_all_player_visibility(dispatch, now_ms);
      add_legacy_trace(dispatch, "LegacyCombat", "revive", mail, current_tick, now_ms,
                       true, target_x, target_y, "SM_ALIVE");
      break;
    }
    case ActorMailKind::persistence_loaded: {
      const auto operation = decode_offline_guild_character_op(mail.payload);
      if (!operation.has_value()) {
        break;
      }

      auto requester_it = objects_.find(mail.actor_id);
      if (requester_it == objects_.end()) {
        break;
      }
      auto* speaker = as_player(requester_it->second.get());
      if (speaker == nullptr) {
        break;
      }

      auto* guild_state = find_guild_state(guild_castle_snapshot_, operation->guild_name);
      if (guild_state == nullptr || !equals_ignore_case(speaker->character().guild_name, operation->guild_name) ||
          !equals_ignore_case(guild_state->lord, speaker->character().character_name)) {
        queue_system_notice(dispatch, *speaker,
                            "Guild data changed before the offline request completed.");
        break;
      }

      Player* local_target = nullptr;
      if (mail.target_actor_id != 0) {
        local_target = find_player(mail.target_actor_id);
        if (local_target != nullptr &&
            !equals_ignore_case(local_target->character().character_name, operation->target_name)) {
          local_target = nullptr;
        }
      }
      if (local_target == nullptr) {
        local_target = find_online_player_by_name(objects_, operation->target_name);
      }

      auto target_record = local_target != nullptr ? local_target->snapshot() : mail.character;
      if (target_record.character_name.empty()) {
        target_record.character_name = operation->target_name;
      }
      if (local_target == nullptr && target_record.account_id.empty()) {
        queue_system_notice(dispatch, *speaker,
                            "Character data for " + operation->target_name + " is unavailable.");
        break;
      }
      const auto target_online_elsewhere =
          local_target == nullptr && mail.target_actor_id != 0 &&
          !operation->target_map_id.empty() && operation->target_map_id != config_.id;

      switch (operation->kind) {
        case OfflineGuildCharacterOpKind::approve: {
          if (!guild_has_applicant(*guild_state, operation->target_name)) {
            queue_system_notice(dispatch, *speaker, "That character has no pending application.");
            break;
          }

          const auto target_guild_name =
              local_target != nullptr ? local_target->character().guild_name : target_record.guild_name;
          if (!target_guild_name.empty()) {
            remove_guild_applicant(*guild_state, operation->target_name);
            queue_save_guild_state(dispatch, *guild_state);
            queue_system_notice(dispatch, *speaker,
                                operation->target_name +
                                    " is already in another guild. Application cleared.");
            if (local_target != nullptr) {
              queue_system_notice(dispatch, *local_target,
                                  render_guild_notice_template(
                                      configured_summary_template(
                                          castle_dialog_context_.guild_rejected_notice_template,
                                          "Your application to <$GUILD> was rejected."),
                                      guild_state->guild_name));
            } else if (target_online_elsewhere) {
              queue_cross_map_notice(dispatch, operation->target_map_id, mail.target_actor_id,
                                     render_guild_notice_template(
                                         configured_summary_template(
                                             castle_dialog_context_.guild_rejected_notice_template,
                                             "Your application to <$GUILD> was rejected."),
                                         guild_state->guild_name));
            }
            break;
          }

          remove_guild_applicant(*guild_state, operation->target_name);
          add_guild_member(*guild_state, operation->target_name);
          queue_save_guild_state(dispatch, *guild_state);
          if (local_target != nullptr) {
            local_target->set_guild_membership(guild_state->guild_name, "Member");
            queue_save_character(dispatch, *local_target);
            queue_system_notice(dispatch, *local_target,
                                render_guild_notice_template(
                                    configured_summary_template(
                                        castle_dialog_context_.guild_approved_notice_template,
                                        "Your application to <$GUILD> was approved."),
                                    guild_state->guild_name));
          } else if (target_online_elsewhere) {
            set_character_guild_membership(target_record, guild_state->guild_name, "Member");
            queue_cross_map_guild_membership_sync(
                dispatch, operation->target_map_id, mail.target_actor_id, target_record,
                render_guild_notice_template(
                    configured_summary_template(
                        castle_dialog_context_.guild_approved_notice_template,
                        "Your application to <$GUILD> was approved."),
                    guild_state->guild_name));
          } else {
            set_character_guild_membership(target_record, guild_state->guild_name, "Member");
            queue_save_character(dispatch, target_record);
          }
          queue_system_notice(dispatch, *speaker,
                              render_guild_summary_template(
                                  configured_summary_template(
                                      castle_dialog_context_.guild_approve_summary_template,
                                      "Approved guild application for <$TARGET>."),
                                  guild_state->guild_name, operation->target_name));
          break;
        }
        case OfflineGuildCharacterOpKind::kick: {
          if (equals_ignore_case(operation->target_name, speaker->character().character_name)) {
            queue_system_notice(dispatch, *speaker,
                                "Use @guild leave to remove yourself from the guild.");
            break;
          }
          if (!guild_has_member(*guild_state, operation->target_name)) {
            queue_system_notice(dispatch, *speaker, "That character is not a guild member.");
            break;
          }

          remove_guild_member(*guild_state, operation->target_name);
          queue_save_guild_state(dispatch, *guild_state);
          if (local_target != nullptr) {
            local_target->clear_guild_membership();
            queue_save_character(dispatch, *local_target);
            queue_system_notice(dispatch, *local_target,
                                render_guild_notice_template(
                                    configured_summary_template(
                                        castle_dialog_context_.guild_removed_notice_template,
                                        "You were removed from guild <$GUILD>."),
                                    guild_state->guild_name));
          } else if (target_online_elsewhere) {
            clear_character_guild_membership(target_record);
            queue_cross_map_guild_membership_sync(
                dispatch, operation->target_map_id, mail.target_actor_id, target_record,
                render_guild_notice_template(
                    configured_summary_template(
                        castle_dialog_context_.guild_removed_notice_template,
                        "You were removed from guild <$GUILD>."),
                    guild_state->guild_name));
          } else {
            clear_character_guild_membership(target_record);
            queue_save_character(dispatch, target_record);
          }
          queue_system_notice(dispatch, *speaker,
                              render_guild_summary_template(
                                  configured_summary_template(
                                      castle_dialog_context_.guild_kick_summary_template,
                                      "Kicked guild member <$TARGET>."),
                                  guild_state->guild_name, operation->target_name));
          break;
        }
        case OfflineGuildCharacterOpKind::transfer: {
          if (equals_ignore_case(operation->target_name, speaker->character().character_name)) {
            queue_system_notice(dispatch, *speaker, "You already lead this guild.");
            break;
          }
          if (!guild_has_member(*guild_state, operation->target_name)) {
            queue_system_notice(dispatch, *speaker, "That character is not a guild member.");
            break;
          }

          guild_state->lord = operation->target_name;
          speaker->set_guild_membership(guild_state->guild_name, "Member");
          queue_save_guild_state(dispatch, *guild_state);
          queue_save_character(dispatch, *speaker);
          if (local_target != nullptr) {
            local_target->set_guild_membership(guild_state->guild_name, "Lord");
            queue_save_character(dispatch, *local_target);
            queue_system_notice(dispatch, *local_target,
                                render_guild_notice_template(
                                    configured_summary_template(
                                        castle_dialog_context_.guild_new_lord_notice_template,
                                        "You are now the guild lord of <$GUILD>."),
                                    guild_state->guild_name));
          } else if (target_online_elsewhere) {
            set_character_guild_membership(target_record, guild_state->guild_name, "Lord");
            queue_cross_map_guild_membership_sync(
                dispatch, operation->target_map_id, mail.target_actor_id, target_record,
                render_guild_notice_template(
                    configured_summary_template(
                        castle_dialog_context_.guild_new_lord_notice_template,
                        "You are now the guild lord of <$GUILD>."),
                    guild_state->guild_name));
          } else {
            set_character_guild_membership(target_record, guild_state->guild_name, "Lord");
            queue_save_character(dispatch, target_record);
          }
          if (equals_ignore_case(castle_dialog_context_.owner_guild, guild_state->guild_name)) {
            castle_dialog_context_.lord = guild_state->lord;
            guild_castle_snapshot_.castle_dialog = castle_dialog_context_;
            queue_save_castle_state(dispatch, castle_dialog_context_);
          }
          queue_system_notice(dispatch, *speaker,
                              render_guild_summary_template(
                                  configured_summary_template(
                                      castle_dialog_context_.guild_transfer_summary_template,
                                      "Transferred guild leadership to <$TARGET>."),
                                  guild_state->guild_name, operation->target_name));
          break;
        }
        case OfflineGuildCharacterOpKind::title: {
          if (operation->title_name.empty()) {
            queue_system_notice(dispatch, *speaker,
                                "Usage: @guild title <member_name> <title>");
            break;
          }
          if (!guild_has_member(*guild_state, operation->target_name)) {
            queue_system_notice(dispatch, *speaker, "That character is not a guild member.");
            break;
          }
          if (equals_ignore_case(operation->target_name, guild_state->lord)) {
            queue_system_notice(dispatch, *speaker,
                                "Use @guild transfer to change the guild lord.");
            break;
          }

          if (local_target != nullptr) {
            local_target->set_guild_membership(guild_state->guild_name, operation->title_name);
            queue_save_character(dispatch, *local_target);
            queue_system_notice(dispatch, *local_target,
                                render_guild_notice_template(
                                    configured_summary_template(
                                        castle_dialog_context_.guild_title_changed_notice_template,
                                        "Your guild title is now <$TITLE>."),
                                    guild_state->guild_name, {}, operation->title_name));
          } else if (target_online_elsewhere) {
            set_character_guild_membership(target_record, guild_state->guild_name,
                                           operation->title_name);
            queue_cross_map_guild_membership_sync(
                dispatch, operation->target_map_id, mail.target_actor_id, target_record,
                render_guild_notice_template(
                    configured_summary_template(
                        castle_dialog_context_.guild_title_changed_notice_template,
                        "Your guild title is now <$TITLE>."),
                    guild_state->guild_name, {}, operation->title_name));
          } else {
            set_character_guild_membership(target_record, guild_state->guild_name,
                                           operation->title_name);
            queue_save_character(dispatch, target_record);
          }
          queue_system_notice(dispatch, *speaker,
                              render_guild_summary_template(
                                  configured_summary_template(
                                      castle_dialog_context_.guild_title_summary_template,
                                      "Set guild title for <$TARGET> to <$TITLE>."),
                                  guild_state->guild_name, operation->target_name,
                                  operation->title_name));
          break;
        }
        default:
          break;
      }
      break;
    }
    case ActorMailKind::say: {
      auto requester_it = objects_.find(mail.actor_id);
      if (requester_it == objects_.end()) {
        break;
      }
      auto* speaker = as_player(requester_it->second.get());
      if (speaker == nullptr) {
        break;
      }
      const auto parsed = parse_legacy_chat_input(mail.payload);
      if (handle_guild_castle_business_command(*speaker, objects_, mail.payload,
                                               guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }
      if (handle_castle_admin_command(*speaker, mail.payload, guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }
      if (parsed.kind != LegacyChatInputKind::normal) {
        break;
      }
      const auto line = speaker->character().character_name + ": " + parsed.message_text;
      queue_actor_origin_packet(objects_, dispatch, *speaker, true, [&](const Player& player) {
        queue_packet(dispatch, player.session_id(),
                     make_legacy_chat_packet(player.session_id(), LegacyChatDeliveryKind::normal,
                                             speaker->id(), line));
      });
      break;
    }
    case ActorMailKind::legacy_chat_delivery: {
      switch (mail.legacy_chat_kind) {
        case LegacyChatDeliveryKind::normal: {
          auto* speaker = find_player(mail.actor_id);
          if (speaker == nullptr) {
            break;
          }
          const auto line = speaker->character().character_name + ": " + mail.payload;
          queue_actor_origin_packet(objects_, dispatch, *speaker, true, [&](const Player& player) {
            queue_packet(dispatch, player.session_id(),
                         make_legacy_chat_packet(player.session_id(),
                                                 LegacyChatDeliveryKind::normal,
                                                 speaker->id(), line));
          });
          break;
        }
        case LegacyChatDeliveryKind::whisper:
        case LegacyChatDeliveryKind::guild:
        case LegacyChatDeliveryKind::group:
        case LegacyChatDeliveryKind::shout_direct:
        case LegacyChatDeliveryKind::system: {
          auto* target = find_player(mail.actor_id);
          if (target == nullptr) {
            break;
          }
          queue_packet(dispatch, target->session_id(),
                       make_legacy_chat_packet(target->session_id(), mail.legacy_chat_kind,
                                               mail.target_actor_id, mail.payload));
          break;
        }
        case LegacyChatDeliveryKind::shout: {
          auto* speaker = find_player(mail.actor_id);
          if (speaker == nullptr) {
            break;
          }
          const auto line = "(!)" + speaker->character().character_name + ":" + mail.payload;
          for_each_player(objects_, [&](std::uint64_t, const Player& player) {
            if (std::abs(player.x() - speaker->x()) >= 50 ||
                std::abs(player.y() - speaker->y()) >= 50) {
              return;
            }
            queue_packet(dispatch, player.session_id(),
                         make_legacy_chat_packet(player.session_id(),
                                                 LegacyChatDeliveryKind::shout, 0, line));
          });
          break;
        }
        case LegacyChatDeliveryKind::none:
          break;
      }
      break;
    }
    case ActorMailKind::legacy_magic_lvexp: {
      auto* player = find_player(mail.actor_id);
      if (player == nullptr) {
        add_legacy_trace(dispatch, "LegacySkill", "magic_lvexp_missing", mail, current_tick,
                         now_ms, false, mail.magic_id, 0, "player");
        break;
      }
      if (mail.magic_lvexp_generation !=
          player->legacy_magic_lvexp_generation(mail.magic_id)) {
        add_legacy_trace(dispatch, "LegacySkill", "magic_lvexp_stale", mail, current_tick,
                         now_ms, false, mail.magic_id, mail.magic_train, "generation");
        break;
      }
      queue_packet(dispatch, player->session_id(),
                   make_magic_lvexp_packet(player->session_id(), mail.magic_id,
                                           mail.magic_level, mail.magic_train));
      add_legacy_trace(dispatch, "LegacySkill", "magic_lvexp", mail, current_tick, now_ms,
                       true, mail.magic_id, mail.magic_train, "SM_MAGIC_LVEXP");
      break;
    }
    case ActorMailKind::legacy_delayed_effect: {
      auto caster_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      const auto monster_struck =
          mail.delayed_effect_kind == LegacyDelayedEffectKind::monster_struck;
      if (caster_it == objects_.end() || target_it == objects_.end() ||
          (!monster_struck && !is_attackable_target(*target_it->second))) {
        add_legacy_trace(dispatch, "LegacySpell", "delayed_effect_missing", mail,
                         current_tick, now_ms, false, mail.magic_id, 0, "target");
        break;
      }
      if (monster_struck) {
        auto& caster_object = *caster_it->second;
        auto& target = *target_it->second;
        const auto target_died = !is_alive(target);
        auto pending_death_packets =
            target_died && as_monster(&target) != nullptr
                ? collect_legacy_death_packets(objects_, target)
                : std::vector<PendingLegacyPacket>{};
        if (mail.power > 0 || target_died) {
          if (!target_died || as_monster(&target) == nullptr) {
            for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
              if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
                return;
              }
              queue_packet(dispatch, watcher.session_id(),
                           target_died ? make_death_packet(watcher.session_id(), target,
                                                           watcher.id() == target.id())
                                       : make_struck_packet(watcher.session_id(), target,
                                                            caster_object.id(), mail.power,
                                                            mail.magic_id != 0));
            });
          }
        }
        if (target_died) {
          if (auto* slain_monster = as_monster(&target); slain_monster != nullptr) {
            auto reward_actor_id = caster_object.id();
            if (const auto* caster_monster = as_monster(&caster_object);
                caster_monster != nullptr && caster_monster->master_actor_id() != 0) {
              reward_actor_id = caster_monster->master_actor_id();
            }
            finalize_monster_death(slain_monster->id(), reward_actor_id, dispatch,
                                   current_tick);
            queue_legacy_packets(dispatch, std::move(pending_death_packets));
          }
        }
        add_legacy_trace(dispatch, "MonsterSpecial", target_died ? "death" : "struck",
                         mail, current_tick, now_ms, mail.power > 0 || target_died,
                         mail.magic_id, mail.power,
                         target_died ? "SM_DEATH" : "SM_STRUCK");
        break;
      }
      auto* caster = as_player(caster_it->second.get());
      if (caster == nullptr || caster->is_dead()) {
        add_legacy_trace(dispatch, "LegacySpell", "delayed_effect_missing", mail,
                         current_tick, now_ms, false, mail.magic_id, 0, "caster");
        break;
      }
      auto& target = *target_it->second;
      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::make_poison) {
        const auto poison_tick_interval = legacy_delay_ms_to_ticks(2500, budgets_.tick_ms);
        auto applied = false;
        if (auto* player_target = as_player(&target); player_target != nullptr) {
          applied = player_target->apply_legacy_poison(
              mail.poison_kind, mail.duration_ticks, mail.poison_level,
              poison_tick_interval, caster->id(), current_tick);
          if (applied) {
            broadcast_legacy_char_status_changed(dispatch, *player_target);
          }
        } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
          applied = monster_target->apply_legacy_poison(
              mail.poison_kind, mail.duration_ticks, mail.poison_level,
              poison_tick_interval, caster->id(), current_tick);
          monster_target->schedule_next_ai_tick(current_tick);
        }
        add_legacy_trace(dispatch, "LegacySpell", "poison_apply", mail, current_tick,
                         now_ms, applied, mail.poison_kind, mail.poison_level,
                         "RM_MAKEPOISON");
        break;
      }

      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::transparent) {
        auto* player_target = as_player(&target);
        const auto applied =
            player_target != nullptr &&
            player_target->activate_legacy_transparent(mail.duration_ticks, current_tick);
        if (applied) {
          broadcast_legacy_char_status_changed(dispatch, *player_target);
        }
        add_legacy_trace(dispatch, "LegacySpell", "transparent_apply", mail,
                         current_tick, now_ms, applied, mail.magic_id,
                         static_cast<std::int32_t>(mail.duration_ticks), "RM_TRANSPARENT");
        break;
      }

      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::open_health) {
        const auto expire_tick = current_tick + static_cast<std::uint64_t>(std::max(mail.power, 1));
        if (auto* player_target = as_player(&target); player_target != nullptr) {
          player_target->activate_legacy_open_health(expire_tick);
        } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
          monster_target->activate_legacy_open_health(expire_tick);
        }
        add_legacy_trace(dispatch, "LegacySpell", "open_health_apply", mail, current_tick,
                         now_ms, true, mail.magic_id, mail.power, "RM_DOOPENHEALTH");
        break;
      }

      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::mag_healing) {
        auto* player_target = as_player(&target);
        if (player_target == nullptr) {
          add_legacy_trace(dispatch, "LegacySpell", "healing_skip", mail, current_tick,
                           now_ms, false, mail.magic_id, 0, "non_player_target");
          break;
        }
        const auto pending_before = player_target->legacy_healing_pending();
        player_target->queue_legacy_healing(mail.power, current_tick, 1);
        handle_player_health_spell_tick(*player_target, dispatch, current_tick + 1);
        const auto queued = !pending_before || player_target->legacy_healing_pending();
        add_legacy_trace(dispatch, "LegacySpell", "healing_apply", mail, current_tick, now_ms,
                         queued && player_target->legacy_healing_pending(), mail.magic_id,
                         mail.power, "RM_MAGHEALING");
        break;
      }

      if (mail.delayed_effect_kind != LegacyDelayedEffectKind::delay_magic &&
          mail.delayed_effect_kind != LegacyDelayedEffectKind::mag_struck) {
        add_legacy_trace(dispatch, "LegacySpell", "delayed_effect_unknown", mail,
                         current_tick, now_ms, false, mail.magic_id, 0, "kind");
        break;
      }

      if (mail.range > 0 && (std::abs(mail.x - target.x()) > std::max(mail.range, 0) ||
                             std::abs(mail.y - target.y()) > std::max(mail.range, 0))) {
        add_legacy_trace(dispatch, "LegacySpell", "delay_magic_range_reject", mail,
                         current_tick, now_ms, false, mail.magic_id, 0, "range");
        break;
      }

      LegacyRandom fallback_random;
      auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
      auto raw_power = mail.power;
      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::delay_magic) {
        const auto check_damage = legacy_magic_defense_damage(target, mail.power, random,
                                                              current_tick, budgets_.tick_ms,
                                                              false, mail.undead_power);
        if (check_damage <= 0) {
          add_legacy_trace(dispatch, "LegacySpell", "delay_magic_absorbed", mail,
                           current_tick, now_ms, false, mail.magic_id, 0, "GetMagStruckDamage");
          break;
        }
      }

      if (mail.delayed_effect_kind == LegacyDelayedEffectKind::delay_magic &&
          as_monster(&target) != nullptr) {
        raw_power = delphi_round(static_cast<double>(raw_power) * 1.2);
      }
      const auto damage = legacy_magic_defense_damage(target, raw_power, random,
                                                      current_tick, budgets_.tick_ms, true,
                                                      mail.undead_power);
      std::int32_t applied_damage = 0;
      bool target_died = false;
      Monster* slain_monster = nullptr;
      if (auto* player_target = as_player(&target); player_target != nullptr) {
        if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
          player_target->record_pk_hiter(caster->id(), now_ms);
        }
        const auto damage_result = player_target->apply_damage(damage, current_tick);
        applied_damage = damage_result.hp_damage;
        target_died = player_target->is_dead();
        if (target_died &&
            try_legacy_revival(*player_target, dispatch, current_tick, now_ms)) {
          target_died = false;
        }
        if (target_died) {
          const auto death_clear = player_target->mark_dead(now_ms);
          dispatch_player_status_tick_result(*player_target, death_clear, dispatch, false);
          apply_bad_kill_penalty(*caster, *player_target, dispatch, current_tick,
                                 now_ms, "LegacySpell");
          static_cast<void>(settle_player_death(*player_target, dispatch, current_tick,
                                                now_ms));
        }
        if (damage_result.absorbed_damage > 0) {
          queue_packet(dispatch, player_target->session_id(),
                       make_health_spell_changed_packet(player_target->session_id(),
                                                        *player_target));
        }
      } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
        applied_damage = apply_legacy_monster_damage(
            objects_, *monster_target, damage, caster->id(), config_, current_tick, now_ms);
        if (applied_damage > 0) {
          notify_owned_slaves_target(*caster, monster_target->id(), now_ms);
        }
        target_died = monster_target->is_dead();
        slain_monster = target_died ? monster_target : nullptr;
      }
      if (applied_damage <= 0) {
        add_legacy_trace(dispatch, "LegacySpell", "mag_struck_absorbed", mail,
                         current_tick, now_ms, false, mail.magic_id, damage,
                         "RM_MAGSTRUCK");
        break;
      }
      auto pending_death_packets =
          target_died && slain_monster != nullptr
              ? collect_legacy_death_packets(objects_, target)
              : std::vector<PendingLegacyPacket>{};
      if (!target_died || slain_monster == nullptr) {
        for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
          if (watcher.id() != target.id() && !is_legacy_visible_to(watcher, target)) {
            return;
          }
          queue_packet(dispatch, watcher.session_id(),
                       target_died ? make_death_packet(watcher.session_id(), target,
                                                       watcher.id() == target.id())
                                   : make_struck_packet(watcher.session_id(), target, caster->id(),
                                                        applied_damage, true));
        });
      }
      if (slain_monster != nullptr) {
        finalize_monster_death(slain_monster->id(), caster->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms,
                         true, mail.magic_id, applied_damage, "WinExp");
        queue_legacy_packets(dispatch, std::move(pending_death_packets));
      }
      add_legacy_trace(dispatch, "LegacySpell", target_died ? "death" : "mag_struck",
                       mail, current_tick, now_ms, true, mail.magic_id, applied_damage,
                       target_died ? "SM_DEATH" : "SM_STRUCK");
      break;
    }
    default: {
      auto it = objects_.find(mail.actor_id);
      if (it != objects_.end()) {
        if (auto* player = as_player(it->second.get()); player != nullptr) {
          ActorMail effective_mail = mail;
          auto reject_move = [&](bool disconnect) {
            queue_packet(dispatch, player->session_id(), make_ack_packet(player->session_id(), false));
            queue_packet(dispatch, player->session_id(),
                         make_move_fail_packet(player->session_id(), *player));
            if (disconnect) {
              queue_force_disconnect(dispatch, player->session_id(), "speed_hack_movement");
            }
          };

          auto old_x = player->x();
          auto old_y = player->y();
          auto moved_player = false;
          if (mail.kind == ActorMailKind::move || mail.kind == ActorMailKind::run) {
            if (player->is_dead() || !player->can_move_at(current_tick)) {
              reject_move(false);
              break;
            }

            const auto throttle = player->begin_move_attempt(current_tick, budgets_.tick_ms);
            if (!throttle.allowed) {
              reject_move(throttle.disconnect);
              break;
            }

            if (mail.kind == ActorMailKind::run && player->character().ability.hp < 10) {
              player->reset_move_throttle();
              reject_move(false);
              break;
            }

            const auto width = movement_width();
            const auto height = movement_height();
            const auto expected = mail.kind == ActorMailKind::run
                                      ? legacy::requested_run_target(width, height, player->x(),
                                                                     player->y(), mail.x, mail.y)
                                      : legacy::requested_walk_target(width, height, player->x(),
                                                                      player->y(), mail.x, mail.y);
            if (!expected.has_value() || expected->x != mail.x || expected->y != mail.y) {
              player->reset_move_throttle();
              reject_move(false);
              break;
            }

            if (mail.kind == ActorMailKind::run) {
              const auto middle = legacy::step_target(width, height, player->x(), player->y(),
                                                      expected->dir, 1);
              if (!middle.has_value() || !environment_.can_walk(middle->x, middle->y, false)) {
                player->reset_move_throttle();
                reject_move(false);
                break;
              }
            }
            if (!environment_.can_walk(expected->x, expected->y, false)) {
              player->reset_move_throttle();
              reject_move(false);
              break;
            }
            if (environment_.move_to_moving_object(
                    player->x(), player->y(), player->id(), expected->x, expected->y, false,
                    now_ms, moving_state_for(*player)) != 1) {
              player->reset_move_throttle();
              reject_move(false);
              break;
            }

            effective_mail.x = expected->x;
            effective_mail.y = expected->y;
            effective_mail.dir = expected->dir;
            moved_player = true;
            cancel_trade_for(player->id(), dispatch, true);
            player->clear_legacy_npc_item_mode();
          } else if (mail.kind == ActorMailKind::turn && effective_mail.dir >= 8) {
            reject_move(false);
            break;
          }

          it->second->on_mail(effective_mail, context);

          if (effective_mail.kind == ActorMailKind::turn || effective_mail.kind == ActorMailKind::move ||
              effective_mail.kind == ActorMailKind::run) {
            queue_packet(dispatch, player->session_id(), make_ack_packet(player->session_id(), true));
          }

          if (effective_mail.kind == ActorMailKind::move || effective_mail.kind == ActorMailKind::run) {
            player->consume_move_action(current_tick, effective_mail.kind == ActorMailKind::run,
                                        budgets_.tick_ms);
            sync_area_state(dispatch, config_, *player);
            if (try_gate_transfer(*player, dispatch, current_tick, now_ms)) {
              break;
            }
          }

          for (const auto watcher_id : legacy_ref_target_player_ids(*player, now_ms)) {
            if (watcher_id == player->id()) {
              continue;
            }
            const auto* watcher = find_player(watcher_id);
            if (watcher == nullptr) {
              continue;
            }
            switch (effective_mail.kind) {
              case ActorMailKind::turn:
                queue_packet(dispatch, watcher->session_id(),
                             make_turn_like_packet(watcher->session_id(), kSmTurn, *player, false));
                break;
              case ActorMailKind::move:
                queue_packet(dispatch, watcher->session_id(),
                             make_turn_like_packet(watcher->session_id(), kSmWalk, *player, false));
                break;
              case ActorMailKind::run:
                queue_packet(dispatch, watcher->session_id(),
                             make_turn_like_packet(watcher->session_id(), kSmRun, *player, false));
                break;
              default:
                break;
            }
          }
          if (moved_player) {
            sync_visibility_after_actor_move(*player, old_x, old_y, player->x(), player->y(),
                                             dispatch, now_ms);
          }
        } else {
          it->second->on_mail(mail, context);
        }
      }
      break;
    }
  }
}

