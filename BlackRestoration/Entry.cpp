#include "Common/Base.hpp"
#include "Common/DefaultLogger.hpp"
#include "BlackRestoration/Config.hpp"
#include "BlackRestoration/D3D11Hooks.hpp"
#include "BlackRestoration/Entry.hpp"
#include "BlackRestoration/SharedVersion.h"

SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
// D3D11 global shaders are created very early in LE2. Request SPI preload so
// SpiOnAttach runs before the GlobalShaderCache creates its D3D11 shader objects.
SPI_PLUGINSIDE_PRELOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::LESDK::Initializer init{InterfacePtr, SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A};
    Common::SetupDefaultLogger(SDK_TARGET_NAME_A, ASI_NAME_NO_SPACE_A);
    return ::BlackRestoration::Initialize(init);
}

SPI_IMPLEMENT_DETACH
{
    LEASI_UNUSED(InterfacePtr);
    ::BlackRestoration::Shutdown();
    ::Common::ShutdownLogger();
    return true;
}

namespace BlackRestoration {
bool Initialize(::LESDK::Initializer& init) {
    LEASI_INFO("LE2 Black Restoration {} starting (early D3D11 attach)", VERSION_STRING_A);
    if (!Config().Initialize()) return false;
    if (!InitializeD3D11Hooks(init)) {
        Config().Shutdown();
        return false;
    }
    return true;
}

void Shutdown() {
    Config().Shutdown();
    ShutdownD3D11Hooks();
}
}
