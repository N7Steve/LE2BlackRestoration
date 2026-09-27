// Exercise the production hook/publication code with real WARP shader objects.
// Only the config snapshot provider and SPI/logging services are substituted.
#include "BlackRestoration/D3D11Hooks.cpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>

namespace {
BlackRestoration::ConfigSnapshot testConfig;
std::unordered_map<ID3D11PixelShader*, std::vector<std::uint8_t>> createdBytes;
int failAfter = 0;
int callbackAfter = 0;
std::function<void()> creationCallback;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

HRESULT STDMETHODCALLTYPE TestCreate(ID3D11Device* device, const void* bytes,
    SIZE_T length, ID3D11ClassLinkage* linkage, ID3D11PixelShader** shader) {
    if (failAfter > 0 && --failAfter == 0) return E_FAIL;
    const auto hr = device->CreatePixelShader(bytes, length, linkage, shader);
    if (SUCCEEDED(hr) && shader && *shader) {
        const auto* first = static_cast<const std::uint8_t*>(bytes);
        createdBytes[*shader] = {first, first + length};
    }
    if (callbackAfter > 0 && --callbackAfter == 0) creationCallback();
    return hr;
}
}

namespace BlackRestoration {
ConfigManager& Config() { static ConfigManager manager; return manager; }
ConfigSnapshot ConfigManager::Current() const { return testConfig; }
std::optional<ConfigSnapshot> ConfigManager::ConsumePending(std::uint64_t applied) const {
    if (testConfig.version > applied) return testConfig;
    return std::nullopt;
}
}

int main(int argc, char** argv) {
    using namespace BlackRestoration;
    try {
        require(argc == 3, "Pass modern and Legacy shader paths");
        const auto read = [](const char* path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read shader");
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), {}};
        };
        const auto baseline = read(argv[1]);
        const auto legacy = read(argv[2]);
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf())), "WARP device creation failed");
        g_deviceVtables.emplace(*reinterpret_cast<void***>(device.Get()), TestCreate);
        const auto capture = [&](const auto& bytes) {
            ComPtr<ID3D11PixelShader> shader;
            require(SUCCEEDED(CreatePixelShaderHook(device.Get(), bytes.data(), bytes.size(),
                nullptr, shader.GetAddressOf())), "Capture failed");
            return shader;
        };
        const auto verifyGeneration = [&](std::uint64_t version, std::size_t count) {
            const auto generation = LoadGeneration();
            require(generation->configVersion == version, "Wrong published version");
            require(g_appliedConfigVersion.load() == version, "Wrong applied version");
            require(generation->replacements.size() == count, "Wrong replacement count");
            const auto expected = blackcrush::patch_shader(baseline, generation->params);
            for (const auto& [key, replacement] : generation->replacements) {
                require(createdBytes.at(replacement.Get()) == expected, "Mixed parameter versions");
            }
        };

        testConfig = {blackcrush::Parameters{}, 1};
        auto a = capture(baseline);
        verifyGeneration(1, 1);
        testConfig.params.near_black_recovery = 0.006;
        testConfig.version = 2;
        auto b = capture(baseline);
        verifyGeneration(1, 2);
        require(Config().ConsumePending(g_appliedConfigVersion.load()).has_value(),
            "Late capture incorrectly consumed pending reload");
        RebuildForConfig(testConfig);
        verifyGeneration(2, 2);
        std::cout << "PASS: late capture before reload and complete reload\n";

        auto c = capture(baseline);
        verifyGeneration(2, 3);
        std::cout << "PASS: late capture after reload\n";

        // Force a completed rebuild between the initial replacement creation
        // and acquisition of the publication lock in CreatePixelShaderHook.
        creationCallback = [&] {
            testConfig.params.near_black_recovery = 0.007;
            testConfig.version = 3;
            RebuildForConfig(testConfig);
        };
        callbackAfter = 2;
        auto d = capture(baseline);
        verifyGeneration(3, 4);
        std::cout << "PASS: rebuild interleaved with capture\n";

        testConfig.params.near_black_recovery = 0.008;
        testConfig.version = 4;
        const auto previous = LoadGeneration();
        failAfter = 2;
        RebuildForConfig(testConfig);
        require(LoadGeneration() == previous, "Failed reload replaced previous generation");
        verifyGeneration(3, 4);
        require(Config().ConsumePending(g_appliedConfigVersion.load()).has_value(),
            "Failed reload consumed pending config");
        RebuildForConfig(testConfig);
        verifyGeneration(4, 4);
        std::cout << "PASS: failed reload preserves generation and retries\n";

        // Failure while adapting a late capture to the published generation
        // must neither register that target nor claim the pending version.
        testConfig.params.near_black_recovery = 0.009;
        testConfig.version = 5;
        const auto beforeCaptureFailure = LoadGeneration();
        failAfter = 3;
        auto failed = capture(baseline);
        require(LoadGeneration() == beforeCaptureFailure, "Failed capture published a generation");
        require(g_targets.size() == 4, "Failed capture registered an incomplete target");
        verifyGeneration(4, 4);
        RebuildForConfig(testConfig);
        verifyGeneration(5, 4);
        std::cout << "PASS: failed late replacement preserves complete generation\n";

        auto untouched = capture(legacy);
        verifyGeneration(5, 4);
        require(createdBytes.at(untouched.Get()) == legacy, "Legacy changed");
        std::cout << "PASS: Legacy left untouched\n";

        // No actual vtables were modified in this harness.
        g_deviceVtables.clear();
        PublishGeneration(std::make_shared<Generation>());
        g_targets.clear();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
