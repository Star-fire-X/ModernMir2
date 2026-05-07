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
        sync_all_player_visibility(dispatch);
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
    case ActorMailKind::despawn: {
      auto it = objects_.find(mail.actor_id);
      if (it != objects_.end()) {
        if (auto* player = as_player(it->second.get()); player != nullptr) {
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
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || merchant == nullptr || !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      if (legacy_execute_npc_script(*requester, *merchant, "@main", dispatch, current_tick,
                                    now_ms)) {
        break;
      }
      if (merchant->supports_buy()) {
        queue_packet(dispatch, requester->session_id(),
                     make_send_goods_list_packet(requester->session_id(), target_it->second->id(),
                                                 *merchant, item_configs_));
      } else if (merchant->supports_storage()) {
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
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_sell_packet(requester->session_id(), target_it->second->id()));
      } else if (merchant->supports_repair()) {
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
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || merchant == nullptr || !in_interaction_range(*requester, *target_it->second)) {
        break;
      }

      const auto lowered_payload = util::lower_copy(mail.payload);
      const auto script_handled = legacy_execute_npc_script(
          *requester, *merchant, mail.payload, dispatch, current_tick, now_ms);
      if (script_handled && !legacy_script_action_uses_existing_business(lowered_payload, *merchant)) {
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

      if (mail.payload == "@buy" && merchant->supports_buy()) {
        queue_packet(dispatch, requester->session_id(),
                     make_send_goods_list_packet(requester->session_id(), target_it->second->id(),
                                                 *merchant, item_configs_));
      } else if (mail.payload == "@sell" && merchant->supports_sell()) {
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_sell_packet(requester->session_id(), target_it->second->id()));
      } else if (mail.payload == "@repair" && merchant->supports_repair()) {
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_repair_packet(requester->session_id(), target_it->second->id()));
      } else if (mail.payload == "@storage" && merchant->supports_storage()) {
        const auto storage_count = static_cast<std::uint16_t>(std::count_if(
            requester->character().storage_items.begin(), requester->character().storage_items.end(),
            [](const LegacyUserItem& item) { return !is_empty(item); }));
        queue_packet(dispatch, requester->session_id(),
                     make_send_user_storage_packet(requester->session_id(), target_it->second->id(),
                                                   storage_count));
      } else if (mail.payload == "@getback" && merchant->supports_storage()) {
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_storage() ||
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_buy() ||
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_repair() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto* item = requester->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      const auto cost = item != nullptr ? compute_repair_cost(*item, item_configs_) : -1;
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_sell() ||
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

      GroundItem ground_item;
      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "drop_item");
      ground_item.id = next_ground_item_id_;
      ground_item.item = *bag_item;
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
      ground_item.item = *removed;
      ground_items_[ground_item.id] = ground_item;
      player->refresh_derived_state(item_configs_);

      sync_visibility_after_item_change(ground_item.x, ground_item.y, dispatch, ground_item.id);
      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_drop_result_packet(player->session_id(), true, removed->make_index,
                                           ground_item.name));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_save_character(dispatch, *player);
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
          add_legacy_trace(dispatch, "LegacyItem", "merge_state_reject", mail, current_tick,
                           now_ms, false, mail.amount, 0, "drop_gold");
          break;
        }
        player->spend_gold(mail.amount);
        refresh_ground_item_ownership(existing->second, now_ms);
        const auto same_owner = existing->second.owner_actor_id == ground_item.owner_actor_id;
        existing->second.gold_amount += mail.amount;
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

      sync_visibility_after_item_change(ground_item.x, ground_item.y, dispatch, ground_item.id);
      queue_packet(dispatch, player->session_id(),
                   make_gold_changed_packet(player->session_id(), player->character().gold));
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", add_result.merged ? "merged" : "success", mail,
                       current_tick, now_ms, true, ground_item.gold_amount, 0, "drop_gold");
      break;
    }
    case ActorMailKind::repair_item: {
      auto requester_it = objects_.find(mail.actor_id);
      auto target_it = objects_.find(mail.target_actor_id);
      if (requester_it == objects_.end() || target_it == objects_.end() ||
          target_it->second->kind() != GameObjectKind::npc) {
        break;
      }
      auto* requester = as_player(requester_it->second.get());
      const auto* merchant = as_npc(target_it->second.get());
      if (requester == nullptr || merchant == nullptr || !merchant->supports_repair() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      auto* item = requester->bag_item_mutable(mail.item_make_index, mail.payload, item_configs_);
      if (item == nullptr) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_repair_result_packet(requester->session_id(), false, 0, 0, 0));
        break;
      }
      const auto cost = compute_repair_cost(*item, item_configs_);
      if (cost < 0 || (cost > 0 && !requester->can_spend_gold(cost))) {
        queue_packet(dispatch, requester->session_id(),
                     make_user_repair_result_packet(requester->session_id(), false, 0, 0, 0));
        break;
      }
      requester->spend_gold(cost);
      const auto dura_gap = static_cast<std::int32_t>(item->dura_max) - static_cast<std::int32_t>(item->dura);
      if (dura_gap > 0) {
        item->dura_max =
            static_cast<std::uint16_t>(std::max(0, static_cast<std::int32_t>(item->dura_max) - dura_gap / 30));
      }
      item->dura = item->dura_max;
      queue_packet(dispatch, requester->session_id(),
                   make_user_repair_result_packet(requester->session_id(), true,
                                                  requester->character().gold, item->dura,
                                                  item->dura_max));
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_sell() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto item = requester->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!item.has_value() || !can_sell_item(*merchant, *item, item_configs_)) {
        if (item.has_value()) {
          static_cast<void>(requester->add_bag_item(*item));
        }
        queue_packet(dispatch, requester->session_id(),
                     make_user_sell_result_packet(requester->session_id(), false, 0));
        break;
      }
      const auto price = compute_buy_price(*item, item_configs_, merchant->merchant_price(item->index));
      if (price < 0) {
        static_cast<void>(requester->add_bag_item(*item));
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_buy() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      auto item = take_merchant_item(*merchant, mail.payload, mail.item_make_index, item_configs_);
      if (!item.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 1, 0));
        break;
      }
      const auto price = compute_merchant_sell_price(*merchant, *item, item_configs_);
      if (!requester->can_add_bag_item(*item, item_configs_) || !requester->has_free_bag_slot()) {
        merchant->merchant_items_mutable().push_back(*item);
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      if (price <= 0 || !requester->can_spend_gold(price)) {
        merchant->merchant_items_mutable().push_back(*item);
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 3, 0));
        break;
      }
      requester->spend_gold(price);
      if (!requester->add_bag_item(*item)) {
        requester->add_gold(price);
        merchant->merchant_items_mutable().push_back(*item);
        queue_packet(dispatch, requester->session_id(),
                     make_buy_item_result_packet(requester->session_id(), false, 2, 0));
        break;
      }
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_add_item_packet(requester->session_id(), *item, item_configs_));
      queue_packet(dispatch, requester->session_id(),
                   make_weight_changed_packet(requester->session_id(), requester->character()));
      queue_packet(dispatch, requester->session_id(),
                   make_buy_item_result_packet(requester->session_id(), true,
                                               requester->character().gold, item->make_index));
      dispatch.persist_requests.push_back(make_save_merchant_state_request(*merchant));
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_storage() ||
          !in_interaction_range(*requester, *target_it->second)) {
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
      if (requester == nullptr || merchant == nullptr || !merchant->supports_storage() ||
          !in_interaction_range(*requester, *target_it->second)) {
        break;
      }
      const auto item =
          requester->remove_storage_item(mail.item_make_index, mail.payload, item_configs_);
      if (!item.has_value()) {
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFail, 0));
        break;
      }
      if (!requester->can_add_bag_item(*item, item_configs_) || !requester->add_bag_item(*item)) {
        static_cast<void>(requester->add_storage_item(*item));
        queue_packet(dispatch, requester->session_id(),
                     make_take_back_storage_result_packet(requester->session_id(),
                                                          kSmTakeBackStorageItemFullBag, 0));
        break;
      }
      requester->refresh_derived_state(item_configs_);
      queue_packet(dispatch, requester->session_id(),
                   make_add_item_packet(requester->session_id(), *item, item_configs_));
      queue_packet(dispatch, requester->session_id(),
                   make_take_back_storage_result_packet(requester->session_id(),
                                                        kSmTakeBackStorageItemOk,
                                                        item->make_index));
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

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "pickup_item");
      const auto first_item_id = environment_.first_item_object_id(player->x(), player->y());
      auto ground_it = first_item_id.has_value() ? ground_items_.find(*first_item_id)
                                                 : ground_items_.end();
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
        remove_item_from_visibility(ground_item.id, dispatch);
        ground_items_.erase(ground_it);

        queue_packet(dispatch, player->session_id(),
                     make_gold_changed_packet(player->session_id(), player->character().gold));
        queue_save_character(dispatch, *player);
        add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                         ground_item.gold_amount, 0, "pickup_gold");
        break;
      }

      if (!player->can_add_bag_item(ground_it->second.item, item_configs_) ||
          !player->add_bag_item(ground_it->second.item)) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         0, 0, "pickup_item");
        break;
      }

      player->refresh_derived_state(item_configs_);
      const auto ground_item = ground_it->second;
      static_cast<void>(environment_.delete_from_map(
          ground_item.x, ground_item.y, LegacyMapObjectShape::item_object, ground_item.id));
      remove_item_from_visibility(ground_item.id, dispatch);
      ground_items_.erase(ground_it);

      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), ground_item.item, item_configs_));
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

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true,
                       mail.item_slot, 0, "take_on_item");
      player->refresh_derived_state(item_configs_);
      const auto previous_feature = player->character().feature;
      const auto removed =
          player->remove_bag_item(mail.item_make_index, mail.payload, item_configs_);
      if (!removed.has_value()) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         mail.item_slot, 0, "take_on_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      const auto* item_config = find_item_config(item_configs_, removed->index);
      if (item_config == nullptr || !item_fits_slot(*item_config, mail.item_slot)) {
        add_legacy_trace(dispatch, "LegacyItem", "slot_reject", mail, current_tick, now_ms, false,
                         mail.item_slot, 0, "take_on_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      const auto* current_equipped =
          player->equipped_item(static_cast<std::size_t>(mail.item_slot));
      const auto* current_config =
          current_equipped != nullptr ? find_item_config(item_configs_, current_equipped->index)
                                      : nullptr;
      if (current_equipped != nullptr && !legacy_item_can_take_off(current_config, *current_equipped)) {
        add_legacy_trace(dispatch, "LegacyItem", "takeoff_locked", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      std::string reject_reason;
      const auto old_slot_weight =
          current_equipped != nullptr ? item_weight(*current_equipped, item_configs_) : 0;
      if (!legacy_can_take_on_item(player->character(), *item_config, *removed, mail.item_slot,
                                   player->character().ability.wear_weight,
                                   player->character().ability.hand_weight, old_slot_weight,
                                   &reject_reason)) {
        add_legacy_trace(dispatch, "LegacyItem", reject_reason, mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }
      if (current_equipped != nullptr && !is_empty(*current_equipped) &&
          !player->can_add_bag_item(*current_equipped, item_configs_)) {
        add_legacy_trace(dispatch, "LegacyItem", "swap_bag_weight", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_on_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_take_on_result_packet(player->session_id(), false, 0));
        break;
      }

      std::optional<LegacyUserItem> swapped_item;
      if (const auto* equipped = player->equipped_item(static_cast<std::size_t>(mail.item_slot));
          equipped != nullptr && !is_empty(*equipped)) {
        swapped_item =
            player->remove_equipped_item(static_cast<std::size_t>(mail.item_slot), equipped->make_index,
                                         {}, item_configs_);
      }

      if (swapped_item.has_value()) {
        static_cast<void>(player->add_bag_item(*swapped_item));
      }
      player->equip_item(static_cast<std::size_t>(mail.item_slot), *removed);
      player->refresh_derived_state(item_configs_);

      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed, item_configs_));
      if (swapped_item.has_value()) {
        queue_packet(dispatch, player->session_id(),
                     make_del_item_packet(player->session_id(), player->id(), *swapped_item,
                                          item_configs_));
        queue_packet(dispatch, player->session_id(),
                     make_add_item_packet(player->session_id(), *swapped_item, item_configs_));
      }
      queue_packet(dispatch, player->session_id(),
                   make_take_on_result_packet(player->session_id(), true, player->character().feature));
      queue_packet(dispatch, player->session_id(),
                   make_update_item_packet(player->session_id(), player->id(),
                                           player->character()
                                               .equipped_items[static_cast<std::size_t>(mail.item_slot)],
                                           item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_ability_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(), make_sub_ability_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_use_items_packet(player->session_id(), *player, item_configs_));
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
          !player->can_add_bag_item(*current_equipped, item_configs_)) {
        add_legacy_trace(dispatch, "LegacyItem", "takeoff_reject", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_off_item");
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }
      const auto previous_feature = player->character().feature;
      const auto removed = player->remove_equipped_item(static_cast<std::size_t>(mail.item_slot),
                                                        mail.item_make_index, mail.payload,
                                                        item_configs_);
      if (!removed.has_value() || !player->add_bag_item(*removed)) {
        add_legacy_trace(dispatch, "LegacyItem", "state_rollback", mail, current_tick, now_ms,
                         false, mail.item_slot, 0, "take_off_item");
        if (removed.has_value()) {
          player->equip_item(static_cast<std::size_t>(mail.item_slot), *removed);
        }
        queue_packet(dispatch, player->session_id(),
                     make_take_off_result_packet(player->session_id(), false, 0));
        break;
      }

      player->refresh_derived_state(item_configs_);
      queue_packet(dispatch, player->session_id(),
                   make_del_item_packet(player->session_id(), player->id(), *removed, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_take_off_result_packet(player->session_id(), true, player->character().feature));
      queue_packet(dispatch, player->session_id(),
                   make_add_item_packet(player->session_id(), *removed, item_configs_));
      queue_packet(dispatch, player->session_id(),
                   make_ability_packet(player->session_id(), player->character()));
      queue_packet(dispatch, player->session_id(), make_sub_ability_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_use_items_packet(player->session_id(), *player, item_configs_));
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

      add_legacy_trace(dispatch, "LegacyItem", "validate", mail, current_tick, now_ms, true, 0, 0,
                       "eat_item");
      const auto removed =
          mail.item_make_index != 0
              ? player->remove_bag_item(mail.item_make_index, mail.payload, item_configs_)
              : (mail.item_slot >= 0 ? player->remove_bag_item_at(static_cast<std::size_t>(mail.item_slot))
                                      : std::nullopt);
      if (!removed.has_value()) {
        add_legacy_trace(dispatch, "LegacyItem", "bag_reject", mail, current_tick, now_ms, false,
                         0, 0, "eat_item");
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }

      const auto* item_config = find_item_config(item_configs_, removed->index);
      if (item_config != nullptr && legacy_item_is_unbind_bundle(*item_config)) {
        const auto* target_config = find_item_config_by_name_or_id(item_configs_, item_config->unbind_item);
        if (target_config == nullptr || item_config->unbind_count <= 0) {
          add_legacy_trace(dispatch, "LegacyItem", "unbind_config_reject", mail, current_tick,
                           now_ms, false, removed->index, 0, "eat_item");
          static_cast<void>(player->add_bag_item(*removed));
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        player->refresh_derived_state(item_configs_);
        const auto free_slots = std::count_if(
            player->character().bag_items.begin(), player->character().bag_items.end(),
            [](const LegacyUserItem& item) { return is_empty(item); });
        const auto current_weight = static_cast<std::int32_t>(player->character().ability.weight);
        const auto target_weight = std::max(target_config->weight, 0) * item_config->unbind_count;
        if (free_slots < item_config->unbind_count ||
            current_weight + target_weight >
                static_cast<std::int32_t>(player->character().ability.max_weight)) {
          add_legacy_trace(dispatch, "LegacyItem", "unbind_bag_reject", mail, current_tick,
                           now_ms, false, item_config->unbind_count, 0, "eat_item");
          static_cast<void>(player->add_bag_item(*removed));
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
              static_cast<void>(
                  player->remove_bag_item(rollback.make_index, {}, item_configs_));
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
                       make_add_item_packet(player->session_id(), item, item_configs_));
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

      if (item_config != nullptr && legacy_item_is_magic_book(*item_config)) {
        const auto book_result = legacy_read_magic_book(*player, *item_config, magic_configs_);
        if (book_result.status != LegacyReadBookStatus::learned) {
          add_legacy_trace(dispatch, "LegacySkill", "book_reject", mail, current_tick, now_ms,
                           false, book_result.magic_id, 0,
                           legacy_read_book_status_name(book_result.status));
          static_cast<void>(player->add_bag_item(*removed));
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }

        const auto* learned_magic = player->learned_magic(book_result.magic_id);
        if (learned_magic == nullptr) {
          add_legacy_trace(dispatch, "LegacySkill", "book_reject", mail, current_tick, now_ms,
                           false, book_result.magic_id, 0, "slot_missing");
          static_cast<void>(player->add_bag_item(*removed));
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
                           now_ms, false, removed->index, 0, kind);
          static_cast<void>(player->add_bag_item(*removed));
          queue_packet(dispatch, player->session_id(),
                       make_eat_result_packet(player->session_id(), false));
          break;
        }
        player->refresh_derived_state(item_configs_);
        const auto session_id = player->session_id();
        const auto actor_id = player->id();
        const auto character_after_use = player->character();
        if (!try_item_map_move(*player, target_map, target_x, target_y, dispatch, current_tick,
                               now_ms)) {
          add_legacy_trace(dispatch, "LegacyItem", "scroll_transfer_reject", mail,
                           current_tick, now_ms, false, removed->index, 0, kind);
          if (auto* rollback_player = find_player(actor_id); rollback_player != nullptr) {
            static_cast<void>(rollback_player->add_bag_item(*removed));
            rollback_player->refresh_derived_state(item_configs_);
          }
          queue_packet(dispatch, session_id, make_eat_result_packet(session_id, false));
        } else {
          queue_packet(dispatch, session_id,
                       make_del_item_packet(session_id, actor_id, *removed, item_configs_));
          queue_packet(dispatch, session_id, make_eat_result_packet(session_id, true));
          queue_packet(dispatch, session_id,
                       make_weight_changed_packet(session_id, character_after_use));
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
                         removed->index, 0, "eat_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }

      if (config_.no_drug) {
        add_legacy_trace(dispatch, "LegacyItem", "nodrug_reject", mail, current_tick, now_ms,
                         false, removed->index, 0, "eat_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }

      const auto old_hp = player->character().ability.hp;
      const auto old_mp = player->character().ability.mp;
      player->apply_consumable(*item_config);
      if (player->character().ability.hp == old_hp && player->character().ability.mp == old_mp) {
        add_legacy_trace(dispatch, "LegacyItem", "consume_no_effect", mail, current_tick,
                         now_ms, false, removed->index, 0, "eat_item");
        static_cast<void>(player->add_bag_item(*removed));
        queue_packet(dispatch, player->session_id(),
                     make_eat_result_packet(player->session_id(), false));
        break;
      }
      auto consumed_item = *removed;
      const auto keep_consumed_item = consumed_item.dura > 1;
      if (keep_consumed_item) {
        --consumed_item.dura;
        static_cast<void>(player->add_bag_item(consumed_item));
      }
      player->refresh_derived_state(item_configs_);
      if (keep_consumed_item) {
        queue_packet(dispatch, player->session_id(),
                     make_update_item_packet(player->session_id(), player->id(), consumed_item,
                                             item_configs_));
      } else {
        queue_packet(dispatch, player->session_id(),
                     make_del_item_packet(player->session_id(), player->id(), *removed,
                                          item_configs_));
      }
      queue_packet(dispatch, player->session_id(),
                   make_eat_result_packet(player->session_id(), true));
      queue_packet(dispatch, player->session_id(),
                   make_health_spell_changed_packet(player->session_id(), *player));
      queue_packet(dispatch, player->session_id(),
                   make_weight_changed_packet(player->session_id(), player->character()));
      queue_save_character(dispatch, *player);
      add_legacy_trace(dispatch, "LegacyItem", "success", mail, current_tick, now_ms, true,
                       removed->index, 0, "eat_item");
      break;
    }
    case ActorMailKind::attack: {
      auto attacker_it = objects_.find(mail.actor_id);
      if (attacker_it == objects_.end()) {
        break;
      }
      auto* attacker = as_player(attacker_it->second.get());
      if (attacker == nullptr || attacker->is_dead()) {
        break;
      }

      ActorMail effective_mail = mail;
      auto effective_ident = mail.game_message.ident;
      auto sword_magic_id = legacy_sword_skill_for_attack_ident(effective_ident);
      auto prepared_sword_magic_id = 0;
      if (effective_ident == kCmHit) {
        const auto prepared_magic_id = attacker->pending_legacy_sword_skill(current_tick);
        if (prepared_magic_id != 0) {
          prepared_sword_magic_id = prepared_magic_id;
          sword_magic_id = prepared_magic_id;
          effective_ident = legacy_attack_ident_for_sword_skill(prepared_magic_id);
        } else if (attacker->learned_magic(3) != nullptr) {
          attacker->clear_legacy_sword_skill();
          sword_magic_id = 3;
        }
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
      }

      attacker->on_mail(effective_mail, context);

      const auto attack_range = resolve_attack_range(effective_ident);
      GameObject* target =
          find_attack_target_by_actor_id(objects_, *attacker, mail.target_actor_id, attack_range);
      if (target == nullptr && (mail.x != 0 || mail.y != 0)) {
        target = find_attack_target_by_position(objects_, *attacker, mail.x, mail.y, attack_range);
      }
      if (target == nullptr) {
        target = find_attack_target_in_front(objects_, *attacker, attack_range);
      }
      if (effective_ident == kCmLongHit && target != nullptr) {
        if (!target_in_attack_line(*attacker, *target, 2)) {
          target = nullptr;
        } else if (std::max(std::abs(target->x() - attacker->x()),
                            std::abs(target->y() - attacker->y())) == 2) {
          const auto [dx, dy] = direction_delta(actor_dir(*attacker));
          if (!environment_.can_walk(attacker->x() + dx, attacker->y() + dy, false)) {
            target = nullptr;
          }
        }
      }
      auto wide_targets =
          effective_ident == kCmWideHit
              ? collect_wide_hit_targets(objects_, *attacker, config_, now_ms)
              : std::vector<GameObject*>{};
      if (effective_ident == kCmWideHit) {
        target = wide_targets.empty() ? nullptr : wide_targets.front();
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

      if (auto* weapon = attacker->equipped_item_mutable(static_cast<std::size_t>(kEquipWeapon));
          weapon != nullptr && !is_empty(*weapon) && weapon->dura > 0) {
        const auto before = *weapon;
        weapon->dura = weapon->dura > 100 ? static_cast<std::uint16_t>(weapon->dura - 100) : 0;
        queue_packet(dispatch, attacker->session_id(),
                     make_update_item_packet(attacker->session_id(), attacker->id(), *weapon,
                                             item_configs_));
        if (display_dura_units(before.dura) != display_dura_units(weapon->dura) ||
            weapon->dura == 0) {
          queue_packet(dispatch, attacker->session_id(),
                       make_dura_change_packet(attacker->session_id(), kEquipWeapon, *weapon,
                                               item_configs_));
        }
      }

      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (watcher.id() != attacker->id() && !is_legacy_visible_to(watcher, *attacker)) {
          return;
        }
        queue_packet(dispatch, watcher.session_id(),
                     make_hit_packet(watcher.session_id(), *attacker, effective_ident));
      });
      add_legacy_trace(dispatch, "LegacyCombat", "attack_broadcast", effective_mail, current_tick, now_ms,
                       true, 0, 0, "SM_HIT");

      if (target == nullptr) {
        add_legacy_trace(dispatch, "LegacyCombat", "no_target", effective_mail, current_tick, now_ms,
                         false, 0, 0, "attack");
        break;
      }

      const auto dc_min = packed_min(attacker->character().ability.dc);
      const auto dc_max = std::max(dc_min, packed_max(attacker->character().ability.dc));
      const auto attack_roll =
          legacy_random_value(dispatch, "LegacyCombat", "attack_power_roll",
                              std::max(1, dc_max - dc_min + 1), attacker->id(),
                              target->id(), "attack", now_ms, current_tick);
      const auto attack_power =
          legacy_packed_attack_power(*attacker, effective_ident, attack_roll);
      const auto undead_power = legacy_player_undead_power(*attacker, item_configs_);
      const auto hit_roll =
          legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                              std::max(legacy_speed_point(*target), 1), attacker->id(),
                              target->id(), "attack", now_ms, current_tick);
      if (legacy_accuracy_point(*attacker) <= hit_roll) {
        add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick, now_ms, false,
                         hit_roll, 0, "AccuracyPoint<=Random(SpeedPoint)");
        break;
      }

      const auto [ac_min, ac_max] = actor_physical_defense_range(*target);
      const auto armor_roll =
          legacy_random_value(dispatch, "LegacyCombat", "armor_roll",
                              std::max(1, ac_max - ac_min + 1), attacker->id(),
                              target->id(), "attack", now_ms, current_tick);
      const auto damage =
          legacy_physical_struck_damage(*target, attack_power, armor_roll, undead_power);
      add_legacy_trace(dispatch, "LegacyCombat", "damage", effective_mail, current_tick, now_ms, true,
                       attack_power, damage, "GetAttackPower/GetHitStruckDamage");
      std::int32_t applied_damage = 0;
      std::int32_t absorbed_damage = 0;
      bool shield_broken = false;
      std::string shield_name{};
      bool target_died = false;
      Monster* slain_monster = nullptr;

      if (auto* player_target = as_player(target); player_target != nullptr) {
        if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
          player_target->record_pk_hiter(attacker->id(), now_ms);
        }
        const auto damage_result = player_target->apply_damage(damage, current_tick);
        applied_damage = damage_result.hp_damage;
        absorbed_damage = damage_result.absorbed_damage;
        shield_broken = damage_result.shield_broken;
        shield_name = damage_result.shield_name;
        target_died = player_target->is_dead();
        if (applied_damage > 0) {
          const auto struck_wdam =
              legacy_random_value(dispatch, "LegacyCombat", "struck_dura_damage", 10,
                                  attacker->id(), player_target->id(), "StruckDamage",
                                  now_ms, current_tick) +
              5;
          auto apply_struck_dura = [&](std::size_t slot, bool force) {
            auto* item = player_target->equipped_item_mutable(slot);
            if (item == nullptr || is_empty(*item) || item->dura == 0) {
              return;
            }
            if (!force) {
              const auto chance =
                  legacy_random_value(dispatch, "LegacyCombat", "struck_dura_gate", 8,
                                      attacker->id(), player_target->id(), "StruckDamage",
                                      now_ms, current_tick);
              if (chance != 0) {
                return;
              }
            }
            const auto before = *item;
            item->dura = item->dura > struck_wdam
                             ? static_cast<std::uint16_t>(item->dura - struck_wdam)
                             : 0;
            queue_packet(dispatch, player_target->session_id(),
                         make_update_item_packet(player_target->session_id(), player_target->id(),
                                                 *item, item_configs_));
            if (display_dura_units(before.dura) != display_dura_units(item->dura) ||
                item->dura == 0) {
              queue_packet(dispatch, player_target->session_id(),
                           make_dura_change_packet(player_target->session_id(), slot, *item,
                                                   item_configs_));
            }
            if (item->dura == 0) {
              player_target->refresh_derived_state(item_configs_);
              queue_packet(dispatch, player_target->session_id(),
                           make_ability_packet(player_target->session_id(),
                                               player_target->character()));
              queue_packet(dispatch, player_target->session_id(),
                           make_sub_ability_packet(player_target->session_id(), *player_target));
            }
          };
          apply_struck_dura(kEquipDress, true);
          for (std::size_t slot = 1; slot <= kEquipBoots; ++slot) {
            if (slot == kEquipBujuk) {
              continue;
            }
            apply_struck_dura(slot, false);
          }
        }
        if (target_died) {
          player_target->mark_dead(now_ms);
          if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2 &&
              player_target->has_recent_pk_hiter(attacker->id(), now_ms)) {
            attacker->inc_pk_point(100);
            queue_packet(dispatch, attacker->session_id(),
                         make_username_packet(attacker->session_id(), attacker->id(),
                                              attacker->character().character_name,
                                              actor_name_color(*attacker)));
          }
        }
      } else if (auto* monster_target = as_monster(target); monster_target != nullptr) {
        applied_damage = apply_legacy_monster_damage(
            objects_, *monster_target, damage, attacker->id(), now_ms);
        if (applied_damage > 0) {
          notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
        }
        target_died = monster_target->is_dead();
        slain_monster = target_died ? monster_target : nullptr;
      }

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

      for_each_player(objects_, [&](std::uint64_t, const Player& watcher) {
        if (watcher.id() != target->id() && !is_legacy_visible_to(watcher, *target)) {
          return;
        }
        queue_packet(dispatch, watcher.session_id(),
                     target_died ? make_death_packet(watcher.session_id(), *target,
                                                     watcher.id() == target->id())
                                 : make_struck_packet(watcher.session_id(), *target, attacker->id(),
                                                      applied_damage, false));
      });
      add_legacy_trace(dispatch, "LegacyCombat", target_died ? "death" : "struck", effective_mail,
                       current_tick, now_ms, true, 0, applied_damage,
                       target_died ? "SM_DEATH" : "SM_STRUCK");

      if (slain_monster != nullptr) {
        finalize_monster_death(slain_monster->id(), attacker->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick, now_ms, true, 0,
                         applied_damage, "WinExp");
      }
      if (effective_ident == kCmWideHit) {
        for (auto* extra_target : wide_targets) {
          if (extra_target == nullptr || extra_target == target ||
              objects_.find(extra_target->id()) == objects_.end() ||
              !is_attackable_target(*extra_target)) {
            continue;
          }
          const auto extra_hit_roll =
              legacy_random_value(dispatch, "LegacyCombat", "hit_check",
                                  std::max(legacy_speed_point(*extra_target), 1), attacker->id(),
                                  extra_target->id(), "wide_hit", now_ms, current_tick);
          if (legacy_accuracy_point(*attacker) <= extra_hit_roll) {
            add_legacy_trace(dispatch, "LegacyCombat", "miss", effective_mail, current_tick,
                             now_ms, false, extra_hit_roll, 0,
                             "WideHit AccuracyPoint<=Random(SpeedPoint)");
            continue;
          }
          const auto extra_attack_roll =
              legacy_random_value(dispatch, "LegacyCombat", "attack_power_roll",
                                  std::max(1, dc_max - dc_min + 1), attacker->id(),
                                  extra_target->id(), "wide_hit", now_ms, current_tick);
          const auto extra_attack_power =
              legacy_packed_attack_power(*attacker, effective_ident, extra_attack_roll);
          const auto [extra_ac_min, extra_ac_max] = actor_physical_defense_range(*extra_target);
          const auto extra_armor_roll =
              legacy_random_value(dispatch, "LegacyCombat", "armor_roll",
                                  std::max(1, extra_ac_max - extra_ac_min + 1),
                                  attacker->id(), extra_target->id(), "wide_hit", now_ms,
                                  current_tick);
          const auto extra_damage =
              legacy_physical_struck_damage(*extra_target, extra_attack_power, extra_armor_roll,
                                            undead_power);
          std::int32_t extra_applied_damage = 0;
          bool extra_target_died = false;
          Monster* extra_slain_monster = nullptr;
          if (auto* player_target = as_player(extra_target); player_target != nullptr) {
            if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2) {
              player_target->record_pk_hiter(attacker->id(), now_ms);
            }
            const auto damage_result = player_target->apply_damage(extra_damage, current_tick);
            extra_applied_damage = damage_result.hp_damage;
            extra_target_died = player_target->is_dead();
            if (extra_target_died) {
              player_target->mark_dead(now_ms);
            }
          } else if (auto* monster_target = as_monster(extra_target); monster_target != nullptr) {
            extra_applied_damage = apply_legacy_monster_damage(
                objects_, *monster_target, extra_damage, attacker->id(), now_ms);
            if (extra_applied_damage > 0) {
              notify_owned_slaves_target(*attacker, monster_target->id(), now_ms);
            }
            extra_target_died = monster_target->is_dead();
            extra_slain_monster = extra_target_died ? monster_target : nullptr;
          }
          if (extra_applied_damage <= 0) {
            continue;
          }
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
                                                  attacker->id(), extra_applied_damage, false));
          });
          add_legacy_trace(dispatch, "LegacyCombat",
                           extra_target_died ? "death" : "struck", effective_mail,
                           current_tick, now_ms, true, sword_magic_id, extra_applied_damage,
                           extra_target_died ? "SM_DEATH" : "SM_STRUCK");
          if (extra_slain_monster != nullptr) {
            finalize_monster_death(extra_slain_monster->id(), attacker->id(), dispatch,
                                   current_tick);
            add_legacy_trace(dispatch, "LegacyCombat", "exp", effective_mail, current_tick,
                             now_ms, true, sword_magic_id, extra_applied_damage, "WinExp");
          }
        }
      }
      if (sword_magic_id != 0) {
        if (auto* user_magic = attacker->learned_magic_mutable(sword_magic_id);
            user_magic != nullptr) {
          const auto magic_it = magic_configs_.find(sword_magic_id);
          if (magic_it != magic_configs_.end()) {
            LegacyRandom fallback_random;
            auto& random = legacy_random_ != nullptr ? *legacy_random_ : fallback_random;
            const auto training =
                legacy_train_magic(*attacker, *user_magic, magic_it->second, random);
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

      const auto magic_id = static_cast<std::int32_t>(mail.game_message.tag);
      const auto magic_it = magic_configs_.find(magic_id);
      if (magic_it == magic_configs_.end()) {
        add_legacy_trace(dispatch, "LegacySpell", "magic_missing", mail, current_tick, now_ms,
                         false, magic_id, 0, "spell");
        queue_packet(dispatch, attacker->session_id(), make_ack_packet(attacker->session_id(), false));
        break;
      }

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
            current_tick + legacy_delay_ms_to_ticks(5000, budgets_.tick_ms);
        attacker->prepare_legacy_sword_skill(magic_id, expire_tick);
        queue_packet(dispatch, attacker->session_id(),
                     make_ack_packet(attacker->session_id(), true));
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

        auto fail_magic = [&](std::string label) {
          add_legacy_trace(dispatch, "LegacySpell", "spell_fail", mail, current_tick, now_ms,
                           false, magic_id, 0, std::move(label));
          queue_packet(dispatch, attacker->session_id(),
                       make_ack_packet(attacker->session_id(), false));
          queue_actor_origin_packet(objects_, dispatch, *attacker, true, [&](const Player& watcher) {
            queue_packet(dispatch, watcher.session_id(),
                         make_magic_fire_fail_packet(watcher.session_id(), *attacker));
          });
        };

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
            (magic_id == 8 || magic_id == 14 || magic_id == 15 || magic_id == 18 ||
             magic_id == 19 || magic_id == 24 || magic_id == 31 || magic_id == 36 ||
             magic_id == 17 || magic_id == 30)) {
          fire_x = attacker->x();
          fire_y = attacker->y();
        }

        if (magic_id == 1 || magic_id == 5 || magic_id == 11 || magic_id == 32 ||
            magic_id == 35) {
          std::string reason;
          if (!harmful_target_ok(target, reason)) {
            fail_magic(reason);
            break;
          }
        }
        if (magic_id == 1 || magic_id == 5) {
          if (!legacy_mag_can_hit_target(attacker->x(), attacker->y(), target)) {
            fail_magic("MagCanHitTarget");
            break;
          }
          if (std::abs(target->x() - fire_x) > 1 || std::abs(target->y() - fire_y) > 1) {
            fail_magic("target_not_near_spell_xy");
            break;
          }
        }
        if (magic_id == 28 && target == nullptr) {
          fail_magic("target_missing");
          break;
        }
        if ((magic_id == 9 || magic_id == 10) &&
            attacker->x() == fire_x && attacker->y() == fire_y) {
          fail_magic("line_direction");
          break;
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
          const auto result = apply_legacy_magic_damage(objects_, dispatch, *attacker,
                                                        hit_target, config_, damage,
                                                        current_tick, now_ms);
          if (result.applied_damage > 0 && as_monster(&hit_target) != nullptr) {
            notify_owned_slaves_target(*attacker, hit_target.id(), now_ms);
          }
          add_legacy_trace(dispatch, "LegacySpell",
                           result.target_died ? "death" : "mag_struck", mail,
                           current_tick, now_ms, result.applied_damage > 0, magic_id,
                           result.applied_damage, std::string(label));
          if (result.slain_monster_id != 0) {
            finalize_monster_death(result.slain_monster_id, attacker->id(), dispatch,
                                   current_tick);
            add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms,
                             true, magic_id, result.applied_damage, "WinExp");
          }
          return result.applied_damage > 0;
        };

        bool train = false;
        bool send_magic_fire = true;
        bool spell_branch_aborted = false;
        std::uint64_t fire_target_id = target != nullptr ? target->id() : 0;
        switch (magic_id) {
          case 1:
          case 5: {
            const auto anti_roll = random.random(10);
            const auto anti_magic = target != nullptr ? legacy_actor_anti_magic(*target) : 0;
            add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail, current_tick,
                             now_ms, legacy_anti_magic_pass(anti_magic, anti_roll), anti_roll,
                             anti_magic,
                             "AntiMagic");
            if (legacy_anti_magic_pass(anti_magic, anti_roll) && target != nullptr) {
              const auto power = legacy_fireball_power(*attacker, magic_it->second.legacy,
                                                       user_magic->level, random);
              queue_delayed_hit(*target, power, 600, 2, LegacyDelayedEffectKind::delay_magic);
              train = as_monster(target) != nullptr;
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
              add_legacy_trace(dispatch, "LegacySpell", "poison_target_reject", mail,
                               current_tick, now_ms, false, magic_id, 0, reason);
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
          case 8: {
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
            const auto anti_roll = random.random(10);
            const auto anti_magic = target != nullptr ? legacy_actor_anti_magic(*target) : 0;
            add_legacy_trace(dispatch, "LegacySpell", "anti_magic", mail, current_tick,
                             now_ms, legacy_anti_magic_pass(anti_magic, anti_roll), anti_roll,
                             anti_magic,
                             "AntiMagic");
            if (legacy_anti_magic_pass(anti_magic, anti_roll) && target != nullptr) {
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
            }
            break;
          }
          case 13:
          case 17:
          case 14:
          case 15:
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
            add_legacy_trace(dispatch, "LegacySpell", "bujuk_used", mail, current_tick,
                             now_ms, true, magic_id,
                             bujuk_slot->item != nullptr ? bujuk_slot->item->dura : 0,
                             bujuk_slot->slot == kEquipBujuk ? "U_BUJUK" : "U_ARMRINGL");

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

            if (magic_id == 14 || magic_id == 15 || magic_id == 36) {
              const auto seconds = legacy_defence_status_seconds(
                  *attacker, magic_it->second.legacy, user_magic->level, random);
              auto targets = collect_legacy_area_targets(objects_, *attacker, config_,
                                                         fire_x, fire_y, 3, true);
              auto applied = 0;
              for (auto* friend_target : targets) {
                auto* player_target = as_player(friend_target);
                if (player_target == nullptr) {
                  continue;
                }
                const auto duration_ticks = legacy_delay_ms_to_ticks(
                    static_cast<std::uint32_t>(std::max(seconds, 1) * 1000),
                    budgets_.tick_ms);
                const auto changed = magic_id == 14
                                         ? player_target->activate_legacy_magic_defence_up(
                                               duration_ticks, current_tick)
                                         : player_target->activate_legacy_defence_up(
                                               duration_ticks, current_tick);
                if (changed) {
                  ++applied;
                  broadcast_legacy_char_status_changed(dispatch, *player_target);
                  queue_packet(dispatch, player_target->session_id(),
                               make_ability_packet(player_target->session_id(),
                                                   player_target->character()));
                  queue_packet(dispatch, player_target->session_id(),
                               make_sub_ability_packet(player_target->session_id(),
                                                       *player_target));
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
            if (monster_target == nullptr) {
              add_legacy_trace(dispatch, "LegacySlave", "tame_reject", mail,
                               current_tick, now_ms, false, magic_id, 0, "target_missing");
              break;
            }
            const auto max_slaves = 2 + static_cast<std::int32_t>(user_magic->level);
            const auto level_limit =
                static_cast<std::int32_t>(attacker->character().ability.level) +
                static_cast<std::int32_t>(user_magic->level) * 2 + 2;
            if (monster_target->level() > level_limit) {
              add_legacy_trace(dispatch, "LegacySlave", "tame_reject", mail,
                               current_tick, now_ms, false, monster_target->level(),
                               level_limit, "level");
              break;
            }
            train = tame_player_slave(*attacker, *monster_target, user_magic->level,
                                      max_slaves, dispatch, current_tick, now_ms, mail);
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
            auto* monster_target = as_monster(target);
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
                      objects_, dispatch, *attacker, *monster_target, config_, damage,
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
                    finalize_monster_death(result.slain_monster_id, attacker->id(), dispatch,
                                           current_tick);
                    add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick,
                                     now_ms, true, magic_id, damage, "WinExp");
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
          queue_actor_origin_packet(objects_, dispatch, *attacker, true, [&](const Player& watcher) {
            queue_packet(dispatch, watcher.session_id(),
                         make_magic_fire_packet(watcher.session_id(), *attacker, fire_x, fire_y,
                                                magic_it->second, fire_target_id));
          });
          add_legacy_trace(dispatch, "LegacySpell", "magic_fire", mail, current_tick, now_ms,
                           true, magic_id, 0, "SM_MAGICFIRE");
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

      std::vector<std::uint64_t> slain_monster_ids;
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
            if (magic_it->second.dispel_negative &&
                player_target->clear_negative_status_effects(current_tick) > 0) {
              applied_player_effect = true;
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
          if (target_died) {
            player_target->mark_dead(now_ms);
            if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2 &&
                player_target->has_recent_pk_hiter(attacker->id(), now_ms)) {
              attacker->inc_pk_point(100);
              queue_packet(dispatch, attacker->session_id(),
                           make_username_packet(attacker->session_id(), attacker->id(),
                                                attacker->character().character_name,
                                                actor_name_color(*attacker)));
            }
          }
        } else if (auto* monster_target = as_monster(&resolved_target); monster_target != nullptr) {
          if (harmful_spell) {
            applied_damage = apply_legacy_monster_damage(
                objects_, *monster_target, damage, attacker->id(), now_ms);
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

        if (applied_damage > 0) {
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
          add_legacy_trace(dispatch, "LegacySpell", target_died ? "death" : "magic_fire", mail,
                           current_tick, now_ms, true, magic_id, applied_damage,
                           target_died ? "SM_DEATH" : "RM_MAGICFIRE");
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
          slain_monster_ids.push_back(slain_monster->id());
        }
      }

      std::sort(slain_monster_ids.begin(), slain_monster_ids.end());
      slain_monster_ids.erase(std::unique(slain_monster_ids.begin(), slain_monster_ids.end()),
                              slain_monster_ids.end());
      for (const auto slain_monster_id : slain_monster_ids) {
        finalize_monster_death(slain_monster_id, attacker->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms, true,
                         static_cast<std::int32_t>(slain_monster_id), 0, "WinExp");
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
      sync_player_visibility(*player, dispatch, true);
      sync_all_player_visibility(dispatch);
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
      if (handle_guild_castle_business_command(*speaker, objects_, mail.payload,
                                               guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }
      if (handle_castle_admin_command(*speaker, mail.payload, guild_castle_snapshot_, dispatch)) {
        castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
        break;
      }
      const auto line = speaker->character().character_name + ": " + mail.payload;
      queue_actor_origin_packet(objects_, dispatch, *speaker, true, [&](const Player& player) {
        queue_packet(dispatch, player.session_id(),
                     make_hear_packet(player.session_id(), speaker->id(), line));
      });
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
        if (mail.power > 0 || target_died) {
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
        add_legacy_trace(dispatch, "MonsterSpecial", target_died ? "death" : "struck",
                         mail, current_tick, now_ms, mail.power > 0 || target_died,
                         mail.magic_id, mail.power,
                         target_died ? "SM_DEATH" : "SM_STRUCK");
        if (target_died) {
          if (auto* slain_monster = as_monster(&target); slain_monster != nullptr) {
            auto reward_actor_id = caster_object.id();
            if (const auto* caster_monster = as_monster(&caster_object);
                caster_monster != nullptr && caster_monster->master_actor_id() != 0) {
              reward_actor_id = caster_monster->master_actor_id();
            }
            finalize_monster_death(slain_monster->id(), reward_actor_id, dispatch,
                                   current_tick);
          }
        }
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
                                                              false);
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
                                                      current_tick, budgets_.tick_ms);
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
        if (target_died) {
          player_target->mark_dead(now_ms);
          if (!config_.fight_zone && !config_.fight3_zone && player_target->pk_level() < 2 &&
              player_target->has_recent_pk_hiter(caster->id(), now_ms)) {
            caster->inc_pk_point(100);
            queue_packet(dispatch, caster->session_id(),
                         make_username_packet(caster->session_id(), caster->id(),
                                              caster->character().character_name,
                                              actor_name_color(*caster)));
          }
        }
        if (damage_result.absorbed_damage > 0) {
          queue_packet(dispatch, player_target->session_id(),
                       make_health_spell_changed_packet(player_target->session_id(),
                                                        *player_target));
        }
      } else if (auto* monster_target = as_monster(&target); monster_target != nullptr) {
        applied_damage = apply_legacy_monster_damage(
            objects_, *monster_target, damage, caster->id(), now_ms);
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
      add_legacy_trace(dispatch, "LegacySpell", target_died ? "death" : "mag_struck",
                       mail, current_tick, now_ms, true, mail.magic_id, applied_damage,
                       target_died ? "SM_DEATH" : "SM_STRUCK");
      if (slain_monster != nullptr) {
        finalize_monster_death(slain_monster->id(), caster->id(), dispatch, current_tick);
        add_legacy_trace(dispatch, "LegacySpell", "exp", mail, current_tick, now_ms,
                         true, mail.magic_id, applied_damage, "WinExp");
      }
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

          for_each_player(objects_, [&](std::uint64_t actor_id, const Player& watcher) {
            if (actor_id == player->id()) {
              return;
            }
            if (!in_legacy_view_range(watcher, *player)) {
              return;
            }
            switch (effective_mail.kind) {
              case ActorMailKind::turn:
                queue_packet(dispatch, watcher.session_id(),
                             make_turn_like_packet(watcher.session_id(), kSmTurn, *player, false));
                break;
              case ActorMailKind::move:
                queue_packet(dispatch, watcher.session_id(),
                             make_turn_like_packet(watcher.session_id(), kSmWalk, *player, false));
                break;
              case ActorMailKind::run:
                queue_packet(dispatch, watcher.session_id(),
                             make_turn_like_packet(watcher.session_id(), kSmRun, *player, false));
                break;
              default:
                break;
            }
          });
          if (moved_player) {
            sync_visibility_after_actor_move(*player, old_x, old_y, player->x(), player->y(),
                                             dispatch);
          }
        } else {
          it->second->on_mail(mail, context);
        }
      }
      break;
    }
  }
}

