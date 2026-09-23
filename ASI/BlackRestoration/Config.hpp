#pragma once

#include "BlackRestoration/DxbcPatcher.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace BlackRestoration {

struct ConfigSnapshot {
    blackcrush::Parameters params{};
    std::uint64_t version = 0;
};

struct HotkeyConfig {
    bool enabled = true;
    int decreaseKey = 0;
    int increaseKey = 0;
    int actionKey = 0;
    int bypassKey = 0;
};

class ConfigManager final {
public:
    bool Initialize();
    void Shutdown();

    [[nodiscard]] ConfigSnapshot Current() const;
    [[nodiscard]] std::optional<ConfigSnapshot> ConsumePending(std::uint64_t appliedVersion) const;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
    [[nodiscard]] const std::filesystem::path& ProfilePath() const noexcept { return profilePath_; }

private:
    static std::filesystem::path ResolveConfigPath();
    static std::optional<blackcrush::Parameters> ParseFile(const std::filesystem::path& path, bool fileRequired);
    static std::optional<HotkeyConfig> ParseHotkeys(const std::filesystem::path& path);
    static bool WriteProfileFile(const std::filesystem::path& path, const blackcrush::Parameters& params);
    void WatchLoop(std::stop_token stopToken);
    bool TryReload();
    bool SaveProfile();
    bool LoadProfile();
    void PollHotkeys();
    void ApplyHotkeyParameters(const blackcrush::Parameters& params, const std::string& action);
    void ResetHotkeyState() noexcept;

    std::filesystem::path path_;
    std::filesystem::path profilePath_;
    mutable std::mutex mutex_;
    ConfigSnapshot current_{};
    std::jthread watcher_;
    std::filesystem::file_time_type lastWrite_{};
    bool haveLastWrite_ = false;

    // tunedParams_ is never destroyed by SDR comparison or bypass, so the
    // comparison hotkey can always return to the user's current tuning.
    HotkeyConfig hotkeys_{};
    bool fixEnabled_ = true;
    bool compareSdr_ = false;
    blackcrush::Parameters tunedParams_{};
    bool decreaseDown_ = false;
    bool increaseDown_ = false;
    bool actionDown_ = false;
    bool bypassDown_ = false;
};

ConfigManager& Config();

} // namespace BlackRestoration
