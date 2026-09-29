#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rip
{

    std::uint32_t crc32(std::span<const std::byte> data);

} // namespace rip