param([string]$Base = "")

. "$PSScriptRoot/common.ps1"

$blockedExtensions = @(
  ".wil",
  ".wix",
  ".map",
  ".wav",
  ".dat",
  ".mp3",
  ".ogg"
)

$changed = @(Get-ChangedFiles $Base)
$blocked = @()
foreach ($file in $changed) {
  $extension = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
  if ($blockedExtensions -contains $extension) {
    $blocked += $file
  }
}

if ($blocked.Count -eq 0) {
  Write-Host "Resource format guard passed."
  exit 0
}

foreach ($file in $blocked) {
  Write-CiError `
    -File $file `
    -Title "Legacy resource file added or changed" `
    -Message "Do not commit full legacy client resources or maps to this repository. Keep real WIL/WIX/MAP/WAV/DAT assets local until a reviewed fixture or private artifact policy exists."
}

Fail "Legacy resource files were changed. Remove them from the PR or split into an explicitly reviewed resource-fixture change."
