[CmdletBinding()]
param(
    [string]$PackageDirectory,
    [string]$PatcherDirectory,
    [string]$BuildDirectory,
    [string]$ReportDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $repoRoot 'LE2 Black Restoration' }
if (-not $PatcherDirectory) { $PatcherDirectory = Join-Path $repoRoot 'ASI\BlackRestoration' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '_tools\shader-tool-build' }
if (-not $ReportDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ReportDirectory = Join-Path $repoRoot "_tools\validation-$stamp"
}

$tool = & (Join-Path $PSScriptRoot 'Build-ShaderTool.ps1') -PatcherDirectory $PatcherDirectory -BuildDirectory $BuildDirectory | Select-Object -Last 1
if (-not (Test-Path -LiteralPath $tool)) { throw "Shader tool not found at $tool" }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null

$installOptions = Join-Path $PackageDirectory 'InstallOptions'
$presetIniRoot = Join-Path $PackageDirectory 'DLC_MOD_LE2BlackRestoration'
$modernPresets = @(
    'SDR_Reference',
    'SDR_Stronger',
    'HDR_Reference',
    'HDR_Stronger'
)
$legacyPreset = 'Original_ME2_Black_Crush_Fix_Legacy_Emulation'
$allPresets = $modernPresets + $legacyPreset
$baseline = Join-Path $installOptions 'SDR_Reference\CookedPCConsole'

$moddesc = Get-Content -LiteralPath (Join-Path $PackageDirectory 'moddesc.ini') -Raw
if ($moddesc -notmatch '(?m)^cmmver=9\.2\s*$') { throw 'moddesc.ini does not declare cmmver=9.2.' }
if ([regex]::Matches($moddesc, 'OptionGroup=Preset').Count -ne 5) { throw 'Expected five members in the Preset option group.' }
if ([regex]::Matches($moddesc, 'CheckedByDefault=true').Count -ne 1) { throw 'Expected exactly one CheckedByDefault=true option.' }
if ($moddesc -notmatch 'FriendlyName="SDR: Reference \(Recommended\)"[^\r\n]*CheckedByDefault=true') {
    throw 'SDR Reference is not the sole default option.'
}
$expectedPreviewImages = @(
    'SDR_Reference.png',
    'SDR_Stronger.png',
    'HDR_Reference.png',
    'HDR_Stronger.png',
    'Legacy_Emulation.png'
)
if ([regex]::Matches($moddesc, 'ImageHeight=300').Count -ne $expectedPreviewImages.Count) {
    throw 'Expected all five preset options to use 300px preview images.'
}
Add-Type -AssemblyName System.Drawing
foreach ($previewName in $expectedPreviewImages) {
    if ([regex]::Matches($moddesc, "ImageAssetName=$([regex]::Escape($previewName))").Count -ne 1) {
        throw "Expected exactly one installer option to reference $previewName."
    }
    $previewImage = Join-Path $PackageDirectory "M3Images\$previewName"
    if (-not (Test-Path -LiteralPath $previewImage -PathType Leaf)) {
        throw "Preset preview image M3Images/$previewName is missing."
    }
    $previewBitmap = [System.Drawing.Image]::FromFile($previewImage)
    try {
        if ($previewBitmap.Width -ne 1720 -or $previewBitmap.Height -ne 720) {
            throw "Preset preview $previewName must be 1720x720; found $($previewBitmap.Width)x$($previewBitmap.Height)."
        }
    }
    finally {
        $previewBitmap.Dispose()
    }
}
if ($moddesc -notmatch '(?m)^bannerimagename=banner\.png\s*$') {
    throw 'moddesc.ini does not use the case-sensitive bannerimagename key for M3Images/banner.png.'
}
$bannerImage = Join-Path $PackageDirectory 'M3Images\banner.png'
if (-not (Test-Path -LiteralPath $bannerImage -PathType Leaf)) {
    throw 'Banner image M3Images/banner.png is missing.'
}
$bannerBitmap = [System.Drawing.Image]::FromFile($bannerImage)
try {
    if ($bannerBitmap.Width * 47 -ne $bannerBitmap.Height * 580) {
        throw "Banner image must use the 580:47 aspect ratio; found $($bannerBitmap.Width)x$($bannerBitmap.Height)."
    }
}
finally {
    $bannerBitmap.Dispose()
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
