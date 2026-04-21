# setup-submodules.ps1
#
# First-time setup: add LVGL v8.3 and lv_drivers as git submodules.
# Run once after cloning the sandbox repo.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
  if (-not (Test-Path "lvgl/.git")) {
    Write-Host "Adding LVGL v8.3 submodule…"
    git submodule add --branch release/v8.3 https://github.com/lvgl/lvgl.git lvgl
  }
  if (-not (Test-Path "lv_drivers/.git")) {
    Write-Host "Adding lv_drivers submodule…"
    git submodule add --branch release/v8.3 https://github.com/lvgl/lv_drivers.git lv_drivers
  }
  git submodule update --init --recursive
  Write-Host "Done. Now run ./scripts/sync-firmware.ps1 and ./scripts/build.ps1"
} finally {
  Pop-Location
}
