param(
  [ValidateSet("all", "server", "client")]
  [string]$Project = "all",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug",

  [string]$Generator = "",
  [string]$Arch = "x64",
  [string]$ToolchainFile = "",

  [switch]$Fast,
  [switch]$Clean
)

. "$PSScriptRoot/common.ps1"

function Get-CMakeGenerators {
  $help = cmake --help
  return $help | Where-Object { $_ -match '^\s*(\*?\s*)?[A-Za-z].*=' }
}

function Resolve-CMakeGenerator {
  param([string]$RequestedGenerator = "")

  if ($RequestedGenerator) {
    return $RequestedGenerator
  }

  $available = @(Get-CMakeGenerators)
  $preferred = @(
    "Visual Studio 18 2026",
    "Visual Studio 17 2022",
    "Visual Studio 16 2019",
    "Ninja"
  )

  foreach ($candidate in $preferred) {
    if ($available | Where-Object { $_ -match [regex]::Escape($candidate) }) {
      return $candidate
    }
  }

  Fail "No supported CMake generator found. Install Visual Studio 2026/2022 or Ninja."
}

$ResolvedGenerator = Resolve-CMakeGenerator $Generator

function Resolve-VcpkgToolchain {
  param([string]$RequestedToolchainFile = "")

  if ($RequestedToolchainFile) {
    return $RequestedToolchainFile
  }

  $roots = @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT, "C:\vcpkg") | Where-Object { $_ }
  foreach ($root in $roots) {
    $candidate = Join-Path $root "scripts/buildsystems/vcpkg.cmake"
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  return ""
}

$ResolvedToolchainFile = Resolve-VcpkgToolchain $ToolchainFile

function Get-FastTargets {
  param([Parameter(Mandatory = $true)][string]$Name)

  if ($Name -eq "ModernServer") {
    return @(
      "mir2_host",
      "mir2_core_smoke",
      "mir2_logic_smoke",
      "mir2_legacy_frame_smoke",
      "mir2_client_v1_protocol_smoke",
      "mir2_movement_blocking_legacy_smoke",
      "mir2_combat_smoke",
      "mir2_items_smoke"
    )
  }

  if ($Name -eq "ModernClient") {
    return @(
      "modern_mir2_client",
      "modern_client_asset_smoke",
      "modern_client_protocol_map_smoke",
      "modern_client_flow_smoke",
      "modern_client_text_encoding_smoke",
      "modern_client_movement_smoke"
    )
  }

  return @()
}

function Invoke-CMakeBuild {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $sourceDir = Join-Path $RepoRoot $RelativePath
  $buildDir = Join-Path $sourceDir "build-ci"

  if (-not (Test-Path -LiteralPath (Join-Path $sourceDir "CMakeLists.txt"))) {
    Fail "$Name does not contain CMakeLists.txt at $sourceDir."
  }

  if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $sourceFull = [System.IO.Path]::GetFullPath($sourceDir).TrimEnd('\')
    $buildFull = [System.IO.Path]::GetFullPath($buildDir).TrimEnd('\')
    if (($buildFull -notlike "$sourceFull\*") -or ((Split-Path -Leaf $buildFull) -ne "build-ci")) {
      Fail "Refusing to clean unexpected build directory: $buildFull"
    }

    Write-Host "Cleaning $Name build directory..."
    Remove-Item -LiteralPath $buildFull -Recurse -Force
  }

  Write-Host "Configuring $Name..."
  $configureArgs = @(
    "-S", $sourceDir,
    "-B", $buildDir,
    "-G", $ResolvedGenerator,
    "-DBUILD_TESTING=ON",
    "-DCMAKE_BUILD_TYPE=$Config"
  )
  if ($Arch -and $ResolvedGenerator -match "Visual Studio") {
    $configureArgs += @("-A", $Arch)
    if ($ResolvedToolchainFile) {
      $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$ResolvedToolchainFile")
      if ($Arch -eq "x64") {
        $configureArgs += @("-DVCPKG_TARGET_TRIPLET=x64-windows")
      }
    }
  }

  Write-Host "Using CMake generator: $ResolvedGenerator"
  if ($ResolvedToolchainFile -and $ResolvedGenerator -match "Visual Studio") {
    Write-Host "Using vcpkg toolchain: $ResolvedToolchainFile"
  }
  cmake @configureArgs
  if ($LASTEXITCODE -ne 0) {
    Fail "$Name CMake configure failed. Check dependencies, CMakeLists.txt, and whether new source files were added to the target."
  }

  Write-Host "Building $Name ($Config)..."
  if ($Fast) {
    foreach ($target in Get-FastTargets $Name) {
      Write-Host "Building $Name target: $target"
      cmake --build $buildDir --config $Config --target $target --parallel
      if ($LASTEXITCODE -ne 0) {
        Fail "$Name target $target build failed. Fix the first compiler/linker error before changing tests or CI policy."
      }
    }
  } else {
    cmake --build $buildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) {
      Fail "$Name build failed. Fix the first compiler/linker error before changing tests or CI policy."
    }
  }
}

if ($Project -in @("all", "server")) {
  Invoke-CMakeBuild -Name "ModernServer" -RelativePath "ModernServer"
}

if ($Project -in @("all", "client")) {
  Invoke-CMakeBuild -Name "ModernClient" -RelativePath "ModernClient"
}
