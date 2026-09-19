#include "BlackRestoration/D3D11Hooks.hpp"

#include "BlackRestoration/Config.hpp"
#include "BlackRestoration/DxbcPatcher.hpp"
#include "Common/Base.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <d3d11_3.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <span>
#include <utility>
#include <vector>

namespace BlackRestoration {
namespace {
using Microsoft::WRL::ComPtr;

using D3D11CreateDeviceFn = HRESULT (WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using D3D11CreateDeviceAndSwapChainFn = HRESULT (WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using CreatePixelShaderFn = HRESULT (STDMETHODCALLTYPE*)(
    ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
using PSSetShaderFn = void (STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);

D3D11CreateDeviceFn g_d3d11CreateDeviceOrig = nullptr;
D3D11CreateDeviceAndSwapChainFn g_d3d11CreateDeviceAndSwapChainOrig = nullptr;

constexpr std::size_t kCreatePixelShaderIndex = 15;
constexpr std::size_t kPSSetShaderIndex = 9;

struct TargetShader {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11PixelShader> baselineObject;
    ComPtr<ID3D11ClassLinkage> classLinkage;
    std::vector<std::uint8_t> baselineDxbc;
};

struct Generation {
    std::uint64_t configVersion = 0;
    std::unordered_map<ID3D11PixelShader*, ComPtr<ID3D11PixelShader>> replacements;
};

std::mutex g_targetsMutex;
std::unordered_map<ID3D11PixelShader*, TargetShader> g_targets;
std::atomic<std::shared_ptr<const Generation>> g_generation{std::make_shared<Generation>()};
std::mutex g_reloadMutex;
std::atomic<std::uint64_t> g_appliedConfigVersion{0};
std::atomic<std::uint32_t> g_targetCount{0};

// The concrete D3D11 COM implementation used by the game can have different
// vtable entry addresses from a probe device. Capture the actual vtables that
// LE2 receives from D3D11CreateDevice[AndSwapChain], then patch those entries.
std::mutex g_vtableMutex;
std::unordered_map<void**, CreatePixelShaderFn> g_deviceVtables;
std::unordered_map<void**, PSSetShaderFn> g_contextVtables;

std::shared_ptr<const Generation> LoadGeneration() {
    return g_generation.load(std::memory_order_acquire);
}

void PublishGeneration(std::shared_ptr<const Generation> generation) {
    g_generation.store(std::move(generation), std::memory_order_release);
}

bool WriteVtableSlot(void** slot, void* replacement) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), replacement);

    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
    return true;
}

CreatePixelShaderFn OriginalCreatePixelShader(ID3D11Device* device) {
    if (device == nullptr) return nullptr;
    auto** vtable = *reinterpret_cast<void***>(device);
    std::scoped_lock lock(g_vtableMutex);
    const auto it = g_deviceVtables.find(vtable);
    return it == g_deviceVtables.end() ? nullptr : it->second;
}

PSSetShaderFn OriginalPSSetShader(ID3D11DeviceContext* context) {
    if (context == nullptr) return nullptr;
    auto** vtable = *reinterpret_cast<void***>(context);
    std::scoped_lock lock(g_vtableMutex);
    const auto it = g_contextVtables.find(vtable);
    return it == g_contextVtables.end() ? nullptr : it->second;
}

ComPtr<ID3D11PixelShader> BuildReplacement(const TargetShader& target, const blackcrush::Parameters& params) {
    const auto patched = blackcrush::patch_shader(target.baselineDxbc, params);
    const auto original = OriginalCreatePixelShader(target.device.Get());
    if (original == nullptr) {
        throw blackcrush::PatchError("original CreatePixelShader function unavailable for target device");
    }

    ComPtr<ID3D11PixelShader> replacement;
    const HRESULT hr = original(
        target.device.Get(), patched.data(), patched.size(), target.classLinkage.Get(), replacement.GetAddressOf());
    if (FAILED(hr) || !replacement) {
        throw blackcrush::PatchError("ID3D11Device::CreatePixelShader failed for replacement");
    }
    return replacement;
}

void RebuildForConfig(const ConfigSnapshot& config) {
    std::unique_lock reloadLock(g_reloadMutex, std::try_to_lock);
    if (!reloadLock.owns_lock()) return;
    if (g_appliedConfigVersion.load(std::memory_order_acquire) >= config.version) return;

    std::vector<std::pair<ID3D11PixelShader*, TargetShader>> targets;
    {
        std::scoped_lock lock(g_targetsMutex);
        targets.reserve(g_targets.size());
        for (const auto& [key, target] : g_targets) targets.emplace_back(key, target);
    }

    auto next = std::make_shared<Generation>();
    next->configVersion = config.version;
    next->replacements.reserve(targets.size());

    try {
        for (const auto& [key, target] : targets) {
            next->replacements.emplace(key, BuildReplacement(target, config.params));
        }
    } catch (const std::exception& e) {
        LEASI_ERROR("hot reload v{} failed; keeping previous generation: {}", config.version, e.what());
        return;
    }

    PublishGeneration(next);
    g_appliedConfigVersion.store(config.version, std::memory_order_release);
    LEASI_INFO("hot reload applied v{} to {}/32 captured shaders",
        config.version, next->replacements.size());
}

HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(
    ID3D11Device* device,
    const void* bytecode,
    SIZE_T bytecodeLength,
    ID3D11ClassLinkage* classLinkage,
    ID3D11PixelShader** outShader) {

    const auto original = OriginalCreatePixelShader(device);
    if (original == nullptr) {
        LEASI_ERROR("CreatePixelShader hook called for unknown device vtable: device={}", static_cast<void*>(device));
        return E_FAIL;
    }

    const HRESULT hr = original(device, bytecode, bytecodeLength, classLinkage, outShader);
    if (FAILED(hr) || outShader == nullptr || *outShader == nullptr || bytecode == nullptr || bytecodeLength == 0) {
        return hr;
    }

    const auto bytes = std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(bytecode), bytecodeLength);
    try {
        // Recognition is intentionally strict: only the validated shadow-recovery + near-black instruction topology can be patched.
        // Encoded tuning immediates are treated as wildcards so known tuned baselines remain compatible.
        const auto current = Config().Current();
        const auto patched = blackcrush::patch_shader(bytes, current.params);

        TargetShader target;
        target.device = device;
        target.baselineObject = *outShader; // keep one reference so raw-pointer identity cannot be recycled
        target.classLinkage = classLinkage;
        target.baselineDxbc.assign(bytes.begin(), bytes.end());

        ComPtr<ID3D11PixelShader> replacement;
        const HRESULT replacementHr = original(
            device, patched.data(), patched.size(), classLinkage, replacement.GetAddressOf());
        if (FAILED(replacementHr) || !replacement) {
            LEASI_ERROR("target shader recognized, but initial replacement creation failed: HRESULT=0x{:08X}",
                static_cast<unsigned int>(replacementHr));
            return hr;
        }

        {
            // Same lock ordering as RebuildForConfig: reload -> targets. This prevents
            // a config reload and late shader capture from publishing generations over each other.
            std::scoped_lock reloadLock(g_reloadMutex);
            {
                std::scoped_lock targetsLock(g_targetsMutex);
                const auto [it, inserted] = g_targets.emplace(*outShader, std::move(target));
                if (!inserted) return hr;
            }

            auto previous = LoadGeneration();
            auto next = std::make_shared<Generation>(*previous);
            next->configVersion = current.version;
            next->replacements[*outShader] = std::move(replacement);
            PublishGeneration(next);
            g_appliedConfigVersion.store(current.version, std::memory_order_release);
        }

        const auto count = g_targetCount.fetch_add(1, std::memory_order_acq_rel) + 1;
        LEASI_INFO("captured Black Restoration target shader {}/32: object={}", count, static_cast<void*>(*outShader));
        if (count == 32) LEASI_INFO("all 32 target pixel shaders captured");
        if (count > 32) LEASI_WARN("captured more than 32 matching shader objects ({})", count);
    } catch (const blackcrush::PatchError&) {
        // Expected for unrelated pixel shaders. Strict recognition is the fail-safe.
    } catch (const std::exception& e) {
        LEASI_ERROR("unexpected CreatePixelShader hook error: {}", e.what());
    }

    return hr;
}

void STDMETHODCALLTYPE PSSetShaderHook(
    ID3D11DeviceContext* context,
    ID3D11PixelShader* pixelShader,
    ID3D11ClassInstance* const* classInstances,
    UINT numClassInstances) {

    const auto original = OriginalPSSetShader(context);
    if (original == nullptr) {
        LEASI_ERROR("PSSetShader hook called for unknown context vtable: context={}", static_cast<void*>(context));
        return;
    }

    const auto applied = g_appliedConfigVersion.load(std::memory_order_acquire);
    if (const auto pending = Config().ConsumePending(applied)) {
        RebuildForConfig(*pending);
    }

    if (pixelShader != nullptr) {
        const auto generation = LoadGeneration();
        const auto it = generation->replacements.find(pixelShader);
        if (it != generation->replacements.end()) {
            original(context, it->second.Get(), classInstances, numClassInstances);
            return;
        }
    }
    original(context, pixelShader, classInstances, numClassInstances);
}

bool PatchDeviceVtable(ID3D11Device* device, const char* label) {
    if (device == nullptr) return false;
    auto** vtable = *reinterpret_cast<void***>(device);
    void** slot = &vtable[kCreatePixelShaderIndex];
    void* const hook = reinterpret_cast<void*>(&CreatePixelShaderHook);

    std::scoped_lock lock(g_vtableMutex);
    if (g_deviceVtables.contains(vtable)) return true;

    void* const current = *slot;
    if (current == hook) {
        LEASI_WARN("actual device vtable already points at our CreatePixelShader hook but no original was recorded");
        return false;
    }

    if (!WriteVtableSlot(slot, hook)) {
        LEASI_ERROR("VirtualProtect failed while patching actual ID3D11Device vtable (error={})", GetLastError());
        return false;
    }

    g_deviceVtables.emplace(vtable, reinterpret_cast<CreatePixelShaderFn>(current));
    const auto count = g_deviceVtables.size();
    LEASI_INFO("patched device interface vtable #{} [{}]: interface={} vtable={} original CreatePixelShader={}",
        count, label != nullptr ? label : "?", static_cast<void*>(device), static_cast<void*>(vtable), current);
    return true;
}

bool PatchContextVtable(ID3D11DeviceContext* context, const char* label) {
    if (context == nullptr) return false;
    auto** vtable = *reinterpret_cast<void***>(context);
    void** slot = &vtable[kPSSetShaderIndex];
    void* const hook = reinterpret_cast<void*>(&PSSetShaderHook);

    std::scoped_lock lock(g_vtableMutex);
    if (g_contextVtables.contains(vtable)) return true;

    void* const current = *slot;
    if (current == hook) {
        LEASI_WARN("actual context vtable already points at our PSSetShader hook but no original was recorded");
        return false;
    }

    if (!WriteVtableSlot(slot, hook)) {
        LEASI_ERROR("VirtualProtect failed while patching actual ID3D11DeviceContext vtable (error={})", GetLastError());
        return false;
    }

    g_contextVtables.emplace(vtable, reinterpret_cast<PSSetShaderFn>(current));
    const auto count = g_contextVtables.size();
    LEASI_INFO("patched context interface vtable #{} [{}]: interface={} vtable={} original PSSetShader={}",
        count, label != nullptr ? label : "?", static_cast<void*>(context), static_cast<void*>(vtable), current);
    return true;
}

template <typename T>
void QueryAndPatchDeviceInterface(ID3D11Device* base, const char* label) {
    if (base == nullptr) return;
    ComPtr<T> iface;
    const HRESULT hr = base->QueryInterface(IID_PPV_ARGS(iface.GetAddressOf()));
    if (SUCCEEDED(hr) && iface) {
        PatchDeviceVtable(static_cast<ID3D11Device*>(iface.Get()), label);
    }
}

template <typename T>
void QueryAndPatchContextInterface(ID3D11DeviceContext* base, const char* label) {
    if (base == nullptr) return;
    ComPtr<T> iface;
    const HRESULT hr = base->QueryInterface(IID_PPV_ARGS(iface.GetAddressOf()));
    if (SUCCEEDED(hr) && iface) {
        PatchContextVtable(static_cast<ID3D11DeviceContext*>(iface.Get()), label);
    }
}

void CaptureContextFamily(ID3D11DeviceContext* context) {
    if (context == nullptr) return;

    const auto type = context->GetType();
    LEASI_INFO("capturing D3D11 context interface family: base={} type={}",
        static_cast<void*>(context), type == D3D11_DEVICE_CONTEXT_IMMEDIATE ? "immediate" : "deferred");

    PatchContextVtable(context, "ID3D11DeviceContext");
    QueryAndPatchContextInterface<ID3D11DeviceContext1>(context, "ID3D11DeviceContext1");
    QueryAndPatchContextInterface<ID3D11DeviceContext2>(context, "ID3D11DeviceContext2");
    QueryAndPatchContextInterface<ID3D11DeviceContext3>(context, "ID3D11DeviceContext3");
    QueryAndPatchContextInterface<ID3D11DeviceContext4>(context, "ID3D11DeviceContext4");
}

void CaptureActualDeviceAndContext(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (device == nullptr) return;

    LEASI_INFO("capturing D3D11 device interface family: base={}", static_cast<void*>(device));
    PatchDeviceVtable(device, "ID3D11Device");
    QueryAndPatchDeviceInterface<ID3D11Device1>(device, "ID3D11Device1");
    QueryAndPatchDeviceInterface<ID3D11Device2>(device, "ID3D11Device2");
    QueryAndPatchDeviceInterface<ID3D11Device3>(device, "ID3D11Device3");
    QueryAndPatchDeviceInterface<ID3D11Device4>(device, "ID3D11Device4");
    QueryAndPatchDeviceInterface<ID3D11Device5>(device, "ID3D11Device5");

    if (context != nullptr) {
        CaptureContextFamily(context);
        return;
    }

    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    if (immediate) CaptureContextFamily(immediate.Get());
}

HRESULT WINAPI D3D11CreateDeviceHook(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driverType,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* featureLevels,
    UINT featureLevelCount,
    UINT sdkVersion,
    ID3D11Device** outDevice,
    D3D_FEATURE_LEVEL* outFeatureLevel,
    ID3D11DeviceContext** outImmediateContext) {

    const HRESULT hr = g_d3d11CreateDeviceOrig(
        adapter, driverType, software, flags, featureLevels, featureLevelCount,
        sdkVersion, outDevice, outFeatureLevel, outImmediateContext);

    if (SUCCEEDED(hr) && outDevice != nullptr && *outDevice != nullptr) {
        ID3D11DeviceContext* context =
            (outImmediateContext != nullptr) ? *outImmediateContext : nullptr;
        LEASI_INFO("intercepted D3D11CreateDevice: device={} context={}",
            static_cast<void*>(*outDevice), static_cast<void*>(context));
        CaptureActualDeviceAndContext(*outDevice, context);
    }
    return hr;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChainHook(
    IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE driverType,
    HMODULE software,
    UINT flags,
    const D3D_FEATURE_LEVEL* featureLevels,
    UINT featureLevelCount,
    UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
    IDXGISwapChain** outSwapChain,
    ID3D11Device** outDevice,
    D3D_FEATURE_LEVEL* outFeatureLevel,
    ID3D11DeviceContext** outImmediateContext) {

    const HRESULT hr = g_d3d11CreateDeviceAndSwapChainOrig(
        adapter, driverType, software, flags, featureLevels, featureLevelCount,
        sdkVersion, swapChainDesc, outSwapChain, outDevice, outFeatureLevel, outImmediateContext);

    if (SUCCEEDED(hr) && outDevice != nullptr && *outDevice != nullptr) {
        ID3D11DeviceContext* context =
            (outImmediateContext != nullptr) ? *outImmediateContext : nullptr;
        LEASI_INFO("intercepted D3D11CreateDeviceAndSwapChain: device={} context={} swapChain={}",
            static_cast<void*>(*outDevice), static_cast<void*>(context),
            static_cast<void*>((outSwapChain != nullptr) ? *outSwapChain : nullptr));
        CaptureActualDeviceAndContext(*outDevice, context);
    }
    return hr;
}

void RestoreActualVtables() {
    std::scoped_lock lock(g_vtableMutex);

    void* const createHook = reinterpret_cast<void*>(&CreatePixelShaderHook);
    for (const auto& [vtable, original] : g_deviceVtables) {
        void** slot = &vtable[kCreatePixelShaderIndex];
        if (*slot == createHook) {
            WriteVtableSlot(slot, reinterpret_cast<void*>(original));
        }
    }

    void* const setHook = reinterpret_cast<void*>(&PSSetShaderHook);
    for (const auto& [vtable, original] : g_contextVtables) {
        void** slot = &vtable[kPSSetShaderIndex];
        if (*slot == setHook) {
            WriteVtableSlot(slot, reinterpret_cast<void*>(original));
        }
    }

    g_deviceVtables.clear();
    g_contextVtables.clear();
}
} // namespace

bool InitializeD3D11Hooks(::LESDK::Initializer& init) {
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    if (d3d11 == nullptr) d3d11 = LoadLibraryW(L"d3d11.dll");
    if (d3d11 == nullptr) {
        LEASI_ERROR("failed to load d3d11.dll (error={})", GetLastError());
        return false;
    }

    void* const createDeviceTarget = reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11CreateDevice"));
    void* const createDeviceAndSwapChainTarget = reinterpret_cast<void*>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
    if (createDeviceTarget == nullptr || createDeviceAndSwapChainTarget == nullptr) {
        LEASI_ERROR("failed to resolve D3D11 device creation exports");
        return false;
    }

    g_d3d11CreateDeviceOrig = reinterpret_cast<D3D11CreateDeviceFn>(
        init.InstallHook("D3D11CreateDevice", createDeviceTarget, D3D11CreateDeviceHook));
    g_d3d11CreateDeviceAndSwapChainOrig = reinterpret_cast<D3D11CreateDeviceAndSwapChainFn>(
        init.InstallHook("D3D11CreateDeviceAndSwapChain", createDeviceAndSwapChainTarget, D3D11CreateDeviceAndSwapChainHook));

    if (g_d3d11CreateDeviceOrig == nullptr || g_d3d11CreateDeviceAndSwapChainOrig == nullptr) {
        LEASI_ERROR("failed to install one or more D3D11 creation hooks");
        return false;
    }

    LEASI_INFO("D3D11 creation hooks installed (actual game device/context will be captured; D3D11CreateDevice target={} D3D11CreateDeviceAndSwapChain target={})",
        createDeviceTarget, createDeviceAndSwapChainTarget);
    return true;
}

void ShutdownD3D11Hooks() {
    LEASI_INFO("shutting down D3D11 hooks; captured={}/32",
        g_targetCount.load(std::memory_order_relaxed));
    RestoreActualVtables();
    PublishGeneration(std::make_shared<Generation>());
    {
        std::scoped_lock lock(g_targetsMutex);
        g_targets.clear();
    }
    g_targetCount.store(0, std::memory_order_release);
}


} // namespace BlackRestoration
