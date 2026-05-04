param(
  [string]$Base = "",
  [string]$OutputPath = $env:GITHUB_OUTPUT
)

. "$PSScriptRoot/common.ps1"

if ($env:GITHUB_EVENT_NAME -eq "workflow_dispatch") {
  $runServer = $true
  $runClient = $true
  $changedFiles = @()
} else {
  if (-not $Base) {
    $Base = Get-CiBase
  }

  $changedFiles = @(Get-ChangedFiles -Base $Base)
  $runServer = $false
  $runClient = $false

  foreach ($file in $changedFiles) {
    $normalized = $file -replace '\\', '/'

    if ($normalized -match '^(ModernServer/|Server/|Mir200/)') {
      $runServer = $true
      continue
    }

    if ($normalized -match '^(ModernClient/|Client/|WIL/)') {
      $runClient = $true
      continue
    }

    if ($normalized -match '^(shared/|ci/|\.github/workflows/)' -or
        $normalized -match '(^|/)(CMakeLists\.txt|CMakePresets\.json|vcpkg\.json|vcpkg-configuration\.json)$') {
      $runServer = $true
      $runClient = $true
    }
  }
}

Write-Host "Fast CI changed files:"
foreach ($file in $changedFiles) {
  Write-Host "  $file"
}

Write-Host "Run server fast CI: $runServer"
Write-Host "Run client fast CI: $runClient"

if ($OutputPath) {
  Add-Content -LiteralPath $OutputPath -Value "run_server=$($runServer.ToString().ToLowerInvariant())"
  Add-Content -LiteralPath $OutputPath -Value "run_client=$($runClient.ToString().ToLowerInvariant())"
}
