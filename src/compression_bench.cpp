#include "rip/compression.hpp"

#include <zlib.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<std::byte>;

Bytes make_repetitive_text()
{
    const std::string block =
        "RIP archive compression benchmark. "
        "This sentence is intentionally repeated "
        "to create highly compressible data.\n";

    Bytes data;

    constexpr std::size_t repetitions = 100'000;

    data.reserve(
        block.size() * repetitions);

    for (std::size_t i = 0;
         i < repetitions;
         ++i)
    {
        for (const char character : block)
        {
            data.push_back(
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        character)));
        }
    }

    return data;
}

Bytes make_source_code()
{
    const std::string block =
        R"(#include <iostream>
#include <vector>
#include <string>

int process_file(
    const std::string& path,
    std::vector<std::string>& files)
{
    if (path.empty())
    {
        return -1;
    }

    files.push_back(path);

    std::cout
        << "Processing: "
        << path
        << '\n';

    return 0;
}

)";

    Bytes data;

    constexpr std::size_t repetitions = 20'000;

    data.reserve(
        block.size() * repetitions);

    for (std::size_t i = 0;
         i < repetitions;
         ++i)
    {
        for (const char character : block)
        {
            data.push_back(
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        character)));
        }
    }

    return data;
}

Bytes make_random_data()
{
    constexpr std::size_t size =
        4 * 1024 * 1024;

    Bytes data(size);

    std::mt19937 generator(0x52495043u);
    std::uniform_int_distribution<unsigned int>
        distribution(0, 255);

    for (auto& value : data)
    {
        value =
            static_cast<std::byte>(
                distribution(generator));
    }

    return data;
}

bool compress_deflate(
    const Bytes& input,
    Bytes& output)
{
    if (input.empty())
    {
        output.clear();
        return true;
    }

    const uLong source_size =
        static_cast<uLong>(
            input.size());

    const uLong maximum =
        ::compressBound(
            source_size);

    output.resize(
        static_cast<std::size_t>(
            maximum));

    uLong destination_size = maximum;

    const int result =
        ::compress2(
            reinterpret_cast<Bytef*>(
                output.data()),
            &destination_size,
            reinterpret_cast<const Bytef*>(
                input.data()),
            source_size,
            Z_BEST_COMPRESSION);

    if (result != Z_OK)
    {
        output.clear();
        return false;
    }

    output.resize(
        static_cast<std::size_t>(
            destination_size));

    return true;
}

struct Result
{
    std::size_t original_size{};
    std::size_t compressed_size{};
    double milliseconds{};
};

Result benchmark_ripc(
    const Bytes& input)
{
    Bytes output;

    const auto start =
        std::chrono::steady_clock::now();

    const bool success =
        rip::compression::compress(
            input,
            output);

    const auto end =
        std::chrono::steady_clock::now();

    if (!success)
    {
        return {};
    }

    const double milliseconds =
        std::chrono::duration<double, std::milli>(
            end - start)
            .count();

    return {
        input.size(),
        output.size(),
        milliseconds};
}

Result benchmark_deflate(
    const Bytes& input)
{
    Bytes output;

    const auto start =
        std::chrono::steady_clock::now();

    const bool success =
        compress_deflate(
            input,
            output);

    const auto end =
        std::chrono::steady_clock::now();

    if (!success)
    {
        return {};
    }

    const double milliseconds =
        std::chrono::duration<double, std::milli>(
            end - start)
            .count();

    return {
        input.size(),
        output.size(),
        milliseconds};
}

void print_result(
    const char* name,
    const Result& result)
{
    const double ratio =
        result.original_size == 0
            ? 0.0
            : (
                static_cast<double>(
                    result.compressed_size) /
                static_cast<double>(
                    result.original_size)
            ) * 100.0;

    const double savings =
        100.0 - ratio;

    std::cout
        << std::left
        << std::setw(10)
        << name
        << std::right
        << std::setw(12)
        << result.compressed_size
        << " bytes  "
        << std::fixed
        << std::setprecision(2)
        << std::setw(7)
        << ratio
        << "%  "
        << std::setw(7)
        << savings
        << "% saved  "
        << std::setw(10)
        << result.milliseconds
        << " ms\n";
}

void run_case(
    const char* name,
    const Bytes& input)
{
    std::cout
        << '\n'
        << name
        << '\n'
        << std::string(
               std::char_traits<char>::length(name),
               '-')
        << '\n';

    std::cout
        << "Original: "
        << input.size()
        << " bytes\n\n";

    const Result ripc =
        benchmark_ripc(input);

    const Result deflate =
        benchmark_deflate(input);

    std::cout
        << std::left
        << std::setw(10)
        << "Method"
        << std::right
        << std::setw(18)
        << "Compressed"
        << "  Ratio     Savings       Time\n";

    print_result(
        "RIPC",
        ripc);

    print_result(
        "DEFLATE",
        deflate);
}

} // namespace

int main()
{
    std::cout
        << "RIP Compression Benchmark\n"
        << "=========================\n"
        << "RIPC v0.2 vs DEFLATE\n";

    run_case(
        "Repetitive text",
        make_repetitive_text());

    run_case(
        "Source code",
        make_source_code());

    run_case(
        "Random data",
        make_random_data());

    std::cout
        << '\n'
        << "Benchmark complete.\n";

    return 0;
}