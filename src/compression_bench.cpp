#include "rip/compression.hpp"

#include <zlib.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <limits>

namespace
{

namespace fs = std::filesystem;

using Bytes = std::vector<std::byte>;

Bytes read_file(
    const fs::path& path)
{
    std::ifstream file(
        path,
        std::ios::binary |
        std::ios::ate);

    if (!file)
    {
        return {};
    }

    const std::streamsize size =
        file.tellg();

    if (size < 0)
    {
        return {};
    }

    file.seekg(0);

    Bytes data(
        static_cast<std::size_t>(
            size));

    if (!data.empty())
    {
        file.read(
            reinterpret_cast<char*>(
                data.data()),
            size);
    }

    if (!file)
    {
        return {};
    }

    return data;
}

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

    if (input.size() >
        static_cast<std::size_t>(
            std::numeric_limits<uLong>::max()))
    {
        return false;
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

    uLong destination_size =
        maximum;

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
    bool success{};
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
        milliseconds,
        true};
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
        milliseconds,
        true};
}

void print_result(
    const char* name,
    const Result& result)
{
    if (!result.success)
    {
        std::cout
            << std::left
            << std::setw(10)
            << name
            << "FAILED\n";

        return;
    }

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
    const std::string& name,
    const Bytes& input)
{
    std::cout
        << '\n'
        << name
        << '\n'
        << std::string(
               name.size(),
               '-')
        << '\n';

    std::cout
        << "Original: "
        << input.size()
        << " bytes\n\n";

    std::cout
        << std::left
        << std::setw(10)
        << "Method"
        << std::right
        << std::setw(18)
        << "Compressed"
        << "  Ratio     Savings       Time\n";

    const Result ripc =
        benchmark_ripc(input);

    const Result deflate =
        benchmark_deflate(input);

    print_result(
        "RIPC",
        ripc);

    print_result(
        "DEFLATE",
        deflate);
}

struct ProjectFile
{
    fs::path relative_path;
};

} // namespace

int main(
    int argc,
    char** argv)
{
    (void)argc;

    std::cout
        << "RIP Compression Benchmark\n"
        << "=========================\n"
        << "RIPC v0.3 vs DEFLATE\n";

    run_case(
        "Synthetic: repetitive text",
        make_repetitive_text());

    run_case(
        "Synthetic: source code",
        make_source_code());

    run_case(
        "Synthetic: random data",
        make_random_data());

    fs::path executable_path =
        fs::absolute(argv[0]);

    fs::path project_root =
        executable_path
            .parent_path()
            .parent_path()
            .parent_path();

    std::cout
        << '\n'
        << "Actual RIP project files\n"
        << "========================\n";

    const std::vector<ProjectFile> project_files = {
        {"CMakeLists.txt"},
        {"include/rip/archive.hpp"},
        {"include/rip/compression.hpp"},
        {"include/rip/crc32.hpp"},
        {"include/rip/format.hpp"},
        {"src/archive.cpp"},
        {"src/compression.cpp"},
        {"src/compression_bench.cpp"},
        {"src/compression_test.cpp"},
        {"src/crc32.cpp"},
        {"src/gui_main.cpp"},
        {"gui/src/main.ts"},
        {"gui/src/style.css"}
    };

    Bytes combined;

    for (const auto& project_file :
         project_files)
    {
        const fs::path path =
            project_root /
            project_file.relative_path;

        const Bytes data =
            read_file(path);

        if (data.empty())
        {
            if (!fs::exists(path))
            {
                std::cout
                    << "\nSKIP: "
                    << project_file.relative_path
                    << " (not found)\n";
            }
            else
            {
                std::cout
                    << "\nSKIP: "
                    << project_file.relative_path
                    << " (empty)\n";
            }

            continue;
        }

        run_case(
            "File: " +
                project_file.relative_path.generic_string(),
            data);

        combined.insert(
            combined.end(),
            data.begin(),
            data.end());

        // Separating files prevents a file boundary
        // from becoming an artificial repeated sequence.
        combined.push_back(
            static_cast<std::byte>('\n'));
        combined.push_back(
            static_cast<std::byte>('\n'));
    }

    if (!combined.empty())
    {
        run_case(
            "Combined RIP source",
            combined);
    }

    std::cout
        << '\n'
        << "Benchmark complete.\n";

    return 0;
}