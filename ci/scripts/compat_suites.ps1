$ErrorActionPreference = "Stop"

function Get-CiSuiteNames {
  return @("phase1-fast", "phase2-fast", "phase3-nightly")
}

function Join-Unique {
  param([string[]]$Values)

  $seen = @{}
  $result = @()
  foreach ($value in $Values) {
    if (-not $value) {
      continue
    }
    if (-not $seen.ContainsKey($value)) {
      $seen[$value] = $true
      $result += $value
    }
  }
  return $result
}

function Assert-CiSuite {
  param([Parameter(Mandatory = $true)][string]$Suite)

  if ((Get-CiSuiteNames) -notcontains $Suite) {
    Fail "Unknown CI suite '$Suite'. Valid suites: $((Get-CiSuiteNames) -join ', ')."
  }
}

function Get-CiBuildTargets {
  param(
    [Parameter(Mandatory = $true)][string]$Suite,
    [Parameter(Mandatory = $true)][string]$ProjectName
  )

  Assert-CiSuite $Suite

  $serverPhase1 = @(
    "mir2_host",
    "mir2_core_smoke",
    "mir2_logic_smoke",
    "mir2_legacy_frame_smoke",
    "mir2_legacy_protocol_command_golden_smoke",
    "mir2_canonical_legacy_command_smoke",
    "mir2_client_v1_protocol_smoke",
    "mir2_movement_blocking_legacy_smoke",
    "mir2_combat_smoke",
    "mir2_items_smoke"
  )

  $serverPhase2 = Join-Unique -Values ($serverPhase1 + @(
    "mir2_world_tick_cadence_smoke",
    "mir2_player_movement_smoke",
    "mir2_visibility_delta_smoke",
    "mir2_monster_legacy_combat_damage_smoke",
    "mir2_skill_formula_golden_smoke",
    "mir2_skill_protocol_golden_smoke",
    "mir2_skill_status_poison_buff_hide_shield_smoke",
    "mir2_equipment_cantakeon_legacy_smoke",
    "mir2_equipment_special_combat_smoke",
    "mir2_weapon_upgrade_smoke",
    "mir2_player_death_drop_smoke",
    "mir2_weapon_luck_lifecycle_smoke",
    "mir2_item_phase2_ground_smoke",
    "mir2_item_phase2_use_smoke",
    "mir2_special_consumables_legacy_smoke",
    "mir2_monster_legacy_drop_scatter_smoke",
    "mir2_canonical_login_state_smoke",
    "mir2_client_v1_canonical_command_smoke",
    "mir2_client_v1_auth_boundary_smoke",
    "mir2_client_v1_login_to_world_flow_smoke",
    "mir2_client_v1_game_command_bridge_smoke",
    "mir2_client_v1_inventory_npc_smoke",
    "mir2_client_v1_world_min_loop_smoke",
    "mir2_client_v1_magic_gateway_smoke"
  ))

  $serverPhase3 = Join-Unique -Values ($serverPhase2 + @(
    "mir2_legacy_environment_smoke",
    "mir2_legacy_mapinfo_import_routes_smoke",
    "mir2_visibility_phase3_smoke",
    "mir2_safe_zone_legacy_smoke",
    "mir2_door_open_close_smoke",
    "mir2_cross_map_gate_transfer_smoke",
    "mir2_legacy_player_lifecycle_smoke",
    "mir2_legacy_player_inbox_smoke",
    "mir2_legacy_creature_order_smoke",
    "mir2_legacy_monster_cursor_smoke",
    "mir2_legacy_monster_import_defs_spawns_drops_smoke",
    "mir2_monster_base_object_smoke",
    "mir2_monster_legacy_tick_ai_smoke",
    "mir2_monster_struck_retaliation_smoke",
    "mir2_monster_death_corpse_ghost_smoke",
    "mir2_monster_spawn_alive_count_smoke",
    "mir2_monster_drop_gold_split_smoke",
    "mir2_monster_slave_ownership_smoke",
    "mir2_monster_drop_random_upgrade_smoke",
    "mir2_legacy_npc_merchant_cursor_smoke",
    "mir2_legacy_random_smoke",
    "mir2_magic_config_golden_smoke",
    "mir2_skill_legacy_spell_sequence_smoke",
    "mir2_skill_book_lifecycle_smoke",
    "mir2_skill_training_lvexp_smoke",
    "mir2_skill_bujuk_status_phase4_smoke",
    "mir2_skill_summon_slave_smoke",
    "mir2_skill_common_magic_matrix_smoke",
    "mir2_skill_weapon_sword_common_smoke",
    "mir2_skill_training_persistence_smoke",
    "mir2_legacy_combat_formula_smoke",
    "mir2_player_derived_ability_equipment_smoke",
    "mir2_pk_attack_mode_rules_smoke",
    "mir2_player_death_revive_smoke",
    "mir2_experience_levelup_legacy_smoke",
    "mir2_legacy_spell_resolution_smoke",
    "mir2_legacy_item_resolution_smoke",
    "mir2_legacy_script_runtime_smoke",
    "mir2_legacy_script_condition_action_smoke",
    "mir2_legacy_script_command_decode_full_smoke",
    "mir2_repository_smoke",
    "mir2_legacy_character_import_smoke",
    "mir2_legacy_character_import_unknown_data_smoke",
    "mir2_auth_smoke",
    "mir2_account_smoke",
    "mir2_world_kick_smoke",
    "mir2_world_invalid_command_smoke",
    "mir2_gateway_kick_smoke",
    "mir2_item_phase3_trade_smoke",
    "mir2_repair_smoke",
    "mir2_sell_smoke",
    "mir2_buy_smoke",
    "mir2_merchant_dialog_smoke",
    "mir2_scripted_merchant_smoke",
    "mir2_scripted_npc_dialog_smoke",
    "mir2_scripted_castle_dialog_smoke",
    "mir2_merchant_castle_admin_smoke",
    "mir2_guild_castle_menu_smoke",
    "mir2_guild_castle_business_smoke",
    "mir2_world_castle_refresh_smoke",
    "mir2_world_castle_push_refresh_smoke",
    "mir2_world_guild_offline_admin_smoke",
    "mir2_world_guild_cross_map_sync_smoke",
    "mir2_storage_smoke",
    "mir2_monster_ai_smoke",
    "mir2_pk_rules_smoke",
    "mir2_aoe_spell_smoke",
    "mir2_debuff_spell_smoke",
    "mir2_player_buff_smoke",
    "mir2_player_debuff_smoke",
    "mir2_player_shield_smoke",
    "mir2_host_startup_order_smoke",
    "mir2_host_status_snapshot_smoke",
    "mir2_client_v1_account_state_smoke",
    "mir2_client_v1_lobby_character_state_smoke",
    "mir2_client_v1_enter_map_persistence_smoke",
    "mir2_client_v1_enter_world_boundary_smoke",
    "mir2_client_v1_session_boundary_smoke",
    "mir2_client_v1_gateway_lifecycle_smoke",
    "mir2_client_v1_world_entry_smoke",
    "mir2_client_v1_game_gateway_notice_smoke",
    "mir2_client_v1_imported_character_flow_smoke",
    "mir2_client_v1_host_runtime_flow_smoke",
    "mir2_config_gateway_flags_smoke"
  ))

  $clientPhase1 = @(
    "modern_mir2_client",
    "modern_client_asset_smoke",
    "modern_client_protocol_map_smoke",
    "modern_client_flow_smoke",
    "modern_client_text_encoding_smoke",
    "modern_client_movement_smoke"
  )

  $clientPhase2 = Join-Unique -Values ($clientPhase1 + @(
    "modern_client_item_pending_smoke",
    "modern_client_character_select_smoke",
    "modern_client_legacy_animation_smoke"
  ))

  $clientPhase3 = Join-Unique -Values ($clientPhase2 + @(
    "modern_client_map_render_alignment_smoke",
    "modern_client_ui_smoke",
    "modern_client_audio_mapping_smoke",
    "modern_client_wav_reader_smoke",
    "modern_client_direct_sound_backend_smoke",
    "modern_client_scene_audio_smoke",
    "modern_client_audio_service_smoke",
    "modern_client_legacy_sound_rules_smoke",
    "modern_client_legacy_audio_cue_tracker_smoke"
  ))

  if ($ProjectName -eq "ModernServer") {
    if ($Suite -eq "phase1-fast") {
      return $serverPhase1
    }
    if ($Suite -eq "phase2-fast") {
      return $serverPhase2
    }
    return $serverPhase3
  }

  if ($ProjectName -eq "ModernClient") {
    if ($Suite -eq "phase1-fast") {
      return $clientPhase1
    }
    if ($Suite -eq "phase2-fast") {
      return $clientPhase2
    }
    return $clientPhase3
  }

  return @()
}

function Get-CiTestNames {
  param(
    [Parameter(Mandatory = $true)][string]$Suite,
    [Parameter(Mandatory = $true)][string]$ProjectName
  )

  $targets = @(Get-CiBuildTargets -Suite $Suite -ProjectName $ProjectName)
  if ($ProjectName -eq "ModernServer") {
    return @($targets | Where-Object { $_ -ne "mir2_host" })
  }

  if ($ProjectName -eq "ModernClient") {
    $localResourceTests = @(Get-LocalResourceTestNames)
    return @($targets | Where-Object {
      $_ -ne "modern_mir2_client" -and
      ($localResourceTests -notcontains $_)
    })
  }

  return @()
}

function Get-CiAggregateBuildTarget {
  param(
    [Parameter(Mandatory = $true)][string]$Suite,
    [Parameter(Mandatory = $true)][string]$ProjectName
  )

  Assert-CiSuite $Suite

  if ($Suite -ne "phase2-fast") {
    return ""
  }

  if ($ProjectName -eq "ModernServer") {
    return "mir2_ci_phase2_fast"
  }

  if ($ProjectName -eq "ModernClient") {
    return "modern_client_ci_phase2_fast"
  }

  return ""
}

function Get-CiTestRegex {
  param(
    [Parameter(Mandatory = $true)][string]$Suite,
    [Parameter(Mandatory = $true)][string]$ProjectName
  )

  $tests = @(Get-CiTestNames -Suite $Suite -ProjectName $ProjectName)
  if ($tests.Count -eq 0) {
    return ""
  }

  $escaped = @($tests | ForEach-Object { [regex]::Escape($_) })
  return "^($($escaped -join '|'))$"
}

function Get-CiQuarantinedTests {
  return @(
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_legacy_combat_resolution_smoke"
      Reason = "Debug/MSVC currently asserts and then times out; keep outside nightly until fixed in a focused PR."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_legacy_user_engine_timer_smoke"
      Reason = "Debug/MSVC currently asserts and then times out; keep outside nightly until fixed in a focused PR."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_death_drop_smoke"
      Reason = "Debug/MSVC currently asserts and then times out; keep outside nightly until fixed in a focused PR."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_legacy_player_disconnect_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_spawn_count_range_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_spawn_zentime_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_drop_owner_protection_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_slave_exp_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_slave_lifecycle_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_skill_active_spell_phase3_smoke"
      Reason = "Local phase3 validation fails in Debug/MSVC; keep outside nightly until skill behavior is fixed separately."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_equipment_derived_upgrade_stats_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_legacy_mapquest_import_trigger_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_legacy_event_manager_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_race_ai_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_special_race_smoke"
      Reason = "Local phase3 validation asserts in Debug/MSVC and then waits for the assertion path until timeout."
    },
    [pscustomobject]@{
      Project = "ModernServer"
      Test = "mir2_monster_home_leash_smoke"
      Reason = "Local phase3 validation times out and asserts on the monster walk expectation; keep outside nightly until fixed separately."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_asset_smoke"
      Reason = "Requires a real Legend of Mir resource tree; run with ci/scripts/run_local_resource_tests.ps1 instead."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_map_render_alignment_smoke"
      Reason = "Ends with real-resource assertions against map and WIL data; keep local-only until fixture support exists."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_audio_mapping_smoke"
      Reason = "Requires real Wav resources; run with ci/scripts/run_local_resource_tests.ps1 instead."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_wav_reader_smoke"
      Reason = "Requires real Wav resources; run with ci/scripts/run_local_resource_tests.ps1 instead."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_audio_service_smoke"
      Reason = "Requires real Wav resources; run with ci/scripts/run_local_resource_tests.ps1 instead."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_scene_audio_smoke"
      Reason = "Requires real Wav resources; run with ci/scripts/run_local_resource_tests.ps1 instead."
    },
    [pscustomobject]@{
      Project = "ModernClient"
      Test = "modern_client_legacy_audio_cue_tracker_smoke"
      Reason = "Requires real Wav resources; run with ci/scripts/run_local_resource_tests.ps1 instead."
    }
  )
}

function Get-LocalResourceTestNames {
  return @(
    "modern_client_asset_smoke",
    "modern_client_map_render_alignment_smoke",
    "modern_client_audio_mapping_smoke",
    "modern_client_wav_reader_smoke",
    "modern_client_audio_service_smoke",
    "modern_client_scene_audio_smoke",
    "modern_client_legacy_audio_cue_tracker_smoke"
  )
}
