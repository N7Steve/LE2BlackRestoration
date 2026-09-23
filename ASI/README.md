# Optional LE2 Black Restoration ASI

`BlackRestoration/` contains the complete source for the optional LE2 x64 D3D11 ASI. The primary Mod Manager package does not require it.

The ASI recognizes only the validated modern Black Restoration DXBC topology, patches parameter immediates in place, and leaves unknown shaders untouched. Runtime configuration, exact-modifier hotkeys, profiles, comparison, and bypass are documented in `BlackRestoration/LE2BlackRestoration.ini.example`.

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
