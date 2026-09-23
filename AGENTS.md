# LE2 Black Restoration agent guide

## Mission and release state

This is the main repository for **LE2 Black Restoration v1.0.0**, a Mass Effect 2 Legendary Edition (PC, D3D11 x64) mod by N7SteveMods. It contains the release-candidate Mod Manager package, reproducibility tooling, documentation, and optional ASI source. The effect restores near-black separation in the native post-processing path immediately before the Filmic LUT. It must preserve absolute black, must not become a general exposure/gamma/contrast adjustment, and does not use ReShade.

Treat the checked-in package as the release source of truth. The shader design is frozen: investigate and prove a concrete release-blocking defect before changing shader math, topology, preset values, generated M3GS files, or installer behavior. Do not perform aesthetic refactors during release preparation.

## Repository layout

```text
LE2 Black Restoration/                 repository root
├─ AGENTS.md
├─ README.md
├─ LICENSE
├─ .gitignore
├─ ASI/
│  └─ BlackRestoration/                optional ASI source and example INI
├─ LE2 Black Restoration/             real ME3Tweaks Mod Manager package
│  ├─ moddesc.ini
│  ├─ DLC_MOD_LE2BlackRestoration/
│  │  ├─ CookedPCConsole/             BIOEngine.ini, Mount.dlc, eight TLKs
│  │  └─ Preset_*.ini                 authoritative preset references
│  └─ InstallOptions/                 five alternate-DLC payloads
│     └─ <preset>/
│        ├─ CookedPCConsole/           exactly 32 M3GS overrides
│        └─ LE2BlackRestoration.ini    installed preset/runtime reference
├─ scripts/                            tracked generators and validators
├─ _external/                         ignored third-party/source checkouts
└─ _tools/                            ignored builds, reports, disassembly, temp output
```

Never move `_external` or `_tools` into the package or commit their contents. In particular, do not vendor nested Git repositories, compiler binaries, LExASIs, ME3TweaksModManager, ME3TweaksCore, build directories, caches, deployment archives, PDB/OBJ/LIB/DLL/ASI files, or Mod Manager deployment output.

The inspected package has 181 files / 1,270,167 bytes: 160 `.m3gs` files / 1,250,000 bytes, 12 INIs, 8 TLKs, and one `Mount.dlc`. There is no ASI in the primary package.

## Public package and installer

The main DLC package must work without an ASI. The optional ASI is an advanced, separately distributed runtime-tuning component and is not a dependency of the Nexus package. Do not add an ASI manifest dependency to the DLC.

`moddesc.ini` uses `cmmver=9.2`, one `OptionGroup=DisplayPreset`, and five mutually exclusive `OP_ADD_FOLDERFILES_TO_CUSTOMDLC` alternatives. Exactly one item is checked by default: `IPS-SDR - Reference`. Current ME3Tweaks source confirms that OptionGroup is mutually exclusive, must contain exactly one default, and M3GS requires ModDesc 9.2.

The five choices are:

| Installer choice | Purpose |
| --- | --- |
| IPS-SDR - Reference | Recommended default for standard SDR LCD/IPS displays. |
| IPS-SDR - Stronger Near-Black | Stronger SDR near-black recovery/separation. |
| OLED/HDR - Reference | Reference tuning for OLED and HDR output. |
| OLED/HDR - Stronger Near-Black | Stronger OLED/HDR near-black recovery/separation. |
| Original ME2 Black Crush Fix - Legacy Emulation | Static, conservative emulation of the historical original-ME2 fix; not the modern curve. |

## Frozen modern shader design

For `x < NearBlackRange`, the intended per-channel curve is:

```text
t  = clamp(x / PureBlackProtection, 0, 1)
S  = t²(3 - 2t)
x' = x + (1 - x/NearBlackRange)²
         * [x*NearBlackDetail + NearBlackRecovery*S(t) + BlackFloorLift]
```

With `BlackFloorLift=0`, the non-negotiable invariant is `x=0 -> x'=0`. The optional luminance shadow stage is controlled separately by `ShadowBoost`, `ShadowRange`, and integer `ShadowFade`.

The validated in-place DXBC patcher must preserve total DXBC size, chunk offsets/tags/sizes, SHEX size and token count, instruction count, `DCL_TEMPS=5`, and the complete STAT chunk. Only the recognized luma/near-black parameter immediates and the DXBC checksum may change for modern presets. The matcher must find exactly one known topology; unknown or duplicate matches must fail instead of being patched.

All preset sets use the identical 32 shader indices:

```text
65-88, 97-104
```

Each set totals 250,000 bytes and has the same filename, size, container, SHEX, instruction-count, `DCL_TEMPS`, and STAT structure per index. All 160 checked-in blobs have valid DXBC checksums.

## Authoritative preset values

The checked-in `DLC_MOD_LE2BlackRestoration/Preset_*.ini` files are authoritative. Their currently validated values are:

| Parameter | IPS Ref | IPS Stronger | OLED/HDR Ref | OLED/HDR Stronger |
| --- | ---: | ---: | ---: | ---: |
| ShadowBoost | 0 | 0 | 0 | 0 |
| ShadowRange | 0.026 | 0.026 | 0.030 | 0.030 |
| ShadowFade | 3 | 3 | 3 | 3 |
| NearBlackRange | 0.040 | 0.045 | 0.058 | 0.058 |
| NearBlackDetail | 0.1 | 0.2 | 0 | 0.2 |
| NearBlackRecovery | 0.0045 | 0.006 | 0.006 | 0.0085 |
| PureBlackProtection | 0.0005 | 0.0005 | 0.0007 | 0.0007 |
| BlackFloorLift | 0 | 0 | 0 | 0 |

The existing patcher reproduces all 128 modern checked-in M3GS files byte-for-byte from the IPS-SDR Reference set plus the corresponding preset INI.

## Legacy emulation

Historical intent in original ME2 was to change `max(0, InLinearColour - 0.004)` to `max(0, InLinearColour)`. Literal mathematical equivalence has not been proved in LE2, so retain the name **Legacy Emulation** and do not claim an exact port.

The current legacy blobs keep the same container/SHEX sizes, token and instruction counts, `DCL_TEMPS=5`, and STAT data as the modern blobs, but they deliberately do **not** match the modern parameter topology. Their repurposed pre-LUT instruction sequence zeros modern recovery/detail, forms a constant `+0.004`, makes the former near-black mask equal one, adds the constant to RGB, and then applies the unchanged Filmic LUT scale (approximately `0.06160822`). This is shader-only and is not representable by the current ASI parameter model. Do not pass Legacy through the modern generator.

## Reproducible shader workflow

The tracked wrapper builds directly against the canonical `ASI/BlackRestoration/DxbcPatcher.cpp/.hpp` in this repository; it does not contain a second patcher implementation or require an external source checkout.

Build the validator/generator:

```powershell
./scripts/Build-ShaderTool.ps1
```

Generate a modern preset into a new/empty ignored directory (never directly over release assets):

```powershell
./scripts/Generate-PresetShaders.ps1 `
  -BaselineDirectory './LE2 Black Restoration/InstallOptions/IPS_SDR_Reference/CookedPCConsole' `
  -PresetIni './LE2 Black Restoration/DLC_MOD_LE2BlackRestoration/Preset_OLED_HDR_Reference.ini' `
  -OutputDirectory './_tools/generated/OLED_HDR_Reference'
```

Validate the complete release and emit SHA-256/reproduction CSV reports under `_tools`:

```powershell
./scripts/Validate-Release.ps1
```

The workflow must fail on an invalid checksum, invalid/missing parameter, unsafe curve, unrecognized or duplicate topology, malformed/duplicate/missing shader index, wrong count, changed output size, changed DXBC structure/STAT, or byte mismatch against a modern checked-in preset.

## External references and local tools

The investigation on 2026-09-23 used these ignored shallow checkouts:

| Checkout | Inspected commit | Purpose |
| --- | --- | --- |
| Git history before unification | `2b6d681893806c42bc612aa83734d0ecce03f221` | Original ASI/DXBC patcher source, now integrated under `ASI/BlackRestoration`. |
| `_external/ME3TweaksModManager` | `1c98d93e410c589cf48624e7e13aec02079d3bcd` | ModDesc 9.2, Alternate DLC/OptionGroup, install/deploy behavior. |
| `_external/ME3TweaksCore` | `b5f006add38dbaea91dde3f16f45ecbc3b0155a9` | Current GlobalShaderMerge implementation. |
| `_external/LExASIs` | `aaad03ab6e5112e27ff5528f7b08c3bcf3870c31` | Optional ASI build host; ignored and not vendored. |

LExASIs is not vendored. Clone it under `_external/LExASIs` only when building the optional ASI; copy `ASI/BlackRestoration` into that checkout as `BlackRestoration` and add its subdirectory as documented in the root and ASI READMEs. The integrated source was successfully built as `BlackRestoration-LE2` in RELEASE with the commit above and Visual Studio 2022/MSVC 19.44.

Available local tools at investigation time: Git 2.51, PowerShell 7.6, Python 3.11, CMake 4.4, Visual Studio 2022 Build Tools/MSVC 19.44, and Windows SDK 10.0.26100 with x64 `fxc.exe` and `dxc.exe`. `fxc`/`dxc` are not on the normal PATH; use the Windows SDK path discovered locally. Modern generation does not require recompiling HLSL.

ME3TweaksCore scans only top-level `GlobalShader-*.m3gs` files in each installed `DLC_MOD_*/CookedPCConsole`, processes eligible DLC mods in mount order, extracts the numeric index from `GlobalShader-INDEX-...`, and replaces that Global Shader Cache entry. It requires installed DLC metadata with ModDesc feature level 9.2 or later.

## Required validation before release

Run `scripts/Validate-Release.ps1`, then confirm all of the following:

1. `moddesc.ini` remains at feature level 9.2, contains five members in one DisplayPreset OptionGroup, and has only IPS-SDR Reference checked by default.
2. No ASI, manifest dependency, build product, nested repository, or deployment archive is inside the package/tracked tree.
3. Every preset has exactly 32 correctly named shaders with indices `65-88,97-104`; all five filename/index sets are identical.
4. All 160 DXBC checksums are valid and per-index container/SHEX/STAT invariants match across presets.
5. Every modern blob matches regeneration byte-for-byte from the authoritative preset INI; `BlackFloorLift=0` and the numerical monotonicity guard pass.
6. Legacy fails the modern topology matcher and its disassembly still shows the targeted `+0.004` immediately before the unchanged Filmic LUT scale.
7. Installed `LE2BlackRestoration.ini` values/comments agree with their option and Legacy remains marked static/ASI-incompatible.
8. Perform a clean Mod Manager install test for each of the five choices, confirm only the selected 32 M3GS files deploy, Global Shader Merge completes, and the game launches.
9. Repeat representative visual regression tests, including absolute black, near-black gradients, dark scenes, menus/cinematics, bypass/comparison where applicable, and uninstall/reinstall behavior.
10. Create the final Nexus archive only through official ME3Tweaks Mod Manager: **Mod Utils -> Developer Options -> Deploy mod**. Do not manually recompress it; that triggers “Improperly deployed mod”. Verify the deployed archive by reimporting/installing it.

## Known discrepancies and unresolved work

- **Optional ASI defaults are aligned but need a full ASI rebuild/regression test.** The integrated source now uses the public IPS-SDR Reference values (`NearBlackDetail=0.1`, `NearBlackRecovery=0.0045`). Rebuild through LExASIs and repeat runtime/hotkey/profile tests before distributing the ASI.
- **Optional ASI is not trusted-release ready.** `SharedVersion.h` still has placeholder `ASI_GROUP_ID_RC 0`; ME3Tweaks must assign the real GroupID before a trusted ASI build.
- **Legacy equivalence is unproved.** The observed LE2 implementation is a conservative visual emulation, not a demonstrated literal removal of an LE2 `-0.004` operation.
- **Official packaging/in-game validation remains manual.** The source/bytecode checks do not replace clean Mod Manager deployment and gameplay testing.

## Safe-change policy

Before any technical change beyond documentation or isolated tooling, record: the concrete problem, risk of leaving it unchanged, risk of changing it, whether all shaders must be regenerated, whether the ASI must be rebuilt, and the exact tests to repeat. Preserve unrelated user changes and do not silently regenerate binary assets.

Release blockers are: invalid or unreproducible M3GS, incorrect/multiple/no default installer choice, broken ModDesc/M3GS integration, absolute-black regression, package dependency on the optional ASI, failed clean install/launch, or an improperly deployed final archive. Optional work includes ASI live tuning, trusted ASI manifest submission, extra developer ergonomics, and style-only cleanup; do not let it destabilize the primary release.
