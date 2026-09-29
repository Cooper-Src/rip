#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rip::compression
{

enum class TokenType : std::uint8_t
{
    Literal = 0,
    Match = 1,
    LiteralRun = 2,
    MatchRepeat = 3
};

struct Token
{
    TokenType type{};

    std::uint16_t distance{};

    std::size_t length{};

    std::vector<std::byte> literals;
};

bool encode_tokens(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

bool decode_tokens(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error = nullptr);

} // namespace rip::compression