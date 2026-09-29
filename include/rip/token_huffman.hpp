#pragma once

#include "rip/token_codec.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace rip::compression
{

bool huffman_encode_tokens(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error = nullptr);

bool huffman_decode_tokens(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error = nullptr);

} // namespace rip::compression