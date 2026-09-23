[CmdletBinding()]
param(
    [string]$SourceCheckout,
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceCheckout) {
    $SourceCheckout = Join-Path $repoRoot '_external\LE2BlackRestoration'
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repoRoot '_tools\shader-tool-build'
}
$patcherDirectory = Join-Path $SourceCheckout 'BlackRestoration'
if (-not (Test-Path -LiteralPath (Join-Path $patcherDirectory 'DxbcPatcher.cpp'))) {
    throw "DxbcPatcher.cpp was not found. Clone https://github.com/N7Steve/LE2BlackRestoration into $SourceCheckout"
}

cmake -S (Join-Path $PSScriptRoot 'shader-tool') -B $BuildDirectory "-DLE2BR_PATCHER_DIR=$patcherDirectory"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $BuildDirectory --config Release --target le2br-shader-tool
if ($LASTEXITCODE -ne 0) { throw 'Shader tool build failed.' }

$candidate = Join-Path $BuildDirectory 'Release\le2br-shader-tool.exe'
if (-not (Test-Path -LiteralPath $candidate)) {
    $candidate = Join-Path $BuildDirectory 'le2br-shader-tool.exe'
}
if (-not (Test-Path -LiteralPath $candidate)) { throw 'Built shader tool was not found.' }
Write-Output $candidate
