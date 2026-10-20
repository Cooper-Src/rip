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


bool test_three_byte_prefix_matches()
{
    /*
     * Every 64-byte record starts with the same three-byte marker, but its
     * fourth byte and payload differ. A four-byte matcher misses these
     * matches. A three-byte matcher can encode later markers as repeat-distance
     * tokens while leaving the otherwise random payload as literal runs.
     */
    constexpr std::size_t record_count = 2'048;
    constexpr std::size_t record_size = 64;

    std::vector<std::byte> input;
    input.reserve(record_count * record_size);

    std::mt19937 generator(0x33524950u);

    for (std::size_t record = 0; record < record_count; ++record)
    {
        input.push_back(static_cast<std::byte>('R'));
        input.push_back(static_cast<std::byte>('I'));
        input.push_back(static_cast<std::byte>('P'));
        input.push_back(static_cast<std::byte>(record & 0xffu));

        for (std::size_t i = 4; i < record_size; ++i)
        {
            input.push_back(
                static_cast<std::byte>(generator() & 0xffu));
        }
    }

    std::vector<std::byte> compressed;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(input, compressed, &error))
    {
        std::cerr << "Three-byte prefix compression failed: "
                  << error << '\n';
        return false;
    }

    constexpr std::size_t minimum_expected_saving = 1'024;
    if (compressed.size() + minimum_expected_saving >= input.size())
    {
        std::cerr
            << "Three-byte marker fixture did not compress as expected ("
            << input.size() << " -> " << compressed.size() << " bytes)\n";
        return false;
    }

    if (!rip::compression::decompress(compressed, restored, &error) ||
        restored != input)
    {
        std::cerr << "Three-byte prefix round trip failed: "
                  << error << '\n';
        return false;
    }

    std::cout
        << "Three-byte prefix matches: PASS ("
        << input.size() << " -> " << compressed.size()
        << " bytes, saved "
        << (input.size() - compressed.size()) << " bytes)\n";

    return true;
}

bool test_ultra_finds_deep_dictionary_match()
{
    /*
     * Keep more than 16,384 identical four-byte hashes in the 64 KiB
     * window, but make their fifth byte differ from a later long match.
     * Maximum stops before reaching the older occurrence; Ultra's full
     * chain search should find it and produce a smaller stream.
     */
    constexpr std::size_t source_size = 8192;
    constexpr std::size_t repeated_prefix_run = 20'000;

    std::vector<std::byte> source(source_size);
    source[0] = static_cast<std::byte>('A');
    source[1] = static_cast<std::byte>('A');
    source[2] = static_cast<std::byte>('A');
    source[3] = static_cast<std::byte>('A');
    source[4] = static_cast<std::byte>('B');

    std::mt19937 generator(0x554c5452u);

    for (std::size_t i = 5; i < source.size(); ++i)
    {
        source[i] = static_cast<std::byte>(generator() & 0xffu);
    }

    std::vector<std::byte> input;
    input.reserve(source_size * 2 + repeated_prefix_run);
    input.insert(input.end(), source.begin(), source.end());
    input.insert(
        input.end(),
        repeated_prefix_run,
        static_cast<std::byte>('A'));
    // Break the all-A run so a nearby overlapping match cannot span
    // into the copied source block.
    input.push_back(static_cast<std::byte>('Z'));
    input.insert(input.end(), source.begin(), source.end());

    std::vector<std::byte> maximum;
    std::vector<std::byte> ultra;
    std::vector<std::byte> automatic;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(
            input,
            maximum,
            &error,
            rip::compression::CompressionLevel::Maximum) ||
        !rip::compression::compress(
            input,
            ultra,
            &error,
            rip::compression::CompressionLevel::Ultra))
    {
        std::cerr << "Ultra deep-match compression failed: " << error << '\n';
        return false;
    }

    if (ultra.size() >= maximum.size())
    {
        std::cerr
            << "Ultra did not improve the deep-dictionary fixture ("
            << ultra.size() << " vs " << maximum.size() << " bytes)\n";
        return false;
    }

    if (!rip::compression::decompress(ultra, restored, &error) ||
        restored != input)
    {
        std::cerr << "Ultra deep-match round trip failed: " << error << '\n';
        return false;
    }

    if (!rip::compression::compress(input, automatic, &error) ||
        automatic.size() > ultra.size())
    {
        std::cerr
            << "Auto did not retain the smallest deep-match result: "
            << error << '\n';
        return false;
    }

    std::cout
        << "Ultra deep dictionary match: PASS (Maximum "
        << maximum.size() << " -> Ultra "
        << ultra.size() << " -> Auto "
        << automatic.size() << " bytes)\n";

    return true;
}

bool test_ultra_prefers_cheaper_repeat_distance()
{
    /*
     * Set up three occurrences of a long prefix. The immediately preceding
     * occurrence has the previous match distance but ends one byte earlier;
     * an older occurrence matches one byte more but needs a new distance.
     * Ultra should account for the two-byte distance field instead of always
     * taking the longest match.
     */
    constexpr std::size_t block_size = 8192;
    constexpr std::size_t motif_offset = 64;
    constexpr std::size_t shared_length = 128;
    constexpr std::size_t target_tail_size = 512;

    std::mt19937 generator(0x44535443u);
    std::vector<std::byte> older(block_size);
    std::vector<std::byte> repeated(block_size);

    for (auto& value : older)
    {
        value = static_cast<std::byte>(generator() & 0xffu);
    }

    for (auto& value : repeated)
    {
        value = static_cast<std::byte>(generator() & 0xffu);
    }

    for (std::size_t i = 0; i < shared_length; ++i)
    {
        const std::byte value =
            static_cast<std::byte>(generator() & 0xffu);
        older[motif_offset + i] = value;
        repeated[motif_offset + i] = value;
    }

    older[motif_offset + shared_length] = static_cast<std::byte>('B');
    repeated[motif_offset + shared_length] = static_cast<std::byte>('A');

    const std::byte older_next =
        older[motif_offset + shared_length + 1];
    const std::byte target_next =
        older_next == static_cast<std::byte>(0xA5)
            ? static_cast<std::byte>(0x5A)
            : static_cast<std::byte>(0xA5);

    std::vector<std::byte> input;
    input.reserve(
        block_size * 3 +
        motif_offset +
        shared_length +
        2 +
        target_tail_size);

    input.insert(input.end(), older.begin(), older.end());
    input.insert(input.end(), repeated.begin(), repeated.end());
    input.insert(input.end(), repeated.begin(), repeated.end());

    std::vector<std::byte> gap(motif_offset);
    for (auto& value : gap)
    {
        value = static_cast<std::byte>(generator() & 0xffu);
    }

    if (gap.front() == repeated.front())
    {
        gap.front() ^= static_cast<std::byte>(0x01);
    }

    input.insert(input.end(), gap.begin(), gap.end());
    input.insert(
        input.end(),
        repeated.begin() + static_cast<std::ptrdiff_t>(motif_offset),
        repeated.begin() + static_cast<std::ptrdiff_t>(motif_offset + shared_length));
    input.push_back(static_cast<std::byte>('B'));
    input.push_back(target_next);

    for (std::size_t i = 0; i < target_tail_size; ++i)
    {
        input.push_back(static_cast<std::byte>(generator() & 0xffu));
    }

    std::vector<std::byte> maximum;
    std::vector<std::byte> ultra;
    std::vector<std::byte> automatic;
    std::vector<std::byte> restored;
    std::string error;

    if (!rip::compression::compress(
            input,
            maximum,
            &error,
            rip::compression::CompressionLevel::Maximum) ||
        !rip::compression::compress(
            input,
            ultra,
            &error,
            rip::compression::CompressionLevel::Ultra))
    {
        std::cerr << "Repeat-distance compression failed: " << error << '\n';
        return false;
    }

    if (ultra.size() >= maximum.size())
    {
        std::cerr
            << "Ultra did not reduce repeat-distance fixture size (Maximum "
            << maximum.size() << " vs Ultra " << ultra.size() << " bytes)\n";
        return false;
    }

    if (!rip::compression::decompress(ultra, restored, &error) ||
        restored != input)
    {
        std::cerr << "Repeat-distance round trip failed: " << error << '\n';
        return false;
    }

    if (!rip::compression::compress(input, automatic, &error) ||
        automatic.size() > ultra.size())
    {
        std::cerr << "Auto lost the repeat-distance result: " << error << '\n';
        return false;
    }

    std::cout
        << "Ultra repeat-distance cost: PASS (Maximum "
        << maximum.size() << " -> Ultra " << ultra.size()
        << " -> Auto " << automatic.size() << " bytes)\n";

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

    if (compressed.size() < 20 ||
        static_cast<std::uint8_t>(compressed[4]) !=
            rip::compression::RIPC_VERSION ||
        static_cast<std::uint8_t>(compressed[5]) != 0x08)
    {
        std::cerr
            << "Large input did not use the expected RIPC v11 chunked stream\n";
        return false;
    }

    const std::uint32_t chunk_count =
        static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(compressed[16])) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(compressed[17])) << 8u) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(compressed[18])) << 16u) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint8_t>(compressed[19])) << 24u);

    if (chunk_count != 2)
    {
        std::cerr
            << "Expected two 2 MiB chunks for the 3 MiB test input, got "
            << chunk_count << '\n';
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

if (!test_three_byte_prefix_matches() ||
    !test_parallel_chunked() ||
    !test_ultra_finds_deep_dictionary_match() ||
    !test_ultra_prefers_cheaper_repeat_distance())
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
    4>
    levels =
{
    rip::compression::CompressionLevel::Fast,
    rip::compression::CompressionLevel::Balanced,
    rip::compression::CompressionLevel::Maximum,
    rip::compression::CompressionLevel::Ultra
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