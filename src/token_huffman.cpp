#include "rip/token_huffman.hpp"

#include <algorithm>
#include <array>
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

constexpr std::uint8_t VERSION = 1;

constexpr std::size_t LITERAL_SYMBOLS = 256;
constexpr std::size_t EVENT_SYMBOLS = 2;

constexpr std::size_t MATCH_EVENT = 1;

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

} // namespace

bool huffman_encode_tokens(
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
        EVENT_SYMBOLS>
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
        EVENT_SYMBOLS>
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
        EVENT_SYMBOLS>
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
                 TokenType::Match)
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

bool huffman_decode_tokens(
    std::span<const std::byte> input,
    std::vector<Token>& tokens,
    std::string* error)
{
    tokens.clear();

    constexpr std::size_t HEADER_SIZE =
        4 + 1 + 1 + 2 + 8 + 8 +
        LITERAL_SYMBOLS +
        EVENT_SYMBOLS;

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

    if (version != VERSION)
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
        EVENT_SYMBOLS>
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
        EVENT_SYMBOLS>
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

} // namespace rip::compression