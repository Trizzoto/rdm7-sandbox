# build.ps1
# Build the sandbox WASM artifact via Emscripten.
# Requires emsdk installed (default C:\emsdk, override with $env:EMSDK).
# Output: public/rdm7-sandbox.js + public/rdm7-sandbox.wasm

$ErrorActionPreference = "Stop"

$Root   = Split-Path -Parent $PSScriptRoot
$Emsdk  = if ($env:EMSDK) { $env:EMSDK } else { "C:\emsdk" }
$Build  = Join-Path $Root "build"

if (-not (Test-Path $Emsdk)) {
  Write-Error "Emscripten SDK not found at $Emsdk. Set `$env:EMSDK or install from https://emscripten.org"
}

$EmccRoot = Join-Path $Emsdk "upstream\emscripten"
$Emcmake  = Join-Path $EmccRoot "emcmake.bat"
$Emmake   = Join-Path $EmccRoot "emmake.bat"

if (-not (Test-Path $Emcmake)) {
  Write-Error "emcmake not found at $Emcmake - is your emsdk installation complete?"
}

New-Item -ItemType Directory -Force -Path $Build | Out-Null
Push-Location $Build
try {
  & $Emcmake cmake .. -G "MinGW Makefiles"
  if ($LASTEXITCODE -ne 0) { throw "emcmake failed" }
  & $Emmake mingw32-make -j4
  if ($LASTEXITCODE -ne 0) { throw "emmake failed" }
} finally {
  Pop-Location
}

$PublicDir = Join-Path $Root "public"
New-Item -ItemType Directory -Force -Path $PublicDir | Out-Null
Copy-Item (Join-Path $Build "rdm7-sandbox.js")   (Join-Path $PublicDir "rdm7-sandbox.js")   -Force
Copy-Item (Join-Path $Build "rdm7-sandbox.wasm") (Join-Path $PublicDir "rdm7-sandbox.wasm") -Force

Write-Host ""
Write-Host "Built: public/rdm7-sandbox.js + public/rdm7-sandbox.wasm"
Write-Host "Run 'npm run dev' to preview."
