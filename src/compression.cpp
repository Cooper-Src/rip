#include "rip/compression.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <queue>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rip::compression
{

namespace
{

constexpr char MAGIC[4] =
{
    'R',
    'P',
    'C',
    '1'
};

constexpr std::size_t WINDOW_SIZE =
    65535;

constexpr std::size_t MAX_MATCH_LENGTH =
    65'793;

constexpr std::size_t MIN_MATCH_LENGTH =
    3;

constexpr std::size_t MAX_CHAIN_LENGTH =
    64;

constexpr std::uint8_t EXTENDED_LENGTH_MARKER =
    0xFF;

constexpr std::size_t EXTENDED_LENGTH_BASE =
    258;

constexpr std::size_t HUFFMAN_SYMBOLS =
    257;

constexpr std::uint8_t FLAG_HUFFMAN =
    0x01;

using Bytes =
    std::vector<std::byte>;

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
    Bytes& output,
    std::uint8_t value)
{
    output.push_back(
        static_cast<std::byte>(value));
}

void write_u16(
    Bytes& output,
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
    Bytes& output,
    std::uint64_t value)
{
    for (unsigned int i = 0;
         i < 8;
         ++i)
    {
        output.push_back(
            static_cast<std::byte>(
                (value >> (i * 8u)) &
                0xffu));
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

    for (unsigned int i = 0;
         i < 8;
         ++i)
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
            static_cast<std::uint8_t>(
                data[0]));

    const auto b =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(
                data[1]));

    const auto c =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(
                data[2]));

    std::uint32_t hash =
        a * 251u;

    hash ^= b * 911u;
    hash ^= c * 3571u;

    return hash & 0xffffu;
}

struct LzToken
{
    std::uint8_t type{};

    std::uint16_t distance{};

    std::size_t length{};

    std::vector<std::byte> literals;
};

Bytes lz_compress(
    std::span<const std::byte> input)
{
    if (input.empty())
    {
        return {};
    }

    std::array<int, 65536> head;
    head.fill(-1);

    std::vector<int> previous(
        input.size(),
        -1);

    auto insert_position =
        [&](std::size_t position)
        {
            if (position + 2 >= input.size())
            {
                return;
            }

            const std::uint32_t hash =
                hash3(
                    input.data() +
                    position);

            previous[position] =
                head[hash];

            head[hash] =
                static_cast<int>(
                    position);
        };

    auto find_match =
        [&](std::size_t position)
        {
            std::size_t best_length = 0;
            std::size_t best_distance = 0;

            if (position + 2 >= input.size())
            {
                return std::pair{
                    best_length,
                    best_distance};
            }

            const std::uint32_t hash =
                hash3(
                    input.data() +
                    position);

            int candidate =
                head[hash];

            std::size_t chain_count = 0;

            while (
                candidate >= 0 &&
                chain_count < MAX_CHAIN_LENGTH)
            {
                const std::size_t candidate_position =
                    static_cast<std::size_t>(
                        candidate);

                if (candidate_position >= position)
                {
                    break;
                }

                const std::size_t distance =
                    position -
                    candidate_position;

                if (distance > WINDOW_SIZE)
                {
                    break;
                }

                const std::size_t maximum =
                    std::min(
                        MAX_MATCH_LENGTH,
                        input.size() -
                            position);

                std::size_t length = 0;

                while (
                    length < maximum &&
                    input[position + length] ==
                        input[
                            candidate_position +
                            length])
                {
                    ++length;
                }

                if (length > best_length)
                {
                    best_length =
                        length;

                    best_distance =
                        distance;

                    if (length == maximum)
                    {
                        break;
                    }
                }

                candidate =
                    previous[
                        candidate_position];

                ++chain_count;
            }

            return std::pair{
                best_length,
                best_distance};
        };

    std::vector<LzToken> tokens;

    tokens.reserve(
        input.size() / 2 +
        1);

    std::size_t position = 0;

    while (position < input.size())
    {
        const auto [
            match_length,
            match_distance
        ] =
            find_match(position);

        if (match_length >= MIN_MATCH_LENGTH)
        {
            LzToken token;

            token.type = 1;

            token.distance =
                static_cast<std::uint16_t>(
                    match_distance);

            token.length =
                match_length;

            tokens.push_back(
                std::move(token));

            const std::size_t end =
                position +
                match_length;

            while (position < end)
            {
                insert_position(position);
                ++position;
            }

            continue;
        }

        const std::size_t run_start =
            position;

        while (position < input.size())
        {
            const auto [
                next_match_length,
                unused_distance
            ] =
                find_match(position);

            (void)unused_distance;

            if (next_match_length >=
                MIN_MATCH_LENGTH)
            {
                break;
            }

            insert_position(position);

            ++position;

            if (position - run_start >= 255)
            {
                break;
            }
        }

        const std::size_t run_length =
            position -
            run_start;

        if (run_length >= 3)
        {
            LzToken token;

            token.type = 2;

            token.length =
                run_length;

            token.literals.insert(
                token.literals.end(),
                input.begin() +
                    static_cast<std::ptrdiff_t>(
                        run_start),
                input.begin() +
                    static_cast<std::ptrdiff_t>(
                        position));

            tokens.push_back(
                std::move(token));
        }
        else
        {
            for (std::size_t i = 0;
                 i < run_length;
                 ++i)
            {
                LzToken token;

                token.type = 0;

                token.literals.push_back(
                    input[
                        run_start + i]);

                tokens.push_back(
                    std::move(token));
            }
        }
    }

    Bytes output;

    output.reserve(
        input.size() + 32);

    std::size_t token_index = 0;

    while (token_index < tokens.size())
    {
        const std::size_t control_position =
            output.size();

        output.push_back(
            static_cast<std::byte>(0));

        std::uint8_t control = 0;

        for (unsigned int slot = 0;
             slot < 4 &&
             token_index < tokens.size();
             ++slot, ++token_index)
        {
            const LzToken& token =
                tokens[token_index];

            control |=
                static_cast<std::uint8_t>(
                    token.type <<
                    (slot * 2u));

            if (token.type == 0)
            {
                output.push_back(
                    token.literals.front());
            }
            else if (token.type == 1)
            {
                write_u16(
                    output,
                    token.distance);

                if (token.length <= 257)
                {
                    write_u8(
                        output,
                        static_cast<std::uint8_t>(
                            token.length -
                            MIN_MATCH_LENGTH));
                }
                else
                {
                    write_u8(
                        output,
                        EXTENDED_LENGTH_MARKER);

                    write_u16(
                        output,
                        static_cast<std::uint16_t>(
                            token.length -
                            EXTENDED_LENGTH_BASE));
                }
            }
            else
            {
                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        token.literals.size()));

                output.insert(
                    output.end(),
                    token.literals.begin(),
                    token.literals.end());
            }
        }

        output[control_position] =
            static_cast<std::byte>(
                control);
    }

    return output;
}

bool lz_decompress(
    std::span<const std::byte> input,
    std::uint64_t original_size,
    Bytes& output,
    std::string* error)
{
    output.clear();

    if (original_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()))
    {
        set_error(
            error,
            "RIPC output is too large.");

        return false;
    }

    output.reserve(
        static_cast<std::size_t>(
            original_size));

    std::size_t position = 0;

    while (
        position < input.size() &&
        output.size() <
            static_cast<std::size_t>(
                original_size))
    {
        const std::uint8_t control =
            read_u8(
                input,
                position);

        for (unsigned int token = 0;
             token < 4 &&
             output.size() <
                 static_cast<std::size_t>(
                     original_size);
             ++token)
        {
            const std::uint8_t type =
                static_cast<std::uint8_t>(
                    (control >>
                     (token * 2u)) &
                    0x03u);

            switch (type)
            {
            case 0:
            {
                if (position >=
                    input.size())
                {
                    set_error(
                        error,
                        "Unexpected end of RIPC stream.");

                    return false;
                }

                output.push_back(
                    input[position++]);

                break;
            }

            case 1:
            {
                if (position + 3 >
                    input.size())
                {
                    set_error(
                        error,
                        "Truncated RIPC match.");

                    return false;
                }

                const std::uint16_t distance =
                    read_u16(
                        input,
                        position);

                const std::uint8_t length_code =
                    read_u8(
                        input,
                        position);

                std::size_t length = 0;

                if (length_code ==
                    EXTENDED_LENGTH_MARKER)
                {
                    if (position + 2 >
                        input.size())
                    {
                        set_error(
                            error,
                            "Truncated RIPC extended match length.");

                        return false;
                    }

                    length =
                        EXTENDED_LENGTH_BASE +
                        static_cast<std::size_t>(
                            read_u16(
                                input,
                                position));
                }
                else
                {
                    length =
                        static_cast<std::size_t>(
                            length_code) +
                        MIN_MATCH_LENGTH;
                }

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
                    output.size() -
                    distance;

                for (std::size_t i = 0;
                     i < length;
                     ++i)
                {
                    output.push_back(
                        output[
                            source + i]);
                }

                break;
            }

            case 2:
            {
                if (position >=
                    input.size())
                {
                    set_error(
                        error,
                        "Truncated RIPC literal run.");

                    return false;
                }

                const std::size_t length =
                    static_cast<std::size_t>(
                        read_u8(
                            input,
                            position));

                if (length == 0)
                {
                    set_error(
                        error,
                        "Invalid RIPC literal run.");

                    return false;
                }

                if (position + length >
                    input.size())
                {
                    set_error(
                        error,
                        "Truncated RIPC literal data.");

                    return false;
                }

                if (output.size() + length >
                    static_cast<std::size_t>(
                        original_size))
                {
                    set_error(
                        error,
                        "RIPC literal run exceeds output size.");

                    return false;
                }

                output.insert(
                    output.end(),
                    input.begin() +
                        static_cast<std::ptrdiff_t>(
                            position),
                    input.begin() +
                        static_cast<std::ptrdiff_t>(
                            position + length));

                position += length;

                break;
            }

            default:
                set_error(
                    error,
                    "Unknown RIPC token type.");

                return false;
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

} // namespace

bool compress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    const Bytes raw =
        lz_compress(input);

    output.reserve(
        raw.size() + 16);

    output.insert(
        output.end(),
        reinterpret_cast<
            const std::byte*>(
                MAGIC),
        reinterpret_cast<
            const std::byte*>(
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

    output.insert(
        output.end(),
        raw.begin(),
        raw.end());

    (void)error;

    return true;
}

bool decompress(
    std::span<const std::byte> input,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    constexpr std::size_t HEADER_SIZE =
        16;

    if (input.size() <
        HEADER_SIZE)
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
        read_u8(
            input,
            position);

    if (version !=
        RIPC_VERSION)
    {
        set_error(
            error,
            "Unsupported RIPC version.");

        return false;
    }

    const std::uint8_t flags =
        read_u8(
            input,
            position);

    if (flags != 0)
    {
        set_error(
            error,
            "Unsupported RIPC flags.");

        return false;
    }

    (void)read_u16(
        input,
        position);

    const std::uint64_t original_size =
        read_u64(
            input,
            position);

    return lz_decompress(
        input.subspan(position),
        original_size,
        output,
        error);
}

} // namespace rip::compression