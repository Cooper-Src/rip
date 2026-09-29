#include "rip/token_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace rip::compression
{

namespace
{

constexpr std::uint8_t EXTENDED_LENGTH_MARKER =
    0xFF;

constexpr std::size_t EXTENDED_LENGTH_BASE =
    258;

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

bool read_u8(
    std::span<const std::byte> input,
    std::size_t& position,
    std::uint8_t& value)
{
    if (position >= input.size())
    {
        return false;
    }

    value =
        static_cast<std::uint8_t>(
            input[position++]);

    return true;
}

bool read_u16(
    std::span<const std::byte> input,
    std::size_t& position,
    std::uint16_t& value)
{
    std::uint8_t low = 0;
    std::uint8_t high = 0;

    if (!read_u8(input, position, low) ||
        !read_u8(input, position, high))
    {
        return false;
    }

    value =
        static_cast<std::uint16_t>(
            low |
            (static_cast<std::uint16_t>(
                high) << 8u));

    return true;
}

} // namespace

bool encode_tokens(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

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
            const Token& token =
                tokens[token_index];

            const auto type =
                static_cast<std::uint8_t>(
                    token.type);

            if (type > 2)
            {
                set_error(
                    error,
                    "Invalid RIPC token type.");

                return false;
            }

            control |=
                static_cast<std::uint8_t>(
                    type << (slot * 2u));

            switch (token.type)
            {
            case TokenType::Literal:
                if (token.literals.size() != 1)
                {
                    set_error(
                        error,
                        "Literal token must contain exactly one byte.");

                    return false;
                }

                output.push_back(
                    token.literals[0]);

                break;

            case TokenType::Match:
                if (token.distance == 0)
                {
                    set_error(
                        error,
                        "Match distance cannot be zero.");

                    return false;
                }

                if (token.length < 3)
                {
                    set_error(
                        error,
                        "Match length is too small.");

                    return false;
                }

                write_u16(
                    output,
                    token.distance);

                if (token.length <= 257)
                {
                    write_u8(
                        output,
                        static_cast<std::uint8_t>(
                            token.length - 3));
                }
                else
                {
                    const std::size_t extended =
                        token.length -
                        EXTENDED_LENGTH_BASE;

                    if (extended > 0xFFFFu)
                    {
                        set_error(
                            error,
                            "Match length is too large.");

                        return false;
                    }

                    write_u8(
                        output,
                        EXTENDED_LENGTH_MARKER);

                    write_u16(
                        output,
                        static_cast<std::uint16_t>(
                            extended));
                }

                break;

            case TokenType::LiteralRun:
                if (token.literals.empty() ||
                    token.literals.size() > 255)
                {
                    set_error(
                        error,
                        "Invalid literal run length.");

                    return false;
                }

                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        token.literals.size()));

                output.insert(
                    output.end(),
                    token.literals.begin(),
                    token.literals.end());

                break;

            default:
                set_error(
                    error,
                    "Unsupported RIPC token type.");

                return false;
            }
        }

        output[control_position] =
            static_cast<std::byte>(
                control);
    }

    return true;
}

bool decode_tokens(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    tokens.clear();

    std::size_t position = 0;

    while (position < input.size())
    {
        std::uint8_t control = 0;

        if (!read_u8(
                input,
                position,
                control))
        {
            set_error(
                error,
                "Missing RIPC control byte.");

            return false;
        }

        for (unsigned int slot = 0;
             slot < 4;
             ++slot)
        {
            if (position >= input.size())
            {
                break;
            }

            const std::uint8_t raw_type =
                static_cast<std::uint8_t>(
                    (control >>
                     (slot * 2u)) &
                    0x03u);

            if (raw_type == 3)
            {
                set_error(
                    error,
                    "Reserved RIPC token type encountered.");

                return false;
            }

            Token token;

            token.type =
                static_cast<TokenType>(
                    raw_type);

            switch (token.type)
            {
            case TokenType::Literal:
            {
                std::uint8_t value = 0;

                if (!read_u8(
                        input,
                        position,
                        value))
                {
                    set_error(
                        error,
                        "Truncated RIPC literal.");

                    return false;
                }

                token.literals.push_back(
                    static_cast<std::byte>(
                        value));

                break;
            }

            case TokenType::Match:
            {
                if (!read_u16(
                        input,
                        position,
                        token.distance))
                {
                    set_error(
                        error,
                        "Truncated RIPC match distance.");

                    return false;
                }

                std::uint8_t length_code = 0;

                if (!read_u8(
                        input,
                        position,
                        length_code))
                {
                    set_error(
                        error,
                        "Truncated RIPC match length.");

                    return false;
                }

                if (length_code ==
                    EXTENDED_LENGTH_MARKER)
                {
                    std::uint16_t extended = 0;

                    if (!read_u16(
                            input,
                            position,
                            extended))
                    {
                        set_error(
                            error,
                            "Truncated extended RIPC match length.");

                        return false;
                    }

                    token.length =
                        EXTENDED_LENGTH_BASE +
                        static_cast<std::size_t>(
                            extended);
                }
                else
                {
                    token.length =
                        static_cast<std::size_t>(
                            length_code) +
                        3;
                }

                if (token.distance == 0 ||
                    token.length < 3)
                {
                    set_error(
                        error,
                        "Invalid RIPC match.");

                    return false;
                }

                break;
            }

            case TokenType::LiteralRun:
            {
                std::uint8_t length = 0;

                if (!read_u8(
                        input,
                        position,
                        length))
                {
                    set_error(
                        error,
                        "Truncated RIPC literal run.");

                    return false;
                }

                if (length == 0 ||
                    position + length >
                        input.size())
                {
                    set_error(
                        error,
                        "Invalid RIPC literal run.");

                    return false;
                }

                token.length =
                    static_cast<std::size_t>(
                        length);

                token.literals.insert(
                    token.literals.end(),
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
                    "Invalid RIPC token.");

                return false;
            }

            tokens.push_back(
                std::move(token));
        }
    }

    return true;
}

} // namespace rip::compression