#include "BlackRestoration/DxbcPatcher.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>

namespace blackcrush {
namespace {

using Bytes = std::vector<std::uint8_t>;
using Words = std::vector<std::uint32_t>;

constexpr std::array<std::uint32_t, 3> kLumaWeightsBits{
    0x3E59C66Du, 0x3F3714BAu, 0x3D93CD57u
};

constexpr std::array<std::uint32_t, 64> kMd5S = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5,9,14,20, 5,9,14,20, 5,9,14,20, 5,9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xD76AA478u,0xE8C7B756u,0x242070DBu,0xC1BDCEEEu,
    0xF57C0FAFu,0x4787C62Au,0xA8304613u,0xFD469501u,
    0x698098D8u,0x8B44F7AFu,0xFFFF5BB1u,0x895CD7BEu,
    0x6B901122u,0xFD987193u,0xA679438Eu,0x49B40821u,
    0xF61E2562u,0xC040B340u,0x265E5A51u,0xE9B6C7AAu,
    0xD62F105Du,0x02441453u,0xD8A1E681u,0xE7D3FBC8u,
    0x21E1CDE6u,0xC33707D6u,0xF4D50D87u,0x455A14EDu,
    0xA9E3E905u,0xFCEFA3F8u,0x676F02D9u,0x8D2A4C8Au,
    0xFFFA3942u,0x8771F681u,0x6D9D6122u,0xFDE5380Cu,
    0xA4BEEA44u,0x4BDECFA9u,0xF6BB4B60u,0xBEBFBC70u,
    0x289B7EC6u,0xEAA127FAu,0xD4EF3085u,0x04881D05u,
    0xD9D4D039u,0xE6DB99E5u,
    0x1FA27CF8u,0xC4AC5665u,0xF4292244u,0x432AFF97u,
    0xAB9423A7u,0xFC93A039u,0x655B59C3u,0x8F0CCC92u,
    0xFFEFF47Du,0x85845DD1u,0x6FA87E4Fu,0xFE2CE6E0u,
    0xA3014314u,0x4E0811A1u,0xF7537E82u,0xBD3AF235u,
    0x2AD7D2BBu,0xEB86D391u
};

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        throw PatchError("read_u32 out of range");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

void write_u32(std::span<std::uint8_t> data, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > data.size()) {
        throw PatchError("write_u32 out of range");
    }
    data[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::uint32_t f32_bits(double value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    return std::bit_cast<std::uint32_t>(static_cast<float>(value));
}

double bits_f32(std::uint32_t value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    return static_cast<double>(std::bit_cast<float>(value));
}

Bytes pack_words(std::span<const std::uint32_t> words) {
    Bytes out(words.size() * 4u);
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_u32(out, i * 4u, words[i]);
    }
    return out;
}

std::uint32_t rol32(std::uint32_t value, std::uint32_t amount) {
    return std::rotl(value, static_cast<int>(amount));
}

void md5_transform(std::array<std::uint32_t, 4>& state, std::span<const std::uint8_t> block) {
    if (block.size() != 64u) {
        throw PatchError("MD5 transform requires exactly 64 bytes");
    }

    std::array<std::uint32_t, 16> m{};
    for (std::size_t i = 0; i < m.size(); ++i) {
        m[i] = read_u32(block, i * 4u);
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];

    for (std::uint32_t i = 0; i < 64u; ++i) {
        std::uint32_t f{};
        std::uint32_t g{};
        if (i < 16u) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32u) {
            f = (d & b) | ((~d) & c);
            g = (5u * i + 1u) % 16u;
        } else if (i < 48u) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) % 16u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * i) % 16u;
        }

        const auto new_b = b + rol32(a + f + kMd5K[i] + m[g], kMd5S[i]);
        a = d;
        d = c;
        c = b;
        b = new_b;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

Bytes build_luma_shadow_block(double shadow_boost, double shadow_range, std::uint32_t shadow_fade) {
    if (!(shadow_range > 0.0)) {
        throw PatchError("shadow_range must be > 0");
    }
    if (shadow_fade < 1u || shadow_fade > 4u) {
        throw PatchError("shadow_fade must be in [1, 4]");
    }

    // r3.x holds (L - ShadowRange), clamped so only values below the range survive.
    // We have three same-size MUL slots. By replacing unused r3 sources with immediate 1.0,
    // the same DXBC topology can realize integer powers 1..4 without changing instruction count.
    // Odd powers need a negative coefficient because (L-T)^p = -(T-L)^p for odd p.
    const double sign = (shadow_fade % 2u == 0u) ? 1.0 : -1.0;
    const double coeff = sign * shadow_boost / std::pow(shadow_range, static_cast<double>(shadow_fade));

    constexpr std::uint32_t kRegScalar = 0x0010003Au;
    constexpr std::uint32_t kImmediateScalar = 0x00004001u;
    const auto one_bits = f32_bits(1.0);

    const auto source_token = [](bool active) { return active ? kRegScalar : kImmediateScalar; };
    const auto source_value = [one_bits](bool active) { return active ? 0x00000003u : one_bits; };

    const bool stage1 = shadow_fade >= 2u;
    const bool stage2 = shadow_fade >= 3u;
    const bool stage3 = shadow_fade >= 4u;

    Words words = {
        0x0A000010u, 0x00100082u, 0x00000003u,
        0x00100246u, 0x00000000u,
        0x00004002u,
        kLumaWeightsBits[0], kLumaWeightsBits[1], kLumaWeightsBits[2], 0x00000000u,

        0x07000000u, 0x00100082u, 0x00000003u,
        0x0010003Au, 0x00000003u,
        0x00004001u, f32_bits(-shadow_range),

        0x07000033u, 0x00100082u, 0x00000003u,
        0x0010003Au, 0x00000003u,
        0x00004001u, 0x00000000u,

        0x07000038u, 0x00100082u, 0x00000004u,
        0x0010003Au, 0x00000003u,
        source_token(stage1), source_value(stage1),

        0x07000038u, 0x00100082u, 0x00000004u,
        0x0010003Au, 0x00000004u,
        source_token(stage2), source_value(stage2),

        0x07000038u, 0x00100082u, 0x00000004u,
        0x0010003Au, 0x00000004u,
        source_token(stage3), source_value(stage3),

        0x07000038u, 0x00100082u, 0x00000004u,
        0x0010003Au, 0x00000004u,
        0x00004001u, f32_bits(coeff),

        0x09000032u, 0x00100072u, 0x00000000u,
        0x00100246u, 0x00000000u,
        0x0010003Au, 0x00000004u,
        0x00100246u, 0x00000000u,
    };
    return pack_words(words);
}

// Near-black shaping with true-black protection plus optional explicit floor lift.
// For x < T:
//   q = (1 - x/T)^2
//   t = clamp(x / PureBlackProtection, 0, 1)
//   protected = smoothstep(t) = t^2 * (3 - 2t)
//   x' = x + q * (x*NearBlackDetail + NearBlackRecovery*protected + BlackFloorLift)
//
// With BlackFloorLift=0 this guarantees x=0 -> 0 exactly. The smoothstep ramp
// has zero slope at both ends, so protected recovery enters without a derivative
// corner at x=0 or x=PureBlackProtection.
Bytes build_near_black_detail_block(
    double range,
    double detail,
    double recovery,
    double pure_black_protection,
    double black_floor_lift) {
    if (!(range > 0.0)) {
        throw PatchError("near_black_range must be > 0");
    }
    if (!(pure_black_protection > 0.0)) {
        throw PatchError("pure_black_protection must be > 0");
    }

    const double inv_t2 = 1.0 / (range * range);
    const double detail_coeff = detail * inv_t2;
    const double recovery_coeff = recovery * inv_t2;
    const double floor_coeff = black_floor_lift * inv_t2;
    const double inv_protection = 1.0 / pure_black_protection;

    const auto t_bits = f32_bits(-range);
    const auto detail_bits = f32_bits(detail_coeff);
    const auto recovery_bits = f32_bits(recovery_coeff);
    const auto floor_bits = f32_bits(floor_coeff);
    const auto inv_protection_bits = f32_bits(inv_protection);
    const auto zero_bits = f32_bits(0.0);
    const auto one_bits = f32_bits(1.0);

    Words words = {
        // r3.xyz = clamp(rgb / PureBlackProtection, 0, 1)
        0x0A000038u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000000u,
        0x00004002u,
        inv_protection_bits, inv_protection_bits, inv_protection_bits, inv_protection_bits,

        0x0A000034u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00004002u,
        zero_bits, zero_bits, zero_bits, zero_bits,

        0x0A000033u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00004002u,
        one_bits, one_bits, one_bits, one_bits,

        // smoothstep(t) = 3*t^2 - 2*t^3. Keep t^2 in r4 while forming t^3 in r3.
        0x07000038u, 0x00100072u, 0x00000004u,
        0x00100246u, 0x00000003u,
        0x00100246u, 0x00000003u,

        0x07000038u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00100246u, 0x00000004u,

        // r3 = -2*t^3 + t^2
        0x0C000032u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00004002u,
        f32_bits(-2.0), f32_bits(-2.0), f32_bits(-2.0), f32_bits(-2.0),
        0x00100246u, 0x00000004u,

        // r3 = 2*t^2 + r3 = smoothstep(t)
        0x0C000032u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000004u,
        0x00004002u,
        f32_bits(2.0), f32_bits(2.0), f32_bits(2.0), f32_bits(2.0),
        0x00100246u, 0x00000003u,

        // r3.xyz *= NearBlackRecovery / range^2
        0x0A000038u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00004002u,
        recovery_bits, recovery_bits, recovery_bits, recovery_bits,

        // r3.xyz = rgb * (NearBlackDetail / range^2) + protected recovery term
        0x0C000032u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000000u,
        0x00004002u,
        detail_bits, detail_bits, detail_bits, detail_bits,
        0x00100246u, 0x00000003u,

        // Optional real black-floor lift. Keep 0 to preserve absolute black.
        0x0A000000u, 0x00100072u, 0x00000003u,
        0x00100246u, 0x00000003u,
        0x00004002u,
        floor_bits, floor_bits, floor_bits, floor_bits,

        // r4.xyz = (range-rgb)^2. Computing q after smoothstep lets us reuse r4.
        0x0A000000u, 0x00100072u, 0x00000004u,
        0x00100246u, 0x00000000u,
        0x00004002u,
        t_bits, t_bits, t_bits, 0x00000000u,

        0x0A000033u, 0x00100072u, 0x00000004u,
        0x00100246u, 0x00000004u,
        0x00004002u,
        zero_bits, zero_bits, zero_bits, zero_bits,

        0x07000038u, 0x00100072u, 0x00000004u,
        0x00100246u, 0x00000004u,
        0x00100246u, 0x00000004u,

        // rgb += (range-rgb)^2 * combined coefficient
        0x09000032u, 0x00100072u, 0x00000000u,
        0x00100246u, 0x00000004u,
        0x00100246u, 0x00000003u,
        0x00100246u, 0x00000000u,
    };
    return pack_words(words);
}

const Bytes& base_luma_block() {
    static const Bytes block = build_luma_shadow_block(kBaseShadowBoost, kBaseShadowRange, kBaseShadowFade);
    return block;
}

const Bytes& base_near_black_block() {
    static const Bytes block = build_near_black_detail_block(
        kBaseNearBlackRange,
        kBaseNearBlackDetail,
        kBaseNearBlackRecovery,
        kBasePureBlackProtection,
        kBaseBlackFloorLift);
    return block;
}

struct BlockLocations {
    DxbcInfo info;
    std::size_t luma_abs{};
    std::size_t near_black_abs{};
};

bool word_is_wildcard(std::size_t index, std::span<const std::size_t> wildcard_words) {
    return std::find(wildcard_words.begin(), wildcard_words.end(), index) != wildcard_words.end();
}

bool match_block_template(
    std::span<const std::uint8_t> haystack,
    std::size_t offset,
    std::span<const std::uint8_t> pattern,
    std::span<const std::size_t> wildcard_words) {

    if ((offset % 4u) != 0u || (pattern.size() % 4u) != 0u || offset + pattern.size() > haystack.size()) {
        return false;
    }

    const auto word_count = pattern.size() / 4u;
    for (std::size_t i = 0; i < word_count; ++i) {
        if (word_is_wildcard(i, wildcard_words)) continue;
        if (read_u32(haystack, offset + i * 4u) != read_u32(pattern, i * 4u)) {
            return false;
        }
    }
    return true;
}

bool validate_luma_parameter_words(
    std::span<const std::uint8_t> shex,
    std::size_t luma_rel) {

    const double neg_gain_t = bits_f32(read_u32(shex, luma_rel + 16u * 4u));
    const double gain_coeff = bits_f32(read_u32(shex, luma_rel + 51u * 4u));

    const auto stage_is_active = [&](std::size_t token_word, std::size_t value_word) -> std::optional<bool> {
        const auto token = read_u32(shex, luma_rel + token_word * 4u);
        const auto value = read_u32(shex, luma_rel + value_word * 4u);
        if (token == 0x0010003Au && value == 0x00000003u) return true;
        if (token == 0x00004001u && value == f32_bits(1.0)) return false;
        return std::nullopt;
    };

    const auto stage1 = stage_is_active(29u, 30u);
    const auto stage2 = stage_is_active(36u, 37u);
    const auto stage3 = stage_is_active(43u, 44u);
    if (!stage1 || !stage2 || !stage3) return false;

    std::uint32_t shadow_fade = 0;
    if (!*stage1 && !*stage2 && !*stage3) shadow_fade = 1u;
    else if (*stage1 && !*stage2 && !*stage3) shadow_fade = 2u;
    else if (*stage1 && *stage2 && !*stage3) shadow_fade = 3u;
    else if (*stage1 && *stage2 && *stage3) shadow_fade = 4u;
    else return false;

    if (!std::isfinite(neg_gain_t) || !std::isfinite(gain_coeff)) return false;

    const double gain_t = -neg_gain_t;
    constexpr double kTolerance = 1e-6;
    if (gain_t < 0.005 - kTolerance || gain_t > 0.100 + kTolerance) return false;

    // Odd powers use a negative coefficient because the shader stores (L-T), not (T-L).
    if (shadow_fade % 2u == 0u) {
        if (gain_coeff < 0.0) return false;
    } else {
        if (gain_coeff > 0.0) return false;
    }

    const double recovered_shadow_boost = std::abs(gain_coeff) * std::pow(gain_t, static_cast<double>(shadow_fade));
    return recovered_shadow_boost >= -kTolerance && recovered_shadow_boost <= 4.0 + kTolerance;
}

bool validate_near_black_parameter_words(
    std::span<const std::uint8_t> shex,
    std::size_t near_black_rel) {

    const auto same4 = [&](std::size_t first) {
        const auto a = read_u32(shex, near_black_rel + first * 4u);
        return a == read_u32(shex, near_black_rel + (first + 1u) * 4u) &&
               a == read_u32(shex, near_black_rel + (first + 2u) * 4u) &&
               a == read_u32(shex, near_black_rel + (first + 3u) * 4u);
    };

    if (!same4(6u) || !same4(74u) || !same4(84u) || !same4(96u)) return false;

    const auto range0 = read_u32(shex, near_black_rel + 106u * 4u);
    if (range0 != read_u32(shex, near_black_rel + 107u * 4u) ||
        range0 != read_u32(shex, near_black_rel + 108u * 4u)) {
        return false;
    }

    const double inv_protection = bits_f32(read_u32(shex, near_black_rel + 6u * 4u));
    const double recovery_coeff = bits_f32(read_u32(shex, near_black_rel + 74u * 4u));
    const double detail_coeff = bits_f32(read_u32(shex, near_black_rel + 84u * 4u));
    const double floor_coeff = bits_f32(read_u32(shex, near_black_rel + 96u * 4u));
    const double neg_range = bits_f32(range0);

    if (!std::isfinite(inv_protection) || !std::isfinite(recovery_coeff) ||
        !std::isfinite(detail_coeff) || !std::isfinite(floor_coeff) || !std::isfinite(neg_range)) {
        return false;
    }
    if (!(inv_protection > 0.0) || recovery_coeff < 0.0 || detail_coeff < 0.0 || floor_coeff < 0.0) {
        return false;
    }

    const double range = -neg_range;
    const double protection = 1.0 / inv_protection;
    constexpr double kTolerance = 1e-6;
    if (range < 0.005 - kTolerance || range > 0.100 + kTolerance ||
        protection < 0.00005 - kTolerance || protection > 0.005 + kTolerance ||
        protection > range + kTolerance) {
        return false;
    }

    const double range2 = range * range;
    const double detail = detail_coeff * range2;
    const double recovery = recovery_coeff * range2;
    const double floor_lift = floor_coeff * range2;
    return detail >= -kTolerance && detail <= 2.5 + kTolerance &&
           recovery >= -kTolerance && recovery <= 0.020 + kTolerance &&
           floor_lift >= -kTolerance && floor_lift <= 0.020 + kTolerance;
}

BlockLocations find_modern_blocks(std::span<const std::uint8_t> blob) {
    auto info = parse_dxbc_info(blob);
    if (info.dcl_temps != kExpectedDclTemps) {
        std::ostringstream ss;
        ss << "DCL_TEMPS=" << info.dcl_temps << "; expected " << kExpectedDclTemps;
        throw PatchError(ss.str());
    }

    const auto start = static_cast<std::size_t>(info.shex_offset) + 16u;
    const auto end = static_cast<std::size_t>(info.shex_offset) + 8u + info.shex_size;
    const auto shex = blob.subspan(start, end - start);

    const auto& luma = base_luma_block();
    const auto& near_black = base_near_black_block();

    constexpr std::array<std::size_t, 8> kLumaWildcards{
        16u,                  // ShadowRange immediate
        29u, 30u,            // ShadowFade stage 1 source
        36u, 37u,            // ShadowFade stage 2 source
        43u, 44u,            // ShadowFade stage 3 source
        51u                   // ShadowBoost/range/fade coefficient
    };

    // Only the user-facing immediates are wildcarded. Smoothstep constants,
    // opcodes, registers, masks and instruction lengths remain structural anchors.
    constexpr std::array<std::size_t, 19> kNearBlackWildcards{
        6u, 7u, 8u, 9u,          // 1 / PureBlackProtection
        74u, 75u, 76u, 77u,      // NearBlackRecovery / range^2
        84u, 85u, 86u, 87u,      // NearBlackDetail / range^2
        96u, 97u, 98u, 99u,      // BlackFloorLift / range^2
        106u, 107u, 108u          // -NearBlackRange
    };

    std::size_t match_count = 0;
    std::size_t matched_luma_rel = 0;
    std::size_t matched_near_black_rel = 0;

    const auto combined_size = luma.size() + near_black.size();
    for (std::size_t luma_rel = 0; luma_rel + combined_size <= shex.size(); luma_rel += 4u) {
        const auto near_black_rel = luma_rel + luma.size();
        if (!match_block_template(shex, luma_rel, luma, kLumaWildcards)) continue;
        if (!match_block_template(shex, near_black_rel, near_black, kNearBlackWildcards)) continue;
        if (!validate_luma_parameter_words(shex, luma_rel)) continue;
        if (!validate_near_black_parameter_words(shex, near_black_rel)) continue;

        ++match_count;
        matched_luma_rel = luma_rel;
        matched_near_black_rel = near_black_rel;
    }

    if (match_count != 1u) {
        std::ostringstream ss;
        ss << "Expected exactly one modern LE2 Black Restoration topology, found " << match_count;
        throw PatchError(ss.str());
    }

    return {std::move(info), start + matched_luma_rel, start + matched_near_black_rel};
}

void compare_container_after_in_place_patch(
    std::span<const std::uint8_t> before,
    std::span<const std::uint8_t> after) {

    const auto old_info = parse_dxbc_info(before);
    const auto new_info = parse_dxbc_info(after);

    if (after.size() != before.size() || new_info.total_size != old_info.total_size) {
        throw PatchError("DXBC size changed during in-place parameter patch");
    }
    if (old_info.chunk_offsets != new_info.chunk_offsets ||
        old_info.chunk_tags != new_info.chunk_tags ||
        old_info.chunk_sizes != new_info.chunk_sizes) {
        throw PatchError("DXBC chunk layout changed during in-place parameter patch");
    }
    if (new_info.shex_offset != old_info.shex_offset ||
        new_info.shex_size != old_info.shex_size ||
        new_info.shex_version != old_info.shex_version ||
        new_info.shex_token_count != old_info.shex_token_count ||
        new_info.instruction_count != old_info.instruction_count ||
        new_info.dcl_temps != old_info.dcl_temps) {
        throw PatchError("SHEX structure changed during in-place parameter patch");
    }
    if (new_info.stat_bytes != old_info.stat_bytes) {
        throw PatchError("STAT chunk changed during in-place parameter patch");
    }
}

} // namespace

void validate_parameters(const Parameters& p) {
    const auto within = [](double value, double lo, double hi) { return value >= lo && value <= hi; };
    if (!within(p.shadow_boost, 0.0, 4.0)) throw PatchError("ShadowBoost outside safety range [0, 4]");
    if (!within(p.shadow_range, 0.005, 0.100)) throw PatchError("ShadowRange outside safety range [0.005, 0.100]");
    if (p.shadow_fade < 1u || p.shadow_fade > 4u) throw PatchError("ShadowFade outside safety range [1, 4]");
    if (!within(p.near_black_range, 0.005, 0.100)) throw PatchError("NearBlackRange outside safety range [0.005, 0.100]");
    if (!within(p.near_black_detail, 0.0, 2.5)) throw PatchError("NearBlackDetail outside safety range [0, 2.5]");
    if (!within(p.near_black_recovery, 0.0, 0.020)) throw PatchError("NearBlackRecovery outside safety range [0, 0.020]");
    if (!within(p.pure_black_protection, 0.00005, 0.005)) throw PatchError("PureBlackProtection outside safety range [0.00005, 0.005]");
    if (p.pure_black_protection > p.near_black_range) throw PatchError("PureBlackProtection must not exceed NearBlackRange");
    if (!within(p.black_floor_lift, 0.0, 0.020)) throw PatchError("BlackFloorLift outside safety range [0, 0.020]");

    // Numerically guard monotonic tonal ordering for the complete piecewise curve.
    // This directly covers the protected recovery ramp and is intentionally conservative.
    const auto curve = [&p](double x) {
        if (x >= p.near_black_range) return x;
        const double q = 1.0 - x / p.near_black_range;
        const double t = std::clamp(x / p.pure_black_protection, 0.0, 1.0);
        const double protected_activation = t * t * (3.0 - 2.0 * t);
        return x + q * q * (
            x * p.near_black_detail +
            p.near_black_recovery * protected_activation +
            p.black_floor_lift);
    };
    constexpr int kSamples = 2048;
    double previous_x = 0.0;
    double previous_y = curve(0.0);
    for (int i = 1; i <= kSamples; ++i) {
        const double x = p.near_black_range * static_cast<double>(i) / static_cast<double>(kSamples);
        const double y = curve(x);
        const double dx = x - previous_x;
        if (y - previous_y < 0.02 * dx) {
            throw PatchError("Near-black settings compress/reverse tonal ordering too strongly; reduce NearBlackRecovery/BlackFloorLift/NearBlackDetail, or increase NearBlackRange/PureBlackProtection");
        }
        previous_x = x;
        previous_y = y;
    }

    if (p.black_floor_lift == 0.0 && curve(0.0) != 0.0) {
        throw PatchError("Internal error: absolute black is not preserved");
    }
}

std::array<std::uint8_t, 16> calculate_dxbc_checksum(std::span<const std::uint8_t> blob) {
    if (blob.size() < 20u || std::memcmp(blob.data(), "DXBC", 4u) != 0) {
        throw PatchError("Not a valid DXBC container");
    }

    const auto data = blob.subspan(20u);
    const auto size = data.size();
    const auto bit_count = static_cast<std::uint32_t>((size * 8u) & 0xFFFFFFFFu);
    std::array<std::uint32_t, 4> state{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u};

    const auto full_size = size & ~std::size_t{63};
    for (std::size_t offset = 0; offset < full_size; offset += 64u) {
        md5_transform(state, data.subspan(offset, 64u));
    }

    const auto tail = data.subspan(full_size);
    const auto last = tail.size();
    std::array<std::uint8_t, 64> block{};
    if (last >= 56u) {
        std::copy(tail.begin(), tail.end(), block.begin());
        block[last] = 0x80u;
        md5_transform(state, block);

        block.fill(0u);
        write_u32(block, 0u, bit_count);
        write_u32(block, 60u, (bit_count >> 2u) | 1u);
        md5_transform(state, block);
    } else {
        write_u32(block, 0u, bit_count);
        std::copy(tail.begin(), tail.end(), block.begin() + 4);
        block[4u + last] = 0x80u;
        write_u32(block, 60u, (bit_count >> 2u) | 1u);
        md5_transform(state, block);
    }

    std::array<std::uint8_t, 16> out{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        write_u32(out, i * 4u, state[i]);
    }
    return out;
}

bool verify_dxbc_checksum(std::span<const std::uint8_t> blob) {
    if (blob.size() < 20u) return false;
    const auto calculated = calculate_dxbc_checksum(blob);
    return std::equal(calculated.begin(), calculated.end(), blob.begin() + 4);
}

DxbcInfo parse_dxbc_info(std::span<const std::uint8_t> blob) {
    if (blob.size() < 32u || std::memcmp(blob.data(), "DXBC", 4u) != 0) {
        throw PatchError("File is not a DXBC container");
    }

    const auto declared_total = read_u32(blob, 24u);
    if (declared_total != blob.size()) {
        throw PatchError("DXBC total-size mismatch");
    }

    const auto chunk_count = read_u32(blob, 28u);
    const std::size_t table_end = 32u + static_cast<std::size_t>(chunk_count) * 4u;
    if (table_end > blob.size()) {
        throw PatchError("DXBC chunk table extends beyond file");
    }

    DxbcInfo info{};
    info.total_size = declared_total;
    info.chunk_offsets.reserve(chunk_count);
    info.chunk_tags.reserve(chunk_count);
    info.chunk_sizes.reserve(chunk_count);

    bool found_shex = false;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        const auto offset = read_u32(blob, 32u + static_cast<std::size_t>(i) * 4u);
        info.chunk_offsets.push_back(offset);
        if (static_cast<std::size_t>(offset) + 8u > blob.size()) {
            throw PatchError("DXBC chunk offset is out of range");
        }

        std::array<char, 4> tag{};
        std::memcpy(tag.data(), blob.data() + offset, 4u);
        const auto size = read_u32(blob, static_cast<std::size_t>(offset) + 4u);
        const auto end = static_cast<std::size_t>(offset) + 8u + size;
        if (end > blob.size()) {
            throw PatchError("DXBC chunk extends beyond file");
        }

        info.chunk_tags.push_back(tag);
        info.chunk_sizes.push_back(size);
        const bool is_shex = std::memcmp(tag.data(), "SHEX", 4u) == 0 || std::memcmp(tag.data(), "SHDR", 4u) == 0;
        if (is_shex) {
            if (found_shex) throw PatchError("Multiple SHEX/SHDR chunks found");
            if (size < 8u || (size % 4u) != 0u) throw PatchError("Invalid SHEX/SHDR chunk size");
            found_shex = true;
            info.shex_offset = offset;
            info.shex_size = size;
            info.shex_version = read_u32(blob, static_cast<std::size_t>(offset) + 8u);
            info.shex_token_count = read_u32(blob, static_cast<std::size_t>(offset) + 12u);
            if (info.shex_token_count * 4u != info.shex_size) {
                throw PatchError("SHEX token count mismatch");
            }
        } else if (std::memcmp(tag.data(), "STAT", 4u) == 0) {
            info.stat_bytes.assign(blob.begin() + offset, blob.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }

    if (!found_shex) throw PatchError("No SHEX/SHDR program chunk found");

    const auto instruction_bytes = static_cast<std::size_t>(info.shex_size) - 8u;
    const auto start = static_cast<std::size_t>(info.shex_offset) + 16u;
    const auto end = static_cast<std::size_t>(info.shex_offset) + 8u + info.shex_size;
    if (end > blob.size() || instruction_bytes % 4u != 0u) {
        throw PatchError("Invalid SHEX instruction range");
    }

    const auto word_count = instruction_bytes / 4u;
    if (word_count != info.shex_token_count - 2u) {
        throw PatchError("SHEX stream size mismatch");
    }

    std::size_t cursor_words = 0;
    std::uint32_t dcl_temps_count = 0;
    while (cursor_words < word_count) {
        const auto token = read_u32(blob, start + cursor_words * 4u);
        const auto opcode = token & 0x7FFu;
        const auto length = (token >> 24u) & 0x7Fu;
        if (length == 0u || cursor_words + length > word_count) {
            throw PatchError("Invalid SHEX instruction length");
        }
        if (opcode == 104u) {
            if (length != 2u) throw PatchError("Unexpected DCL_TEMPS length");
            ++dcl_temps_count;
            info.dcl_temps = read_u32(blob, start + (cursor_words + 1u) * 4u);
        }
        ++info.instruction_count;
        cursor_words += length;
    }

    if (dcl_temps_count != 1u) {
        throw PatchError("Expected exactly one DCL_TEMPS declaration");
    }
    return info;
}

std::vector<std::uint8_t> patch_shader(std::span<const std::uint8_t> blob, const Parameters& params) {
    validate_parameters(params);
    if (!verify_dxbc_checksum(blob)) {
        throw PatchError("Baseline DXBC checksum is invalid");
    }

    const auto locations = find_modern_blocks(blob);
    const auto new_luma = build_luma_shadow_block(params.shadow_boost, params.shadow_range, params.shadow_fade);
    const auto new_near_black = build_near_black_detail_block(
        params.near_black_range,
        params.near_black_detail,
        params.near_black_recovery,
        params.pure_black_protection,
        params.black_floor_lift);

    if (new_luma.size() != base_luma_block().size() ||
        new_near_black.size() != base_near_black_block().size()) {
        throw PatchError("Generated parameter block changed topology length");
    }

    Bytes out(blob.begin(), blob.end());
    std::copy(new_luma.begin(), new_luma.end(),
              out.begin() + static_cast<std::ptrdiff_t>(locations.luma_abs));
    std::copy(new_near_black.begin(), new_near_black.end(),
              out.begin() + static_cast<std::ptrdiff_t>(locations.near_black_abs));

    std::fill(out.begin() + 4, out.begin() + 20, std::uint8_t{0});
    const auto checksum = calculate_dxbc_checksum(out);
    std::copy(checksum.begin(), checksum.end(), out.begin() + 4);

    if (!verify_dxbc_checksum(out)) throw PatchError("Generated DXBC checksum failed validation");
    compare_container_after_in_place_patch(blob, out);
    return out;
}

} // namespace blackcrush
