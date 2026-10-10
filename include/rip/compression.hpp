#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rip::compression
{

constexpr std::uint8_t RIPC_VERSION = 10;

enum class CompressionLevel : std::uint8_t
{
    Auto = 0,
    Fast = 1,
    Balanced = 5,
    Maximum = 9
};

// Auto evaluates each search profile and returns the smallest final stream.
constexpr CompressionLevel RIPC_DEFAULT_LEVEL =
    CompressionLevel::Auto;

bool compress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr,
    CompressionLevel level = RIPC_DEFAULT_LEVEL);

bool decompress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

} // namespace rip::compression