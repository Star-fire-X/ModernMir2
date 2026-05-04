param(
  [ValidateSet("all", "server", "client")]
  [string]$Project = "all",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug",

  [string]$Regex = "",
  [int]$TimeoutSeconds = 120
)

. "$PSScriptRoot/common.ps1"

function Invoke-Ctest {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $buildDir = Join-Path $RepoRoot "$RelativePath/build-ci"
  if (-not (Test-Path -LiteralPath $buildDir)) {
    Fail "$Name build directory is missing: $buildDir. Run ci/scripts/check_build.ps1 first."
  }

  $args = @(
    "--test-dir", $buildDir,
    "-C", $Config,
    "--output-on-failure",
    "--timeout", "$TimeoutSeconds"
  )

  if ($Regex) {
    $args += @("-R", $Regex)
  }

  Write-Host "Running $Name tests..."
  ctest @args
  if ($LASTEXITCODE -ne 0) {
    Fail "$Name tests failed. Reproduce locally with: ctest --test-dir $buildDir -C $Config --output-on-failure"
  }
}

if ($Project -in @("all", "server")) {
  Invoke-Ctest -Name "ModernServer" -RelativePath "ModernServer"
}

if ($Project -in @("all", "client")) {
  Invoke-Ctest -Name "ModernClient" -RelativePath "ModernClient"
}
