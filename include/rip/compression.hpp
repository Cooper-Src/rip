#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rip::compression
{

constexpr std::uint8_t RIPC_VERSION = 8;

enum class CompressionLevel : std::uint8_t
{
    Fast = 1,
    Balanced = 5,
    Maximum = 9
};

constexpr CompressionLevel RIPC_DEFAULT_LEVEL =
    CompressionLevel::Balanced;

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