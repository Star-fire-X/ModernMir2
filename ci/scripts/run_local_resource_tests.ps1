param(
  [string]$AssetRoot = "F:\mir2\Legend of Mir",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug"
)

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/compat_suites.ps1"

$assetRootFull = [System.IO.Path]::GetFullPath($AssetRoot)
if (-not (Test-Path -LiteralPath $assetRootFull -PathType Container)) {
  Fail "Asset root not found: $assetRootFull. Provide -AssetRoot or keep real client resources outside Git in F:\mir2\Legend of Mir."
}

if (-not (Test-Path -LiteralPath (Join-Path $assetRootFull "Data") -PathType Container)) {
  Fail "Asset root is missing Data/: $assetRootFull"
}

if (-not (Test-Path -LiteralPath (Join-Path $assetRootFull "Map") -PathType Container)) {
  Fail "Asset root is missing Map/: $assetRootFull"
}

$buildDir = Join-Path $RepoRoot "ModernClient/build-ci"
if (-not (Test-Path -LiteralPath $buildDir)) {
  Fail "ModernClient build directory is missing: $buildDir. Run ci/scripts/check_build.ps1 -Project client -Config $Config -Suite phase3-nightly first."
}

$binaryDir = Join-Path $buildDir $Config
if (-not (Test-Path -LiteralPath $binaryDir)) {
  Fail "ModernClient binary directory is missing: $binaryDir. Run the client phase3-nightly build first."
}

$tests = @(Get-LocalResourceTestNames)
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mir2-local-resource-tests-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $workDir | Out-Null

$link = Join-Path $workDir "Legend of Mir"
$createdLink = $false
try {
  New-Item -ItemType Junction -Path $link -Target $assetRootFull | Out-Null
  $createdLink = $true

  Push-Location $workDir
  try {
    foreach ($test in $tests) {
      $exe = Join-Path $binaryDir "$test.exe"
      if (-not (Test-Path -LiteralPath $exe)) {
        Fail "Missing test executable: $exe. Build phase3-nightly client targets before running local resource tests."
      }

      Write-Host "Running local resource test: $test"
      & $exe
      if ($LASTEXITCODE -ne 0) {
        Fail "Local resource test failed: $test"
      }
    }
  } finally {
    Pop-Location
  }
} finally {
  if ($createdLink -and (Test-Path -LiteralPath $link)) {
    Remove-Item -LiteralPath $link -Force
  }
  if (Test-Path -LiteralPath $workDir) {
    Remove-Item -LiteralPath $workDir -Force
  }
}

Write-Host "Local resource tests passed for: $assetRootFull"
