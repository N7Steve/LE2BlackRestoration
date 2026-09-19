#include "BlackRestoration/Config.hpp"

#include "Common/Base.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

namespace BlackRestoration {
namespace {
constexpr wchar_t kSection[] = L"LE2BlackRestoration";
constexpr wchar_t kHotkeysSection[] = L"Hotkeys";
constexpr wchar_t kDefaultDlc[] = L"DLC_MOD_LE2BlackRestoration";
constexpr wchar_t kFileName[] = L"LE2BlackRestoration.ini";
constexpr wchar_t kProfileFileName[] = L"LE2BlackRestoration_Profile.ini";
constexpr wchar_t kMissingIniValue[] = L"{LE2BR-MISSING-6F4A43B9}";

constexpr double kShadowBoostStep = 0.1;
constexpr double kRangeStep = 0.002;
constexpr double kNearBlackDetailStep = 0.1;
constexpr double kNearBlackRecoveryStep = 0.0005;
constexpr double kPureBlackProtectionStep = 0.0001;

std::wstring Trim(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }).base(), value.end());
    return value;
}

std::optional<std::wstring> ReadIniValue(
    const std::filesystem::path& path,
    const wchar_t* section,
    const wchar_t* key) {

    wchar_t buffer[128]{};
    const DWORD count = GetPrivateProfileStringW(
        section,
        key,
        kMissingIniValue,
        buffer,
        static_cast<DWORD>(std::size(buffer)),
        path.c_str());

    std::wstring value(buffer, count);
    if (value == kMissingIniValue) return std::nullopt;
    return Trim(std::move(value));
}

std::optional<double> ReadOptionalDoubleStrict(
    const std::filesystem::path& path,
    const wchar_t* key,
    bool& valid) {

    const auto raw = ReadIniValue(path, kSection, key);
    if (!raw) return std::nullopt;
    if (raw->empty()) {
        LEASI_WARN(L"empty [{}] {} value", kSection, key);
        valid = false;
        return std::nullopt;
    }

    wchar_t* end = nullptr;
    const double value = std::wcstod(raw->c_str(), &end);
    if (end == raw->c_str()) {
        LEASI_WARN(L"invalid [{}] {}='{}'", kSection, key, raw->c_str());
        valid = false;
        return std::nullopt;
    }
    while (*end != L'\0' && std::iswspace(*end)) ++end;
    if (*end != L'\0' || !std::isfinite(value)) {
        LEASI_WARN(L"invalid [{}] {}='{}'", kSection, key, raw->c_str());
        valid = false;
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint32_t> ReadOptionalUIntStrict(
    const std::filesystem::path& path,
    const wchar_t* key,
    bool& valid) {

    const auto value = ReadOptionalDoubleStrict(path, key, valid);
    if (!value || !valid) return std::nullopt;

    const double rounded = std::round(*value);
    if (std::abs(*value - rounded) > 1e-9 || rounded < 0.0 || rounded > 4294967295.0) {
        LEASI_WARN(L"[{}] {} must be an unsigned integer", kSection, key);
        valid = false;
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(rounded);
}

std::optional<bool> ParseBoolean(const std::wstring& raw) {
    std::wstring value = Trim(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });

    if (value == L"1" || value == L"true" || value == L"yes" || value == L"on") return true;
    if (value == L"0" || value == L"false" || value == L"no" || value == L"off") return false;
    return std::nullopt;
}

HotkeyConfig DefaultHotkeys() {
    HotkeyConfig h{};
    h.enabled = true;
    h.decreaseKey = VK_F6;
    h.increaseKey = VK_F7;
    h.actionKey = VK_F8;
    h.bypassKey = VK_F9;
    return h;
}

std::optional<int> ParseKeyName(std::wstring raw) {
    raw = Trim(std::move(raw));
    std::transform(raw.begin(), raw.end(), raw.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });

    raw.erase(std::remove_if(raw.begin(), raw.end(), [](wchar_t ch) {
        return ch == L' ' || ch == L'_' || ch == L'-';
    }), raw.end());

    if (raw.size() >= 2 && raw[0] == L'F') {
        wchar_t* end = nullptr;
        const long number = std::wcstol(raw.c_str() + 1, &end, 10);
        if (end != raw.c_str() + 1 && *end == L'\0' && number >= 1 && number <= 24) {
            return VK_F1 + static_cast<int>(number - 1);
        }
    }

    if (raw.size() == 1 && raw[0] >= L'A' && raw[0] <= L'Z') {
        return static_cast<int>(raw[0]);
    }
    if (raw.size() == 1 && raw[0] >= L'0' && raw[0] <= L'9') {
        return static_cast<int>(raw[0]);
    }

    if (raw.rfind(L"NUMPAD", 0) == 0 && raw.size() == 7 && raw[6] >= L'0' && raw[6] <= L'9') {
        return VK_NUMPAD0 + static_cast<int>(raw[6] - L'0');
    }

    struct NamedKey {
        const wchar_t* name;
        int vk;
    };
    static constexpr NamedKey kNamedKeys[] = {
        {L"HOME", VK_HOME},
        {L"END", VK_END},
        {L"INSERT", VK_INSERT},
        {L"INS", VK_INSERT},
        {L"DELETE", VK_DELETE},
        {L"DEL", VK_DELETE},
        {L"PAGEUP", VK_PRIOR},
        {L"PGUP", VK_PRIOR},
        {L"PAGEDOWN", VK_NEXT},
        {L"PGDN", VK_NEXT},
        {L"UP", VK_UP},
        {L"DOWN", VK_DOWN},
        {L"LEFT", VK_LEFT},
        {L"RIGHT", VK_RIGHT},
    };

    for (const auto& named : kNamedKeys) {
        if (raw == named.name) return named.vk;
    }
    return std::nullopt;
}

std::string KeyName(int vk) {
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "Numpad" + std::to_string(vk - VK_NUMPAD0);
    }

    switch (vk) {
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    default: return "VK(" + std::to_string(vk) + ")";
    }
}

bool IsCurrentProcessForeground() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

bool IsDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

double RoundForStep(double value, double step) {
    return std::round(value / step) * step;
}

blackcrush::Parameters ReleaseDefaultParameters() {
    blackcrush::Parameters p{};
    p.shadow_boost = 0.0;
    p.shadow_range = 0.026;
    p.shadow_fade = 3;
    p.near_black_range = 0.040;
    p.near_black_detail = 0.0;
    p.near_black_recovery = 0.005;
    p.pure_black_protection = 0.0005;
    p.black_floor_lift = 0.0;
    return p;
}

blackcrush::Parameters NeutralParameters(const blackcrush::Parameters& shapeSource) {
    auto p = shapeSource;
    p.shadow_boost = 0.0;
    p.near_black_detail = 0.0;
    p.near_black_recovery = 0.0;
    p.black_floor_lift = 0.0;
    return p;
}

blackcrush::Parameters ClampParameters(blackcrush::Parameters p) {
    p.shadow_boost = std::clamp(p.shadow_boost, 0.0, 4.0);
    p.shadow_range = std::clamp(p.shadow_range, 0.005, 0.100);
    p.shadow_fade = std::clamp<std::uint32_t>(p.shadow_fade, 1u, 4u);
    p.near_black_range = std::clamp(p.near_black_range, 0.005, 0.100);
    p.near_black_detail = std::clamp(p.near_black_detail, 0.0, 2.5);
    p.near_black_recovery = std::clamp(p.near_black_recovery, 0.0, 0.020);
    p.pure_black_protection = std::clamp(p.pure_black_protection, 0.00005, 0.005);
    p.black_floor_lift = std::clamp(p.black_floor_lift, 0.0, 0.020);
    return p;
}

void LogParameters(const char* prefix, std::uint64_t version, const blackcrush::Parameters& p) {
    LEASI_INFO(
        "{} v{}: ShadowBoost={} ShadowRange={} ShadowFade={} NearBlackRange={} NearBlackDetail={} NearBlackRecovery={} PureBlackProtection={} BlackFloorLift={}",
        prefix, version, p.shadow_boost, p.shadow_range, p.shadow_fade,
        p.near_black_range, p.near_black_detail, p.near_black_recovery,
        p.pure_black_protection, p.black_floor_lift);
}
} // namespace

ConfigManager& Config() {
    static ConfigManager manager;
    return manager;
}

std::filesystem::path ConfigManager::ResolveConfigPath() {
    wchar_t exePath[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (len == 0 || len >= std::size(exePath)) {
        return std::filesystem::path{kFileName};
    }

    const auto win64 = std::filesystem::path(exePath).parent_path();
    const auto dlcRoot = (win64 / L".." / L".." / L"BIOGame" / L"DLC").lexically_normal();
    return dlcRoot / kDefaultDlc / kFileName;
}

std::optional<blackcrush::Parameters> ConfigManager::ParseFile(
    const std::filesystem::path& path,
    bool fileRequired) {

    if (!std::filesystem::exists(path)) {
        if (fileRequired) return std::nullopt;
        return ReleaseDefaultParameters();
    }

    // Missing keys deliberately inherit IPS-SDR release defaults. A key that is
    // present but malformed is rejected instead of silently changing behavior.
    auto p = ReleaseDefaultParameters();
    bool valid = true;

    if (const auto value = ReadOptionalDoubleStrict(path, L"ShadowBoost", valid)) {
        p.shadow_boost = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"ShadowRange", valid)) {
        p.shadow_range = *value;
    }
    if (const auto value = ReadOptionalUIntStrict(path, L"ShadowFade", valid)) {
        p.shadow_fade = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"NearBlackRange", valid)) {
        p.near_black_range = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"NearBlackDetail", valid)) {
        p.near_black_detail = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"NearBlackRecovery", valid)) {
        p.near_black_recovery = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"PureBlackProtection", valid)) {
        p.pure_black_protection = *value;
    }
    if (const auto value = ReadOptionalDoubleStrict(path, L"BlackFloorLift", valid)) {
        p.black_floor_lift = *value;
    }

    if (!valid) return std::nullopt;

    try {
        blackcrush::validate_parameters(p);
    } catch (const std::exception& e) {
        LEASI_WARN("invalid restoration parameters: {}", e.what());
        return std::nullopt;
    }
    return p;
}

std::optional<HotkeyConfig> ConfigManager::ParseHotkeys(const std::filesystem::path& path) {
    HotkeyConfig h = DefaultHotkeys();
    if (!std::filesystem::exists(path)) return h;

    if (const auto raw = ReadIniValue(path, kHotkeysSection, L"Enabled")) {
        const auto enabled = ParseBoolean(*raw);
        if (!enabled) {
            LEASI_WARN(L"invalid [Hotkeys] Enabled='{}'", raw->c_str());
            return std::nullopt;
        }
        h.enabled = *enabled;
    }

    struct Binding {
        const wchar_t* name;
        int* target;
    };
    Binding bindings[] = {
        {L"DecreaseKey", &h.decreaseKey},
        {L"IncreaseKey", &h.increaseKey},
        {L"ActionKey", &h.actionKey},
        {L"BypassKey", &h.bypassKey},
    };

    for (const auto& binding : bindings) {
        const auto raw = ReadIniValue(path, kHotkeysSection, binding.name);
        if (!raw) continue;

        const auto parsed = ParseKeyName(*raw);
        if (!parsed) {
            LEASI_WARN(L"invalid [Hotkeys] {}='{}'", binding.name, raw->c_str());
            return std::nullopt;
        }
        *binding.target = *parsed;
    }

    const int keys[] = {h.decreaseKey, h.increaseKey, h.actionKey, h.bypassKey};
    for (std::size_t i = 0; i < std::size(keys); ++i) {
        for (std::size_t j = i + 1; j < std::size(keys); ++j) {
            if (keys[i] == keys[j]) {
                LEASI_WARN("invalid [Hotkeys]: base keys must be unique");
                return std::nullopt;
            }
        }
    }

    return h;
}

bool ConfigManager::Initialize() {
    path_ = ResolveConfigPath();
    profilePath_ = path_.parent_path() / kProfileFileName;

    const auto initial = ParseFile(path_, false);
    const auto initialHotkeys = ParseHotkeys(path_);
    if (!initial || !initialHotkeys) {
        LEASI_ERROR(L"invalid initial config {}; using IPS-SDR defaults and default hotkeys", path_.c_str());
        current_.params = ReleaseDefaultParameters();
        hotkeys_ = DefaultHotkeys();
    } else {
        current_.params = *initial;
        hotkeys_ = *initialHotkeys;
    }

    current_.version = 1;
    tunedParams_ = current_.params;
    fixEnabled_ = true;
    compareIpsSdr_ = false;
    ResetHotkeyState();

    std::error_code ec;
    if (std::filesystem::exists(path_, ec)) {
        lastWrite_ = std::filesystem::last_write_time(path_, ec);
        haveLastWrite_ = !ec;
    }

    LEASI_INFO(L"config: {}", path_.c_str());
    LEASI_INFO(L"saved profile: {}", profilePath_.c_str());
    LogParameters("parameters", current_.version, current_.params);
    LEASI_INFO(
        "hotkeys {}: Decrease={} Increase={} Action={} Bypass={} (exact modifiers required)",
        hotkeys_.enabled ? "enabled" : "disabled",
        KeyName(hotkeys_.decreaseKey),
        KeyName(hotkeys_.increaseKey),
        KeyName(hotkeys_.actionKey),
        KeyName(hotkeys_.bypassKey));

    watcher_ = std::jthread([this](std::stop_token token) { WatchLoop(token); });
    return true;
}

void ConfigManager::Shutdown() {
    if (watcher_.joinable()) {
        watcher_.request_stop();
        watcher_.join();
    }
}

ConfigSnapshot ConfigManager::Current() const {
    std::scoped_lock lock(mutex_);
    return current_;
}

std::optional<ConfigSnapshot> ConfigManager::ConsumePending(std::uint64_t appliedVersion) const {
    std::scoped_lock lock(mutex_);
    if (current_.version <= appliedVersion) return std::nullopt;
    return current_;
}

bool ConfigManager::TryReload() {
    const auto parsed = ParseFile(path_, true);
    const auto parsedHotkeys = ParseHotkeys(path_);
    if (!parsed || !parsedHotkeys) {
        LEASI_WARN(L"config change ignored: file is invalid or outside safety limits ({})", path_.c_str());
        return false;
    }

    std::scoped_lock lock(mutex_);
    hotkeys_ = *parsedHotkeys;
    tunedParams_ = *parsed;
    current_.params = *parsed;
    fixEnabled_ = true;
    compareIpsSdr_ = false;
    ResetHotkeyState();
    ++current_.version;
    LEASI_INFO(
        "config accepted v{}: ShadowBoost={} ShadowRange={} ShadowFade={} NearBlackRange={} NearBlackDetail={} NearBlackRecovery={} PureBlackProtection={} BlackFloorLift={} hotkeys={} Decrease={} Increase={} Action={} Bypass={}",
        current_.version,
        current_.params.shadow_boost,
        current_.params.shadow_range,
        current_.params.shadow_fade,
        current_.params.near_black_range,
        current_.params.near_black_detail,
        current_.params.near_black_recovery,
        current_.params.pure_black_protection,
        current_.params.black_floor_lift,
        hotkeys_.enabled ? "on" : "off",
        KeyName(hotkeys_.decreaseKey),
        KeyName(hotkeys_.increaseKey),
        KeyName(hotkeys_.actionKey),
        KeyName(hotkeys_.bypassKey));
    return true;
}

bool ConfigManager::WriteProfileFile(
    const std::filesystem::path& path,
    const blackcrush::Parameters& p) {

    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    out << std::setprecision(9);
    out << "[LE2BlackRestoration]\n";
    out << "; Saved runtime tuning profile. Load it with the configured Load Profile hotkey.\n";
    out << "ShadowBoost=" << p.shadow_boost << "\n";
    out << "ShadowRange=" << p.shadow_range << "\n";
    out << "ShadowFade=" << p.shadow_fade << "\n";
    out << "NearBlackRange=" << p.near_black_range << "\n";
    out << "NearBlackDetail=" << p.near_black_detail << "\n";
    out << "NearBlackRecovery=" << p.near_black_recovery << "\n";
    out << "PureBlackProtection=" << p.pure_black_protection << "\n";
    out << "BlackFloorLift=" << p.black_floor_lift << "\n";
    out.flush();
    return static_cast<bool>(out);
}

bool ConfigManager::SaveProfile() {
    blackcrush::Parameters saved;
    {
        std::scoped_lock lock(mutex_);
        saved = tunedParams_;
    }
    if (!WriteProfileFile(profilePath_, saved)) {
        LEASI_ERROR(L"failed to save profile: {}", profilePath_.c_str());
        return false;
    }
    LEASI_INFO(L"saved custom tuning profile: {}", profilePath_.c_str());
    LogParameters("saved profile", 0, saved);
    return true;
}

bool ConfigManager::LoadProfile() {
    const auto parsed = ParseFile(profilePath_, true);
    if (!parsed) {
        LEASI_WARN(L"saved profile missing/invalid: {}", profilePath_.c_str());
        return false;
    }
    ApplyHotkeyParameters(*parsed, "load saved profile");
    LEASI_INFO(L"loaded saved custom tuning profile: {}", profilePath_.c_str());
    return true;
}

void ConfigManager::ApplyHotkeyParameters(
    const blackcrush::Parameters& params,
    const std::string& action) {

    blackcrush::Parameters safe = ClampParameters(params);
    try {
        blackcrush::validate_parameters(safe);
    } catch (const std::exception& e) {
        LEASI_WARN("hotkey ignored ({}): {}", action, e.what());
        return;
    }

    std::scoped_lock lock(mutex_);
    tunedParams_ = safe;
    current_.params = safe;
    fixEnabled_ = true;
    compareIpsSdr_ = false;
    ++current_.version;
    LEASI_INFO(
        "hotkey {} -> v{}: ShadowBoost={} ShadowRange={} ShadowFade={} NearBlackRange={} NearBlackDetail={} NearBlackRecovery={} PureBlackProtection={} BlackFloorLift={}",
        action,
        current_.version,
        safe.shadow_boost,
        safe.shadow_range,
        safe.shadow_fade,
        safe.near_black_range,
        safe.near_black_detail,
        safe.near_black_recovery,
        safe.pure_black_protection,
        safe.black_floor_lift);
}

void ConfigManager::ResetHotkeyState() noexcept {
    decreaseDown_ = false;
    increaseDown_ = false;
    actionDown_ = false;
    bypassDown_ = false;
}

void ConfigManager::PollHotkeys() {
    HotkeyConfig hotkeys;
    {
        std::scoped_lock lock(mutex_);
        hotkeys = hotkeys_;
    }

    if (!hotkeys.enabled || !IsCurrentProcessForeground()) {
        ResetHotkeyState();
        return;
    }

    const bool decrease = IsDown(hotkeys.decreaseKey);
    const bool increase = IsDown(hotkeys.increaseKey);
    const bool action = IsDown(hotkeys.actionKey);
    const bool bypass = IsDown(hotkeys.bypassKey);

    const bool pressDecrease = decrease && !decreaseDown_;
    const bool pressIncrease = increase && !increaseDown_;
    const bool pressAction = action && !actionDown_;
    const bool pressBypass = bypass && !bypassDown_;

    decreaseDown_ = decrease;
    increaseDown_ = increase;
    actionDown_ = action;
    bypassDown_ = bypass;

    const bool shift = IsDown(VK_SHIFT);
    const bool ctrl = IsDown(VK_CONTROL);
    const bool alt = IsDown(VK_MENU);

    // Bypass is deliberately Shift + BypassKey only. Extra modifiers do nothing.
    if (pressBypass && shift && !ctrl && !alt) {
        std::scoped_lock lock(mutex_);
        if (fixEnabled_) {
            current_.params = NeutralParameters(compareIpsSdr_ ? ReleaseDefaultParameters() : tunedParams_);
            fixEnabled_ = false;
            ++current_.version;
            LEASI_INFO("hotkey Shift+{} -> effect OFF v{}", KeyName(hotkeys.bypassKey), current_.version);
        } else {
            current_.params = compareIpsSdr_ ? ReleaseDefaultParameters() : tunedParams_;
            fixEnabled_ = true;
            ++current_.version;
            LEASI_INFO(
                "hotkey Shift+{} -> effect ON v{} ({})",
                KeyName(hotkeys.bypassKey),
                current_.version,
                compareIpsSdr_ ? "IPS-SDR comparison" : "custom tuning");
        }
        return;
    }

    if (pressAction) {
        if (ctrl && shift && !alt) {
            LoadProfile();
            return;
        }
        if (ctrl && !shift && !alt) {
            SaveProfile();
            return;
        }
        if (shift && !ctrl && !alt) {
            std::scoped_lock lock(mutex_);
            fixEnabled_ = true;
            if (compareIpsSdr_) {
                current_.params = tunedParams_;
                compareIpsSdr_ = false;
                ++current_.version;
                LEASI_INFO(
                    "hotkey Shift+{} -> CUSTOM tuning v{} (IPS-SDR comparison OFF)",
                    KeyName(hotkeys.actionKey), current_.version);
            } else {
                current_.params = ReleaseDefaultParameters();
                compareIpsSdr_ = true;
                ++current_.version;
                LEASI_INFO(
                    "hotkey Shift+{} -> IPS-SDR v{} (custom tuning preserved)",
                    KeyName(hotkeys.actionKey), current_.version);
            }
            return;
        }
        if (!ctrl && !shift && !alt) {
            std::scoped_lock lock(mutex_);
            fixEnabled_ = true;
            tunedParams_ = ReleaseDefaultParameters();
            current_.params = tunedParams_;
            compareIpsSdr_ = false;
            ++current_.version;
            LEASI_INFO(
                "hotkey {} -> reset custom tuning to IPS-SDR v{}",
                KeyName(hotkeys.actionKey), current_.version);
            return;
        }
        return;
    }

    // Simultaneous decrease/increase presses are ambiguous and intentionally ignored.
    if (pressDecrease == pressIncrease) return;
    const double direction = pressIncrease ? 1.0 : -1.0;
    const std::string baseKey = KeyName(pressIncrease ? hotkeys.increaseKey : hotkeys.decreaseKey);

    blackcrush::Parameters p;
    {
        std::scoped_lock lock(mutex_);
        p = tunedParams_;
    }

    std::string actionText;
    if (!ctrl && !shift && !alt) {
        p.near_black_recovery = RoundForStep(
            p.near_black_recovery + direction * kNearBlackRecoveryStep,
            kNearBlackRecoveryStep);
        actionText = baseKey + (direction > 0
            ? " NearBlackRecovery +0.0005"
            : " NearBlackRecovery -0.0005");
    } else if (shift && !ctrl && !alt) {
        p.near_black_detail = RoundForStep(
            p.near_black_detail + direction * kNearBlackDetailStep,
            kNearBlackDetailStep);
        actionText = "Shift+" + baseKey + (direction > 0
            ? " NearBlackDetail +0.1"
            : " NearBlackDetail -0.1");
    } else if (alt && !ctrl && !shift) {
        p.pure_black_protection = RoundForStep(
            p.pure_black_protection + direction * kPureBlackProtectionStep,
            kPureBlackProtectionStep);
        actionText = "Alt+" + baseKey + (direction > 0
            ? " PureBlackProtection +0.0001"
            : " PureBlackProtection -0.0001");
    } else if (ctrl && !shift && !alt) {
        p.near_black_range = RoundForStep(
            p.near_black_range + direction * kRangeStep,
            kRangeStep);
        actionText = "Ctrl+" + baseKey + (direction > 0
            ? " NearBlackRange +0.002"
            : " NearBlackRange -0.002");
    } else if (ctrl && shift && !alt) {
        p.shadow_boost = RoundForStep(
            p.shadow_boost + direction * kShadowBoostStep,
            kShadowBoostStep);
        actionText = "Ctrl+Shift+" + baseKey + (direction > 0
            ? " ShadowBoost +0.1"
            : " ShadowBoost -0.1");
    } else if (ctrl && alt && !shift) {
        p.shadow_range = RoundForStep(
            p.shadow_range + direction * kRangeStep,
            kRangeStep);
        actionText = "Ctrl+Alt+" + baseKey + (direction > 0
            ? " ShadowRange +0.002"
            : " ShadowRange -0.002");
    } else if (ctrl && shift && alt) {
        const int next = static_cast<int>(p.shadow_fade) + (direction > 0 ? 1 : -1);
        p.shadow_fade = static_cast<std::uint32_t>(std::clamp(next, 1, 4));
        actionText = "Ctrl+Shift+Alt+" + baseKey + (direction > 0
            ? " ShadowFade +1"
            : " ShadowFade -1");
    } else {
        return;
    }

    ApplyHotkeyParameters(p, actionText);
}

void ConfigManager::WatchLoop(std::stop_token stopToken) {
    using namespace std::chrono_literals;
    auto nextFileCheck = std::chrono::steady_clock::now();

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(50ms);
        if (stopToken.stop_requested()) break;

        PollHotkeys();

        const auto now = std::chrono::steady_clock::now();
        if (now < nextFileCheck) continue;
        nextFileCheck = now + 250ms;

        std::error_code ec;
        if (!std::filesystem::exists(path_, ec) || ec) continue;
        const auto write = std::filesystem::last_write_time(path_, ec);
        if (ec) continue;

        if (!haveLastWrite_) {
            lastWrite_ = write;
            haveLastWrite_ = true;
            TryReload();
            continue;
        }
        if (write == lastWrite_) continue;

        lastWrite_ = write;
        std::this_thread::sleep_for(150ms);
        TryReload();
    }
}

} // namespace BlackRestoration
