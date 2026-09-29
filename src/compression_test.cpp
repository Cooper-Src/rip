#include "rip/compression.hpp"

#include <cstddef>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main()
{
    std::cout
        << "RIPC v0.2 compression test\n"
        << "==========================\n\n";

    const std::string text =
        "RIP compression algorithm test. "
        "RIP compression algorithm test. "
        "RIP compression algorithm test. "
        "This should contain many repeated sequences. ";

    std::vector<std::byte> input(
        text.size());

    for (std::size_t i = 0;
         i < text.size();
         ++i)
    {
        input[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    text[i]));
    }

    std::vector<std::byte> compressed;
    std::string error;

    if (!rip::compression::compress(
            input,
            compressed,
            &error))
    {
        std::cerr
            << "Compression failed: "
            << error
            << '\n';

        return 1;
    }

    std::vector<std::byte> restored;

    if (!rip::compression::decompress(
            compressed,
            restored,
            &error))
    {
        std::cerr
            << "Decompression failed: "
            << error
            << '\n';

        return 1;
    }

    if (restored != input)
    {
        std::cerr
            << "FAIL: restored data "
               "does not match input.\n";

        return 1;
    }

    std::cout
        << "Original size : "
        << input.size()
        << " bytes\n";

    std::cout
        << "RIPC size     : "
        << compressed.size()
        << " bytes\n";

    std::cout
        << "Round-trip    : PASS\n";

    return 0;
}