param([string]$Base = "")

. "$PSScriptRoot/common.ps1"

if (-not $Base) {
  $Base = Get-CiBase
}

$diffRange = if ($Base -eq "HEAD") { "HEAD" } else { "$Base...HEAD" }
Write-Host "Checking whitespace hygiene for diff range: $diffRange"
$diffOutput = git diff --check $diffRange 2>&1
if ($LASTEXITCODE -ne 0) {
  foreach ($line in $diffOutput) {
    Write-Host $line
  }
  Fail "Whitespace errors found. Fix trailing whitespace, conflict markers, or other git diff --check failures."
}

Write-Host "Checking PowerShell script syntax..."
$failed = $false
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $RepoRoot "ci/scripts") -Filter "*.ps1" -File) {
  $tokens = $null
  $parseErrors = $null
  [System.Management.Automation.Language.Parser]::ParseFile(
    $file.FullName,
    [ref]$tokens,
    [ref]$parseErrors) | Out-Null

  if ($parseErrors.Count -gt 0) {
    $failed = $true
    foreach ($error in $parseErrors) {
      Write-CiError `
        -File $file.FullName `
        -Title "PowerShell syntax error" `
        -Message "$($error.Message) at line $($error.Extent.StartLineNumber), column $($error.Extent.StartColumnNumber)."
    }
  }
}

if ($failed) {
  Fail "PowerShell syntax errors found in ci/scripts."
}

Write-Host "Text hygiene guard passed."
