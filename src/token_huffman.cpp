#include "rip/token_huffman.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rip::compression
{

namespace
{

constexpr char MAGIC[4] =
{
    'R',
    'T',
    'H',
    '1'
};

constexpr std::uint8_t LEGACY_VERSION = 1;
constexpr std::uint8_t VERSION = 2;
constexpr std::uint8_t V3_VERSION = 3;

constexpr std::size_t LITERAL_SYMBOLS = 256;

constexpr std::size_t LEGACY_EVENT_SYMBOLS = 2;
constexpr std::size_t V2_EVENT_SYMBOLS = 3;

constexpr std::size_t MATCH_EVENT = 1;
constexpr std::size_t MATCH_REPEAT_EVENT = 2;

constexpr std::size_t LENGTH_SYMBOLS = 17;
constexpr std::size_t DISTANCE_SYMBOLS = 16;

constexpr std::uint8_t EXTENDED_LENGTH_MARKER =
    0xFF;

constexpr std::size_t EXTENDED_LENGTH_BASE =
    258;

struct HuffmanNode
{
    std::uint64_t frequency{};
    int left{-1};
    int right{-1};
    int symbol{-1};
};

struct HuffmanCompare
{
    const std::vector<HuffmanNode>* nodes{};

    bool operator()(int a, int b) const
    {
        if ((*nodes)[a].frequency !=
            (*nodes)[b].frequency)
        {
            return
                (*nodes)[a].frequency >
                (*nodes)[b].frequency;
        }

        return a > b;
    }
};

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

void write_u64(
    std::vector<std::byte>& output,
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
            (static_cast<std::uint16_t>(high)
             << 8u));

    return true;
}

bool read_u64(
    std::span<const std::byte> input,
    std::size_t& position,
    std::uint64_t& value)
{
    value = 0;

    for (unsigned int i = 0;
         i < 8;
         ++i)
    {
        std::uint8_t byte = 0;

        if (!read_u8(
                input,
                position,
                byte))
        {
            return false;
        }

        value |=
            static_cast<std::uint64_t>(byte)
            << (i * 8u);
    }

    return true;
}

bool increment_code(
    std::vector<std::uint8_t>& code)
{
    if (code.empty())
    {
        return false;
    }

    for (std::size_t i = code.size();
         i > 0;
         --i)
    {
        const std::size_t index = i - 1;

        if (code[index] == 0)
        {
            code[index] = 1;
            return true;
        }

        code[index] = 0;
    }

    return false;
}

template <std::size_t SymbolCount>
bool build_huffman_lengths(
    const std::array<
        std::uint64_t,
        SymbolCount>& frequencies,
    std::array<
        std::uint8_t,
        SymbolCount>& lengths)
{
    lengths.fill(0);

    std::vector<HuffmanNode> nodes;

    nodes.reserve(
        SymbolCount * 2);

    for (std::size_t symbol = 0;
         symbol < SymbolCount;
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
            static_cast<int>(symbol);

        nodes.push_back(node);
    }

    if (nodes.empty())
    {
        return true;
    }

    if (nodes.size() == 1)
    {
        lengths[
            static_cast<std::size_t>(
                nodes[0].symbol)] = 1;

        return true;
    }

    HuffmanCompare compare;

    compare.nodes = &nodes;

    std::priority_queue<
        int,
        std::vector<int>,
        HuffmanCompare>
        queue(compare);

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

        parent.left = left;
        parent.right = right;

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

            const HuffmanNode& node =
                nodes[
                    static_cast<std::size_t>(
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

                lengths[
                    static_cast<std::size_t>(
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

template <std::size_t SymbolCount>
bool build_canonical_codes(
    const std::array<
        std::uint8_t,
        SymbolCount>& lengths,
    std::array<
        std::vector<std::uint8_t>,
        SymbolCount>& codes)
{
    for (auto& code : codes)
    {
        code.clear();
    }

    std::vector<int> symbols;

    for (std::size_t symbol = 0;
         symbol < SymbolCount;
         ++symbol)
    {
        if (lengths[symbol] != 0)
        {
            symbols.push_back(
                static_cast<int>(symbol));
        }
    }

    std::sort(
        symbols.begin(),
        symbols.end(),
        [&](int a, int b)
        {
            const auto length_a =
                lengths[
                    static_cast<std::size_t>(
                        a)];

            const auto length_b =
                lengths[
                    static_cast<std::size_t>(
                        b)];

            if (length_a != length_b)
            {
                return length_a < length_b;
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
        const auto length =
            lengths[
                static_cast<std::size_t>(
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

        codes[
            static_cast<std::size_t>(
                symbol)] =
            current;
    }

    return true;
}

class BitWriter
{
public:
    void write(
        const std::vector<std::uint8_t>& bits)
    {
        for (const std::uint8_t bit : bits)
        {
            write_bit(bit);
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

    void write_bits(
    std::uint32_t value,
    unsigned int count)
{
    for (unsigned int i = count;
         i > 0;
         --i)
    {
        write_bit(
            static_cast<std::uint8_t>(
                (value >>
                 (i - 1)) &
                1u));
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
        std::uint8_t& bit)
    {
        if (position_ >= input_.size())
        {
            return false;
        }

        const auto value =
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

    bool read_bits(
    unsigned int count,
    std::uint32_t& value)
{
    value = 0;

    for (unsigned int i = 0;
         i < count;
         ++i)
    {
        std::uint8_t bit = 0;

        if (!read(bit))
        {
            return false;
        }

        value =
            (value << 1u) |
            bit;
    }

    return true;
}

private:
    std::span<const std::byte> input_;
    std::size_t position_{};
    std::uint8_t bit_count_{};
};

template <std::size_t SymbolCount>
bool build_decode_tree(
    const std::array<
        std::uint8_t,
        SymbolCount>& lengths,
    const std::array<
        std::vector<std::uint8_t>,
        SymbolCount>& codes,
    std::vector<HuffmanNode>& tree,
    std::string* error)
{
    tree.clear();
    tree.push_back(HuffmanNode{});

    for (std::size_t symbol = 0;
         symbol < SymbolCount;
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
            if (node < 0 ||
                static_cast<std::size_t>(node) >=
                    tree.size())
            {
                set_error(
                    error,
                    "Invalid Huffman tree node.");

                return false;
            }

            if (tree[
                    static_cast<std::size_t>(node)]
                    .symbol >= 0)
            {
                set_error(
                    error,
                    "Huffman code passes through a leaf.");

                return false;
            }

            int next = -1;

            if (bit == 0)
            {
                next =
                    tree[
                        static_cast<std::size_t>(node)]
                        .left;
            }
            else
            {
                next =
                    tree[
                        static_cast<std::size_t>(node)]
                        .right;
            }

            if (next < 0)
            {
                next =
                    static_cast<int>(
                        tree.size());

                tree.push_back(
                    HuffmanNode{});

                if (bit == 0)
                {
                    tree[
                        static_cast<std::size_t>(node)]
                        .left = next;
                }
                else
                {
                    tree[
                        static_cast<std::size_t>(node)]
                        .right = next;
                }
            }

            node = next;
        }

        if (node < 0 ||
            static_cast<std::size_t>(node) >=
                tree.size())
        {
            set_error(
                error,
                "Invalid Huffman leaf.");

            return false;
        }

        auto& leaf =
            tree[
                static_cast<std::size_t>(
                    node)];

        if (leaf.symbol >= 0 ||
            leaf.left >= 0 ||
            leaf.right >= 0)
        {
            set_error(
                error,
                "Conflicting Huffman codes.");

            return false;
        }

        leaf.symbol =
            static_cast<int>(
                symbol);
    }

    (void)lengths;

    return true;
}

bool decode_symbol(
    BitReader& reader,
    const std::vector<HuffmanNode>& tree,
    int& symbol)
{
    int node = 0;

    while (true)
    {
        if (node < 0 ||
            static_cast<std::size_t>(node) >=
                tree.size())
        {
            return false;
        }

        const auto& current =
            tree[
                static_cast<std::size_t>(
                    node)];

        if (current.symbol >= 0)
        {
            symbol = current.symbol;
            return true;
        }

        std::uint8_t bit = 0;

        if (!reader.read(bit))
        {
            return false;
        }

        node =
            bit == 0
                ? current.left
                : current.right;
    }
}

bool encode_length(
    std::size_t length,
    std::uint8_t& symbol,
    unsigned int& extra_bits,
    std::uint32_t& extra_value)
{
    if (length < 3 ||
        length > 65'793)
    {
        return false;
    }

    const std::uint32_t value =
        static_cast<std::uint32_t>(
            length - 3);

    if (value == 0)
    {
        symbol = 0;
        extra_bits = 0;
        extra_value = 0;
        return true;
    }

    const unsigned int order =
        31u -
        std::countl_zero(value);

    symbol =
        static_cast<std::uint8_t>(
            order + 1);

    extra_bits =
        order;

    extra_value =
        value -
        (std::uint32_t{1} << order);

    return true;
}

bool decode_length(
    std::uint8_t symbol,
    BitReader& reader,
    std::size_t& length)
{
    if (symbol == 0)
    {
        length = 3;
        return true;
    }

    if (symbol >= LENGTH_SYMBOLS)
    {
        return false;
    }

    const unsigned int order =
        symbol - 1;

    std::uint32_t extra = 0;

    if (!reader.read_bits(
            order,
            extra))
    {
        return false;
    }

    const std::uint32_t value =
        (std::uint32_t{1} << order) +
        extra;

    length =
        static_cast<std::size_t>(
            value) +
        3;

    return length <= 65'793;
}

bool encode_distance(
    std::uint16_t distance,
    std::uint8_t& symbol,
    unsigned int& extra_bits,
    std::uint32_t& extra_value)
{
    if (distance == 0)
    {
        return false;
    }

    const unsigned int order =
        31u -
        std::countl_zero(
            static_cast<std::uint32_t>(
                distance));

    symbol =
        static_cast<std::uint8_t>(
            order);

    extra_bits =
        order;

    extra_value =
        static_cast<std::uint32_t>(
            distance) -
        (std::uint32_t{1} << order);

    return true;
}

bool decode_distance(
    std::uint8_t symbol,
    BitReader& reader,
    std::uint16_t& distance)
{
    if (symbol >= DISTANCE_SYMBOLS)
    {
        return false;
    }

    const unsigned int order =
        symbol;

    std::uint32_t extra = 0;

    if (!reader.read_bits(
            order,
            extra))
    {
        return false;
    }

    const std::uint32_t value =
        (std::uint32_t{1} << order) +
        extra;

    if (value == 0 ||
        value > 65'535)
    {
        return false;
    }

    distance =
        static_cast<std::uint16_t>(
            value);

    return true;
}

} // namespace

bool huffman_encode_tokens_v1(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    std::array<
        std::uint64_t,
        LITERAL_SYMBOLS>
        literal_frequencies{};

    std::array<
        std::uint64_t,
        LEGACY_EVENT_SYMBOLS>
        event_frequencies{};

    std::vector<std::byte>
        parameters;

    std::uint64_t event_count = 0;

    for (const Token& token : tokens)
    {
        switch (token.type)
        {
        case TokenType::Literal:
        {
            if (token.literals.size() != 1)
            {
                set_error(
                    error,
                    "Invalid literal token.");

                return false;
            }

            ++event_frequencies[0];

            ++literal_frequencies[
                static_cast<std::uint8_t>(
                    token.literals[0])];

            ++event_count;

            break;
        }

        case TokenType::LiteralRun:
        {
            if (token.literals.empty() ||
                token.literals.size() > 255)
            {
                set_error(
                    error,
                    "Invalid literal run.");

                return false;
            }

            for (const std::byte value :
                 token.literals)
            {
                ++event_frequencies[0];

                ++literal_frequencies[
                    static_cast<std::uint8_t>(
                        value)];

                ++event_count;
            }

            break;
        }

        case TokenType::Match:
case TokenType::MatchRepeat:
        {
            if (token.distance == 0 ||
                token.length < 3)
            {
                set_error(
                    error,
                    "Invalid match token.");

                return false;
            }

            ++event_frequencies[
                MATCH_EVENT];

            ++event_count;

            write_u16(
                parameters,
                token.distance);

            if (token.length <= 257)
            {
                write_u8(
                    parameters,
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
                    parameters,
                    EXTENDED_LENGTH_MARKER);

                write_u16(
                    parameters,
                    static_cast<std::uint16_t>(
                        extended));
            }

            break;
        }

        default:
            set_error(
                error,
                "Unknown token type.");

            return false;
        }
    }

    std::array<
        std::uint8_t,
        LITERAL_SYMBOLS>
        literal_lengths{};

    std::array<
        std::uint8_t,
        LEGACY_EVENT_SYMBOLS>
        event_lengths{};

    if (!build_huffman_lengths(
            literal_frequencies,
            literal_lengths) ||
        !build_huffman_lengths(
            event_frequencies,
            event_lengths))
    {
        set_error(
            error,
            "Unable to build Huffman trees.");

        return false;
    }

    std::array<
        std::vector<std::uint8_t>,
        LITERAL_SYMBOLS>
        literal_codes;

    std::array<
        std::vector<std::uint8_t>,
        LEGACY_EVENT_SYMBOLS>
        event_codes;

    if (!build_canonical_codes(
            literal_lengths,
            literal_codes) ||
        !build_canonical_codes(
            event_lengths,
            event_codes))
    {
        set_error(
            error,
            "Unable to build canonical Huffman codes.");

        return false;
    }

    BitWriter writer;

    for (const Token& token : tokens)
    {
        if (token.type ==
                TokenType::Literal ||
            token.type ==
                TokenType::LiteralRun)
        {
            for (const std::byte value :
                 token.literals)
            {
                writer.write(
                    event_codes[0]);

                writer.write(
                    literal_codes[
                        static_cast<std::uint8_t>(
                            value)]);
            }
        }
        else if (token.type ==
             TokenType::Match ||
         token.type ==
             TokenType::MatchRepeat)
{
    writer.write(
        event_codes[MATCH_EVENT]);
}
    }

    const auto bitstream =
        writer.finish();

    output.insert(
        output.end(),
        reinterpret_cast<
            const std::byte*>(
                MAGIC),
        reinterpret_cast<
            const std::byte*>(
                MAGIC + 4));

    write_u8(
    output,
    LEGACY_VERSION);

    write_u8(
        output,
        0);

    write_u16(
        output,
        0);

    write_u64(
        output,
        event_count);

    write_u64(
        output,
        static_cast<std::uint64_t>(
            parameters.size()));

    for (const auto length :
         literal_lengths)
    {
        write_u8(
            output,
            length);
    }

    for (const auto length :
         event_lengths)
    {
        write_u8(
            output,
            length);
    }

    output.insert(
        output.end(),
        parameters.begin(),
        parameters.end());

    output.insert(
        output.end(),
        bitstream.begin(),
        bitstream.end());

    return true;
}

bool huffman_encode_tokens_v2(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error)
{
    output.clear();

    std::array<
        std::uint64_t,
        LITERAL_SYMBOLS>
        literal_frequencies{};

    std::array<
        std::uint64_t,
        V2_EVENT_SYMBOLS>
        event_frequencies{};

    std::array<
        std::uint64_t,
        LENGTH_SYMBOLS>
        length_frequencies{};

    std::array<
        std::uint64_t,
        DISTANCE_SYMBOLS>
        distance_frequencies{};

    std::uint64_t event_count = 0;

    for (const Token& token : tokens)
    {
        switch (token.type)
        {
        case TokenType::Literal:
        {
            if (token.literals.size() != 1)
            {
                set_error(
                    error,
                    "Invalid literal token.");

                return false;
            }

            ++event_frequencies[0];

            ++literal_frequencies[
                static_cast<std::uint8_t>(
                    token.literals[0])];

            ++event_count;

            break;
        }

        case TokenType::LiteralRun:
        {
            if (token.literals.empty() ||
                token.literals.size() > 255)
            {
                set_error(
                    error,
                    "Invalid literal run.");

                return false;
            }

            for (const std::byte value :
                 token.literals)
            {
                ++event_frequencies[0];

                ++literal_frequencies[
                    static_cast<std::uint8_t>(
                        value)];

                ++event_count;
            }

            break;
        }

        case TokenType::Match:
        {
            std::uint8_t length_symbol = 0;
            unsigned int length_extra_bits = 0;
            std::uint32_t length_extra = 0;

            std::uint8_t distance_symbol = 0;
            unsigned int distance_extra_bits = 0;
            std::uint32_t distance_extra = 0;

            if (!encode_length(
                    token.length,
                    length_symbol,
                    length_extra_bits,
                    length_extra) ||
                !encode_distance(
                    token.distance,
                    distance_symbol,
                    distance_extra_bits,
                    distance_extra))
            {
                set_error(
                    error,
                    "Invalid RIPC match parameters.");

                return false;
            }

            (void)length_extra_bits;
            (void)length_extra;
            (void)distance_extra_bits;
            (void)distance_extra;

            ++event_frequencies[
                MATCH_EVENT];

            ++length_frequencies[
                length_symbol];

            ++distance_frequencies[
                distance_symbol];

            ++event_count;

            break;
        }

        case TokenType::MatchRepeat:
        {
            std::uint8_t length_symbol = 0;
            unsigned int length_extra_bits = 0;
            std::uint32_t length_extra = 0;

            if (!encode_length(
                    token.length,
                    length_symbol,
                    length_extra_bits,
                    length_extra))
            {
                set_error(
                    error,
                    "Invalid repeated match length.");

                return false;
            }

            (void)length_extra_bits;
            (void)length_extra;

            ++event_frequencies[
                MATCH_REPEAT_EVENT];

            ++length_frequencies[
                length_symbol];

            ++event_count;

            break;
        }

        default:
            set_error(
                error,
                "Unknown token type.");

            return false;
        }
    }

    std::array<
        std::uint8_t,
        LITERAL_SYMBOLS>
        literal_lengths{};

    std::array<
        std::uint8_t,
        V2_EVENT_SYMBOLS>
        event_lengths{};

    std::array<
        std::uint8_t,
        LENGTH_SYMBOLS>
        length_lengths{};

    std::array<
        std::uint8_t,
        DISTANCE_SYMBOLS>
        distance_lengths{};

    if (!build_huffman_lengths(
            literal_frequencies,
            literal_lengths) ||
        !build_huffman_lengths(
            event_frequencies,
            event_lengths) ||
        !build_huffman_lengths(
            length_frequencies,
            length_lengths) ||
        !build_huffman_lengths(
            distance_frequencies,
            distance_lengths))
    {
        set_error(
            error,
            "Unable to build RIPC v0.9 Huffman trees.");

        return false;
    }

    std::array<
        std::vector<std::uint8_t>,
        LITERAL_SYMBOLS>
        literal_codes;

    std::array<
        std::vector<std::uint8_t>,
        V2_EVENT_SYMBOLS>
        event_codes;

    std::array<
        std::vector<std::uint8_t>,
        LENGTH_SYMBOLS>
        length_codes;

    std::array<
        std::vector<std::uint8_t>,
        DISTANCE_SYMBOLS>
        distance_codes;

    if (!build_canonical_codes(
            literal_lengths,
            literal_codes) ||
        !build_canonical_codes(
            event_lengths,
            event_codes) ||
        !build_canonical_codes(
            length_lengths,
            length_codes) ||
        !build_canonical_codes(
            distance_lengths,
            distance_codes))
    {
        set_error(
            error,
            "Unable to build RIPC v0.9 Huffman codes.");

        return false;
    }

    BitWriter writer;

    for (const Token& token : tokens)
    {
        if (token.type ==
                TokenType::Literal ||
            token.type ==
                TokenType::LiteralRun)
        {
            for (const std::byte value :
                 token.literals)
            {
                writer.write(
                    event_codes[0]);

                writer.write(
                    literal_codes[
                        static_cast<std::uint8_t>(
                            value)]);
            }

            continue;
        }

        if (token.type ==
            TokenType::Match)
        {
            writer.write(
                event_codes[
                    MATCH_EVENT]);

            std::uint8_t length_symbol = 0;
            unsigned int length_extra_bits = 0;
            std::uint32_t length_extra = 0;

            std::uint8_t distance_symbol = 0;
            unsigned int distance_extra_bits = 0;
            std::uint32_t distance_extra = 0;

            if (!encode_length(
                    token.length,
                    length_symbol,
                    length_extra_bits,
                    length_extra) ||
                !encode_distance(
                    token.distance,
                    distance_symbol,
                    distance_extra_bits,
                    distance_extra))
            {
                set_error(
                    error,
                    "Invalid RIPC match parameters.");

                return false;
            }

            writer.write(
                length_codes[
                    length_symbol]);

            writer.write_bits(
                length_extra,
                length_extra_bits);

            writer.write(
                distance_codes[
                    distance_symbol]);

            writer.write_bits(
                distance_extra,
                distance_extra_bits);

            continue;
        }

        if (token.type ==
            TokenType::MatchRepeat)
        {
            writer.write(
                event_codes[
                    MATCH_REPEAT_EVENT]);

            std::uint8_t length_symbol = 0;
            unsigned int length_extra_bits = 0;
            std::uint32_t length_extra = 0;

            if (!encode_length(
                    token.length,
                    length_symbol,
                    length_extra_bits,
                    length_extra))
            {
                set_error(
                    error,
                    "Invalid repeated match length.");

                return false;
            }

            writer.write(
                length_codes[
                    length_symbol]);

            writer.write_bits(
                length_extra,
                length_extra_bits);
        }
    }

    const auto bitstream =
        writer.finish();

    output.insert(
        output.end(),
        reinterpret_cast<
            const std::byte*>(
                MAGIC),
        reinterpret_cast<
            const std::byte*>(
                MAGIC + 4));

    write_u8(
        output,
        VERSION);

    write_u8(
        output,
        0);

    write_u16(
        output,
        0);

    write_u64(
        output,
        event_count);

    write_u64(
        output,
        0);

    for (const auto length :
         literal_lengths)
    {
        write_u8(
            output,
            length);
    }

    for (const auto length :
         event_lengths)
    {
        write_u8(
            output,
            length);
    }

    for (const auto length :
         length_lengths)
    {
        write_u8(
            output,
            length);
    }

    for (const auto length :
         distance_lengths)
    {
        write_u8(
            output,
            length);
    }

    output.insert(
        output.end(),
        bitstream.begin(),
        bitstream.end());

    return true;
}

bool huffman_decode_tokens_v2(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error);

bool huffman_encode_tokens_v3(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error)
{
    std::vector<std::byte> v2;

    if (!huffman_encode_tokens_v2(
            tokens,
            v2,
            error))
    {
        return false;
    }

    constexpr std::size_t HEADER_SIZE =
        4 + 1 + 1 + 2 + 8 + 8;

    constexpr std::size_t TABLE_SIZE =
        LITERAL_SYMBOLS +
        V2_EVENT_SYMBOLS +
        LENGTH_SYMBOLS +
        DISTANCE_SYMBOLS;

    constexpr std::size_t PACKED_TABLE_SIZE =
        (TABLE_SIZE + 1) / 2;

    if (v2.size() <
        HEADER_SIZE + TABLE_SIZE)
    {
        set_error(
            error,
            "RTH1 v2 stream is too small.");

        return false;
    }

    const auto table =
        std::span<const std::byte>(
            v2).subspan(
                HEADER_SIZE,
                TABLE_SIZE);

    for (const std::byte value :
         table)
    {
        if (static_cast<std::uint8_t>(
                value) >
            15)
        {
            set_error(
                error,
                "RTH1 v3 requires Huffman code lengths <= 15.");

            return false;
        }
    }

    output.clear();

    output.reserve(
        HEADER_SIZE +
        PACKED_TABLE_SIZE +
        v2.size() -
        HEADER_SIZE -
        TABLE_SIZE);

    output.insert(
        output.end(),
        v2.begin(),
        v2.begin() +
            static_cast<std::ptrdiff_t>(
                HEADER_SIZE));

    output[4] =
        static_cast<std::byte>(
            V3_VERSION);

    for (std::size_t i = 0;
         i < TABLE_SIZE;
         i += 2)
    {
        const std::uint8_t low =
            static_cast<std::uint8_t>(
                table[i]) &
            0x0Fu;

        const std::uint8_t high =
            i + 1 < TABLE_SIZE
                ? (
                    static_cast<std::uint8_t>(
                        table[i + 1]) &
                    0x0Fu)
                : 0;

        output.push_back(
            static_cast<std::byte>(
                low |
                static_cast<std::uint8_t>(
                    high << 4u)));
    }

    output.insert(
        output.end(),
        v2.begin() +
            static_cast<std::ptrdiff_t>(
                HEADER_SIZE + TABLE_SIZE),
        v2.end());

    return true;
}

bool huffman_decode_tokens_v3(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    constexpr std::size_t HEADER_SIZE =
        4 + 1 + 1 + 2 + 8 + 8;

    constexpr std::size_t TABLE_SIZE =
        LITERAL_SYMBOLS +
        V2_EVENT_SYMBOLS +
        LENGTH_SYMBOLS +
        DISTANCE_SYMBOLS;

    constexpr std::size_t PACKED_TABLE_SIZE =
        (TABLE_SIZE + 1) / 2;

    if (input.size() <
        HEADER_SIZE + PACKED_TABLE_SIZE)
    {
        set_error(
            error,
            "RTH1 v3 stream is too small.");

        return false;
    }

    std::vector<std::byte> expanded;

    expanded.reserve(
        HEADER_SIZE +
        TABLE_SIZE +
        input.size() -
        HEADER_SIZE -
        PACKED_TABLE_SIZE);

    expanded.insert(
        expanded.end(),
        input.begin(),
        input.begin() +
            static_cast<std::ptrdiff_t>(
                HEADER_SIZE));

    expanded[4] =
        static_cast<std::byte>(
            VERSION);

    for (std::size_t i = 0;
         i < TABLE_SIZE;
         ++i)
    {
        const std::uint8_t packed =
            static_cast<std::uint8_t>(
                input[
                    HEADER_SIZE +
                    (i / 2)]);

        const std::uint8_t length =
            (i & 1u) == 0
                ? static_cast<std::uint8_t>(
                    packed & 0x0Fu)
                : static_cast<std::uint8_t>(
                    packed >> 4u);

        expanded.push_back(
            static_cast<std::byte>(
                length));
    }

    expanded.insert(
        expanded.end(),
        input.begin() +
            static_cast<std::ptrdiff_t>(
                HEADER_SIZE +
                PACKED_TABLE_SIZE),
        input.end());

    return huffman_decode_tokens_v2(
        expanded,
        tokens,
        error);
}

bool huffman_decode_tokens_v2(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    tokens.clear();

    constexpr std::size_t HEADER_SIZE =
        4 + 1 + 1 + 2 + 8 + 8 +
        LITERAL_SYMBOLS +
        V2_EVENT_SYMBOLS +
        LENGTH_SYMBOLS +
        DISTANCE_SYMBOLS;

    if (input.size() < HEADER_SIZE)
    {
        set_error(
            error,
            "RTH1 v2 stream is too small.");

        return false;
    }

    std::size_t position = 4;

    std::uint8_t version = 0;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;

    if (!read_u8(
            input,
            position,
            version) ||
        !read_u8(
            input,
            position,
            flags) ||
        !read_u16(
            input,
            position,
            reserved))
    {
        set_error(
            error,
            "Invalid RTH1 v2 header.");

        return false;
    }

    if (version != VERSION ||
        flags != 0 ||
        reserved != 0)
    {
        set_error(
            error,
            "Unsupported RTH1 v2 header.");

        return false;
    }

    std::uint64_t event_count = 0;
    std::uint64_t parameter_size = 0;

    if (!read_u64(
            input,
            position,
            event_count) ||
        !read_u64(
            input,
            position,
            parameter_size))
    {
        set_error(
            error,
            "Invalid RTH1 v2 sizes.");

        return false;
    }

    if (parameter_size != 0 ||
        event_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::size_t>::max()))
    {
        set_error(
            error,
            "Invalid RTH1 v2 payload sizes.");

        return false;
    }

    std::array<
        std::uint8_t,
        LITERAL_SYMBOLS>
        literal_lengths{};

    std::array<
        std::uint8_t,
        V2_EVENT_SYMBOLS>
        event_lengths{};

    std::array<
        std::uint8_t,
        LENGTH_SYMBOLS>
        length_lengths{};

    std::array<
        std::uint8_t,
        DISTANCE_SYMBOLS>
        distance_lengths{};

    for (auto& length :
         literal_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 v2 literal table.");

            return false;
        }
    }

    for (auto& length :
         event_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 v2 event table.");

            return false;
        }
    }

    for (auto& length :
         length_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 v2 length table.");

            return false;
        }
    }

    for (auto& length :
         distance_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 v2 distance table.");

            return false;
        }
    }

    std::array<
        std::vector<std::uint8_t>,
        LITERAL_SYMBOLS>
        literal_codes;

    std::array<
        std::vector<std::uint8_t>,
        V2_EVENT_SYMBOLS>
        event_codes;

    std::array<
        std::vector<std::uint8_t>,
        LENGTH_SYMBOLS>
        length_codes;

    std::array<
        std::vector<std::uint8_t>,
        DISTANCE_SYMBOLS>
        distance_codes;

    if (!build_canonical_codes(
            literal_lengths,
            literal_codes) ||
        !build_canonical_codes(
            event_lengths,
            event_codes) ||
        !build_canonical_codes(
            length_lengths,
            length_codes) ||
        !build_canonical_codes(
            distance_lengths,
            distance_codes))
    {
        set_error(
            error,
            "Invalid RTH1 v2 Huffman tables.");

        return false;
    }

    std::vector<HuffmanNode>
        literal_tree;

    std::vector<HuffmanNode>
        event_tree;

    std::vector<HuffmanNode>
        length_tree;

    std::vector<HuffmanNode>
        distance_tree;

    if (!build_decode_tree(
            literal_lengths,
            literal_codes,
            literal_tree,
            error) ||
        !build_decode_tree(
            event_lengths,
            event_codes,
            event_tree,
            error) ||
        !build_decode_tree(
            length_lengths,
            length_codes,
            length_tree,
            error) ||
        !build_decode_tree(
            distance_lengths,
            distance_codes,
            distance_tree,
            error))
    {
        return false;
    }

    BitReader reader(
        input.subspan(position));

    tokens.reserve(
        static_cast<std::size_t>(
            event_count));

    std::uint16_t previous_distance = 0;

    for (std::uint64_t i = 0;
         i < event_count;
         ++i)
    {
        int event_symbol = -1;

        if (!decode_symbol(
                reader,
                event_tree,
                event_symbol))
        {
            set_error(
                error,
                "Invalid RTH1 v2 event stream.");

            return false;
        }

        if (event_symbol == 0)
        {
            int literal_symbol = -1;

            if (!decode_symbol(
                    reader,
                    literal_tree,
                    literal_symbol))
            {
                set_error(
                    error,
                    "Invalid RTH1 v2 literal stream.");

                return false;
            }

            Token token;

            token.type =
                TokenType::Literal;

            token.literals.push_back(
                static_cast<std::byte>(
                    literal_symbol));

            tokens.push_back(
                std::move(token));

            continue;
        }

        if (event_symbol !=
                MATCH_EVENT &&
            event_symbol !=
                MATCH_REPEAT_EVENT)
        {
            set_error(
                error,
                "Unknown RTH1 v2 event.");

            return false;
        }

        int length_symbol = -1;

        if (!decode_symbol(
                reader,
                length_tree,
                length_symbol))
        {
            set_error(
                error,
                "Invalid RTH1 v2 length stream.");

            return false;
        }

        std::size_t length = 0;

        if (!decode_length(
                static_cast<std::uint8_t>(
                    length_symbol),
                reader,
                length))
        {
            set_error(
                error,
                "Invalid RTH1 v2 match length.");

            return false;
        }

        Token token;

        if (event_symbol ==
            MATCH_REPEAT_EVENT)
        {
            if (previous_distance == 0)
            {
                set_error(
                    error,
                    "RTH1 v2 repeated match has no previous distance.");

                return false;
            }

            token.type =
                TokenType::MatchRepeat;

            token.distance =
                previous_distance;

            token.length =
                length;
        }
        else
        {
            int distance_symbol = -1;

            if (!decode_symbol(
                    reader,
                    distance_tree,
                    distance_symbol))
            {
                set_error(
                    error,
                    "Invalid RTH1 v2 distance stream.");

                return false;
            }

            std::uint16_t distance = 0;

            if (!decode_distance(
                    static_cast<std::uint8_t>(
                        distance_symbol),
                    reader,
                    distance))
            {
                set_error(
                    error,
                    "Invalid RTH1 v2 match distance.");

                return false;
            }

            token.type =
                TokenType::Match;

            token.distance =
                distance;

            token.length =
                length;

            previous_distance =
                distance;
        }

        tokens.push_back(
            std::move(token));
    }

    return true;
}

bool huffman_decode_tokens_v1(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    tokens.clear();

    constexpr std::size_t HEADER_SIZE =
        4 + 1 + 1 + 2 + 8 + 8 +
        LITERAL_SYMBOLS +
        LEGACY_EVENT_SYMBOLS;

    if (input.size() <
        HEADER_SIZE)
    {
        set_error(
            error,
            "RTH1 stream is too small.");

        return false;
    }

    if (input[0] !=
            static_cast<std::byte>('R') ||
        input[1] !=
            static_cast<std::byte>('T') ||
        input[2] !=
            static_cast<std::byte>('H') ||
        input[3] !=
            static_cast<std::byte>('1'))
    {
        set_error(
            error,
            "Invalid RTH1 magic.");

        return false;
    }

    std::size_t position = 4;

    std::uint8_t version = 0;
    std::uint8_t flags = 0;
    std::uint16_t reserved = 0;

    if (!read_u8(
            input,
            position,
            version) ||
        !read_u8(
            input,
            position,
            flags) ||
        !read_u16(
            input,
            position,
            reserved))
    {
        set_error(
            error,
            "Invalid RTH1 header.");

        return false;
    }

    if (version != LEGACY_VERSION)
    {
        set_error(
            error,
            "Unsupported RTH1 version.");

        return false;
    }

    if (flags != 0 ||
        reserved != 0)
    {
        set_error(
            error,
            "Unsupported RTH1 flags.");

        return false;
    }

    std::uint64_t event_count = 0;
    std::uint64_t parameter_size = 0;

    if (!read_u64(
            input,
            position,
            event_count) ||
        !read_u64(
            input,
            position,
            parameter_size))
    {
        set_error(
            error,
            "Invalid RTH1 sizes.");

        return false;
    }

    if (event_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()) ||
        parameter_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()))
    {
        set_error(
            error,
            "RTH1 stream is too large.");

        return false;
    }

    std::array<
        std::uint8_t,
        LITERAL_SYMBOLS>
        literal_lengths{};

    std::array<
        std::uint8_t,
        LEGACY_EVENT_SYMBOLS>
        event_lengths{};

    for (auto& length :
         literal_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 literal table.");

            return false;
        }
    }

    for (auto& length :
         event_lengths)
    {
        if (!read_u8(
                input,
                position,
                length))
        {
            set_error(
                error,
                "Truncated RTH1 event table.");

            return false;
        }
    }

    const std::size_t parameter_bytes =
        static_cast<std::size_t>(
            parameter_size);

    if (parameter_bytes >
        input.size() - position)
    {
        set_error(
            error,
            "Truncated RTH1 parameters.");

        return false;
    }

    const auto parameters =
        input.subspan(
            position,
            parameter_bytes);

    position += parameter_bytes;

    const auto bitstream =
        input.subspan(position);

    std::array<
        std::vector<std::uint8_t>,
        LITERAL_SYMBOLS>
        literal_codes;

    std::array<
        std::vector<std::uint8_t>,
        LEGACY_EVENT_SYMBOLS>
        event_codes;

    if (!build_canonical_codes(
            literal_lengths,
            literal_codes) ||
        !build_canonical_codes(
            event_lengths,
            event_codes))
    {
        set_error(
            error,
            "Invalid RTH1 Huffman tables.");

        return false;
    }

    std::vector<HuffmanNode>
        literal_tree;

    std::vector<HuffmanNode>
        event_tree;

    if (!build_decode_tree(
            literal_lengths,
            literal_codes,
            literal_tree,
            error) ||
        !build_decode_tree(
            event_lengths,
            event_codes,
            event_tree,
            error))
    {
        return false;
    }

    BitReader reader(
        bitstream);

    std::size_t parameter_position = 0;

    tokens.reserve(
        static_cast<std::size_t>(
            event_count));

    for (std::uint64_t i = 0;
         i < event_count;
         ++i)
    {
        int event_symbol = -1;

        if (!decode_symbol(
                reader,
                event_tree,
                event_symbol))
        {
            set_error(
                error,
                "Invalid RTH1 event stream.");

            return false;
        }

        if (event_symbol == 0)
        {
            int literal_symbol = -1;

            if (!decode_symbol(
                    reader,
                    literal_tree,
                    literal_symbol))
            {
                set_error(
                    error,
                    "Invalid RTH1 literal stream.");

                return false;
            }

            Token token;

            token.type =
                TokenType::Literal;

            token.literals.push_back(
                static_cast<std::byte>(
                    literal_symbol));

            tokens.push_back(
                std::move(token));
        }
        else if (event_symbol ==
                 MATCH_EVENT)
        {
            if (parameter_position + 3 >
                parameters.size())
            {
                set_error(
                    error,
                    "Truncated RTH1 match parameters.");

                return false;
            }

            Token token;

            token.type =
                TokenType::Match;

            if (!read_u16(
                    parameters,
                    parameter_position,
                    token.distance))
            {
                set_error(
                    error,
                    "Invalid RTH1 match distance.");

                return false;
            }

            std::uint8_t length_code = 0;

            if (!read_u8(
                    parameters,
                    parameter_position,
                    length_code))
            {
                set_error(
                    error,
                    "Invalid RTH1 match length.");

                return false;
            }

            if (length_code ==
                EXTENDED_LENGTH_MARKER)
            {
                std::uint16_t extended = 0;

                if (!read_u16(
                        parameters,
                        parameter_position,
                        extended))
                {
                    set_error(
                        error,
                        "Invalid RTH1 extended match length.");

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
                    "Invalid RTH1 match token.");

                return false;
            }

            tokens.push_back(
                std::move(token));
        }
        else
        {
            set_error(
                error,
                "Unknown RTH1 event symbol.");

            return false;
        }
    }

    if (parameter_position !=
        parameters.size())
    {
        set_error(
            error,
            "Unused RTH1 match parameters.");

        return false;
    }

    return true;
}

bool huffman_encode_tokens(
    std::span<const Token> tokens,
    std::vector<std::byte>& output,
    std::string* error)
{
    std::vector<std::byte> v3;
    std::string v3_error;

    const bool v3_valid =
        huffman_encode_tokens_v3(
            tokens,
            v3,
            &v3_error);

    std::vector<std::byte> v2;
    std::string v2_error;

    const bool v2_valid =
        huffman_encode_tokens_v2(
            tokens,
            v2,
            &v2_error);

    std::vector<std::byte> v1;
    std::string v1_error;

    const bool v1_valid =
        huffman_encode_tokens_v1(
            tokens,
            v1,
            &v1_error);

    if (!v3_valid &&
        !v2_valid &&
        !v1_valid)
    {
        set_error(
            error,
            "Unable to encode RTH1 token stream.");

        return false;
    }

    output.clear();

    const std::vector<std::byte>* selected = nullptr;

    if (v1_valid)
    {
        selected = &v1;
    }

    if (v2_valid &&
        (!selected ||
         v2.size() < selected->size()))
    {
        selected = &v2;
    }

    if (v3_valid &&
        (!selected ||
         v3.size() < selected->size()))
    {
        selected = &v3;
    }

    output =
        *selected;

    return true;
}

bool huffman_decode_tokens(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    tokens.clear();

    if (input.size() < 5)
    {
        set_error(
            error,
            "RTH1 stream is too small.");

        return false;
    }

    if (input[0] !=
            static_cast<std::byte>('R') ||
        input[1] !=
            static_cast<std::byte>('T') ||
        input[2] !=
            static_cast<std::byte>('H') ||
        input[3] !=
            static_cast<std::byte>('1'))
    {
        set_error(
            error,
            "Invalid RTH1 magic.");

        return false;
    }

    const std::uint8_t version =
        static_cast<std::uint8_t>(
            input[4]);

    if (version == LEGACY_VERSION)
    {
        return huffman_decode_tokens_v1(
            input,
            tokens,
            error);
    }

    if (version == VERSION)
    {
        return huffman_decode_tokens_v2(
            input,
            tokens,
            error);
    }

    if (version == V3_VERSION)
    {
        return huffman_decode_tokens_v3(
            input,
            tokens,
            error);
    }

    set_error(
        error,
        "Unsupported RTH1 version.");

    return false;
}

} // namespace rip::compression