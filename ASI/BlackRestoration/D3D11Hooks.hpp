#pragma once

#include <LESDK/Init.hpp>

namespace BlackRestoration {
bool InitializeD3D11Hooks(::LESDK::Initializer& init);
void ShutdownD3D11Hooks();
}
