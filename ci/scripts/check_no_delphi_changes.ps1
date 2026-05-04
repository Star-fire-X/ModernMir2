param([string]$Base = "")

. "$PSScriptRoot/common.ps1"

$blocked = @(Get-ChangedFiles $Base | Where-Object {
  ($_ -match '^(Source|Component)/') -and ($_ -match '\.(pas|dfm|dpr|inc)$')
})

if ($blocked.Count -eq 0) {
  Write-Host "Delphi reference source guard passed."
  exit 0
}

foreach ($file in $blocked) {
  Write-CiError `
    -File $file `
    -Title "Delphi reference source changed" `
    -Message "Source/ and Component/ contain Delphi reference code for migration comparison. Do not edit it in normal feature work; migrate behavior into ModernServer/ModernClient/shared instead."
}

Fail "Delphi reference source files were changed. Revert these files or split the change into an explicitly approved migration-reference PR."
