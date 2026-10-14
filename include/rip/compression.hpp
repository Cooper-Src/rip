#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rip::compression
{

constexpr std::uint8_t RIPC_VERSION = 11;

enum class CompressionLevel : std::uint8_t
{
    Auto = 0,
    Fast = 1,
    Balanced = 5,
    Maximum = 9,
    Ultra = 12
};

// Auto evaluates each search profile and returns the smallest final stream.
constexpr CompressionLevel RIPC_DEFAULT_LEVEL =
    CompressionLevel::Auto;

using CompressionProgressCallback =
    std::function<void(
        std::uint64_t bytes_processed,
        std::uint64_t bytes_total,
        std::string_view phase)>;

bool compress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr,
    CompressionLevel level = RIPC_DEFAULT_LEVEL,
    CompressionProgressCallback progress = {});

bool decompress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

} // namespace rip::compression