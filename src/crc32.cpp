#include "rip/crc32.hpp"

namespace rip
{

    std::uint32_t crc32(std::span<const std::byte> data)
    {
        std::uint32_t crc = 0xFFFFFFFFu;

        for (const std::byte byte : data)
        {
            crc ^= static_cast<std::uint8_t>(byte);

            for (int bit = 0; bit < 8; ++bit)
            {
                if (crc & 1u)
                {
                    crc = (crc >> 1u) ^ 0xEDB88320u;
                }
                else
                {
                    crc >>= 1u;
                }
            }
        }

        return ~crc;
    }

} // namespace rip