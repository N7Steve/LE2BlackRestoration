# LE2 Black Restoration

**LE2 Black Restoration** is a native black-crush correction and near-black detail restoration mod for Mass Effect 2 Legendary Edition, by **N7SteveMods**. This is the main repository for the complete mod: the ME3Tweaks Mod Manager package, reproducible M3GS tooling, release documentation, and optional ASI source.

The mod uses targeted M3GS overrides for the 32 `FSFXUberPostProcessBlendPixelShader` permutations. The companion ASI hooks D3D11 shader creation/binding, recognizes only the expected Black Restoration shader topology, and reparameterizes that topology in-place at runtime.

The release architecture preserves the game's existing post-processing pipeline and applies the restoration before the Filmic LUT. It does not replace the full tone mapper and does not use ReShade.

## Repository layout

```text
ASI/BlackRestoration/       optional runtime-tuning ASI source
LE2 Black Restoration/     primary ME3Tweaks Mod Manager package
scripts/                   M3GS generator and release validator
AGENTS.md                  architecture, invariants, and release procedure
_external/                 ignored external reference repositories
_tools/                    ignored local builds and validation reports
```

The primary package works without the ASI. The ASI is an optional advanced component for live tuning, hotkeys, profiles, and comparison/bypass controls.

## IPS-SDR defaults

The built-in release defaults preserve absolute black with `BlackFloorLift=0`:

```ini
ShadowBoost=0.0
ShadowRange=0.026
ShadowFade=3
NearBlackRange=0.040
NearBlackDetail=0.1
NearBlackRecovery=0.0045
PureBlackProtection=0.0005
BlackFloorLift=0.0
```

Missing restoration keys in the runtime INI inherit these IPS-SDR defaults. Present but invalid values are rejected so the last valid configuration remains active.

## Runtime configuration and hotkeys

The ASI reads:

```text
BIOGame/DLC/DLC_MOD_LE2BlackRestoration/LE2BlackRestoration.ini
```

The INI supports hot reload, runtime tuning, IPS-SDR comparison, profile save/load, and bypass. Hotkeys are enabled by default but can be disabled or remapped through `[Hotkeys]`.

The default base keys are:

```ini
[Hotkeys]
Enabled=1
DecreaseKey=F6
IncreaseKey=F7
ActionKey=F8
BypassKey=F9
```

Modifiers keep fixed meanings while the four base keys can be changed. Hotkey matching is exact: unlisted extra modifiers do not trigger an action. See `ASI/BlackRestoration/LE2BlackRestoration.ini.example` for all supported key names, combinations, parameter ranges, and examples.

## Validate or regenerate M3GS assets

The tooling compiles a small wrapper around the same `DxbcPatcher` used by the ASI, validates all 160 release shaders, and reproduces all four modern preset sets byte-for-byte:

```powershell
./scripts/Validate-Release.ps1
```

See `AGENTS.md` for the frozen shader invariants, preset definitions, Legacy emulation caveat, and official Mod Manager deployment checklist.

## Building the optional ASI

This source is designed to build inside [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs).

1. Copy `ASI/BlackRestoration` into the root of an LExASIs checkout as `BlackRestoration`.
2. Add the following to the root `CMakeLists.txt`:

```cmake
add_subdirectory ("BlackRestoration")
```

3. Configure LExASIs normally, then build the LE2 Release target:

```powershell
cmake --build Build --config RELEASE --target BlackRestoration-LE2
```

Expected output:

```text
Build/Release/LE2BlackRestoration.asi
```

## ME3Tweaks ASI manifest status

`SharedVersion.h` currently uses `ASI_GROUP_ID_RC 0` as a pre-submission placeholder. It must be replaced with the GroupID assigned by ME3Tweaks before the manifest/trusted release build.

## Target

- Mass Effect 2 Legendary Edition (LE2)
- Windows x64
- D3D11

## License

MIT. Copyright (c) 2026 N7SteveMods.
