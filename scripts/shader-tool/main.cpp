#include "BlackRestoration/DxbcPatcher.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open input: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot open output: " + path.string());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Failed writing output: " + path.string());
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

blackcrush::Parameters read_parameters(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open preset INI: " + path.string());

    std::map<std::string, std::string> values;
    bool in_section = false;
    for (std::string line; std::getline(stream, line);) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            in_section = line == "[LE2BlackRestoration]";
            continue;
        }
        if (!in_section) continue;
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        values[trim(line.substr(0, split))] = trim(line.substr(split + 1));
    }

    const auto required = [&values](const char* key) -> const std::string& {
        const auto found = values.find(key);
        if (found == values.end()) throw std::runtime_error(std::string("Missing required INI key: ") + key);
        return found->second;
    };
    const auto number = [&required](const char* key) {
        const auto& text = required(key);
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) throw std::runtime_error(std::string("Invalid numeric INI value: ") + key);
        return value;
    };

    blackcrush::Parameters result{};
    result.shadow_boost = number("ShadowBoost");
    result.shadow_range = number("ShadowRange");
    {
        const auto& text = required("ShadowFade");
        std::size_t used = 0;
        const auto value = std::stoul(text, &used);
        if (used != text.size()) throw std::runtime_error("Invalid integer INI value: ShadowFade");
        result.shadow_fade = static_cast<std::uint32_t>(value);
    }
    result.near_black_range = number("NearBlackRange");
    result.near_black_detail = number("NearBlackDetail");
    result.near_black_recovery = number("NearBlackRecovery");
    result.pure_black_protection = number("PureBlackProtection");
    result.black_floor_lift = number("BlackFloorLift");
    blackcrush::validate_parameters(result);
    return result;
}

std::vector<fs::path> shader_files(const fs::path& directory) {
    if (!fs::is_directory(directory)) throw std::runtime_error("Shader directory not found: " + directory.string());
    const std::regex name_pattern(R"(^GlobalShader-([0-9]+)-.+\.m3gs$)", std::regex::icase);
    std::vector<fs::path> files;
    std::set<unsigned long> indices;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::smatch match;
        const auto name = entry.path().filename().string();
        if (entry.path().extension() != ".m3gs") continue;
        if (!std::regex_match(name, match, name_pattern)) {
            throw std::runtime_error("Unexpected M3GS filename: " + name);
        }
        const auto index = std::stoul(match[1].str());
        if (!indices.insert(index).second) throw std::runtime_error("Duplicate shader index: " + std::to_string(index));
        files.push_back(entry.path());
    }
    if (files.size() != 32) {
        throw std::runtime_error("Expected exactly 32 GlobalShader-*.m3gs files, found " + std::to_string(files.size()));
    }
    std::sort(files.begin(), files.end());
    return files;
}

void inspect(const fs::path& file, bool require_modern) {
    const auto bytes = read_file(file);
    if (!blackcrush::verify_dxbc_checksum(bytes)) throw std::runtime_error("Invalid DXBC checksum: " + file.string());
    const auto info = blackcrush::parse_dxbc_info(bytes);
    if (require_modern) {
        (void)blackcrush::patch_shader(bytes, blackcrush::Parameters{});
    }
    std::cout << file.filename().string()
              << "\tsize=" << bytes.size()
              << "\tchunks=" << info.chunk_offsets.size()
              << "\tshex=" << info.shex_size
              << "\ttokens=" << info.shex_token_count
              << "\tinstructions=" << info.instruction_count
              << "\tdcl_temps=" << info.dcl_temps << '\n';
}

void generate(const fs::path& baseline, const fs::path& ini, const fs::path& output) {
    const auto params = read_parameters(ini);
    const auto files = shader_files(baseline);
    if (fs::exists(output) && !fs::is_empty(output)) {
        throw std::runtime_error("Output directory must be absent or empty: " + output.string());
    }
    fs::create_directories(output);
    for (const auto& file : files) {
        const auto before = read_file(file);
        const auto after = blackcrush::patch_shader(before, params);
        if (after.size() != before.size()) throw std::runtime_error("Unexpected output size: " + file.string());
        write_file(output / file.filename(), after);
        std::cout << "generated\t" << file.filename().string() << '\n';
    }
}

void usage() {
    std::cerr << "Usage:\n"
              << "  le2br-shader-tool inspect <file> [--modern]\n"
              << "  le2br-shader-tool inspect-dir <directory> [--modern]\n"
              << "  le2br-shader-tool generate <baseline-directory> <preset.ini> <output-directory>\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "inspect") {
            inspect(argv[2], argc == 4 && std::string(argv[3]) == "--modern");
            return 0;
        }
        if (argc >= 3 && std::string(argv[1]) == "inspect-dir") {
            const bool modern = argc == 4 && std::string(argv[3]) == "--modern";
            for (const auto& file : shader_files(argv[2])) inspect(file, modern);
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "generate") {
            generate(argv[2], argv[3], argv[4]);
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
