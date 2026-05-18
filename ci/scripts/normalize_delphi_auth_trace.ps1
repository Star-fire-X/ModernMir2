param(
  [Parameter(Mandatory = $true)]
  [string]$InputPath,

  [string]$OutputPath = "",

  [switch]$Directory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-Line {
  param([string]$Line)

  if ([string]::IsNullOrWhiteSpace($Line)) {
    return $null
  }

  $parts = $Line -split "\|"
  $out = New-Object System.Collections.Generic.List[string]
  foreach ($part in $parts) {
    if ($part -like "tick=*") {
      continue
    }
    $normalized = $part -replace "[A-Za-z]:\\[^|; ]+", "<path>"
    $out.Add($normalized)
  }
  return ($out -join "|")
}

function Normalize-File {
  param(
    [string]$Source,
    [string]$Destination
  )

  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($line in Get-Content -LiteralPath $Source) {
    $normalized = Normalize-Line $line
    if ($null -ne $normalized) {
      $lines.Add($normalized)
    }
  }

  $parent = Split-Path -Parent $Destination
  if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  Set-Content -LiteralPath $Destination -Value $lines -Encoding ASCII
}

if ($Directory) {
  if (-not $OutputPath) {
    throw "-OutputPath is required with -Directory."
  }
  $inputRoot = Resolve-Path -LiteralPath $InputPath
  foreach ($file in Get-ChildItem -LiteralPath $inputRoot -Filter *.trace -File) {
    $destination = Join-Path $OutputPath $file.Name
    Normalize-File -Source $file.FullName -Destination $destination
  }
} else {
  if (-not $OutputPath) {
    $OutputPath = [IO.Path]::ChangeExtension($InputPath, ".normalized.trace")
  }
  Normalize-File -Source $InputPath -Destination $OutputPath
}
