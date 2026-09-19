#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace blackcrush {

struct Parameters {
    // User-facing restoration controls.
    double shadow_boost = 0.0;        // Luminance-based shadow recovery strength.
    double shadow_range = 0.026;      // Luminance range affected by ShadowBoost.
    std::uint32_t shadow_fade = 3;    // 1..4: higher = effect fades faster away from black.
    double near_black_range = 0.040;      // Per-channel range affected by near-black shaping.
    double near_black_detail = 0.0;      // Zero-preserving local contrast/detail expansion.
    double near_black_recovery = 0.005;  // Protected lift for values above absolute black.
    double pure_black_protection = 0.0005;// Ramp distance from 0 before recovery reaches full strength.
    double black_floor_lift = 0.0;        // Advanced true black-floor lift. Keep 0 to guarantee 0 -> 0.
};

struct DxbcInfo {
    std::uint32_t total_size{};
    std::vector<std::uint32_t> chunk_offsets;
    std::vector<std::array<char, 4>> chunk_tags;
    std::vector<std::uint32_t> chunk_sizes;
    std::uint32_t shex_offset{};
    std::uint32_t shex_size{};
    std::uint32_t shex_version{};
    std::uint32_t shex_token_count{};
    std::uint32_t instruction_count{};
    std::uint32_t dcl_temps{};
    std::vector<std::uint8_t> stat_bytes;
};

class PatchError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Structural matcher reference values. These are used only to build a safe
// matcher template; every user-facing immediate is wildcarded. Runtime defaults
// are IPS-SDR and are defined independently in Config.cpp.
constexpr double kBaseShadowBoost = 2.0;
constexpr double kBaseShadowRange = 0.026;
constexpr std::uint32_t kBaseShadowFade = 4;
constexpr double kBaseNearBlackRange = 0.026;
constexpr double kBaseNearBlackDetail = 0.75;
constexpr double kBaseNearBlackRecovery = 0.0025;
constexpr double kBasePureBlackProtection = 0.0005;
constexpr double kBaseBlackFloorLift = 0.0;

constexpr std::uint32_t kExpectedDclTemps = 5;

void validate_parameters(const Parameters& params);
std::array<std::uint8_t, 16> calculate_dxbc_checksum(std::span<const std::uint8_t> blob);
bool verify_dxbc_checksum(std::span<const std::uint8_t> blob);
DxbcInfo parse_dxbc_info(std::span<const std::uint8_t> blob);
std::vector<std::uint8_t> patch_shader(std::span<const std::uint8_t> blob, const Parameters& params);

} // namespace blackcrush
