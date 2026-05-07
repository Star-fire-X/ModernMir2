param(
  [ValidateSet("all", "server", "client")]
  [string]$Project = "all",

  [ValidateSet("Debug", "Release", "RelWithDebInfo")]
  [string]$Config = "Debug",

  [string]$Generator = "",
  [string]$Arch = "x64",
  [string]$ToolchainFile = "",

  [ValidateSet("", "phase1-fast", "phase2-fast", "phase3-nightly")]
  [string]$Suite = "",

  [switch]$Fast,
  [switch]$Clean
)

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/compat_suites.ps1"

if ($Fast -and -not $Suite) {
  $Suite = "phase1-fast"
}

if ($Fast -and $Suite -ne "phase1-fast") {
  Fail "-Fast is a legacy alias for -Suite phase1-fast. Use only one suite selector."
}

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

function Resolve-RequestedCMakeGenerator {
  if ($Generator) {
    return $Generator
  }

  if ($env:CI_CMAKE_GENERATOR) {
    return $env:CI_CMAKE_GENERATOR
  }

  return ""
}

$ResolvedGenerator = Resolve-CMakeGenerator (Resolve-RequestedCMakeGenerator)

function Resolve-VcpkgToolchain {
  param([string]$RequestedToolchainFile = "")

  if ($RequestedToolchainFile) {
    return $RequestedToolchainFile
  }

  $roots = @($env:CI_VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT, "C:\vcpkg", $env:VCPKG_ROOT) | Where-Object { $_ }
  foreach ($root in $roots) {
    $candidate = Join-Path $root "scripts/buildsystems/vcpkg.cmake"
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  return ""
}

$ResolvedToolchainFile = Resolve-VcpkgToolchain $ToolchainFile

function Invoke-CMakeBuild {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $sourceDir = Join-Path $RepoRoot $RelativePath
  $buildDirName = Get-CiCMakeBuildDirName
  $buildDir = Get-CiCMakeBuildDir -RelativePath $RelativePath

  if (-not (Test-Path -LiteralPath (Join-Path $sourceDir "CMakeLists.txt"))) {
    Fail "$Name does not contain CMakeLists.txt at $sourceDir."
  }

  if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $sourceFull = [System.IO.Path]::GetFullPath($sourceDir).TrimEnd('\')
    $buildFull = [System.IO.Path]::GetFullPath($buildDir).TrimEnd('\')
    if (($buildFull -notlike "$sourceFull\*") -or ((Split-Path -Leaf $buildFull) -ne $buildDirName)) {
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
  if ($env:CI_CMAKE_FETCHCONTENT_BASE_DIR) {
    $fetchContentBaseDir = $env:CI_CMAKE_FETCHCONTENT_BASE_DIR
    New-Item -ItemType Directory -Force -Path $fetchContentBaseDir | Out-Null
    $configureArgs += @("-DFETCHCONTENT_BASE_DIR=$fetchContentBaseDir")
  }
  if ($Arch -and $ResolvedGenerator -match "Visual Studio") {
    $configureArgs += @("-A", $Arch)
  }

  $useVcpkgToolchain = $ResolvedToolchainFile -and (($Name -eq "ModernServer") -or $ToolchainFile)
  if ($useVcpkgToolchain) {
    $configureArgs += @("-DCMAKE_TOOLCHAIN_FILE=$ResolvedToolchainFile")
    if ($Arch -eq "x64") {
      $configureArgs += @("-DVCPKG_TARGET_TRIPLET=x64-windows")
    }
  }

  Write-Host "Using CMake generator: $ResolvedGenerator"
  Write-Host "Using CMake build directory: $buildDir"
  if ($useVcpkgToolchain) {
    Write-Host "Using vcpkg toolchain: $ResolvedToolchainFile"
  }
  if ($env:CI_CMAKE_FETCHCONTENT_BASE_DIR) {
    Write-Host "Using FetchContent cache: $env:CI_CMAKE_FETCHCONTENT_BASE_DIR"
  }
  cmake @configureArgs
  if ($LASTEXITCODE -ne 0) {
    Fail "$Name CMake configure failed. Check dependencies, CMakeLists.txt, and whether new source files were added to the target."
  }

  Write-Host "Building $Name ($Config)..."
  if ($Suite) {
    $targets = @(Get-CiBuildTargets -Suite $Suite -ProjectName $Name)
    if ($targets.Count -eq 0) {
      Fail "$Name has no build targets for CI suite '$Suite'."
    }

    $aggregateTarget = Get-CiAggregateBuildTarget -Suite $Suite -ProjectName $Name
    Write-Host "Building $Name CI suite targets:"
    foreach ($target in $targets) {
      Write-Host "  - $target"
    }

    if ($aggregateTarget) {
      Write-Host "Using aggregate CI target: $aggregateTarget"
      cmake --build $buildDir --config $Config --parallel --target $aggregateTarget
      if ($LASTEXITCODE -ne 0) {
        Fail "$Name aggregate target $aggregateTarget build failed. Fix the first compiler/linker error before changing tests or CI policy."
      }
    } else {
      $buildArgs = @("--build", $buildDir, "--config", $Config, "--parallel", "--target") + $targets
      cmake @buildArgs
      if ($LASTEXITCODE -ne 0) {
        Fail "$Name CI suite '$Suite' build failed. Fix the first compiler/linker error before changing tests or CI policy."
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
