#include "rip/token_codec.hpp"
#include "rip/token_huffman.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using rip::compression::Token;
using rip::compression::TokenType;

struct Result
{
    std::size_t original_bytes{};
    std::size_t raw_bytes{};
    std::size_t huffman_bytes{};
    double raw_time{};
    double huffman_time{};
};

std::vector<Token> make_repetitive_tokens()
{
    std::vector<Token> tokens;

    constexpr std::size_t repetitions = 10'000;

    for (std::size_t i = 0;
         i < repetitions;
         ++i)
    {
        Token literal;

        literal.type =
            TokenType::Literal;

        literal.literals.push_back(
            static_cast<std::byte>('A'));

        tokens.push_back(
            std::move(literal));

        Token match;

        match.type =
            TokenType::Match;

        match.distance = 1;
        match.length = 500;

        tokens.push_back(
            std::move(match));
    }

    return tokens;
}

std::vector<Token> make_source_tokens()
{
    const std::string source =
        "const std::string path = "
        "source.parent_path().wstring();\n"
        "if (!fs::exists(path)) {\n"
        "    return false;\n"
        "}\n";

    std::vector<Token> tokens;

    for (const char character : source)
    {
        Token token;

        token.type =
            TokenType::Literal;

        token.literals.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    character)));

        tokens.push_back(
            std::move(token));
    }

    constexpr std::size_t repetitions = 1000;

    std::vector<Token> result;

    result.reserve(
        tokens.size() *
        repetitions);

    for (std::size_t i = 0;
         i < repetitions;
         ++i)
    {
        result.insert(
            result.end(),
            tokens.begin(),
            tokens.end());
    }

    return result;
}

std::vector<Token> make_random_tokens()
{
    std::vector<Token> tokens;

    constexpr std::size_t count =
        100'000;

    tokens.reserve(count);

    std::uint32_t state =
        0x52495043u;

    auto next_random =
        [&]()
        {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;

            return static_cast<std::uint8_t>(
                state & 0xffu);
        };

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        Token token;

        token.type =
            TokenType::Literal;

        token.literals.push_back(
            static_cast<std::byte>(
                next_random()));

        tokens.push_back(
            std::move(token));
    }

    return tokens;
}

Result benchmark(
    const std::vector<Token>& tokens)
{
    std::vector<std::byte> raw;
    std::vector<std::byte> huffman;

    std::string error;

    const auto raw_start =
        std::chrono::steady_clock::now();

    if (!rip::compression::encode_tokens(
            tokens,
            raw,
            &error))
    {
        std::cerr
            << "Raw token encoding failed: "
            << error
            << '\n';

        return {};
    }

    const auto raw_end =
        std::chrono::steady_clock::now();

    const auto huffman_start =
        std::chrono::steady_clock::now();

    if (!rip::compression::huffman_encode_tokens(
            tokens,
            huffman,
            &error))
    {
        std::cerr
            << "Huffman token encoding failed: "
            << error
            << '\n';

        return {};
    }

    const auto huffman_end =
        std::chrono::steady_clock::now();

    std::size_t original_bytes = 0;

    for (const Token& token : tokens)
    {
        original_bytes +=
            token.literals.size();

        if (token.type ==
            TokenType::Match)
        {
            original_bytes +=
                token.length;
        }
    }

    return {
        original_bytes,
        raw.size(),
        huffman.size(),
        std::chrono::duration<double, std::milli>(
            raw_end - raw_start)
            .count(),
        std::chrono::duration<double, std::milli>(
            huffman_end - huffman_start)
            .count()
    };
}

void print_result(
    const char* name,
    const Result& result)
{
    const double raw_ratio =
        result.original_bytes == 0
            ? 0.0
            : 100.0 *
                static_cast<double>(
                    result.raw_bytes) /
                static_cast<double>(
                    result.original_bytes);

    const double huffman_ratio =
        result.original_bytes == 0
            ? 0.0
            : 100.0 *
                static_cast<double>(
                    result.huffman_bytes) /
                static_cast<double>(
                    result.original_bytes);

    const double improvement =
        result.raw_bytes == 0
            ? 0.0
            : 100.0 *
                (
                    1.0 -
                    static_cast<double>(
                        result.huffman_bytes) /
                    static_cast<double>(
                        result.raw_bytes)
                );

    std::cout
        << '\n'
        << name
        << '\n'
        << std::string(
               std::char_traits<char>::length(name),
               '-')
        << '\n';

    std::cout
        << "Original token data : "
        << result.original_bytes
        << " bytes\n";

    std::cout
        << "Raw RIPC tokens     : "
        << result.raw_bytes
        << " bytes ("
        << std::fixed
        << std::setprecision(2)
        << raw_ratio
        << "%)\n";

    std::cout
        << "RTH1 Huffman        : "
        << result.huffman_bytes
        << " bytes ("
        << huffman_ratio
        << "%)\n";

    std::cout
        << "Huffman reduction   : "
        << improvement
        << "%\n";

    std::cout
        << "Raw encode time     : "
        << result.raw_time
        << " ms\n";

    std::cout
        << "Huffman encode time : "
        << result.huffman_time
        << " ms\n";
}

} // namespace

int main()
{
    std::cout
        << "RIPC v0.8 Token Huffman Benchmark\n"
        << "==================================\n";

    print_result(
        "Repetitive tokens",
        benchmark(
            make_repetitive_tokens()));

    print_result(
        "Source-like tokens",
        benchmark(
            make_source_tokens()));

    print_result(
        "Random tokens",
        benchmark(
            make_random_tokens()));

    std::cout
        << "\nBenchmark complete.\n";

    return 0;
}