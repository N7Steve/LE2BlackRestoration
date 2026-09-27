# LE2 Black Restoration

**LE2 Black Restoration** is a native black-crush correction and near-black detail restoration mod for Mass Effect 2 Legendary Edition. Its goal is to recover separation and texture in the darkest visible tones without lifting absolute black or turning the effect into a general exposure, gamma, or contrast adjustment. This is the main repository for the complete mod: the ME3Tweaks Mod Manager package, reproducible M3GS tooling, release documentation, and optional ASI source.

The project is inspired by the **Original ME2 Black Crush Fix** for the classic release of Mass Effect 2. The included Legacy option is a conservative visual emulation of that historical fix, not a claimed exact shader port to Legendary Edition; the modern Black Restoration presets use a purpose-built near-black curve instead.

Use an **HDR** preset only when HDR is enabled in Mass Effect 2 Legendary Edition and you are playing on an HDR-capable display with HDR active. Those presets were calibrated with the game's HDR output enabled. For every other setup—including an HDR-capable display being used in SDR mode—choose an **SDR** preset.

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

## Download and installation

Download the files from [GitHub Releases](https://github.com/N7Steve/LE2BlackRestoration/releases) or the [Nexus Mods page](https://www.nexusmods.com/masseffectlegendaryedition/mods/3426).

The main mod requires **ME3Tweaks Mod Manager 9.2 or newer**, which supports the M3GS Global Shader Merge system, and Mass Effect 2 Legendary Edition on Windows x64.

1. Download `LE2BlackRestoration_1.0.0.7z` and import it into ME3Tweaks Mod Manager.
2. Select **LE2 Black Restoration** in the mod library and click **Apply Mod**.
3. Choose one preset, then launch Mass Effect 2 Legendary Edition normally.

| Installer choice | Intended use |
| --- | --- |
| SDR: Reference (Recommended) | Balanced near-black recovery for standard SDR play; start here. |
| SDR: Stronger | Stronger near-black recovery for SDR play. |
| HDR: Reference | Calibrated for the game's HDR output with both in-game and display HDR active. |
| HDR: Stronger | Additional recovery with in-game and display HDR active. |
| Legacy: Original ME2 Black Crush Fix | Conservative, static emulation of the historical fix; not an exact port. |

All five choices work without the ASI. To change the static preset, reinstall/reconfigure the mod through Mod Manager and choose another option. An HDR-capable display used in SDR mode still needs an SDR preset.

### Optional ASI: manual installation

`LE2BlackRestorationASI.7z` is a separate download for advanced users who want to tune the restoration against their own display while the game is running. Install the main mod with one of the **four modern presets** first. Legacy Emulation has a different shader topology and does not support ASI tuning.

1. Download and extract `LE2BlackRestorationASI.7z`.
2. Copy `LE2BlackRestoration.asi` into `Mass Effect Legendary Edition\Game\ME2\Binaries\Win64\ASI\`.
3. Make sure the standard Legendary Edition ASI loader is installed, then launch the game.

The ASI is completely optional and is distributed for manual installation, separately from the main Mod Manager package. It is not a ME3Tweaks trusted-manifest/ASI Manager release. It uses the installed `LE2BlackRestoration.ini` described below; do not overwrite your chosen preset's INI with the source example merely to install the ASI.

## Compatibility and scope

The modern presets recover separation immediately above black while keeping `BlackFloorLift=0`. Some reduction in apparent contrast in the deepest shadows is intentional: previously crushed tones become distinguishable. The correction does not apply a global exposure/gamma adjustment, and the native colour grading, Filmic LUT, bloom, vignette and film grain remain in the rendering path.

Mods that replace the same global shader indices may conflict: M3GS selects a complete replacement according to DLC mount order rather than combining independent shader edits. Treat Vignette Remover 2.0 as not guaranteed compatible and Luma as unsupported without a dedicated integration. Black Restoration does not remove the vanilla vignette.

The historical inspiration is GETT0DACH0PPA's **Mass Effect 2 too dark lighting FIX** for the original game. The included Legacy option is a visual emulation; equivalence to removing the original game's `-0.004` offset has not been proved in Legendary Edition.

## SDR defaults

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

Missing restoration keys in the runtime INI inherit these SDR defaults. Present but invalid values are rejected so the last valid configuration remains active.

## Runtime configuration and hotkeys

The ASI reads:

```text
BIOGame/DLC/DLC_MOD_LE2BlackRestoration/LE2BlackRestoration.ini
```

The INI supports hot reload, runtime tuning, SDR comparison, profile save/load, and bypass. Hotkeys are enabled by default but can be disabled or remapped through `[Hotkeys]`.

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

| Default hotkeys | Action |
| --- | --- |
| F6 / F7 | Decrease / increase near-black recovery |
| Shift + F6 / F7 | Decrease / increase near-black detail |
| Ctrl + F6 / F7 | Decrease / increase near-black range |
| Alt + F6 / F7 | Decrease / increase pure-black protection |
| F8 | Reset custom tuning to SDR Reference defaults |
| Shift + F8 | Compare custom tuning with SDR Reference; preserve custom values |
| Ctrl + F8 | Save custom tuning to `LE2BlackRestoration_Profile.ini` |
| Ctrl + Shift + F8 | Load the saved profile and apply it immediately |
| Shift + F9 | Bypass / restore the complete restoration effect |

Saving creates an editable profile INI alongside the runtime INI. You can back it up, edit it, or share it as a personal preset. Saving the runtime INI while the game is running hot-reloads valid settings. `BlackFloorLift` is deliberately INI-only: keep it at `0` to preserve absolute black at the restoration stage. Additional shadow controls are documented in the example INI.

## Validate or regenerate M3GS assets

The tooling compiles a small wrapper around the same `DxbcPatcher` used by the ASI, validates all 160 release shaders, and reproduces all four modern preset sets byte-for-byte:

```powershell
./scripts/Validate-Release.ps1
```

See `AGENTS.md` for the frozen shader invariants, preset definitions, Legacy emulation caveat, and official Mod Manager deployment checklist.

For a source-level explanation of the hexadecimal bytecode, see the [DXBC patcher audit guide](ASI/DXBC-PATCHER.md). It maps the encoded instructions to assembly and equations, lists every permitted change, and explains recognition, checksum handling, and validation limits.

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
