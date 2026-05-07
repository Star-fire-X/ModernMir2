param(
  [string]$Arch = "x64",
  [string]$HostArch = "x64",

  [Parameter(Mandatory = $true)]
  [string]$Command
)

$ErrorActionPreference = "Stop"

function Fail {
  param([Parameter(Mandatory = $true)][string]$Message)

  Write-Host "::error::$Message"
  exit 1
}

function Get-VsDevCmdPath {
  if ($env:VSDEVCMD_BAT -and (Test-Path -LiteralPath $env:VSDEVCMD_BAT)) {
    return $env:VSDEVCMD_BAT
  }

  if ($env:VSINSTALLDIR) {
    $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  $vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
    (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
  ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

  foreach ($vswhere in $vswhereCandidates) {
    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -eq 0 -and $installationPath) {
      $candidate = Join-Path $installationPath.Trim() "Common7\Tools\VsDevCmd.bat"
      if (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    }
  }

  Fail "Unable to locate VsDevCmd.bat. Install Visual Studio C++ tools or set VSDEVCMD_BAT."
}

$vsDevCmd = Get-VsDevCmdPath
if (-not $env:CI_VCPKG_ROOT) {
  if ($env:VCPKG_INSTALLATION_ROOT) {
    $env:CI_VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT
  } elseif ($env:VCPKG_ROOT) {
    $env:CI_VCPKG_ROOT = $env:VCPKG_ROOT
  }
}

$encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
$cmd = $env:ComSpec
if (-not $cmd) {
  $cmd = "cmd.exe"
}

Write-Host "Using VsDevCmd: $vsDevCmd"
& $cmd /d /s /c "call ""$vsDevCmd"" -arch=$Arch -host_arch=$HostArch && pwsh -NoLogo -NoProfile -OutputFormat Text -ExecutionPolicy Bypass -EncodedCommand $encodedCommand"
exit $LASTEXITCODE
