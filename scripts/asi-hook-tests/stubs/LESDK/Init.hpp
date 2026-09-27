#pragma once
namespace LESDK {
// The regression harness calls the hooks directly; SPI installation is not used.
struct Initializer {
    template <typename T>
    void* InstallHook(const char*, void*, T) { return nullptr; }
};
}
