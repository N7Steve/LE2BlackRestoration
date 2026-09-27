[CmdletBinding()]
param([string]$BuildDirectory)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '_tools\asi-hook-tests-build' }
cmake -S (Join-Path $PSScriptRoot 'asi-hook-tests') -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'ASI hook test configuration failed.' }
cmake --build $BuildDirectory --config Release --target le2br-asi-hook-tests
if ($LASTEXITCODE -ne 0) { throw 'ASI hook test build failed.' }
$testExe = Join-Path $BuildDirectory 'Release\le2br-asi-hook-tests.exe'
$options = Join-Path $repoRoot 'LE2 Black Restoration\InstallOptions'
$modern = Join-Path $options 'SDR_Reference\CookedPCConsole'
$legacy = Join-Path $options 'Original_ME2_Black_Crush_Fix_Legacy_Emulation\CookedPCConsole'
$files = @(Get-ChildItem -LiteralPath $modern -Filter '*.m3gs' -File)
if ($files.Count -ne 32) { throw 'Expected 32 modern shader permutations.' }
foreach ($file in $files) {
    $results = & $testExe $file.FullName (Join-Path $legacy $file.Name) 2>&1
    if ($LASTEXITCODE -ne 0) { throw "ASI hook regression failed for $($file.Name): $results" }
}
Write-Output "PASS: six ASI hook scenarios across all $($files.Count) shader permutations (D3D11 WARP)."
