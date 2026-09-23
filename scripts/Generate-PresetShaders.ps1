[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [Parameter(Mandatory)][string]$PresetIni,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SourceCheckout,
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$buildArgs = @{}
if ($SourceCheckout) { $buildArgs.SourceCheckout = $SourceCheckout }
if ($BuildDirectory) { $buildArgs.BuildDirectory = $BuildDirectory }
$tool = & (Join-Path $PSScriptRoot 'Build-ShaderTool.ps1') @buildArgs | Select-Object -Last 1
if (-not (Test-Path -LiteralPath $tool)) { throw "Shader tool not found at $tool" }

& $tool generate $BaselineDirectory $PresetIni $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Preset shader generation failed.' }

$files = Get-ChildItem -LiteralPath $OutputDirectory -File -Filter 'GlobalShader-*.m3gs' | Sort-Object Name
if ($files.Count -ne 32) { throw "Expected 32 generated shaders; found $($files.Count)." }
$files | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    [pscustomobject]@{ Name = $_.Name; Bytes = $_.Length; SHA256 = $hash.Hash }
}
