#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rip::compression
{

constexpr std::uint8_t RIPC_VERSION = 4;

bool compress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

bool decompress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

} // namespace rip::compression