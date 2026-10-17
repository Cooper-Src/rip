#include "rip/compression.hpp"
#include "rip/token_codec.hpp"
#include "rip/token_huffman.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <thread>
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
                '1'};

        constexpr std::size_t WINDOW_SIZE =
            65535;

        /*
         * The token-Huffman length alphabet currently uses
         * 17 symbols, with symbol 16 carrying 15 extra bits.
         * That represents match lengths through 65,538 bytes.
         *
         * Keep the LZ matcher inside that range so RIPC never
         * generates an unrepresentable length symbol.
         */
        constexpr std::size_t MAX_MATCH_LENGTH =
            65'538;

        constexpr std::size_t MIN_MATCH_LENGTH =
            3;

        constexpr std::size_t HASH_BITS =
            18;

        constexpr std::size_t HASH_SIZE =
            std::size_t{1} << HASH_BITS;

        constexpr std::size_t HASH_MASK =
            HASH_SIZE - 1;

        constexpr std::uint8_t EXTENDED_LENGTH_MARKER =
            0xFF;

        constexpr std::size_t EXTENDED_LENGTH_BASE =
            258;

        constexpr std::size_t HUFFMAN_SYMBOLS =
            256;

        constexpr std::uint8_t FLAG_HUFFMAN =
            0x01;

        constexpr std::uint8_t FLAG_TOKEN_HUFFMAN =
            0x02;

        constexpr std::uint8_t FLAG_STORED =
            0x04;

        constexpr std::uint8_t FLAG_CHUNKED =
            0x08;

        // Larger chunks share the per-chunk entropy-coding overhead over
        // more source data, improving ratios while preserving RIPC v11 framing.
        constexpr std::size_t PARALLEL_CHUNK_SIZE =
            2 * 1024 * 1024;

        constexpr std::size_t PARALLEL_MIN_SIZE =
            2 * 1024 * 1024;

        using Bytes =
            std::vector<std::byte>;

        void set_error(
            std::string *error,
            const char *message)
        {
            if (error)
            {
                *error = message;
            }
        }

        void write_u8(
            Bytes &output,
            std::uint8_t value)
        {
            output.push_back(
                static_cast<std::byte>(value));
        }

        void write_u16(
            Bytes &output,
            std::uint16_t value)
        {
            output.push_back(
                static_cast<std::byte>(
                    value & 0xffu));

            output.push_back(
                static_cast<std::byte>(
                    (value >> 8u) & 0xffu));
        }

        void write_u32(
            Bytes &output,
            std::uint32_t value)
        {
            for (unsigned int i = 0; i < 4; ++i)
            {
                output.push_back(
                    static_cast<std::byte>(
                        (value >> (i * 8u)) & 0xffu));
            }
        }

        void write_u64(
            Bytes &output,
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
            std::size_t &position)
        {
            return static_cast<std::uint8_t>(
                input[position++]);
        }

        std::uint16_t read_u16(
            std::span<const std::byte> input,
            std::size_t &position)
        {
            const std::uint16_t a =
                read_u8(input, position);

            const std::uint16_t b =
                read_u8(input, position);

            return static_cast<std::uint16_t>(
                a | (b << 8u));
        }

        std::uint32_t read_u32(
            std::span<const std::byte> input,
            std::size_t &position)
        {
            std::uint32_t value = 0;

            for (unsigned int i = 0; i < 4; ++i)
            {
                value |=
                    static_cast<std::uint32_t>(
                        read_u8(input, position))
                    << (i * 8u);
            }

            return value;
        }

        std::uint64_t read_u64(
            std::span<const std::byte> input,
            std::size_t &position)
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

        std::uint32_t hash4(
            const std::byte *data)
        {
            /*
             * Pack the four-byte sequence in a fixed byte order, then use a
             * multiplicative mixer. The LZ matcher only needs a well-spread
             * 18-bit bucket index; selecting the high bits avoids FNV's weaker
             * low-bit distribution and replaces four serial hash rounds with
             * one multiply and one shift/xor mix.
             */
            std::uint32_t hash =
                (static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(data[0])) << 24u) |
                (static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(data[1])) << 16u) |
                (static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(data[2])) << 8u) |
                static_cast<std::uint32_t>(
                    static_cast<std::uint8_t>(data[3]));

            hash *= 0x9E3779B1u;
            hash ^= hash >> 16u;

            return (hash >> (32u - HASH_BITS)) & HASH_MASK;
        }

        struct LzToken
        {
            std::uint8_t type{};

            std::uint16_t distance{};

            std::size_t length{};

            // Literal bytes already exist in the source span. Keeping an
            // offset avoids one heap allocation per literal token.
            std::size_t literal_offset{};
        };

        Bytes lz_compress(
            std::span<const std::byte> input,
            CompressionLevel compression_level,
            const CompressionProgressCallback &progress)
        {
            if (input.empty())
            {
                return {};
            }

            const int level =
                static_cast<int>(
                    compression_level);

            const std::string_view phase =
                compression_level == CompressionLevel::Fast
                    ? "RIPC Fast"
                    : compression_level == CompressionLevel::Balanced
                          ? "RIPC Balanced"
                          : compression_level == CompressionLevel::Maximum
                                ? "RIPC Maximum"
                                : "RIPC Ultra";

            constexpr std::size_t progress_interval =
                1024 * 1024;
            std::size_t next_progress = progress_interval;

            if (progress)
            {
                progress(0, input.size(), phase);
            }

            /*
             * Match-search tuning:
             *
             * DEFLATE-quality compression depends heavily on
             * how aggressively the LZ matcher searches its
             * dictionary. RIPC keeps the same 64 KiB window,
             * but uses substantially deeper chains at higher
             * compression levels.
             *
             * Equal-length matches prefer the previous match
             * distance so MatchRepeat tokens become more common,
             * and otherwise prefer the shorter distance because
             * it is cheaper to Huffman-code.
             */
            std::size_t max_chain_length = 32;
            std::size_t nice_match_length = 512;
            std::size_t lazy_lookahead = 0;

            if (level <= 1)
            {
                max_chain_length = 32;
                nice_match_length = 512;
                lazy_lookahead = 0;
            }
            else if (level <= 5)
            {
                max_chain_length = 768;
                nice_match_length = 16 * 1024;
                lazy_lookahead = 2;
            }
            else if (level <= 9)
            {
                /*
                 * Maximum is deliberately size-first. It searches
                 * deeper and lets the matcher find essentially the
                 * longest useful match before stopping. The extra
                 * lazy lookahead catches cases where a single literal
                 * or two literals unlock a substantially better match.
                 */
                max_chain_length = 16'384;
                nice_match_length = MAX_MATCH_LENGTH;
                lazy_lookahead = 4;
            }
            else
            {
                /*
                 * Ultra performs a near-exhaustive traversal of the
                 * complete 64 KiB dictionary chain. It is intended for
                 * users who want Auto to have one more opportunity to
                 * find a smaller stream, even when that costs CPU time.
                 * Auto still keeps the smallest completed result.
                 */
                max_chain_length = WINDOW_SIZE;
                nice_match_length = MAX_MATCH_LENGTH;
                lazy_lookahead = 4;
            }

            std::vector<int> head(
                HASH_SIZE,
                -1);

            std::vector<int> previous(
                input.size(),
                -1);

            std::uint16_t last_match_distance = 0;

            auto insert_position =
                [&](std::size_t position)
            {
                if (position + 3 >=
                    input.size())
                {
                    return;
                }

                const std::uint32_t hash =
                    hash4(
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

                if (position + 3 >=
                    input.size())
                {
                    return std::pair{
                        best_length,
                        best_distance};
                }

                const std::uint32_t hash =
                    hash4(
                        input.data() +
                        position);

                int candidate =
                    head[hash];

                std::size_t chain_count = 0;

                while (
                    candidate >= 0 &&
                    chain_count <
                        max_chain_length)
                {
                    const std::size_t
                        candidate_position =
                            static_cast<std::size_t>(
                                candidate);

                    if (candidate_position >=
                        position)
                    {
                        break;
                    }

                    const std::size_t distance =
                        position -
                        candidate_position;

                    if (distance >
                        WINDOW_SIZE)
                    {
                        break;
                    }

                    if (input[candidate_position] !=
                            input[position] ||
                        input[candidate_position + 1] !=
                            input[position + 1] ||
                        input[candidate_position + 2] !=
                            input[position + 2] ||
                        input[candidate_position + 3] !=
                            input[position + 3])
                    {
                        candidate =
                            previous[candidate_position];

                        ++chain_count;
                        continue;
                    }

                    const std::size_t maximum =
                        std::min(
                            MAX_MATCH_LENGTH,
                            input.size() -
                                position);

                    std::size_t length = 4;

                    /*
                     * Compare long matches a machine word at a time.
                     * memcpy keeps the loads valid for unaligned input
                     * pointers and lets compilers lower them efficiently.
                     * If a word differs, inspect just that word bytewise so
                     * the exact match length and match selection stay the
                     * same as the original byte-at-a-time loop.
                     */
                    while (length + sizeof(std::uint64_t) <= maximum)
                    {
                        std::uint64_t current_word = 0;
                        std::uint64_t candidate_word = 0;

                        std::memcpy(
                            &current_word,
                            input.data() + position + length,
                            sizeof(current_word));

                        std::memcpy(
                            &candidate_word,
                            input.data() + candidate_position + length,
                            sizeof(candidate_word));

                        if (current_word != candidate_word)
                        {
                            const std::size_t word_offset = length;
                            std::size_t matching_bytes = 0;

                            while (
                                matching_bytes < sizeof(std::uint64_t) &&
                                input[position + word_offset + matching_bytes] ==
                                    input[candidate_position + word_offset + matching_bytes])
                            {
                                ++matching_bytes;
                            }

                            length += matching_bytes;
                            break;
                        }

                        length += sizeof(std::uint64_t);
                    }

                    while (
                        length < maximum &&
                        input[position + length] ==
                            input[candidate_position + length])
                    {
                        ++length;
                    }

                    bool better_match = false;

                    if (level > 9)
                    {
                        /*
                         * Ultra estimates the encoded cost, not just the
                         * raw length. Reusing the previous distance avoids
                         * two distance bytes, while lengths above 257 need
                         * two extra length bytes. This can make a slightly
                         * shorter repeat-distance match cheaper overall.
                         */
                        const std::int64_t candidate_score =
                            static_cast<std::int64_t>(length) -
                            (distance == last_match_distance ? 0 : 2) -
                            (length > 257 ? 2 : 0);

                        const std::int64_t best_score =
                            best_length == 0
                                ? std::numeric_limits<std::int64_t>::min()
                                : static_cast<std::int64_t>(best_length) -
                                      (best_distance == last_match_distance ? 0 : 2) -
                                      (best_length > 257 ? 2 : 0);

                        better_match =
                            candidate_score > best_score ||
                            (
                                candidate_score == best_score &&
                                (
                                    (
                                        distance == last_match_distance &&
                                        best_distance != last_match_distance
                                    ) ||
                                    (
                                        (distance == last_match_distance) ==
                                            (best_distance == last_match_distance) &&
                                        distance < best_distance
                                    )
                                )
                            );
                    }
                    else
                    {
                        better_match =
                            length > best_length ||
                            (
                                length == best_length &&
                                length >= MIN_MATCH_LENGTH &&
                                (
                                    (
                                        distance == last_match_distance &&
                                        best_distance != last_match_distance
                                    ) ||
                                    distance < best_distance
                                )
                            );
                    }

                    if (better_match)
                    {
                        best_length = length;
                        best_distance = distance;

                        if (length >= nice_match_length ||
                            length == maximum)
                        {
                            break;
                        }
                    }

                    candidate =
                        previous[candidate_position];

                    ++chain_count;
                }

                return std::pair{
                    best_length,
                    best_distance};
            };

            /*
             * Evaluate a match that starts a few bytes later without
             * permanently adding those speculative positions to the
             * dictionary. This fixes the old lazy matcher limitation
             * where position+1 could not see the current position as
             * a dictionary candidate.
             */
            auto find_match_after_literals =
                [&](std::size_t position,
                    std::size_t literal_count)
            {
                struct TemporaryInsert
                {
                    std::size_t position{};
                    std::uint32_t hash{};
                    int old_head{-1};
                    int old_previous{-1};
                };

                std::array<
                    TemporaryInsert,
                    4>
                    temporary{};

                std::size_t inserted = 0;

                for (std::size_t i = 0;
                     i < literal_count &&
                     i < temporary.size();
                     ++i)
                {
                    const std::size_t
                        temporary_position =
                            position + i;

                    if (temporary_position + 3 >=
                        input.size())
                    {
                        break;
                    }

                    const std::uint32_t hash =
                        hash4(
                            input.data() +
                            temporary_position);

                    temporary[inserted] =
                        TemporaryInsert{
                            .position =
                                temporary_position,
                            .hash = hash,
                            .old_head = head[hash],
                            .old_previous =
                                previous[
                                    temporary_position]};

                    previous[temporary_position] =
                        head[hash];

                    head[hash] =
                        static_cast<int>(
                            temporary_position);

                    ++inserted;
                }

                const auto result =
                    find_match(
                        position +
                        literal_count);

                while (inserted > 0)
                {
                    --inserted;

                    const auto& item =
                        temporary[inserted];

                    head[item.hash] =
                        item.old_head;

                    previous[item.position] =
                        item.old_previous;
                }

                return result;
            };

            std::vector<LzToken> tokens;

            tokens.reserve(
                input.size() / 2 +
                1);

            std::size_t position = 0;

            while (position <
                   input.size())
            {
                if (progress && position >= next_progress)
                {
                    progress(position, input.size(), phase);
                    next_progress = position + progress_interval;
                }

                auto [match_length,
                      match_distance] =
                    find_match(position);

                if (lazy_lookahead != 0 &&
                    match_length >=
                        MIN_MATCH_LENGTH)
                {
                    const auto current_score =
                        static_cast<long long>(
                            match_length) +
                        (
                            match_distance ==
                                    last_match_distance
                                ? 2
                                : 0);

                    std::size_t best_skip = 0;
                    std::size_t best_length =
                        match_length;
                    std::size_t best_distance =
                        match_distance;

                    for (std::size_t skip = 1;
                         skip <= lazy_lookahead;
                         ++skip)
                    {
                        if (position + skip + 3 >=
                            input.size())
                        {
                            break;
                        }

                        const auto [next_length,
                                    next_distance] =
                            find_match_after_literals(
                                position,
                                skip);

                        if (next_length <
                            MIN_MATCH_LENGTH)
                        {
                            continue;
                        }

                        const auto candidate_score =
                            static_cast<long long>(
                                next_length) -
                            static_cast<long long>(
                                skip) +
                            (
                                next_distance ==
                                        last_match_distance
                                    ? 2
                                    : 0);

                        if (candidate_score >
                            current_score)
                        {
                            if (best_skip == 0 ||
                                candidate_score >
                                    static_cast<
                                        long long>(
                                        best_length) -
                                    static_cast<
                                        long long>(
                                        best_skip))
                            {
                                best_skip = skip;
                                best_length =
                                    next_length;
                                best_distance =
                                    next_distance;
                            }
                        }
                    }

                    if (best_skip != 0)
                    {
                        (void)best_distance;

                        for (std::size_t i = 0;
                             i < best_skip;
                             ++i)
                        {
                            LzToken token;

                            token.type = 0;

                            token.literal_offset = position;

                            tokens.push_back(
                                std::move(token));

                            insert_position(position);
                            ++position;
                        }

                        continue;
                    }
                }

                if (match_length >=
                    MIN_MATCH_LENGTH)
                {
                    LzToken token;

                    token.type =
                        match_distance ==
                                last_match_distance
                            ? 3
                            : 1;

                    token.distance =
                        static_cast<std::uint16_t>(
                            match_distance);

                    token.length =
                        match_length;

                    last_match_distance =
                        static_cast<std::uint16_t>(
                            match_distance);

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

                while (position <
                       input.size())
                {
                    if (progress && position >= next_progress)
                    {
                        progress(position, input.size(), phase);
                        next_progress = position + progress_interval;
                    }

                    const auto [next_match_length,
                                unused_distance] =
                        find_match(position);

                    (void)unused_distance;

                    if (next_match_length >=
                        MIN_MATCH_LENGTH)
                    {
                        break;
                    }

                    insert_position(position);
                    ++position;

                    if (position -
                            run_start >=
                        255)
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

                    token.literal_offset = run_start;

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

                        token.literal_offset = run_start + i;

                        tokens.push_back(
                            std::move(token));
                    }
                }
            }

            Bytes output;

            output.reserve(
                input.size() + 32);

            std::size_t token_index = 0;

            while (token_index <
                   tokens.size())
            {
                const std::size_t
                    control_position =
                        output.size();

                output.push_back(
                    static_cast<std::byte>(0));

                std::uint8_t control = 0;

                for (unsigned int slot = 0;
                     slot < 4 &&
                     token_index <
                         tokens.size();
                     ++slot,
                                  ++token_index)
                {
                    const LzToken &token =
                        tokens[token_index];

                    control |=
                        static_cast<std::uint8_t>(
                            token.type << (slot * 2u));

                    if (token.type == 0)
                    {
                        output.push_back(input[token.literal_offset]);
                    }
                    else if (token.type == 1 ||
                             token.type == 3)
                    {
                        // Type 1 stores a new distance.
                        // Type 3 reuses the previous match distance.
                        if (token.type == 1)
                        {
                            write_u16(
                                output,
                                token.distance);
                        }

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
                        write_u8(output, static_cast<std::uint8_t>(token.length));

                        output.insert(
                            output.end(),
                            input.begin() +
                                static_cast<std::ptrdiff_t>(token.literal_offset),
                            input.begin() +
                                static_cast<std::ptrdiff_t>(
                                    token.literal_offset + token.length));
                    }
                }

                output[control_position] =
                    static_cast<std::byte>(
                        control);
            }

            if (progress)
            {
                progress(input.size(), input.size(), phase);
            }

            return output;
        }

        bool lz_decompress(
            std::span<const std::byte> input,
            std::uint64_t original_size,
            Bytes &output,
            std::string *error)
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

            /*
             * Expand LZ matches in bulk. When the distance covers the full
             * match, one memcpy is sufficient. For overlapping matches,
             * copy the initial period then double the available pattern;
             * each memcpy's source and destination do not overlap.
             */
            auto append_match =
                [&](std::size_t distance, std::size_t length)
            {
                const std::size_t destination_offset = output.size();
                output.resize(destination_offset + length);

                std::byte* const destination =
                    output.data() + destination_offset;

                if (distance >= length)
                {
                    std::memcpy(
                        destination,
                        destination - distance,
                        length);
                    return;
                }

                std::memcpy(
                    destination,
                    destination - distance,
                    distance);

                std::size_t copied = distance;

                while (copied < length)
                {
                    const std::size_t count =
                        std::min(copied, length - copied);

                    std::memcpy(
                        destination + copied,
                        destination,
                        count);

                    copied += count;
                }
            };

            std::size_t position = 0;
            std::uint16_t last_match_distance = 0;

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

                        last_match_distance =
                            distance;

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

                        append_match(distance, length);

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

                    case 3:
                    {
                        if (last_match_distance == 0)
                        {
                            set_error(
                                error,
                                "Repeated RIPC match has no previous distance.");

                            return false;
                        }

                        if (position >=
                            input.size())
                        {
                            set_error(
                                error,
                                "Truncated repeated RIPC match.");

                            return false;
                        }

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
                                    "Truncated extended repeated RIPC match.");

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

                        if (output.size() + length >
                            static_cast<std::size_t>(
                                original_size))
                        {
                            set_error(
                                error,
                                "RIPC repeated match exceeds output size.");

                            return false;
                        }

                        if (last_match_distance > output.size())
                        {
                            set_error(
                                error,
                                "Invalid repeated RIPC distance.");

                            return false;
                        }

                        append_match(last_match_distance, length);

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

        struct HuffmanNode
        {
            std::uint64_t frequency{};
            int left{-1};
            int right{-1};
            int symbol{-1};
        };

        struct HuffmanCompare
        {
            const std::vector<HuffmanNode> *nodes{};

            bool operator()(
                int a,
                int b) const
            {
                if ((*nodes)[a].frequency !=
                    (*nodes)[b].frequency)
                {
                    return (*nodes)[a].frequency >
                           (*nodes)[b].frequency;
                }

                return a > b;
            }
        };

        bool build_huffman_lengths(
            std::span<const std::byte> input,
            std::array<std::uint8_t, HUFFMAN_SYMBOLS> &lengths)
        {
            lengths.fill(0);

            std::array<std::uint64_t, HUFFMAN_SYMBOLS>
                frequencies{};

            for (const std::byte value : input)
            {
                ++frequencies[static_cast<std::uint8_t>(
                    value)];
            }

            std::vector<HuffmanNode> nodes;

            nodes.reserve(
                HUFFMAN_SYMBOLS * 2);

            for (std::size_t symbol = 0;
                 symbol < HUFFMAN_SYMBOLS;
                 ++symbol)
            {
                if (frequencies[symbol] == 0)
                {
                    continue;
                }

                HuffmanNode node;

                node.frequency =
                    frequencies[symbol];

                node.symbol =
                    static_cast<int>(
                        symbol);

                nodes.push_back(node);
            }

            if (nodes.empty())
            {
                return true;
            }

            if (nodes.size() == 1)
            {
                lengths[static_cast<std::size_t>(
                    nodes[0].symbol)] = 1;

                return true;
            }

            HuffmanCompare comparator;

            comparator.nodes =
                &nodes;

            std::priority_queue<
                int,
                std::vector<int>,
                HuffmanCompare>
                queue(comparator);

            for (std::size_t i = 0;
                 i < nodes.size();
                 ++i)
            {
                queue.push(
                    static_cast<int>(i));
            }

            while (queue.size() > 1)
            {
                const int left =
                    queue.top();

                queue.pop();

                const int right =
                    queue.top();

                queue.pop();

                HuffmanNode parent;

                parent.frequency =
                    nodes[left].frequency +
                    nodes[right].frequency;

                parent.left =
                    left;

                parent.right =
                    right;

                nodes.push_back(parent);

                queue.push(
                    static_cast<int>(
                        nodes.size() - 1));
            }

            const int root =
                queue.top();

            bool valid = true;

            std::function<void(int, std::uint16_t)>
                visit =
                    [&](int index,
                        std::uint16_t depth)
            {
                if (!valid)
                {
                    return;
                }

                const HuffmanNode &node =
                    nodes[static_cast<std::size_t>(
                        index)];

                if (node.symbol >= 0)
                {
                    const std::uint16_t actual_depth =
                        std::max<std::uint16_t>(
                            depth,
                            1);

                    if (actual_depth > 255)
                    {
                        valid = false;
                        return;
                    }

                    lengths[static_cast<std::size_t>(
                        node.symbol)] =
                        static_cast<std::uint8_t>(
                            actual_depth);

                    return;
                }

                if (node.left < 0 ||
                    node.right < 0)
                {
                    valid = false;
                    return;
                }

                visit(
                    node.left,
                    static_cast<std::uint16_t>(
                        depth + 1));

                visit(
                    node.right,
                    static_cast<std::uint16_t>(
                        depth + 1));
            };

            visit(root, 0);

            return valid;
        }

        bool increment_code(
            std::vector<std::uint8_t> &code)
        {
            if (code.empty())
            {
                return false;
            }

            for (std::size_t i = code.size();
                 i > 0;
                 --i)
            {
                const std::size_t index =
                    i - 1;

                if (code[index] == 0)
                {
                    code[index] = 1;
                    return true;
                }

                code[index] = 0;
            }

            return false;
        }

        bool build_canonical_codes(
            const std::array<
                std::uint8_t,
                HUFFMAN_SYMBOLS> &lengths,
            std::array<
                std::vector<std::uint8_t>,
                HUFFMAN_SYMBOLS> &codes)
        {
            for (auto &code : codes)
            {
                code.clear();
            }

            std::vector<int> symbols;

            symbols.reserve(
                HUFFMAN_SYMBOLS);

            for (std::size_t symbol = 0;
                 symbol < HUFFMAN_SYMBOLS;
                 ++symbol)
            {
                if (lengths[symbol] != 0)
                {
                    symbols.push_back(
                        static_cast<int>(
                            symbol));
                }
            }

            std::sort(
                symbols.begin(),
                symbols.end(),
                [&](int a, int b)
                {
                    if (lengths[static_cast<std::size_t>(
                            a)] !=
                        lengths[static_cast<std::size_t>(
                            b)])
                    {
                        return lengths[static_cast<std::size_t>(
                                   a)] <
                               lengths[static_cast<std::size_t>(
                                   b)];
                    }

                    return a < b;
                });

            if (symbols.empty())
            {
                return true;
            }

            std::vector<std::uint8_t>
                current;

            bool first = true;

            for (const int symbol : symbols)
            {
                const std::uint8_t length =
                    lengths[static_cast<std::size_t>(
                        symbol)];

                if (first)
                {
                    current.assign(
                        length,
                        0);

                    first = false;
                }
                else
                {
                    if (!increment_code(current))
                    {
                        return false;
                    }

                    if (current.size() >
                        length)
                    {
                        return false;
                    }

                    current.resize(
                        length,
                        0);
                }

                codes[static_cast<std::size_t>(
                    symbol)] =
                    current;
            }

            return true;
        }

        class BitWriter
        {
        public:
            void write(
                const std::vector<std::uint8_t> &bits)
            {
                for (const std::uint8_t bit : bits)
                {
                    current_ =
                        static_cast<std::uint8_t>(
                            (current_ << 1u) |
                            (bit & 1u));

                    ++bit_count_;

                    if (bit_count_ == 8)
                    {
                        output_.push_back(
                            static_cast<std::byte>(
                                current_));

                        current_ = 0;
                        bit_count_ = 0;
                    }
                }
            }

            void write_bit(
                std::uint8_t bit)
            {
                current_ =
                    static_cast<std::uint8_t>(
                        (current_ << 1u) |
                        (bit & 1u));

                ++bit_count_;

                if (bit_count_ == 8)
                {
                    output_.push_back(
                        static_cast<std::byte>(
                            current_));

                    current_ = 0;
                    bit_count_ = 0;
                }
            }

            std::vector<std::byte> finish()
            {
                if (bit_count_ != 0)
                {
                    current_ =
                        static_cast<std::uint8_t>(
                            current_
                            << (8u - bit_count_));

                    output_.push_back(
                        static_cast<std::byte>(
                            current_));
                }

                return std::move(output_);
            }

        private:
            std::vector<std::byte> output_;
            std::uint8_t current_{};
            std::uint8_t bit_count_{};
        };

        class BitReader
        {
        public:
            explicit BitReader(
                std::span<const std::byte> input)
                : input_(input)
            {
            }

            bool read(
                std::uint8_t &bit)
            {
                if (position_ >= input_.size())
                {
                    return false;
                }

                const std::uint8_t value =
                    static_cast<std::uint8_t>(
                        input_[position_]);

                bit =
                    static_cast<std::uint8_t>(
                        (value >>
                         (7u - bit_count_)) &
                        1u);

                ++bit_count_;

                if (bit_count_ == 8)
                {
                    bit_count_ = 0;
                    ++position_;
                }

                return true;
            }

        private:
            std::span<const std::byte> input_;
            std::size_t position_{};
            std::uint8_t bit_count_{};
        };

        bool huffman_compress(
            std::span<const std::byte> input,
            std::vector<std::byte> &output)
        {
            output.clear();

            if (input.empty())
            {
                return false;
            }

            std::array<
                std::uint8_t,
                HUFFMAN_SYMBOLS>
                lengths{};

            if (!build_huffman_lengths(
                    input,
                    lengths))
            {
                return false;
            }

            std::array<
                std::vector<std::uint8_t>,
                HUFFMAN_SYMBOLS>
                codes;

            if (!build_canonical_codes(
                    lengths,
                    codes))
            {
                return false;
            }

            write_u64(
                output,
                static_cast<std::uint64_t>(
                    input.size()));

            for (const std::uint8_t length :
                 lengths)
            {
                write_u8(
                    output,
                    length);
            }

            BitWriter writer;

            for (const std::byte value :
                 input)
            {
                const std::size_t symbol =
                    static_cast<std::uint8_t>(
                        value);

                writer.write(
                    codes[symbol]);
            }

            const auto encoded =
                writer.finish();

            output.insert(
                output.end(),
                encoded.begin(),
                encoded.end());

            return true;
        }

        bool huffman_decompress(
            std::span<const std::byte> input,
            std::vector<std::byte> &output,
            std::string *error)
        {
            output.clear();

            constexpr std::size_t METADATA_SIZE =
                sizeof(std::uint64_t) +
                HUFFMAN_SYMBOLS;

            if (input.size() < METADATA_SIZE)
            {
                set_error(
                    error,
                    "RIPC Huffman stream is too small.");

                return false;
            }

            std::size_t position = 0;

            const std::uint64_t raw_size =
                read_u64(
                    input,
                    position);

            if (raw_size >
                static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::size_t>::max()))
            {
                set_error(
                    error,
                    "RIPC Huffman output is too large.");

                return false;
            }

            std::array<
                std::uint8_t,
                HUFFMAN_SYMBOLS>
                lengths{};

            for (std::size_t i = 0;
                 i < HUFFMAN_SYMBOLS;
                 ++i)
            {
                lengths[i] =
                    read_u8(
                        input,
                        position);
            }

            std::array<
                std::vector<std::uint8_t>,
                HUFFMAN_SYMBOLS>
                codes;

            if (!build_canonical_codes(
                    lengths,
                    codes))
            {
                set_error(
                    error,
                    "Invalid RIPC Huffman table.");

                return false;
            }

            struct DecodeNode
            {
                int child[2] = {-1, -1};
                int symbol = -1;
            };

            std::vector<DecodeNode>
                tree(1);

            for (std::size_t symbol = 0;
                 symbol < HUFFMAN_SYMBOLS;
                 ++symbol)
            {
                if (codes[symbol].empty())
                {
                    continue;
                }

                int node = 0;

                for (const std::uint8_t bit :
                     codes[symbol])
                {
                    if (tree[node].symbol >= 0)
                    {
                        set_error(
                            error,
                            "Invalid RIPC Huffman tree.");

                        return false;
                    }

                    if (tree[node].child[bit] < 0)
                    {
                        tree[node].child[bit] =
                            static_cast<int>(
                                tree.size());

                        tree.push_back(
                            DecodeNode{});
                    }

                    node =
                        tree[node].child[bit];
                }

                if (tree[node].symbol >= 0 ||
                    tree[node].child[0] >= 0 ||
                    tree[node].child[1] >= 0)
                {
                    set_error(
                        error,
                        "Invalid RIPC Huffman codes.");

                    return false;
                }

                tree[node].symbol =
                    static_cast<int>(
                        symbol);
            }

            BitReader reader(
                input.subspan(position));

            output.reserve(
                static_cast<std::size_t>(
                    raw_size));

            int node = 0;

            while (
                output.size() <
                static_cast<std::size_t>(
                    raw_size))
            {
                std::uint8_t bit = 0;

                if (!reader.read(bit))
                {
                    set_error(
                        error,
                        "Unexpected end of RIPC Huffman stream.");

                    return false;
                }

                const int next =
                    tree[node].child[bit];

                if (next < 0)
                {
                    set_error(
                        error,
                        "Invalid RIPC Huffman bitstream.");

                    return false;
                }

                node = next;

                if (tree[node].symbol >= 0)
                {
                    output.push_back(
                        static_cast<std::byte>(
                            tree[node].symbol));

                    node = 0;
                }
            }

            return true;
        }

    } // namespace

    static bool compress_single(
        std::span<const std::byte> input,
        std::vector<std::byte> &output,
        std::string *error,
        CompressionLevel level,
        CompressionProgressCallback progress)
    {
        output.clear();

        const auto raw =
            lz_compress(
                input,
                level,
                progress);

        // Existing v0.5 byte-oriented Huffman path.
        std::vector<std::byte>
            huffman_payload;

        const bool huffman_valid =
            huffman_compress(
                raw,
                huffman_payload);

        /*
         * Token-Huffman construction is substantially more
         * expensive than the byte-Huffman path. If the LZ
         * stream is already materially larger than the input,
         * it is overwhelmingly dominated by literals/control
         * bytes and a stored block is the useful fallback.
         *
         * Keep a small-input allowance so short files still
         * get the best adaptive choice.
         */
        const std::size_t token_huffman_slack =
            std::max<std::size_t>(
                4096,
                input.size() / 16);

        const bool try_token_huffman =
            input.size() <= 8192 ||
            raw.size() <=
                input.size() +
                token_huffman_slack;

        std::vector<std::byte>
            token_huffman_payload;

        bool token_huffman_valid = false;

        if (try_token_huffman)
        {
            std::vector<Token>
                tokens;

            if (!decode_tokens(
                    raw,
                    tokens,
                    error))
            {
                return false;
            }

            token_huffman_valid =
                huffman_encode_tokens(
                    tokens,
                    token_huffman_payload,
                    error);
        }

        std::uint8_t flags = 0;

        std::span<const std::byte>
            selected_payload =
                raw;

        /*
         * RIPC can now choose a true stored block.
         * This is important for incompressible data:
         * the LZ token stream carries control-byte
         * overhead, while a stored block preserves the
         * original bytes exactly.
         */
        if (input.size() <
            selected_payload.size())
        {
            flags =
                FLAG_STORED;

            selected_payload =
                input;
        }

        if (huffman_valid &&
            huffman_payload.size() <
                selected_payload.size())
        {
            flags =
                FLAG_HUFFMAN;

            selected_payload =
                huffman_payload;
        }

        if (token_huffman_valid &&
            token_huffman_payload.size() <
                selected_payload.size())
        {
            flags =
                FLAG_TOKEN_HUFFMAN;

            selected_payload =
                token_huffman_payload;
        }

        output.reserve(
            16 +
            selected_payload.size());

        output.insert(
            output.end(),
            reinterpret_cast<
                const std::byte *>(
                MAGIC),
            reinterpret_cast<
                const std::byte *>(
                MAGIC + 4));

        write_u8(
            output,
            RIPC_VERSION);

        write_u8(
            output,
            flags);

        write_u16(
            output,
            0);

        write_u64(
            output,
            static_cast<std::uint64_t>(
                input.size()));

        output.insert(
            output.end(),
            selected_payload.begin(),
            selected_payload.end());

        return true;
    }

    bool compress(
        std::span<const std::byte> input,
        std::vector<std::byte> &output,
        std::string *error,
        CompressionLevel level,
        CompressionProgressCallback progress)
    {
        output.clear();

        if (error)
        {
            error->clear();
        }

        constexpr std::array<CompressionLevel, 4> profiles =
        {
            CompressionLevel::Fast,
            CompressionLevel::Balanced,
            CompressionLevel::Maximum,
            CompressionLevel::Ultra
        };

        // Retain the original single-stream layout for small inputs.
        if (input.size() <= PARALLEL_MIN_SIZE)
        {
            if (level != CompressionLevel::Auto)
            {
                return compress_single(
                    input,
                    output,
                    error,
                    level,
                    std::move(progress));
            }

            Bytes best_output;
            bool have_best = false;

            for (const CompressionLevel profile : profiles)
            {
                Bytes candidate;
                std::string candidate_error;

                if (!compress_single(
                        input,
                        candidate,
                        &candidate_error,
                        profile,
                        progress))
                {
                    const std::string message =
                        candidate_error.empty()
                            ? "RIPC compression profile failed."
                            : candidate_error;
                    set_error(error, message.c_str());
                    output.clear();
                    return false;
                }

                if (!have_best ||
                    candidate.size() < best_output.size())
                {
                    best_output = std::move(candidate);
                    have_best = true;
                }
            }

            output = std::move(best_output);
            return have_best;
        }

        const std::size_t chunk_count =
            1 + ((input.size() - 1) / PARALLEL_CHUNK_SIZE);

        if (chunk_count >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()))
        {
            set_error(error, "RIPC input requires too many parallel chunks.");
            return false;
        }

        struct EncodedChunk
        {
            Bytes bytes;
            std::string error;
            bool success = false;
            std::size_t completed_profiles = 0;
            std::size_t best_profile_index =
                std::numeric_limits<std::size_t>::max();
        };

        std::vector<EncodedChunk> chunks(chunk_count);
        std::vector<std::uint64_t> progress_by_chunk(chunk_count, 0);

        const std::size_t profile_count =
            level == CompressionLevel::Auto ? profiles.size() : 1;

        if (chunk_count >
            std::numeric_limits<std::size_t>::max() / profile_count)
        {
            set_error(error, "RIPC input requires too many parallel profile tasks.");
            return false;
        }

        const std::size_t task_count =
            chunk_count * profile_count;

        const std::uint64_t bytes_total =
            static_cast<std::uint64_t>(input.size());

        std::atomic<std::size_t> next_task{0};
        std::atomic<bool> cancelled{false};
        std::mutex progress_mutex;
        std::mutex results_mutex;

        if (progress)
        {
            progress(0, bytes_total, "RIPC parallel compression");
        }

        auto run_profile =
            [&](std::size_t chunk_index,
                std::size_t profile_index,
                CompressionLevel profile,
                Bytes &candidate,
                std::string &candidate_error) -> bool
        {
            const std::size_t offset =
                chunk_index * PARALLEL_CHUNK_SIZE;

            const std::size_t chunk_size =
                std::min(
                    PARALLEL_CHUNK_SIZE,
                    input.size() - offset);

            CompressionProgressCallback chunk_progress;

            if (progress)
            {
                chunk_progress =
                    [&, chunk_index, profile_index, chunk_size](
                        std::uint64_t processed,
                        std::uint64_t,
                        std::string_view phase)
                {
                    std::lock_guard<std::mutex> lock(progress_mutex);

                    const std::uint64_t safe_processed =
                        std::min<std::uint64_t>(
                            processed,
                            static_cast<std::uint64_t>(chunk_size));

                    progress_by_chunk[chunk_index] =
                        static_cast<std::uint64_t>(
                            profile_index * chunk_size) +
                        safe_processed;

                    std::uint64_t total_processed = 0;

                    for (const std::uint64_t amount : progress_by_chunk)
                    {
                        total_processed += amount;
                    }

                    progress(
                        std::min<std::uint64_t>(
                            total_processed / profile_count,
                            bytes_total),
                        bytes_total,
                        phase);
                };
            }

            return compress_single(
                input.subspan(offset, chunk_size),
                candidate,
                &candidate_error,
                profile,
                std::move(chunk_progress));
        };

        /*
         * Schedule each (chunk, profile) pair independently. Auto still
         * evaluates every profile for every chunk and selects the smallest
         * result, but short and uneven inputs no longer leave CPU workers
         * idle while one chunk runs its three profiles serially.
         */
        auto worker_function = [&]()
        {
            while (!cancelled.load(std::memory_order_relaxed))
            {
                const std::size_t task_index =
                    next_task.fetch_add(1, std::memory_order_relaxed);

                if (task_index >= task_count)
                {
                    return;
                }

                if (cancelled.load(std::memory_order_relaxed))
                {
                    return;
                }

                const std::size_t chunk_index =
                    task_index / profile_count;

                const std::size_t profile_index =
                    task_index % profile_count;

                const CompressionLevel profile =
                    level == CompressionLevel::Auto
                        ? profiles[profile_index]
                        : level;

                EncodedChunk &result = chunks[chunk_index];

                try
                {
                    Bytes candidate;
                    std::string candidate_error;

                    if (!run_profile(
                            chunk_index,
                            profile_index,
                            profile,
                            candidate,
                            candidate_error))
                    {
                        {
                            std::lock_guard<std::mutex> lock(results_mutex);

                            if (result.error.empty())
                            {
                                result.error =
                                    candidate_error.empty()
                                        ? "RIPC parallel chunk compression failed."
                                        : std::move(candidate_error);
                            }
                        }

                        cancelled.store(true, std::memory_order_relaxed);
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(results_mutex);

                        /*
                         * Resolve equal-sized candidates by profile order.
                         * This preserves Auto's previous deterministic tie
                         * breaking (Fast, then Balanced, then Maximum),
                         * regardless of worker completion order.
                         */
                        if (!result.success ||
                            candidate.size() < result.bytes.size() ||
                            (candidate.size() == result.bytes.size() &&
                             profile_index < result.best_profile_index))
                        {
                            result.bytes = std::move(candidate);
                            result.best_profile_index = profile_index;
                            result.success = true;
                        }

                        ++result.completed_profiles;
                    }
                }
                catch (const std::exception &exception)
                {
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);

                        if (result.error.empty())
                        {
                            result.error = exception.what();
                        }
                    }

                    cancelled.store(true, std::memory_order_relaxed);
                    return;
                }
                catch (...)
                {
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);

                        if (result.error.empty())
                        {
                            result.error =
                                "Unknown error during RIPC parallel compression.";
                        }
                    }

                    cancelled.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        };

        const unsigned int reported_workers =
            std::thread::hardware_concurrency();

        const std::size_t worker_count =
            std::max<std::size_t>(
                1,
                std::min(
                    task_count,
                    reported_workers == 0
                        ? std::size_t{1}
                        : static_cast<std::size_t>(reported_workers)));

        std::vector<std::thread> workers;
        workers.reserve(worker_count);

        try
        {
            for (std::size_t i = 0; i < worker_count; ++i)
            {
                workers.emplace_back(worker_function);
            }
        }
        catch (...)
        {
            cancelled.store(true, std::memory_order_relaxed);

            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }

            set_error(error, "Unable to start RIPC worker threads.");
            output.clear();
            return false;
        }

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        for (const EncodedChunk &chunk : chunks)
        {
            if (!chunk.success ||
                chunk.completed_profiles != profile_count)
            {
                std::string failure =
                    "RIPC parallel compression did not complete.";

                for (const EncodedChunk &candidate : chunks)
                {
                    if (!candidate.error.empty())
                    {
                        failure = candidate.error;
                        break;
                    }
                }

                set_error(error, failure.c_str());
                output.clear();
                return false;
            }
        }

        Bytes payload;

        payload.reserve(
            4 + chunk_count * 8 + input.size());

        write_u32(
            payload,
            static_cast<std::uint32_t>(chunk_count));

        for (std::size_t i = 0; i < chunk_count; ++i)
        {
            const std::size_t offset =
                i * PARALLEL_CHUNK_SIZE;

            const std::size_t original_chunk_size =
                std::min(
                    PARALLEL_CHUNK_SIZE,
                    input.size() - offset);

            if (chunks[i].bytes.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                set_error(error, "An RIPC compressed chunk exceeds the format limit.");
                output.clear();
                return false;
            }

            write_u32(
                payload,
                static_cast<std::uint32_t>(original_chunk_size));

            write_u32(
                payload,
                static_cast<std::uint32_t>(chunks[i].bytes.size()));

            payload.insert(
                payload.end(),
                chunks[i].bytes.begin(),
                chunks[i].bytes.end());
        }

        output.reserve(16 + payload.size());

        output.insert(
            output.end(),
            reinterpret_cast<const std::byte *>(MAGIC),
            reinterpret_cast<const std::byte *>(MAGIC + 4));

        write_u8(output, RIPC_VERSION);
        write_u8(output, FLAG_CHUNKED);
        write_u16(output, 0);
        write_u64(output, bytes_total);

        output.insert(
            output.end(),
            payload.begin(),
            payload.end());

        if (error)
        {
            error->clear();
        }

        return true;
    }

    bool decompress(
        std::span<const std::byte> input,
        std::vector<std::byte> &output,
        std::string *error)
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

        if (version != 5 &&
            version != 6 &&
            version != 7 &&
            version != 8 &&
            version != 10 &&
            version != RIPC_VERSION)
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

        const std::uint8_t supported_flags =
            FLAG_HUFFMAN |
            FLAG_TOKEN_HUFFMAN |
            FLAG_STORED |
            FLAG_CHUNKED;

        if ((flags &
             static_cast<std::uint8_t>(
                 ~supported_flags)) != 0)
        {
            set_error(
                error,
                "Unsupported RIPC flags.");

            return false;
        }

        const unsigned int compression_flags =
            ((flags & FLAG_HUFFMAN) != 0 ? 1u : 0u) +
            ((flags & FLAG_TOKEN_HUFFMAN) != 0 ? 1u : 0u) +
            ((flags & FLAG_STORED) != 0 ? 1u : 0u);

        if (compression_flags > 1)
        {
            set_error(
                error,
                "Conflicting RIPC compression flags.");

            return false;
        }

        (void)read_u16(
            input,
            position);

        const std::uint64_t original_size =
            read_u64(
                input,
                position);

        const auto payload =
            input.subspan(position);

        if ((flags & FLAG_CHUNKED) != 0)
        {
            if (version != RIPC_VERSION ||
                compression_flags != 0)
            {
                set_error(
                    error,
                    "Invalid RIPC chunked-stream flags or version.");
                return false;
            }

            if (original_size >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()))
            {
                set_error(
                    error,
                    "RIPC chunked output is too large.");
                return false;
            }

            std::size_t chunk_position = 0;

            if (payload.size() < 4)
            {
                set_error(error, "Truncated RIPC chunk table.");
                return false;
            }

            const std::uint32_t chunk_count =
                read_u32(payload, chunk_position);

            if (chunk_count == 0 ||
                chunk_count >
                    (payload.size() - chunk_position) / 8)
            {
                set_error(error, "Invalid RIPC chunk count.");
                return false;
            }

            const std::size_t expected_size =
                static_cast<std::size_t>(original_size);

            struct ChunkDescriptor
            {
                std::uint32_t original_size{};
                std::span<const std::byte> compressed;
            };

            std::vector<ChunkDescriptor> descriptors;
            descriptors.reserve(chunk_count);

            std::size_t total_original_size = 0;

            /*
             * Validate the complete table before starting workers. This
             * prevents malformed sizes from underflowing output arithmetic
             * and makes each chunk's input/output range independent.
             */
            for (std::uint32_t i = 0; i < chunk_count; ++i)
            {
                if (payload.size() - chunk_position < 8)
                {
                    set_error(error, "Truncated RIPC chunk header.");
                    return false;
                }

                const std::uint32_t chunk_original_size =
                    read_u32(payload, chunk_position);

                const std::uint32_t chunk_compressed_size =
                    read_u32(payload, chunk_position);

                if (chunk_original_size == 0 ||
                    total_original_size > expected_size ||
                    chunk_original_size > expected_size - total_original_size ||
                    chunk_compressed_size >
                        payload.size() - chunk_position)
                {
                    set_error(error, "Invalid RIPC chunk sizes.");
                    return false;
                }

                descriptors.push_back(
                    ChunkDescriptor{
                        .original_size = chunk_original_size,
                        .compressed = payload.subspan(
                            chunk_position,
                            chunk_compressed_size)});

                chunk_position += chunk_compressed_size;
                total_original_size += chunk_original_size;
            }

            if (chunk_position != payload.size() ||
                total_original_size != expected_size)
            {
                set_error(error, "RIPC chunked output size mismatch.");
                return false;
            }

            std::vector<Bytes> decoded_chunks(chunk_count);
            std::vector<std::string> chunk_errors(chunk_count);
            std::atomic<std::size_t> next_chunk{0};
            std::atomic<bool> failed{false};

            auto worker_function = [&]()
            {
                while (!failed.load(std::memory_order_relaxed))
                {
                    const std::size_t index =
                        next_chunk.fetch_add(1, std::memory_order_relaxed);

                    if (index >= descriptors.size())
                    {
                        return;
                    }

                    std::string& chunk_error = chunk_errors[index];

                    try
                    {
                        if (!decompress(
                                descriptors[index].compressed,
                                decoded_chunks[index],
                                &chunk_error))
                        {
                            if (chunk_error.empty())
                            {
                                chunk_error = "RIPC chunk decompression failed.";
                            }

                            failed.store(true, std::memory_order_relaxed);
                            return;
                        }

                        if (decoded_chunks[index].size() !=
                            descriptors[index].original_size)
                        {
                            chunk_error = "RIPC chunk output size mismatch.";
                            failed.store(true, std::memory_order_relaxed);
                            return;
                        }
                    }
                    catch (const std::exception& exception)
                    {
                        chunk_error = exception.what();
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    catch (...)
                    {
                        chunk_error = "Unknown error during parallel RIPC decompression.";
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            };

            const unsigned int reported_workers =
                std::thread::hardware_concurrency();

            const std::size_t worker_count =
                std::max<std::size_t>(
                    1,
                    std::min(
                        descriptors.size(),
                        reported_workers == 0
                            ? std::size_t{1}
                            : static_cast<std::size_t>(reported_workers)));

            std::vector<std::thread> workers;
            workers.reserve(worker_count);

            try
            {
                for (std::size_t i = 0; i < worker_count; ++i)
                {
                    workers.emplace_back(worker_function);
                }
            }
            catch (...)
            {
                failed.store(true, std::memory_order_relaxed);

                for (std::thread& worker : workers)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }

                set_error(error, "Unable to start RIPC decompression workers.");
                return false;
            }

            for (std::thread& worker : workers)
            {
                worker.join();
            }

            for (std::size_t i = 0; i < descriptors.size(); ++i)
            {
                if (!chunk_errors[i].empty())
                {
                    set_error(error, chunk_errors[i].c_str());
                    return false;
                }

                if (decoded_chunks[i].size() != descriptors[i].original_size)
                {
                    set_error(error, "RIPC chunk decompression did not complete.");
                    return false;
                }
            }

            output.reserve(expected_size);

            for (const Bytes& decoded_chunk : decoded_chunks)
            {
                output.insert(
                    output.end(),
                    decoded_chunk.begin(),
                    decoded_chunk.end());
            }

            return output.size() == expected_size;
        }

        if ((flags &
             FLAG_STORED) != 0)
        {
            if (payload.size() !=
                static_cast<std::size_t>(
                    original_size))
            {
                set_error(
                    error,
                    "RIPC stored block size mismatch.");

                return false;
            }

            output.assign(
                payload.begin(),
                payload.end());

            return true;
        }

        // v0.6 token-aware Huffman.
        if ((flags &
             FLAG_TOKEN_HUFFMAN) != 0)
        {
            std::vector<Token>
                tokens;

            if (!huffman_decode_tokens(
                    payload,
                    tokens,
                    error))
            {
                return false;
            }

            std::vector<std::byte>
                raw_tokens;

            if (!encode_tokens(
                    tokens,
                    raw_tokens,
                    error))
            {
                return false;
            }

            return lz_decompress(
                raw_tokens,
                original_size,
                output,
                error);
        }

        // v0.5 byte-oriented Huffman.
        if ((flags &
             FLAG_HUFFMAN) != 0)
        {
            std::vector<std::byte>
                raw;

            if (!huffman_decompress(
                    payload,
                    raw,
                    error))
            {
                return false;
            }

            return lz_decompress(
                raw,
                original_size,
                output,
                error);
        }

        // Raw LZ token stream.
        return lz_decompress(
            payload,
            original_size,
            output,
            error);
    }

} // namespace rip::compression