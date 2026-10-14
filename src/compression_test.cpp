#include "rip/compression.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace
{

bool test_data(
    const std::vector<std::byte>& input,
    const char* name)
{
    std::vector<std::byte> compressed;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(
            input,
            compressed,
            &error))
    {
        std::cerr
            << name
            << ": compression failed: "
            << error
            << '\n';

        return false;
    }

    if (!rip::compression::decompress(
            compressed,
            restored,
            &error))
    {
        std::cerr
            << name
            << ": decompression failed: "
            << error
            << '\n';

        return false;
    }

    if (restored != input)
    {
        std::cerr
            << name
            << ": data mismatch\n";

        return false;
    }

    std::cout
        << name
        << ": PASS ("
        << input.size()
        << " -> "
        << compressed.size()
        << " bytes)\n";

    return true;
}

bool test_auto_selects_smallest(
    const std::vector<std::byte>& input,
    const char* name)
{
    constexpr std::array<
        rip::compression::CompressionLevel,
        4>
        levels =
    {
        rip::compression::CompressionLevel::Fast,
        rip::compression::CompressionLevel::Balanced,
        rip::compression::CompressionLevel::Maximum,
        rip::compression::CompressionLevel::Ultra
    };

    std::size_t smallest_size =
        std::numeric_limits<std::size_t>::max();

    for (const auto level : levels)
    {
        std::vector<std::byte> candidate;
        std::string error;

        if (!rip::compression::compress(
                input,
                candidate,
                &error,
                level))
        {
            std::cerr
                << name
                << ": profile compression failed: "
                << error
                << '\n';

            return false;
        }

        if (candidate.size() < smallest_size)
        {
            smallest_size = candidate.size();
        }
    }

    std::vector<std::byte> automatic;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(
            input,
            automatic,
            &error))
    {
        std::cerr
            << name
            << ": automatic compression failed: "
            << error
            << '\n';

        return false;
    }

    if (automatic.size() != smallest_size)
    {
        std::cerr
            << name
            << ": auto mode did not select the smallest profile result ("
            << automatic.size()
            << " vs "
            << smallest_size
            << " bytes)\n";

        return false;
    }

    if (!rip::compression::decompress(
            automatic,
            restored,
            &error) ||
        restored != input)
    {
        std::cerr
            << name
            << ": auto result failed round-trip validation: "
            << error
            << '\n';

        return false;
    }

    std::cout
        << name
        << ": PASS (best of 4 profiles, "
        << automatic.size()
        << " bytes)\n";

    return true;
}


bool test_parallel_chunked()
{
    constexpr std::size_t input_size =
        3 * 1024 * 1024 + 123;

    const std::string pattern =
        "RIPC parallel chunk compression checks all CPU workers. "
        "0123456789abcdefghijklmnopqrstuvwxyz\n";

    std::vector<std::byte> input(input_size);

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        input[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    pattern[i % pattern.size()]));
    }

    std::vector<std::byte> compressed;
    std::vector<std::byte> restored;
    std::string error;

    std::uint64_t last_progress = 0;
    bool progress_valid = true;

    if (!rip::compression::compress(
            input,
            compressed,
            &error,
            rip::compression::CompressionLevel::Fast,
            [&](std::uint64_t processed,
                std::uint64_t total,
                std::string_view phase)
            {
                (void)phase;

                if (total != input.size() ||
                    processed < last_progress ||
                    processed > total)
                {
                    progress_valid = false;
                }

                last_progress = processed;
            }))
    {
        std::cerr
            << "Parallel chunk compression failed: "
            << error
            << '\n';
        return false;
    }

    if (compressed.size() < 16 ||
        static_cast<std::uint8_t>(compressed[4]) !=
            rip::compression::RIPC_VERSION ||
        static_cast<std::uint8_t>(compressed[5]) != 0x08)
    {
        std::cerr
            << "Large input did not use the expected RIPC v11 chunked stream\n";
        return false;
    }

    if (!progress_valid || last_progress != input.size())
    {
        std::cerr
            << "Parallel compression progress was invalid or incomplete\n";
        return false;
    }

    if (!rip::compression::decompress(
            compressed,
            restored,
            &error) ||
        restored != input)
    {
        std::cerr
            << "Parallel chunk round trip failed: "
            << error
            << '\n';
        return false;
    }

    /*
     * Truncating a chunked stream must fail before decoded data is
     * returned. This exercises the chunk-table bounds checks added
     * ahead of parallel worker startup.
     */
    std::vector<std::byte> truncated = compressed;
    truncated.pop_back();

    if (rip::compression::decompress(
            truncated,
            restored,
            &error) ||
        !restored.empty())
    {
        std::cerr
            << "Truncated parallel chunk stream was not rejected safely\n";
        return false;
    }

    std::vector<std::byte> automatic;
    std::vector<std::byte> automatic_restored;

    if (!rip::compression::compress(
            input,
            automatic,
            &error) ||
        !rip::compression::decompress(
            automatic,
            automatic_restored,
            &error) ||
        automatic_restored != input)
    {
        std::cerr
            << "Automatic parallel chunk round trip failed: "
            << error
            << '\n';
        return false;
    }

    if (automatic.size() > compressed.size())
    {
        std::cerr
            << "Automatic mode was larger than the Fast chunk profile\n";
        return false;
    }

    std::cout
        << "Parallel chunks: PASS ("
        << input.size()
        << " -> "
        << automatic.size()
        << " bytes; progress and round trip verified)\n";

    return true;
}

} // namespace

int main()
{
    std::cout
        << "RIPC v11 compression test\n"
        << "==========================\n\n";

    std::string text =
        "RIP compression algorithm test. "
        "RIP compression algorithm test. "
        "RIP compression algorithm test.";

    std::vector<std::byte> text_data(
        text.size());

    for (std::size_t i = 0;
         i < text.size();
         ++i)
    {
        text_data[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    text[i]));
    }

    if (!test_data(
            text_data,
            "Text"))
    {
        return 1;
    }

    std::vector<std::byte> repetitive(
        100'000);

    for (std::size_t i = 0;
         i < repetitive.size();
         ++i)
    {
        repetitive[i] =
            static_cast<std::byte>(
                'A' + (i % 2));
    }

    if (!test_data(
            repetitive,
            "Repetitive"))
    {
        return 1;
    }

    std::vector<std::byte> random_data(
        100'000);

    std::mt19937 generator(
        0x52495043u);

    for (auto& value :
         random_data)
    {
        value =
            static_cast<std::byte>(
                generator() & 0xffu);
    }

    if (!test_data(
            random_data,
            "Random"))
    {
        return 1;
    }

    /*
     * Incompressible input should fall back to the
     * stored representation rather than expanding
     * through the LZ token control bytes.
     */
    {
        std::vector<std::byte> compressed;
        std::string error;

        if (!rip::compression::compress(
                random_data,
                compressed,
                &error))
        {
            std::cerr
                << "Random stored-block compression failed: "
                << error
                << '\n';

            return 1;
        }

        constexpr std::size_t RIPC_HEADER_SIZE = 16;

        if (compressed.size() >
            random_data.size() +
                RIPC_HEADER_SIZE)
        {
            std::cerr
                << "Random data expanded beyond the stored-block limit\n";

            return 1;
        }
    }

    std::vector<std::byte> long_run(
        100'000,
        static_cast<std::byte>('X'));

    if (!test_data(
            long_run,
            "Long match"))
    {
        return 1;
    }

if (!test_parallel_chunked())
{
    return 1;
}

if (!test_auto_selects_smallest(
        text_data,
        "Auto size selection: text") ||
    !test_auto_selects_smallest(
        repetitive,
        "Auto size selection: repetitive") ||
    !test_auto_selects_smallest(
        random_data,
        "Auto size selection: random") ||
    !test_auto_selects_smallest(
        long_run,
        "Auto size selection: long match"))
{
    return 1;
}

std::cout
    << "\nCompression levels\n"
    << "------------------\n";

const std::array<
    rip::compression::CompressionLevel,
    3>
    levels =
{
    rip::compression::CompressionLevel::Fast,
    rip::compression::CompressionLevel::Balanced,
    rip::compression::CompressionLevel::Maximum
};

for (const auto level : levels)
{
    std::vector<std::byte> compressed;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(
            repetitive,
            compressed,
            &error,
            level))
    {
        std::cerr
            << "Level test compression failed: "
            << error
            << '\n';

        return 1;
    }

    if (!rip::compression::decompress(
            compressed,
            restored,
            &error))
    {
        std::cerr
            << "Level test decompression failed: "
            << error
            << '\n';

        return 1;
    }

    if (restored != repetitive)
    {
        std::cerr
            << "Level test data mismatch\n";

        return 1;
    }

    std::cout
        << "Level "
        << static_cast<unsigned>(
               level)
        << ": "
        << compressed.size()
        << " bytes\n";
}

    std::cout
        << "\nAll RIPC v11 tests passed.\n";

    return 0;
}