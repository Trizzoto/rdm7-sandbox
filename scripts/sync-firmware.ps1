# sync-firmware.ps1
#
# Copies the firmware C sources the sandbox depends on from the user's
# local RDM-7 Dash workspace (default: ../RDM-7_Dash) into firmware_src/
# and records each file's git SHA in firmware-sources.lock.json so CI can
# flag drift.
#
# Local-copy (not GitHub fetch) because:
#   1. Works regardless of which branch is pushed upstream
#   2. Stays in lockstep with whatever the user is actively editing
#   3. No rate limits, no auth, works offline
#
# Usage:
#   ./scripts/sync-firmware.ps1                    # default: ../RDM-7_Dash
#   ./scripts/sync-firmware.ps1 -Src C:\path\here

param(
  [string]$Src = ""
)

$ErrorActionPreference = "Stop"

$Root     = Split-Path -Parent $PSScriptRoot
$DestRoot = Join-Path $Root "firmware_src"
$LockFile = Join-Path $Root "firmware-sources.lock.json"

if (-not $Src) {
  $Src = Resolve-Path (Join-Path $Root "..\RDM-7_Dash") -ErrorAction SilentlyContinue
  if (-not $Src) {
    Write-Error "Firmware repo not found at ../RDM-7_Dash. Pass -Src <path>."
  }
}

Write-Host "Syncing from: $Src"

# Files we need for Phase 1 (wizard). Paths relative to the firmware
# repo root. Kept aligned with CMakeLists.txt's WIZARD_SOURCES.
$Files = @(
  # Theme + UI framework
  "main/ui/theme.h",
  "main/ui/ui.h",
  "main/ui/ui_helpers.h",

  # Wizard + its immediate deps
  "main/ui/screens/first_run_wizard.h",
  "main/ui/screens/first_run_wizard.c",
  "main/ui/screens/ui_wifi.h",
  "main/ui/screens/ui_wifi.c",
  "main/ui/screens/ui_ecu_picker.h",
  "main/ui/screens/ui_ecu_picker.c",
  "main/ui/screens/ui_Screen3.h",

  # ECU preset list (wizard Step 3)
  "main/layout/ecu_presets.h",
  "main/layout/layout_manager.h",

  # Config store — wizard writes first_run_done
  "main/storage/config_store.h",

  # CAN bus test + manager — wizard Step 4
  "main/can/can_bus_test.h",
  "main/can/can_manager.h",

  # WiFi manager — wizard Step 2
  "main/net/wifi_manager.h"
)

# Read the repo's current HEAD for provenance.
$HeadSha = (git -C $Src rev-parse HEAD 2>$null).Trim()
$BranchName = (git -C $Src rev-parse --abbrev-ref HEAD 2>$null).Trim()

$Lock = @{
  source   = "local:$Src"
  branch   = $BranchName
  commit   = $HeadSha
  syncedAt = (Get-Date).ToUniversalTime().ToString("o")
  files    = @{}
}

foreach ($Path in $Files) {
  $SrcPath = Join-Path $Src $Path
  if (-not (Test-Path $SrcPath)) {
    Write-Warning "  SKIP (missing): $Path"
    continue
  }
  # Strip leading "main/" so firmware_src/ui/... stays shallow
  $RelDest = $Path -replace "^main/", ""
  $Dest    = Join-Path $DestRoot $RelDest

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null
  Copy-Item -Path $SrcPath -Destination $Dest -Force

  # Compute git blob SHA for drift detection (matches what GitHub stores).
  $Bytes  = [System.IO.File]::ReadAllBytes($Dest)
  $Header = [System.Text.Encoding]::UTF8.GetBytes("blob $($Bytes.Length)`0")
  $Combined = New-Object byte[] ($Header.Length + $Bytes.Length)
  [Array]::Copy($Header, 0, $Combined, 0, $Header.Length)
  [Array]::Copy($Bytes, 0, $Combined, $Header.Length, $Bytes.Length)
  $Hash = ([System.Security.Cryptography.SHA1]::Create().ComputeHash($Combined) `
           | ForEach-Object { $_.ToString("x2") }) -join ""

  $Lock.files[$Path] = $Hash
  Write-Host "  copy $Path"
}

$Lock | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $LockFile
Write-Host ""
Write-Host "Synced $($Lock.files.Count) files from branch '$BranchName' @ $($HeadSha.Substring(0,7))"
Write-Host "Wrote lockfile: firmware-sources.lock.json"
