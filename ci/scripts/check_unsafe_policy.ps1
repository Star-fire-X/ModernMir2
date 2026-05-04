param([string]$Base = "")

. "$PSScriptRoot/common.ps1"

if (-not $Base) {
  $Base = Get-CiBase
}

function Test-CoreCompatibilityPath {
  param([Parameter(Mandatory = $true)][string]$File)

  return $File -match '^(ModernServer/src/world|ModernServer/src/protocol|ModernServer/src/services|ModernClient/src/game|ModernClient/src/protocol|ModernClient/src/assets|shared/(legacy|protocol))/'
}

function Test-CppSourcePath {
  param([Parameter(Mandatory = $true)][string]$File)

  return $File -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$'
}

function Add-UnsafeFinding {
  param(
    [Parameter(Mandatory = $true)][string]$File,
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Message
  )

  Write-CiError -File $File -Title $Title -Message $Message
  $script:Failed = $true
}

$diffRange = if ($Base -eq "HEAD") { "HEAD" } else { "$Base...HEAD" }
$diff = git diff --unified=0 --no-color $diffRange
if ($LASTEXITCODE -ne 0) {
  Fail "Unable to read changed lines for unsafe policy check."
}

$script:Failed = $false
$currentFile = ""
foreach ($line in $diff) {
  if ($line -like "+++ b/*") {
    $currentFile = $line.Substring(6)
    continue
  }

  if (-not $currentFile -or -not $line.StartsWith("+") -or $line.StartsWith("+++")) {
    continue
  }

  $added = $line.Substring(1)
  if ((Test-CppSourcePath $currentFile) -and
      $added -match '#\s*define\s+private\s+public') {
    Add-UnsafeFinding `
      -File $currentFile `
      -Title "Unsafe private access in test/code" `
      -Message "Do not add '#define private public'. Expose a narrow test hook under an explicit test-only compile definition instead."
  }

  if (-not (Test-CoreCompatibilityPath $currentFile)) {
    continue
  }

  if ($added -match '(std::thread|std::jthread|std::async\s*\(|std::this_thread::sleep_for|(^|[^A-Za-z_])Sleep\s*\(|CreateTimerQueueTimer|SetTimer\s*\()') {
    Add-UnsafeFinding `
      -File $currentFile `
      -Title "Unsafe threading or timer change" `
      -Message "Core compatibility paths must preserve the legacy single-loop/tick model. Do not add threads, sleeps, or OS timers here without splitting the design into an explicitly reviewed PR."
  }

  if ($added -match '(std::random_device|std::mt19937|std::mt19937_64|std::rand\s*\(|srand\s*\()' -and
      $added -notmatch 'legacy_random') {
    Add-UnsafeFinding `
      -File $currentFile `
      -Title "Unsafe non-legacy random source" `
      -Message "Core gameplay compatibility should use the deterministic legacy random path. Do not add standard random sources in core compatibility code."
  }

  if ($added -match 'std::chrono::(system_clock|high_resolution_clock)') {
    Add-UnsafeFinding `
      -File $currentFile `
      -Title "Unsafe wall-clock dependency" `
      -Message "Core compatibility code should avoid wall-clock/high-resolution timing. Keep tick behavior deterministic and tied to the existing frame/tick model."
  }
}

if ($script:Failed) {
  Fail "Unsafe policy guard failed. Fix the flagged lines or split the work into an explicitly reviewed migration-policy PR."
}

Write-Host "Unsafe policy guard passed."
