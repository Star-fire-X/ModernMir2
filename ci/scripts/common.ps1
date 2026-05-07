$ErrorActionPreference = "Stop"

$RepoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $RepoRoot) {
  Write-Host "::error::Unable to resolve repository root with git rev-parse."
  exit 1
}

function Test-GitCommit {
  param([AllowEmptyString()][string]$Revision = "")

  if (-not $Revision) {
    return $false
  }

  if ($Revision -match '^0+$') {
    return $false
  }

  git rev-parse --verify "$Revision^{commit}" *> $null
  return $LASTEXITCODE -eq 0
}

function Get-CiBase {
  if (Test-GitCommit $env:CI_BASE_SHA) {
    return $env:CI_BASE_SHA
  }

  if ($env:GITHUB_BASE_REF) {
    git fetch origin $env:GITHUB_BASE_REF --depth=1 *> $null
    $baseRef = "origin/$env:GITHUB_BASE_REF"
    if (Test-GitCommit $baseRef) {
      return $baseRef
    }
  }

  if (Test-GitCommit "origin/main") {
    return "origin/main"
  }

  if (Test-GitCommit "HEAD~1") {
    return "HEAD~1"
  }

  return "HEAD"
}

function Get-ChangedFiles {
  param([string]$Base = "")

  if (-not $Base) {
    $Base = Get-CiBase
  }

  if ($Base -eq "HEAD") {
    return git ls-files
  }

  return git diff --name-only "$Base...HEAD" | Where-Object { $_ }
}

function Fail {
  param([Parameter(Mandatory = $true)][string]$Message)

  Write-Host "::error::$Message"
  exit 1
}

function Get-CiCMakeBuildDirName {
  $name = $env:CI_CMAKE_BUILD_DIR_NAME
  if (-not $name) {
    return "build-ci"
  }

  if ($name -notmatch '^build-ci(?:[-_.][A-Za-z0-9]+)*$') {
    Fail "CI_CMAKE_BUILD_DIR_NAME must be a build-ci leaf directory name without path separators."
  }

  return $name
}

function Get-CiCMakeBuildDir {
  param([Parameter(Mandatory = $true)][string]$RelativePath)

  return Join-Path (Join-Path $RepoRoot $RelativePath) (Get-CiCMakeBuildDirName)
}

function Write-CiError {
  param(
    [Parameter(Mandatory = $true)][string]$Message,
    [string]$File = "",
    [string]$Title = "CI check failed"
  )

  if ($File) {
    Write-Host "::error file=$File,title=$Title::$Message"
  } else {
    Write-Host "::error title=$Title::$Message"
  }
}
