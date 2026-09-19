asi_target (BlackRestoration
  SHARED
  GAMES "LE2"
  SOURCES
    "Entry.cpp"
    "Entry.hpp"
    "Config.cpp"
    "Config.hpp"
    "D3D11Hooks.cpp"
    "D3D11Hooks.hpp"
    "DxbcPatcher.cpp"
    "DxbcPatcher.hpp"
    "SharedVersion.h"
    "BlackRestoration.rc"
  LINKS
    "d3d11"
  VLINKS
    "Common"
    "LESDK"
  )
