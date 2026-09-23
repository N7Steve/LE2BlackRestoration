[CmdletBinding()]
param(
    [string]$PackageDirectory,
    [string]$SourceCheckout,
    [string]$BuildDirectory,
    [string]$ReportDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $repoRoot 'LE2 Black Restoration' }
if (-not $SourceCheckout) { $SourceCheckout = Join-Path $repoRoot '_external\LE2BlackRestoration' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '_tools\shader-tool-build' }
if (-not $ReportDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ReportDirectory = Join-Path $repoRoot "_tools\validation-$stamp"
}

$tool = & (Join-Path $PSScriptRoot 'Build-ShaderTool.ps1') -SourceCheckout $SourceCheckout -BuildDirectory $BuildDirectory | Select-Object -Last 1
if (-not (Test-Path -LiteralPath $tool)) { throw "Shader tool not found at $tool" }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null

$installOptions = Join-Path $PackageDirectory 'InstallOptions'
$presetIniRoot = Join-Path $PackageDirectory 'DLC_MOD_LE2BlackRestoration'
$modernPresets = @(
    'IPS_SDR_Reference',
    'IPS_SDR_Stronger',
    'OLED_HDR_Reference',
    'OLED_HDR_Stronger'
)
$legacyPreset = 'Original_ME2_Black_Crush_Fix_Legacy_Emulation'
$allPresets = $modernPresets + $legacyPreset
$baseline = Join-Path $installOptions 'IPS_SDR_Reference\CookedPCConsole'

$moddesc = Get-Content -LiteralPath (Join-Path $PackageDirectory 'moddesc.ini') -Raw
if ($moddesc -notmatch '(?m)^cmmver=9\.2\s*$') { throw 'moddesc.ini does not declare cmmver=9.2.' }
if ([regex]::Matches($moddesc, 'OptionGroup=DisplayPreset').Count -ne 5) { throw 'Expected five DisplayPreset options.' }
if ([regex]::Matches($moddesc, 'CheckedByDefault=true').Count -ne 1) { throw 'Expected exactly one CheckedByDefault=true option.' }
if ($moddesc -notmatch 'FriendlyName="IPS-SDR - Reference"[^\r\n]*CheckedByDefault=true') {
    throw 'IPS-SDR Reference is not the sole default option.'
}
if (Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File | Where-Object Extension -in '.asi', '.dll', '.pdb', '.obj') {
    throw 'The primary package contains optional ASI/build artifacts.'
}

$expectedNames = $null
$structure = @{}
$hashRows = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $allPresets) {
    $shaderDir = Join-Path $installOptions "$preset\CookedPCConsole"
    $files = @(Get-ChildItem -LiteralPath $shaderDir -File -Filter '*.m3gs' | Sort-Object Name)
    if ($files.Count -ne 32) { throw "$preset has $($files.Count) shaders; expected 32." }
    $names = @($files.Name)
    if ($null -eq $expectedNames) { $expectedNames = $names }
    elseif (Compare-Object $expectedNames $names) { throw "$preset does not have the expected shader filename/index set." }

    $inspectArgs = @('inspect-dir', $shaderDir)
    if ($preset -in $modernPresets) { $inspectArgs += '--modern' }
    $inspection = @(& $tool @inspectArgs)
    if ($LASTEXITCODE -ne 0 -or $inspection.Count -ne 32) { throw "DXBC validation failed for $preset." }
    foreach ($line in $inspection) {
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2) { throw "Unexpected inspection output: $line" }
        if (-not $structure.ContainsKey($parts[0])) { $structure[$parts[0]] = $parts[1] }
        elseif ($structure[$parts[0]] -ne $parts[1]) { throw "DXBC structure differs across presets: $($parts[0])." }
    }

    foreach ($file in $files) {
        $hashRows.Add([pscustomobject]@{
            Preset = $preset
            Name = $file.Name
            Bytes = $file.Length
            SHA256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        })
    }
}

$reproduction = [System.Collections.Generic.List[object]]::new()
foreach ($preset in $modernPresets) {
    $output = Join-Path $ReportDirectory "reproduced\$preset"
    $ini = Join-Path $presetIniRoot "Preset_$preset.ini"
    & $tool generate $baseline $ini $output | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Generation failed for $preset." }
    $mismatches = [System.Collections.Generic.List[string]]::new()
    foreach ($generated in Get-ChildItem -LiteralPath $output -File -Filter '*.m3gs') {
        $expected = Join-Path $installOptions "$preset\CookedPCConsole\$($generated.Name)"
        $generatedHash = (Get-FileHash -LiteralPath $generated.FullName -Algorithm SHA256).Hash
        $expectedHash = (Get-FileHash -LiteralPath $expected -Algorithm SHA256).Hash
        if ($generatedHash -ne $expectedHash) { $mismatches.Add($generated.Name) }
    }
    $reproduction.Add([pscustomobject]@{ Preset = $preset; Files = 32; Mismatches = $mismatches.Count })
    if ($mismatches.Count) { throw "$preset has $($mismatches.Count) non-reproducible shaders." }
}

$hashRows | Export-Csv -LiteralPath (Join-Path $ReportDirectory 'shader-hashes.csv') -NoTypeInformation
$reproduction | Export-Csv -LiteralPath (Join-Path $ReportDirectory 'reproduction.csv') -NoTypeInformation

[pscustomobject]@{
    Package = $PackageDirectory
    Presets = 5
    Shaders = $hashRows.Count
    ShaderBytes = ($hashRows | Measure-Object Bytes -Sum).Sum
    ModernPresetMismatches = ($reproduction | Measure-Object Mismatches -Sum).Sum
    ReportDirectory = $ReportDirectory
}
