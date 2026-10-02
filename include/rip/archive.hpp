#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace rip
{

    struct ArchiveEntryInfo
    {
        std::string path;

        std::uint64_t original_size{};
        std::uint64_t compressed_size{};

        std::uint32_t crc32{};

        std::uint8_t compression{};
    };

    struct ArchiveDetails
    {
        std::uint8_t major_version{};
        std::uint8_t minor_version{};

        std::uint64_t file_size{};

        std::uint32_t flags{};

        bool solid_ripc{};

        std::uint64_t solid_compressed_size{};

        std::vector<ArchiveEntryInfo> entries;
    };

    enum class ArchiveProgressStage : std::uint8_t
    {
        Preparing,
        Compressing,
        Writing,
        Finalizing
    };

    using ProgressCallback =
        std::function<void(
            std::uint64_t processed,
            std::uint64_t total,
            const std::filesystem::path &file,
            ArchiveProgressStage stage,
            std::uint8_t compression)>;

    class Archive
    {
    public:
        static bool create(
            const std::filesystem::path &output,
            const std::filesystem::path &input,
            ProgressCallback progress = {});

        static bool list(
            const std::filesystem::path &archive);

        static bool inspect(
            const std::filesystem::path &archive,
            ArchiveDetails &details);

        static bool test(
            const std::filesystem::path &archive);

        static bool extract(
            const std::filesystem::path &archive,
            const std::filesystem::path &output_directory);
    };

} // namespace rip