param(
  [ValidateSet("phase1-fast", "phase2-fast", "phase3-nightly")]
  [string]$Suite = "phase1-fast",

  [ValidateSet("all", "server", "client")]
  [string]$Project = "all",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug",

  [int]$TimeoutSeconds = 120,

  [switch]$List
)

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/compat_suites.ps1"

function Write-CiSuiteList {
  param([Parameter(Mandatory = $true)][string]$SuiteName)

  Write-Host "CI suite: $SuiteName"
  foreach ($name in @("ModernServer", "ModernClient")) {
    $targets = @(Get-CiBuildTargets -Suite $SuiteName -ProjectName $name)
    $tests = @(Get-CiTestNames -Suite $SuiteName -ProjectName $name)
    Write-Host ""
    Write-Host "$name build targets ($($targets.Count)):"
    foreach ($target in $targets) {
      Write-Host "  - $target"
    }
    Write-Host "$name tests ($($tests.Count)):"
    foreach ($test in $tests) {
      Write-Host "  - $test"
    }
  }

  $quarantined = @(Get-CiQuarantinedTests)
  if ($quarantined.Count -gt 0) {
    Write-Host ""
    Write-Host "Quarantined tests:"
    foreach ($entry in $quarantined) {
      Write-Host "  - $($entry.Project)/$($entry.Test): $($entry.Reason)"
    }
  }
}

if ($List) {
  Write-CiSuiteList -SuiteName $Suite
  exit 0
}

function Invoke-CompatCtest {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $regex = Get-CiTestRegex -Suite $Suite -ProjectName $Name
  if (-not $regex) {
    Fail "$Name has no tests for CI suite '$Suite'."
  }

  $buildDir = Get-CiCMakeBuildDir -RelativePath $RelativePath
  if (-not (Test-Path -LiteralPath $buildDir)) {
    Fail "$Name build directory is missing: $buildDir. Run ci/scripts/check_build.ps1 first."
  }

  Write-Host "Running $Name compatibility suite '$Suite'..."
  Write-Host "CTest regex: $regex"
  ctest `
    --test-dir $buildDir `
    -C $Config `
    --output-on-failure `
    --timeout "$TimeoutSeconds" `
    -R $regex
  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Compatibility suite '$Suite' failed. Suite contents:"
    Write-CiSuiteList -SuiteName $Suite
    Write-Host ""
    Fail "$Name compatibility suite '$Suite' failed. Reproduce locally with: pwsh ci/scripts/run_compat_tests.ps1 -Suite $Suite -Project $Project -Config $Config"
  }
}

if ($Project -in @("all", "server")) {
  Invoke-CompatCtest -Name "ModernServer" -RelativePath "ModernServer"
}

if ($Project -in @("all", "client")) {
  Invoke-CompatCtest -Name "ModernClient" -RelativePath "ModernClient"
}
