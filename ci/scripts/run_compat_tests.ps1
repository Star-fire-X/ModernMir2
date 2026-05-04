param(
  [ValidateSet("phase1-fast", "phase2-fast")]
  [string]$Suite = "phase1-fast",

  [ValidateSet("all", "server", "client")]
  [string]$Project = "all",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug",

  [int]$TimeoutSeconds = 120
)

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/compat_suites.ps1"

function Invoke-CompatCtest {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $regex = Get-CiTestRegex -Suite $Suite -ProjectName $Name
  if (-not $regex) {
    Fail "$Name has no tests for CI suite '$Suite'."
  }

  $buildDir = Join-Path $RepoRoot "$RelativePath/build-ci"
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
    Fail "$Name compatibility suite '$Suite' failed. Reproduce locally with: pwsh ci/scripts/run_compat_tests.ps1 -Suite $Suite -Project $Project -Config $Config"
  }
}

if ($Project -in @("all", "server")) {
  Invoke-CompatCtest -Name "ModernServer" -RelativePath "ModernServer"
}

if ($Project -in @("all", "client")) {
  Invoke-CompatCtest -Name "ModernClient" -RelativePath "ModernClient"
}
