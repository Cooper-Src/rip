#include "rip/token_codec.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using rip::compression::Token;
    using rip::compression::TokenType;

    std::cout
        << "RIPC v0.8 token codec test\n"
        << "==========================\n\n";

    std::vector<Token> original;

    Token literal;
    literal.type = TokenType::Literal;
    literal.literals.push_back(
        static_cast<std::byte>('A'));

    original.push_back(literal);

    Token run;
    run.type = TokenType::LiteralRun;

    const std::string text =
        "hello literal run";

    for (const char character : text)
    {
        run.literals.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    character)));
    }

    run.length =
        run.literals.size();

    original.push_back(run);

    Token match;
    match.type = TokenType::Match;
    match.distance = 16;
    match.length = 65'000;

    original.push_back(match);

    Token repeated_match;

repeated_match.type =
    TokenType::MatchRepeat;

repeated_match.distance = 1;
repeated_match.length = 5000;

original.push_back(
    repeated_match);

    std::vector<std::byte> encoded;
    std::string error;

    if (!rip::compression::encode_tokens(
            original,
            encoded,
            &error))
    {
        std::cerr
            << "Encode failed: "
            << error
            << '\n';

        return 1;
    }

    std::vector<Token> decoded;

    if (!rip::compression::decode_tokens(
            encoded,
            decoded,
            &error))
    {
        std::cerr
            << "Decode failed: "
            << error
            << '\n';

        return 1;
    }

    if (decoded.size() !=
        original.size())
    {
        std::cerr
            << "FAIL: token count mismatch\n";

        return 1;
    }

    if (decoded[0].type !=
            TokenType::Literal ||
        decoded[0].literals.size() != 1 ||
        decoded[0].literals[0] !=
            original[0].literals[0])
    {
        std::cerr
            << "FAIL: literal token mismatch\n";

        return 1;
    }

    if (decoded[1].type !=
            TokenType::LiteralRun ||
        decoded[1].literals !=
            original[1].literals)
    {
        std::cerr
            << "FAIL: literal run mismatch\n";

        return 1;
    }

    if (decoded[2].type !=
            TokenType::Match ||
        decoded[2].distance !=
            original[2].distance ||
        decoded[2].length !=
            original[2].length)
    {
        std::cerr
            << "FAIL: extended match mismatch\n";

        return 1;
    }

    std::cout
        << "Tokens: PASS\n"
        << "Encoded size: "
        << encoded.size()
        << " bytes\n"
        << "All token codec tests passed.\n";

    return 0;
}