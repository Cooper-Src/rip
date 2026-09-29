#include "rip/token_huffman.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace
{

std::vector<std::byte> materialize(
    std::span<const rip::compression::Token> tokens)
{
    std::vector<std::byte> output;

    for (const auto& token : tokens)
    {
        if (token.type ==
            rip::compression::TokenType::Literal)
        {
            output.push_back(
                token.literals.front());

            continue;
        }

        if (token.type ==
            rip::compression::TokenType::LiteralRun)
        {
            output.insert(
                output.end(),
                token.literals.begin(),
                token.literals.end());

            continue;
        }

        if (token.type ==
            rip::compression::TokenType::Match)
        {
            const std::size_t source =
                output.size() -
                token.distance;

            for (std::size_t i = 0;
                 i < token.length;
                 ++i)
            {
                output.push_back(
                    output[source + i]);
            }
        }
    }

    return output;
}

bool run_test()
{
    using rip::compression::Token;
    using rip::compression::TokenType;

    std::vector<Token> original;

    Token literal;
    literal.type =
        TokenType::Literal;

    literal.literals.push_back(
        static_cast<std::byte>('A'));

    original.push_back(
        literal);

    Token match;
    match.type =
        TokenType::Match;

    match.distance = 1;
    match.length = 5000;

    original.push_back(
        match);

    Token run;
    run.type =
        TokenType::LiteralRun;

    const std::string text =
        "RIPC token-aware Huffman coding";

    for (const char character : text)
    {
        run.literals.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    character)));
    }

    run.length =
        run.literals.size();

    original.push_back(
        run);

    const auto original_data =
        materialize(original);

    std::vector<std::byte> compressed;
    std::string error;

    if (!rip::compression::huffman_encode_tokens(
            original,
            compressed,
            &error))
    {
        std::cerr
            << "Encode failed: "
            << error
            << '\n';

        return false;
    }

    std::vector<Token> decoded;

    if (!rip::compression::huffman_decode_tokens(
            compressed,
            decoded,
            &error))
    {
        std::cerr
            << "Decode failed: "
            << error
            << '\n';

        return false;
    }

    const auto decoded_data =
        materialize(decoded);

    if (decoded_data !=
        original_data)
    {
        std::cerr
            << "FAIL: decoded data differs "
               "from original data.\n";

        return false;
    }

    std::cout
        << "Original data : "
        << original_data.size()
        << " bytes\n";

    std::cout
        << "RTH1 size     : "
        << compressed.size()
        << " bytes\n";

    std::cout
        << "Round-trip    : PASS\n";

    return true;
}

} // namespace

int main()
{
    std::cout
        << "RIPC v0.6 token Huffman test\n"
        << "============================\n\n";

    if (!run_test())
    {
        return 1;
    }

    std::cout
        << "\nAll token Huffman tests passed.\n";

    return 0;
}