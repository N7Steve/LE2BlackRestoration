# Optional LE2 Black Restoration ASI

`BlackRestoration/` contains the complete source for the optional LE2 x64 D3D11 ASI. The primary Mod Manager package does not require it.

The ASI recognizes only the validated modern Black Restoration DXBC topology, patches parameter immediates in place, and leaves unknown shaders untouched. Runtime configuration, exact-modifier hotkeys, profiles, comparison, and bypass are documented in `BlackRestoration/LE2BlackRestoration.ini.example`.

See the [DXBC patcher audit guide](DXBC-PATCHER.md) for decoded instructions, equations, exact editable DWORDs (including the three fade operand pairs), checksum handling, rejection rules, and reproducible validation.

## Build with LExASIs

Clone [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) outside this repository or under the ignored `_external/` directory. Copy `BlackRestoration/` into the LExASIs root, add this line to its root `CMakeLists.txt`, and configure LExASIs normally:

```cmake
add_subdirectory ("BlackRestoration")
```

Build the LE2 release target:

```powershell
cmake --build Build --config RELEASE --target BlackRestoration-LE2
```

Expected output:

```text
Build/Release/LE2BlackRestoration.asi
```

The integrated defaults match the public SDR Reference preset. Before distributing the ASI, replace the placeholder `ASI_GROUP_ID_RC 0` in `SharedVersion.h` with the GroupID assigned by ME3Tweaks, rebuild, and repeat runtime/configuration regression tests.

## Hook regression tests

Run `./scripts/Test-AsiHooks.ps1` from the repository root on Windows with
Visual Studio Build Tools and the Windows SDK. The harness executes the production
hook code with real D3D11 WARP shader objects for all 32 permutations. It substitutes
the configuration snapshot provider and SPI/logging services to force capture/reload
interleavings and shader creation failures. It checks that late captures retain the
published generation's parameters, complete reloads advance the version together,
failed operations preserve the previous generation, and Legacy is left untouched.
These tests do not replace in-game hotkey, profile, bypass, and comparison checks.
