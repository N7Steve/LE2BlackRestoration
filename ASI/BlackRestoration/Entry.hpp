#pragma once

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

namespace BlackRestoration {
bool Initialize(::LESDK::Initializer& init);
void Shutdown();
}
