param(
  [string]$Base = "",
  [int]$MaxProductionLines = 600
)

. "$PSScriptRoot/common.ps1"

if (-not $Base) {
  $Base = Get-CiBase
}

function Test-ProductionPath {
  param([Parameter(Mandatory = $true)][string]$File)

  return $File -match '^(ModernServer/src|ModernClient/src|shared)/'
}

function Test-CoreCompatibilityPath {
  param([Parameter(Mandatory = $true)][string]$File)

  return $File -match '^(ModernServer/src/world|ModernServer/src/protocol|ModernServer/src/services|ModernClient/src/game|ModernClient/src/protocol|ModernClient/src/assets|shared/(legacy|protocol))/'
}

$body = $env:PR_BODY
if ($body -and $body -match '(?im)^\s*CI-Large-Change:\s*approved\s*$') {
  Write-Host "Large change scope guard bypassed by PR body marker: CI-Large-Change: approved"
  exit 0
}

$diffRange = if ($Base -eq "HEAD") { "HEAD" } else { "$Base...HEAD" }
$numstat = git diff --numstat $diffRange
if ($LASTEXITCODE -ne 0) {
  Fail "Unable to read numstat for change scope check."
}

$totalProductionLines = 0
$coreTouched = $false
$largest = @()
foreach ($line in $numstat) {
  if (-not $line) {
    continue
  }

  $parts = $line -split "`t"
  if ($parts.Count -lt 3) {
    continue
  }

  $added = if ($parts[0] -match '^\d+$') { [int]$parts[0] } else { 0 }
  $deleted = if ($parts[1] -match '^\d+$') { [int]$parts[1] } else { 0 }
  $file = $parts[2]
  $changedLines = $added + $deleted

  if (Test-ProductionPath $file) {
    $totalProductionLines += $changedLines
    $largest += [pscustomobject]@{ File = $file; Lines = $changedLines }
  }

  if (Test-CoreCompatibilityPath $file) {
    $coreTouched = $true
  }
}

Write-Host "Production code changed lines: $totalProductionLines"
Write-Host "Core compatibility path touched: $coreTouched"

if ($coreTouched -and $totalProductionLines -gt $MaxProductionLines) {
  foreach ($entry in ($largest | Sort-Object Lines -Descending | Select-Object -First 5)) {
    Write-CiError `
      -File $entry.File `
      -Title "Large core compatibility change" `
      -Message "$($entry.Lines) production changed lines in this file."
  }

  Fail "This PR changes core compatibility paths and has $totalProductionLines production changed lines, above the $MaxProductionLines line limit. Split the work, or add an explicit 'CI-Large-Change: approved' line to the PR body after manual review."
}

Write-Host "Change scope guard passed."
