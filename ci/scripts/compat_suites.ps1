$ErrorActionPreference = "Stop"

function Get-CiSuiteNames {
  return @("phase1-fast", "phase2-fast")
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
    "mir2_special_consumables_legacy_smoke",
    "mir2_monster_legacy_drop_scatter_smoke",
    "mir2_client_v1_auth_boundary_smoke",
    "mir2_client_v1_login_to_world_flow_smoke",
    "mir2_client_v1_game_command_bridge_smoke",
    "mir2_client_v1_inventory_npc_smoke",
    "mir2_client_v1_world_min_loop_smoke",
    "mir2_client_v1_magic_gateway_smoke"
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

  if ($ProjectName -eq "ModernServer") {
    if ($Suite -eq "phase1-fast") {
      return $serverPhase1
    }
    return $serverPhase2
  }

  if ($ProjectName -eq "ModernClient") {
    if ($Suite -eq "phase1-fast") {
      return $clientPhase1
    }
    return $clientPhase2
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
    return @($targets | Where-Object {
      $_ -ne "modern_mir2_client" -and
      $_ -ne "modern_client_asset_smoke"
    })
  }

  return @()
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
