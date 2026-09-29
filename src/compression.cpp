#include "rip/compression.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace rip::compression
{

namespace
{

constexpr char MAGIC[4] = {'R', 'P', 'C', '1'};

constexpr std::size_t WINDOW_SIZE = 65535;
constexpr std::size_t MAX_MATCH_LENGTH = 258;
constexpr std::size_t MIN_MATCH_LENGTH = 3;
constexpr std::size_t MAX_CHAIN_LENGTH = 64;

void set_error(
    std::string* error,
    const char* message)
{
    if (error)
    {
        *error = message;
    }
}

void write_u8(
    std::vector<std::byte>& output,
    std::uint8_t value)
{
    output.push_back(
        static_cast<std::byte>(value));
}

void write_u16(
    std::vector<std::byte>& output,
    std::uint16_t value)
{
    output.push_back(
        static_cast<std::byte>(
            value & 0xffu));

    output.push_back(
        static_cast<std::byte>(
            (value >> 8u) & 0xffu));
}

void write_u64(
    std::vector<std::byte>& output,
    std::uint64_t value)
{
    for (unsigned int i = 0; i < 8; ++i)
    {
        output.push_back(
            static_cast<std::byte>(
                (value >> (i * 8u)) & 0xffu));
    }
}

std::uint8_t read_u8(
    std::span<const std::byte> input,
    std::size_t& position)
{
    return static_cast<std::uint8_t>(
        input[position++]);
}

std::uint16_t read_u16(
    std::span<const std::byte> input,
    std::size_t& position)
{
    const std::uint16_t a =
        read_u8(input, position);

    const std::uint16_t b =
        read_u8(input, position);

    return static_cast<std::uint16_t>(
        a | (b << 8u));
}

std::uint64_t read_u64(
    std::span<const std::byte> input,
    std::size_t& position)
{
    std::uint64_t value = 0;

    for (unsigned int i = 0; i < 8; ++i)
    {
        value |=
            static_cast<std::uint64_t>(
                read_u8(input, position))
            << (i * 8u);
    }

    return value;
}

std::uint32_t hash3(
    const std::byte* data)
{
    const auto a =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(data[0]));

    const auto b =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(data[1]));

    const auto c =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(data[2]));

    std::uint32_t hash =
        a * 251u;

    hash ^= b * 911u;
    hash ^= c * 3571u;

    hash ^= hash >> 16u;

    return hash & 0xffffu;
}

} // namespace

bool compress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    if (input.size() >
        std::numeric_limits<std::uint64_t>::max())
    {
        set_error(
            error,
            "Input is too large.");

        return false;
    }

    output.reserve(
        input.size() + 32);

    // Header:
    // 4 bytes  magic
    // 1 byte   version
    // 1 byte   flags
    // 2 bytes  reserved
    // 8 bytes  original size

    output.insert(
        output.end(),
        reinterpret_cast<const std::byte*>(
            MAGIC),
        reinterpret_cast<const std::byte*>(
            MAGIC + 4));

    write_u8(
        output,
        RIPC_VERSION);

    write_u8(
        output,
        0);

    write_u16(
        output,
        0);

    write_u64(
        output,
        static_cast<std::uint64_t>(
            input.size()));

    const std::size_t size =
        input.size();

    if (size == 0)
    {
        return true;
    }

    std::array<int, 65536> head;
    head.fill(-1);

    std::vector<int> previous(
        size,
        -1);

    auto insert_position =
        [&](std::size_t position)
        {
            if (position + 2 >= size)
            {
                return;
            }

            const std::uint32_t hash =
                hash3(
                    input.data() + position);

            previous[position] =
                head[hash];

            head[hash] =
                static_cast<int>(
                    position);
        };

    std::size_t position = 0;

    while (position < size)
    {
        const std::size_t control_offset =
            output.size();

        output.push_back(
            static_cast<std::byte>(0));

        std::uint8_t control = 0;

        std::size_t token_count = 0;

        while (token_count < 8 &&
               position < size)
        {
            std::size_t best_length = 0;
            std::size_t best_distance = 0;

            if (position + 2 < size)
            {
                const std::uint32_t hash =
                    hash3(
                        input.data() + position);

                int candidate =
                    head[hash];

                std::size_t chain_count = 0;

                while (
                    candidate >= 0 &&
                    chain_count <
                        MAX_CHAIN_LENGTH)
                {
                    const std::size_t candidate_pos =
                        static_cast<std::size_t>(
                            candidate);

                    if (candidate_pos >= position)
                    {
                        break;
                    }

                    const std::size_t distance =
                        position - candidate_pos;

                    if (distance >
                        WINDOW_SIZE)
                    {
                        break;
                    }

                    std::size_t length = 0;

                    const std::size_t maximum =
                        std::min(
                            MAX_MATCH_LENGTH,
                            size - position);

                    while (
                        length < maximum &&
                        input[position + length] ==
                            input[candidate_pos + length])
                    {
                        ++length;
                    }

                    if (length > best_length)
                    {
                        best_length = length;
                        best_distance = distance;

                        if (length == maximum)
                        {
                            break;
                        }
                    }

                    candidate =
                        previous[candidate_pos];

                    ++chain_count;
                }
            }

            if (best_length >= MIN_MATCH_LENGTH)
            {
                control |=
                    static_cast<std::uint8_t>(
                        1u << token_count);

                write_u16(
                    output,
                    static_cast<std::uint16_t>(
                        best_distance));

                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        best_length -
                        MIN_MATCH_LENGTH));

                const std::size_t end =
                    position + best_length;

                while (position < end)
                {
                    insert_position(position);
                    ++position;
                }
            }
            else
            {
                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        input[position]));

                insert_position(position);

                ++position;
            }

            ++token_count;
        }

        output[control_offset] =
            static_cast<std::byte>(control);
    }

    return true;
}

bool decompress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    constexpr std::size_t HEADER_SIZE = 16;

    if (input.size() < HEADER_SIZE)
    {
        set_error(
            error,
            "RIPC stream is too small.");

        return false;
    }

    if (std::memcmp(
            input.data(),
            MAGIC,
            4) != 0)
    {
        set_error(
            error,
            "Invalid RIPC magic.");

        return false;
    }

    std::size_t position = 4;

    const std::uint8_t version =
        read_u8(input, position);

    if (version != RIPC_VERSION)
    {
        set_error(
            error,
            "Unsupported RIPC version.");

        return false;
    }

    const std::uint8_t flags =
        read_u8(input, position);

    if (flags != 0)
    {
        set_error(
            error,
            "Unsupported RIPC flags.");

        return false;
    }

    (void)read_u16(input, position);

    const std::uint64_t original_size =
        read_u64(input, position);

    if (original_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()))
    {
        set_error(
            error,
            "RIPC output is too large.");

        return false;
    }

    output.reserve(
        static_cast<std::size_t>(
            original_size));

    while (
        position < input.size() &&
        output.size() <
            static_cast<std::size_t>(
                original_size))
    {
        const std::uint8_t control =
            read_u8(input, position);

        for (unsigned int bit = 0;
             bit < 8 &&
             output.size() <
                 static_cast<std::size_t>(
                     original_size);
             ++bit)
        {
            if ((control & (1u << bit)) == 0)
            {
                if (position >= input.size())
                {
                    set_error(
                        error,
                        "Unexpected end of RIPC stream.");

                    return false;
                }

                output.push_back(
                    input[position++]);

                continue;
            }

            if (position + 3 >
                input.size())
            {
                set_error(
                    error,
                    "Truncated RIPC match.");

                return false;
            }

            const std::uint16_t distance =
                read_u16(input, position);

            const std::uint8_t length_code =
                read_u8(input, position);

            const std::size_t length =
                static_cast<std::size_t>(
                    length_code) +
                MIN_MATCH_LENGTH;

            if (distance == 0 ||
                distance >
                    output.size())
            {
                set_error(
                    error,
                    "Invalid RIPC distance.");

                return false;
            }

            if (output.size() + length >
                static_cast<std::size_t>(
                    original_size))
            {
                set_error(
                    error,
                    "RIPC match exceeds output size.");

                return false;
            }

            const std::size_t source =
                output.size() - distance;

            for (std::size_t i = 0;
                 i < length;
                 ++i)
            {
                output.push_back(
                    output[source + i]);
            }
        }
    }

    if (output.size() !=
        static_cast<std::size_t>(
            original_size))
    {
        set_error(
            error,
            "RIPC output size mismatch.");

        return false;
    }

    return true;
}

} // namespace rip::compression