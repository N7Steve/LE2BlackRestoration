#pragma once

// This file is shared by the C++ entry point and the Windows version resource.
#define CURRENT_YEAR "2026"

#define DEVELOPER_A "N7SteveMods"
#define DEVELOPER_W L"N7SteveMods"
#define ASI_NAME_A "LE2 Black Restoration"
#define ASI_NAME_W L"LE2 Black Restoration"
#define ASI_NAME_NO_SPACE_A "BlackRestoration"
#define ASI_NAME_NO_SPACE_W L"BlackRestoration"
#define ASI_VERSION 1
#define ASI_DESCRIPTION "Native black-crush correction and near-black detail restoration for Mass Effect 2 Legendary Edition."

// === Identification used by ME3Tweaks tooling ===
// LE2's game ID is 5. Group ID 0 is ONLY a pre-submission placeholder.
// Once ME3Tweaks assigns the real GroupID, replace 0 and rebuild from source.
#if defined(SDK_TARGET_LE2)
#define GAME_PREFIX_RC   "LE2"
#define ASI_GROUP_ID_RC  0
#define ASI_GAME_ID_RC   5
#else
#error LE2 Black Restoration currently supports LE2 only.
#endif

// Constants for all ASIs - do not change below.
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
// Version.Unused.GameId.GroupId
#define APP_VERSION_RC ASI_VERSION, 0, ASI_GAME_ID_RC, ASI_GROUP_ID_RC
#define VERSION_STRING_A TOSTRING(ASI_VERSION) ".0." TOSTRING(ASI_GAME_ID_RC) "." TOSTRING(ASI_GROUP_ID_RC)
#define VERSION_STRING_W TOSTRING(ASI_VERSION) L".0." TOSTRING(ASI_GAME_ID_RC) L"." TOSTRING(ASI_GROUP_ID_RC)
