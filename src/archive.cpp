#include <zlib.h>

#include "rip/archive.hpp"
#include "rip/crc32.hpp"
#include "rip/format.hpp"
#include "rip/compression.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <span>

namespace fs = std::filesystem;

namespace rip
{

    namespace
    {

        // Explicit little-endian serialization

        void write_u8(std::ostream &output, std::uint8_t value)
        {
            output.put(static_cast<char>(value));

            if (!output)
            {
                throw std::runtime_error("Failed to write uint8.");
            }
        }

        void write_u16(std::ostream &output, std::uint16_t value)
        {
            write_u8(
                output,
                static_cast<std::uint8_t>(value & 0xFFu));

            write_u8(
                output,
                static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        }

        void write_u32(std::ostream &output, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        (value >> (i * 8)) & 0xFFu));
            }
        }

        void write_u64(std::ostream &output, std::uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                write_u8(
                    output,
                    static_cast<std::uint8_t>(
                        (value >> (i * 8)) & 0xFFu));
            }
        }

        std::uint8_t read_u8(std::istream &input)
        {
            char value{};

            input.get(value);

            if (!input)
            {
                throw std::runtime_error(
                    "Unexpected end of archive.");
            }

            return static_cast<std::uint8_t>(
                static_cast<unsigned char>(value));
        }

        std::uint16_t read_u16(std::istream &input)
        {
            const std::uint16_t b0 = read_u8(input);
            const std::uint16_t b1 = read_u8(input);

            return b0 |
                   static_cast<std::uint16_t>(b1 << 8u);
        }

        std::uint32_t read_u32(std::istream &input)
        {
            std::uint32_t value = 0;

            for (int i = 0; i < 4; ++i)
            {
                value |=
                    static_cast<std::uint32_t>(read_u8(input))
                    << (i * 8);
            }

            return value;
        }

        std::uint64_t read_u64(std::istream &input)
        {
            std::uint64_t value = 0;

            for (int i = 0; i < 8; ++i)
            {
                value |=
                    static_cast<std::uint64_t>(read_u8(input))
                    << (i * 8);
            }

            return value;
        }

        // RIP header serialization

        void write_magic(std::ostream &output)
        {
            output.write(
                RIP_MAGIC,
                sizeof(RIP_MAGIC));

            if (!output)
            {
                throw std::runtime_error(
                    "Failed to write RIP magic.");
            }
        }

        void read_magic(std::istream &input)
        {
            std::array<char, 4> magic{};

            input.read(
                magic.data(),
                static_cast<std::streamsize>(magic.size()));

            if (!input)
            {
                throw std::runtime_error(
                    "Archive is too small to contain a RIP header.");
            }

            if (!std::equal(
                    magic.begin(),
                    magic.end(),
                    std::begin(RIP_MAGIC)))
            {
                throw std::runtime_error(
                    "Not a valid RIP archive.");
            }
        }

        void write_header(
            std::ostream &output,
            const ArchiveHeader &header)
        {
            write_magic(output);

            write_u8(output, header.major_version);
            write_u8(output, header.minor_version);
            write_u16(output, header.header_size);
            write_u32(output, header.flags);
            write_u64(output, header.entry_count);
            write_u64(output, header.index_offset);
            write_u64(output, header.index_size);
        }

        ArchiveHeader read_header(std::istream &input)
        {
            read_magic(input);

            ArchiveHeader header{};

            header.major_version = read_u8(input);
            header.minor_version = read_u8(input);
            header.header_size = read_u16(input);
            header.flags = read_u32(input);
            header.entry_count = read_u64(input);
            header.index_offset = read_u64(input);
            header.index_size = read_u64(input);

            if (header.major_version != RIP_FORMAT_MAJOR)
            {
                throw std::runtime_error(
                    "Unsupported RIP major version: " +
                    std::to_string(header.major_version));
            }

            if (header.header_size < ARCHIVE_HEADER_SIZE)
            {
                throw std::runtime_error(
                    "Invalid RIP header size.");
            }

            return header;
        }

        // File entry serialization

        void write_file_entry(
            std::ostream &output,
            const FileEntry &entry)
        {
            write_u64(output, entry.data_offset);
            write_u64(output, entry.original_size);
            write_u64(output, entry.compressed_size);
            write_u32(output, entry.crc32);
            write_u8(output, entry.compression);
            write_u32(output, entry.path_size);
        }

        FileEntry read_file_entry(std::istream &input)
        {
            FileEntry entry{};

            entry.data_offset = read_u64(input);
            entry.original_size = read_u64(input);
            entry.compressed_size = read_u64(input);
            entry.crc32 = read_u32(input);
            entry.compression = read_u8(input);
            entry.path_size = read_u32(input);

            return entry;
        }

        std::vector<std::byte> compress_deflate(
            std::span<const std::byte> input)
        {
            if (input.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<uLong>::max()))
            {
                throw std::runtime_error(
                    "File is too large for zlib compression.");
            }

            const uLong source_size =
                static_cast<uLong>(input.size());

            const uLong max_compressed_size =
                compressBound(source_size);

            if (max_compressed_size >
                static_cast<uLong>(
                    std::numeric_limits<std::size_t>::max()))
            {
                throw std::runtime_error(
                    "Compressed data is too large for this build.");
            }

            std::vector<std::byte> output(
                static_cast<std::size_t>(
                    max_compressed_size));

            uLongf compressed_size =
                max_compressed_size;

            const int result =
                compress2(
                    reinterpret_cast<Bytef *>(
                        output.data()),
                    &compressed_size,
                    reinterpret_cast<const Bytef *>(
                        input.data()),
                    source_size,
                    Z_DEFAULT_COMPRESSION);

            if (result != Z_OK)
            {
                throw std::runtime_error(
                    "zlib compression failed with error code: " +
                    std::to_string(result));
            }

            output.resize(
                static_cast<std::size_t>(
                    compressed_size));

            return output;
        }

        std::vector<std::byte> decompress_deflate(
            std::span<const std::byte> input,
            std::uint64_t original_size)
        {
            if (input.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<uLong>::max()))
            {
                throw std::runtime_error(
                    "Compressed file is too large for zlib.");
            }

            if (original_size >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()))
            {
                throw std::runtime_error(
                    "Decompressed file is too large for this build.");
            }

            if (original_size >
                static_cast<std::uint64_t>(
                    std::numeric_limits<uLongf>::max()))
            {
                throw std::runtime_error(
                    "Decompressed file is too large for zlib.");
            }

            std::vector<std::byte> output(
                static_cast<std::size_t>(
                    original_size));

            uLongf output_size =
                static_cast<uLongf>(
                    original_size);

            const int result =
                uncompress(
                    reinterpret_cast<Bytef *>(
                        output.data()),
                    &output_size,
                    reinterpret_cast<const Bytef *>(
                        input.data()),
                    static_cast<uLong>(
                        input.size()));

            if (result != Z_OK)
            {
                throw std::runtime_error(
                    "zlib decompression failed with error code: " +
                    std::to_string(result));
            }

            if (output_size !=
                static_cast<uLongf>(original_size))
            {
                throw std::runtime_error(
                    "Decompressed size does not match the RIP entry.");
            }

            return output;
        }

        // Internal data structures

        struct PendingFile
        {
            fs::path source;
            std::string archive_path;

            // Contains the bytes that will actually be stored in the archive.
            std::vector<std::byte> data;

            std::uint64_t original_size{};

            std::uint32_t checksum{};

            std::uint8_t compression =
                COMPRESSION_STORE;

            std::uint64_t data_offset{};
        };

        struct ArchiveInfo
        {
            ArchiveHeader header{};
            std::uint64_t file_size{};
        };

        // File helpers

        std::uint64_t get_file_size(std::ifstream &file)
        {
            file.seekg(0, std::ios::end);

            const std::streampos position = file.tellg();

            if (position < 0)
            {
                throw std::runtime_error(
                    "Unable to determine archive size.");
            }

            return static_cast<std::uint64_t>(position);
        }

        ArchiveInfo read_and_validate_header(
            std::ifstream &archive)
        {
            const std::uint64_t file_size =
                get_file_size(archive);

            if (file_size < ARCHIVE_HEADER_SIZE)
            {
                throw std::runtime_error(
                    "Archive is too small to contain a RIP header.");
            }

            archive.seekg(0);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to seek to RIP header.");
            }

            ArchiveHeader header = read_header(archive);

            // Current format has no additional header fields.
            // If a future minor version adds fields, this is where they
            // can be skipped safely.
            if (header.header_size > ARCHIVE_HEADER_SIZE)
            {
                archive.seekg(
                    static_cast<std::streamoff>(header.header_size));

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to skip extended RIP header.");
                }
            }

            if (header.index_offset < header.header_size)
            {
                throw std::runtime_error(
                    "Invalid RIP index offset.");
            }

            if (header.index_offset > file_size)
            {
                throw std::runtime_error(
                    "RIP index is outside the archive.");
            }

            if (header.index_size >
                file_size - header.index_offset)
            {
                throw std::runtime_error(
                    "RIP index extends beyond the archive.");
            }

            return {
                .header = header,
                .file_size = file_size};
        }

        std::vector<std::pair<FileEntry, std::string>> read_entries(
            std::ifstream &archive,
            const ArchiveInfo &info)
        {
            if (info.header.entry_count >
                info.header.index_size / FILE_ENTRY_SIZE)
            {
                throw std::runtime_error(
                    "RIP entry count exceeds the available index.");
            }

            const std::uint64_t index_end =
                info.header.index_offset +
                info.header.index_size;

            archive.seekg(
                static_cast<std::streamoff>(
                    info.header.index_offset));

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to seek to RIP index.");
            }

            std::vector<std::pair<FileEntry, std::string>> entries;

            entries.reserve(
                static_cast<std::size_t>(
                    std::min<std::uint64_t>(
                        info.header.entry_count,
                        1'000'000)));

            for (std::uint64_t i = 0;
                 i < info.header.entry_count;
                 ++i)
            {
                const std::streampos position =
                    archive.tellg();

                if (position < 0)
                {
                    throw std::runtime_error(
                        "Unable to determine RIP index position.");
                }

                const std::uint64_t current_position =
                    static_cast<std::uint64_t>(position);

                if (current_position < info.header.index_offset ||
                    current_position > index_end)
                {
                    throw std::runtime_error(
                        "RIP index entry is outside the index.");
                }

                if (index_end - current_position <
                    FILE_ENTRY_SIZE)
                {
                    throw std::runtime_error(
                        "Truncated RIP file entry.");
                }

                FileEntry entry =
                    read_file_entry(archive);

                const std::streampos path_position =
                    archive.tellg();

                if (path_position < 0)
                {
                    throw std::runtime_error(
                        "Unable to determine RIP path position.");
                }

                const std::uint64_t path_start =
                    static_cast<std::uint64_t>(path_position);

                if (path_start > index_end ||
                    entry.path_size > index_end - path_start)
                {
                    throw std::runtime_error(
                        "RIP file path extends beyond the index.");
                }

                std::string path(
                    entry.path_size,
                    '\0');

                if (!path.empty())
                {
                    archive.read(
                        path.data(),
                        static_cast<std::streamsize>(
                            path.size()));

                    if (!archive)
                    {
                        throw std::runtime_error(
                            "Unable to read RIP file path.");
                    }
                }

                if (entry.data_offset > info.file_size)
                {
                    throw std::runtime_error(
                        "RIP file data offset is outside the archive.");
                }

                if (entry.compressed_size >
                    info.file_size - entry.data_offset)
                {
                    throw std::runtime_error(
                        "RIP file data extends beyond the archive.");
                }

                if (entry.compression != COMPRESSION_STORE &&
                    entry.compression != COMPRESSION_DEFLATE &&
                    entry.compression != COMPRESSION_RIPC)
                {
                    throw std::runtime_error(
                        "Unsupported compression method: " +
                        std::to_string(entry.compression));
                }
                if (entry.compression != COMPRESSION_STORE &&
    entry.compression != COMPRESSION_DEFLATE &&
    entry.compression != COMPRESSION_RIPC)
{
    throw std::runtime_error(
        "Unsupported compression method: " +
        std::to_string(entry.compression));
}

if (entry.compression == COMPRESSION_STORE &&
    entry.original_size != entry.compressed_size)
{
    throw std::runtime_error(
        "Invalid stored file size: " +
        std::to_string(entry.original_size));
}

                if (entry.compression == COMPRESSION_STORE &&
                    entry.original_size != entry.compressed_size)
                {
                    throw std::runtime_error(
                        "Invalid stored file size: " +
                        path);
                }

                entries.emplace_back(
                    entry,
                    std::move(path));
            }

            const std::streampos final_position =
                archive.tellg();

            if (final_position < 0)
            {
                throw std::runtime_error(
                    "Unable to determine final RIP index position.");
            }

            if (static_cast<std::uint64_t>(final_position) !=
                index_end)
            {
                throw std::runtime_error(
                    "RIP index size does not match its entries.");
            }

            return entries;
        }

        std::vector<std::byte> read_file(
            const fs::path &path)
        {
            std::ifstream file(
                path,
                std::ios::binary | std::ios::ate);

            if (!file)
            {
                throw std::runtime_error(
                    "Unable to open file: " +
                    path.string());
            }

            const std::streampos size = file.tellg();

            if (size < 0)
            {
                throw std::runtime_error(
                    "Unable to determine file size: " +
                    path.string());
            }

            if (static_cast<std::uint64_t>(size) >
                std::numeric_limits<std::size_t>::max())
            {
                throw std::runtime_error(
                    "File is too large for this build.");
            }

            std::vector<std::byte> data(
                static_cast<std::size_t>(size));

            file.seekg(0);

            if (!data.empty())
            {
                file.read(
                    reinterpret_cast<char *>(data.data()),
                    static_cast<std::streamsize>(data.size()));
            }

            if (!file && !data.empty())
            {
                throw std::runtime_error(
                    "Failed reading file: " +
                    path.string());
            }

            return data;
        }

        std::string normalize_path(const fs::path &path)
        {
            std::string result =
                path.generic_string();

            while (!result.empty() &&
                   result.front() == '/')
            {
                result.erase(result.begin());
            }

            return result;
        }

        std::uint64_t count_regular_files(
            const fs::path &input)
        {
            if (fs::is_regular_file(input))
            {
                return 1;
            }

            if (!fs::is_directory(input))
            {
                throw std::runtime_error(
                    "Input is not a file or directory: " +
                    input.string());
            }

            std::uint64_t count = 0;

            for (const auto &entry :
                 fs::recursive_directory_iterator(input))
            {
                if (entry.is_regular_file())
                {
                    ++count;
                }
            }

            return count;
        }

        void collect_files(
            const fs::path &input,
            const fs::path &archive_root,
            std::vector<PendingFile> &files,
            const ProgressCallback &progress,
            std::uint64_t total_files,
            std::uint64_t &processed_files)
        {
            auto prepare_file =
                [&](PendingFile &file)
            {
                file.data =
                    read_file(file.source);

                file.original_size =
                    static_cast<std::uint64_t>(
                        file.data.size());

                file.checksum =
                    crc32(file.data);

                file.compression =
                    COMPRESSION_STORE;

                if (!file.data.empty())
                {
                    std::vector<std::byte>
                        best_data =
                            file.data;

                    std::uint8_t best_method =
                        COMPRESSION_STORE;

                    // DEFLATE
                    {
                        auto compressed =
                            compress_deflate(
                                file.data);

                        if (compressed.size() <
                            best_data.size())
                        {
                            best_data =
                                std::move(compressed);

                            best_method =
                                COMPRESSION_DEFLATE;
                        }
                    }

                    // RIPC
                    {
                        std::vector<std::byte>
                            compressed;

                        std::string ripc_error;

                        if (rip::compression::compress(
                                file.data,
                                compressed,
                                &ripc_error))
                        {
                            if (compressed.size() <
                                best_data.size())
                            {
                                best_data =
                                    std::move(compressed);

                                best_method =
                                    COMPRESSION_RIPC;
                            }
                        }
                    }

                    file.data =
                        std::move(best_data);

                    file.compression =
                        best_method;
                }

                ++processed_files;

                if (progress)
                {
                    progress(
                        processed_files,
                        total_files,
                        file.source,
                        ArchiveProgressStage::Compressing,
                        file.compression);
                }
            };

            if (fs::is_regular_file(input))
            {
                PendingFile file;

                file.source =
                    input;

                file.archive_path =
                    input.filename().generic_string();

                prepare_file(file);

                files.push_back(
                    std::move(file));

                return;
            }

            if (!fs::is_directory(input))
            {
                return;
            }

            for (const auto &entry :
                 fs::directory_iterator(input))
            {
                if (entry.is_regular_file())
                {
                    PendingFile file;

                    file.source =
                        entry.path();

                    file.archive_path =
                        entry.path()
                            .lexically_relative(archive_root)
                            .generic_string();

                    prepare_file(file);

                    files.push_back(
                        std::move(file));
                }
                else if (entry.is_directory())
                {
                    collect_files(
                        entry.path(),
                        archive_root,
                        files,
                        progress,
                        total_files,
                        processed_files);
                }
            }
        }

    } // namespace

    // Create

    bool Archive::create(
        const fs::path &output,
        const fs::path &input,
        ProgressCallback progress)
    {
        try
        {
            const std::uint64_t total_files =
                count_regular_files(input);

            std::uint64_t processed_files = 0;

            if (progress)
            {
                progress(
                    0,
                    total_files,
                    input,
                    ArchiveProgressStage::Preparing,
                    COMPRESSION_STORE);
            }

            std::vector<PendingFile> files;

            collect_files(
                input,
                fs::is_directory(input)
                    ? input
                    : input.parent_path(),
                files,
                progress,
                total_files,
                processed_files);

            /*
             * Solid RIPC is considered only when there are multiple
             * files. Compare the complete archive data payloads,
             * including the 16-byte solid-header extension.
             */
            bool solid_ripc = false;
            std::vector<std::byte> solid_data;

            if (files.size() > 1)
            {
                std::uint64_t independent_size = 0;

                for (const auto &file : files)
                {
                    if (file.data.size() >
                        std::numeric_limits<std::uint64_t>::max() -
                            independent_size)
                    {
                        throw std::runtime_error(
                            "Archive is too large for solid RIPC.");
                    }

                    independent_size +=
                        static_cast<std::uint64_t>(
                            file.data.size());
                }

                std::vector<std::byte> solid_input;

                std::uint64_t original_total = 0;

                for (const auto &file : files)
                {
                    if (file.original_size >
                        std::numeric_limits<std::uint64_t>::max() -
                            original_total)
                    {
                        throw std::runtime_error(
                            "Archive is too large for solid RIPC.");
                    }

                    original_total +=
                        file.original_size;
                }

                if (original_total <=
                    std::numeric_limits<std::size_t>::max())
                {
                    solid_input.reserve(
                        static_cast<std::size_t>(
                            original_total));
                }

                for (const auto &file : files)
                {
                    const auto original =
                        read_file(file.source);

                    if (original.size() !=
                        static_cast<std::size_t>(
                            file.original_size))
                    {
                        throw std::runtime_error(
                            "Source file changed while creating archive: " +
                            file.source.string());
                    }

                    solid_input.insert(
                        solid_input.end(),
                        original.begin(),
                        original.end());
                }

                std::string solid_error;

                if (rip::compression::compress(
                        solid_input,
                        solid_data,
                        &solid_error))
                {
                    const std::uint64_t solid_total =
                        static_cast<std::uint64_t>(
                            solid_data.size()) +
                        SOLID_RIPC_HEADER_SIZE -
                        ARCHIVE_HEADER_SIZE;

                    if (solid_total < independent_size)
                    {
                        solid_ripc = true;

                        for (auto &file : files)
                        {
                            file.compression =
                                COMPRESSION_RIPC;
                            file.data.clear();
                        }
                    }
                    else
                    {
                        solid_data.clear();
                    }
                }
            }

            std::ofstream archive(
                output,
                std::ios::binary |
                    std::ios::trunc);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to create archive: " +
                    output.string());
            }

            ArchiveHeader header{};

            header.major_version =
                RIP_FORMAT_MAJOR;

            header.minor_version =
                RIP_FORMAT_MINOR;

            header.header_size =
                static_cast<std::uint16_t>(
                    solid_ripc
                        ? SOLID_RIPC_HEADER_SIZE
                        : ARCHIVE_HEADER_SIZE);

            header.flags =
                solid_ripc
                    ? ARCHIVE_FLAG_SOLID_RIPC
                    : ARCHIVE_FLAG_NONE;

            header.entry_count =
                files.size();

            header.index_offset = 0;
            header.index_size = 0;

            if (solid_ripc)
            {
                header.solid_data_offset =
                    SOLID_RIPC_HEADER_SIZE;

                header.solid_compressed_size =
                    static_cast<std::uint64_t>(
                        solid_data.size());
            }

            write_header(
                archive,
                header);

            if (solid_ripc)
            {
                if (!solid_data.empty())
                {
                    archive.write(
                        reinterpret_cast<const char *>(
                            solid_data.data()),
                        static_cast<std::streamsize>(
                            solid_data.size()));
                }

                for (auto &file : files)
                {
                    file.data_offset = 0;

                    if (progress)
                    {
                        progress(
                            processed_files,
                            total_files,
                            file.source,
                            ArchiveProgressStage::Writing,
                            file.compression);
                    }
                }
            }
            else
            {
                for (auto &file : files)
                {
                    const std::streampos position =
                        archive.tellp();

                    if (position < 0)
                    {
                        throw std::runtime_error(
                            "Unable to determine file data offset.");
                    }

                    file.data_offset =
                        static_cast<std::uint64_t>(
                            position);

                    if (!file.data.empty())
                    {
                        archive.write(
                            reinterpret_cast<const char *>(
                                file.data.data()),
                            static_cast<std::streamsize>(
                                file.data.size()));
                    }

                    if (progress)
                    {
                        progress(
                            processed_files,
                            total_files,
                            file.source,
                            ArchiveProgressStage::Writing,
                            file.compression);
                    }

                    if (!archive)
                    {
                        throw std::runtime_error(
                            "Failed writing file data.");
                    }
                }
            }

            const std::streampos index_position =
                archive.tellp();

            if (index_position < 0)
            {
                throw std::runtime_error(
                    "Unable to determine index offset.");
            }

            const std::uint64_t index_offset =
                static_cast<std::uint64_t>(
                    index_position);

            for (const auto &file : files)
            {
                FileEntry entry{};

                entry.data_offset =
                    solid_ripc
                        ? 0
                        : file.data_offset;

                entry.original_size =
                    file.original_size;

                entry.compressed_size =
                    solid_ripc
                        ? 0
                        : static_cast<std::uint64_t>(
                            file.data.size());

                entry.crc32 =
                    file.checksum;

                entry.compression =
                    file.compression;

                if (file.archive_path.size() >
                    std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::runtime_error(
                        "Archive path is too long: " +
                        file.archive_path);
                }

                entry.path_size =
                    static_cast<std::uint32_t>(
                        file.archive_path.size());

                write_file_entry(
                    archive,
                    entry);

                if (!file.archive_path.empty())
                {
                    archive.write(
                        file.archive_path.data(),
                        static_cast<std::streamsize>(
                            file.archive_path.size()));
                }

                if (!archive)
                {
                    throw std::runtime_error(
                        "Failed writing archive index.");
                }
            }

            const std::streampos end_position =
                archive.tellp();

            if (end_position < 0)
            {
                throw std::runtime_error(
                    "Unable to determine archive size.");
            }

            const std::uint64_t index_size =
                static_cast<std::uint64_t>(
                    end_position) -
                index_offset;

            header.index_offset =
                index_offset;

            header.index_size =
                index_size;

            if (progress)
            {
                progress(
                    total_files,
                    total_files,
                    output,
                    ArchiveProgressStage::Finalizing,
                    COMPRESSION_STORE);
            }

            archive.seekp(0);

            if (!archive)
            {
                throw std::runtime_error(
                    "Failed to seek to RIP header.");
            }

            write_header(
                archive,
                header);

            if (!archive)
            {
                throw std::runtime_error(
                    "Failed to update RIP header.");
            }

            archive.close();

            if (!archive)
            {
                throw std::runtime_error(
                    "Failed closing archive.");
            }

            std::cout
                << "Created "
                << output
                << '\n'
                << "Files: "
                << files.size()
                << '\n';

            return true;

            if (!archive)
            {
                throw std::runtime_error(
                    "Failed closing archive.");
            }

            std::cout
                << "Created "
                << output
                << '\n'
                << "Files: "
                << files.size()
                << '\n';

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr
                << "rip: "
                << e.what()
                << '\n';

            return false;
        }
    }

    bool Archive::inspect(
        const fs::path &archive_path,
        ArchiveDetails &details)
    {
        try
        {
            std::ifstream archive(
                archive_path,
                std::ios::binary);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to open archive: " +
                    archive_path.string());
            }

            const ArchiveInfo info =
                read_and_validate_header(archive);

            const auto entries =
                read_entries(archive, info);

            details = {};

            details.major_version =
                info.header.major_version;

            details.minor_version =
                info.header.minor_version;

            details.file_size =
                info.file_size;

            details.flags =
                info.header.flags;

            details.solid_ripc =
                (info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0;

            details.solid_compressed_size =
                info.header.solid_compressed_size;

            details.entries.reserve(
                entries.size());

            for (const auto &[entry, path] :
                 entries)
            {
                ArchiveEntryInfo output_entry{};

                output_entry.path =
                    path;

                output_entry.original_size =
                    entry.original_size;

                output_entry.compressed_size =
                    entry.compressed_size;

                output_entry.crc32 =
                    entry.crc32;

                output_entry.compression =
                    entry.compression;

                details.entries.push_back(
                    std::move(output_entry));
            }

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr
                << "rip: "
                << e.what()
                << '\n';

            return false;
        }
    }

    // List

    bool Archive::list(
        const fs::path &archive_path)
    {
        try
        {
            std::ifstream archive(
                archive_path,
                std::ios::binary);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to open archive: " +
                    archive_path.string());
            }

            const ArchiveInfo info =
                read_and_validate_header(
                    archive);

            const auto entries =
                read_entries(
                    archive,
                    info);

            std::cout
                << "RIP Archive\n"
                << "------------\n"
                << "Version: "
                << static_cast<unsigned>(
                       info.header.major_version)
                << '.'
                << static_cast<unsigned>(
                       info.header.minor_version)
                << '\n'
                << "Files:   "
                << info.header.entry_count
                << '\n'
                << "Size:    "
                << info.file_size
                << " bytes"
                << '\n';

            if ((info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0)
            {
                std::cout
                    << "Solid RIPC: "
                    << info.header.solid_compressed_size
                    << " bytes"
                    << '\n';
            }

            std::cout << '\n';

            for (const auto &[entry, path] : entries)
            {
                const char *compression_name =
                    entry.compression ==
                            COMPRESSION_RIPC
                        ? "RIPC"
                    : entry.compression ==
                            COMPRESSION_DEFLATE
                        ? "DEFLATE"
                        : "STORE";

                std::cout
                    << path
                    << "  "
                    << entry.original_size
                    << " bytes";

                if ((info.header.flags &
                     ARCHIVE_FLAG_SOLID_RIPC) != 0)
                {
                    std::cout
                        << "  SOLID RIPC";
                }
                else if (entry.compression !=
                         COMPRESSION_STORE)
                {
                    std::cout
                        << " -> "
                        << entry.compressed_size
                        << " bytes"
                        << "  "
                        << compression_name;
                }
                else
                {
                    std::cout
                        << "  "
                        << compression_name;
                }

                std::cout << '\n';
            }

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr
                << "rip: "
                << e.what()
                << '\n';

            return false;
        }
    }

    // Test

    bool Archive::test(
        const fs::path &archive_path)
    {
        try
        {
            std::ifstream archive(
                archive_path,
                std::ios::binary);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to open archive: " +
                    archive_path.string());
            }

            const ArchiveInfo info =
                read_and_validate_header(archive);

            const auto entries =
                read_entries(archive, info);

            std::cout
                << "RIP Archive Test\n"
                << "----------------\n"
                << "Header       OK\n"
                << "Index        OK\n";

            std::vector<std::byte> solid_original;
            std::size_t solid_offset = 0;

            if ((info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0)
            {
                if (info.header.solid_compressed_size >
                    std::numeric_limits<std::size_t>::max())
                {
                    throw std::runtime_error(
                        "Solid RIPC stream is too large to test.");
                }

                archive.seekg(
                    static_cast<std::streamoff>(
                        info.header.solid_data_offset));

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to seek to solid RIPC data.");
                }

                std::vector<std::byte> compressed(
                    static_cast<std::size_t>(
                        info.header.solid_compressed_size));

                if (!compressed.empty())
                {
                    archive.read(
                        reinterpret_cast<char *>(
                            compressed.data()),
                        static_cast<std::streamsize>(
                            compressed.size()));
                }

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to read solid RIPC data.");
                }

                std::string ripc_error;

                if (!rip::compression::decompress(
                        compressed,
                        solid_original,
                        &ripc_error))
                {
                    throw std::runtime_error(
                        "Solid RIPC decompression failed: " +
                        ripc_error);
                }

                std::uint64_t expected_size = 0;

                for (const auto &[entry, path] : entries)
                {
                if ((info.header.flags &
                     ARCHIVE_FLAG_SOLID_RIPC) != 0)
                {
                    if (entry.original_size >
                        solid_original.size() - solid_offset)
                    {
                        throw std::runtime_error(
                            "Solid RIPC entry exceeds decompressed stream: " +
                            path);
                    }

                    const std::size_t size =
                        static_cast<std::size_t>(
                            entry.original_size);

                    const std::span<const std::byte> original_data(
                        solid_original.data() + solid_offset,
                        size);

                    if (crc32(original_data) != entry.crc32)
                    {
                        throw std::runtime_error(
                            "CRC-32 mismatch: " +
                            path);
                    }

                    std::cout
                        << path
                        << "  OK\n";

                    solid_offset += size;
                    continue;
                }

                    if (entry.original_size >
                        std::numeric_limits<std::uint64_t>::max() -
                            expected_size)
                    {
                        throw std::runtime_error(
                            "Solid RIPC original size overflow.");
                    }

                    expected_size += entry.original_size;
                }

                if (expected_size != solid_original.size())
                {
                    throw std::runtime_error(
                        "Solid RIPC decompressed size mismatch.");
                }
            }

            for (const auto &[entry, path] : entries)
            {
                archive.seekg(
                    static_cast<std::streamoff>(
                        entry.data_offset));

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to seek to: " +
                        path);
                }

                if (entry.compressed_size >
                    std::numeric_limits<std::size_t>::max())
                {
                    throw std::runtime_error(
                        "File is too large to test: " +
                        path);
                }

                std::vector<std::byte> data(
                    static_cast<std::size_t>(
                        entry.compressed_size));

                if (!data.empty())
                {
                    archive.read(
                        reinterpret_cast<char *>(
                            data.data()),
                        static_cast<std::streamsize>(
                            data.size()));
                }

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to read: " +
                        path);
                }

                std::vector<std::byte> original_data;

                if (entry.compression ==
                    COMPRESSION_STORE)
                {
                    original_data =
                        std::move(data);
                }
                else if (entry.compression ==
                         COMPRESSION_DEFLATE)
                {
                    original_data =
                        decompress_deflate(
                            data,
                            entry.original_size);
                }
                else if (entry.compression ==
                         COMPRESSION_RIPC)
                {
                    std::string ripc_error;

                    if (!rip::compression::decompress(
                            data,
                            original_data,
                            &ripc_error))
                    {
                        throw std::runtime_error(
                            "RIPC decompression failed for " +
                            path +
                            ": " +
                            ripc_error);
                    }
                }
                else
                {
                    throw std::runtime_error(
                        "Unsupported compression method: " +
                        std::to_string(entry.compression));
                }

                if (original_data.size() !=
                    entry.original_size)
                {
                    throw std::runtime_error(
                        "Decompressed size mismatch: " +
                        path);
                }

                const std::uint32_t actual_crc =
                    crc32(original_data);

                if (actual_crc != entry.crc32)
                {
                    throw std::runtime_error(
                        "CRC-32 mismatch: " +
                        path);
                }

                std::cout
                    << path
                    << "  OK\n";
            }

            if ((info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0 &&
                solid_offset != solid_original.size())
            {
                throw std::runtime_error(
                    "Solid RIPC stream contains trailing data.");
            }

            std::cout
                << '\n'
                << "Archive is valid.\n";

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr
                << "rip: "
                << e.what()
                << '\n';

            return false;
        }
    }

    // Extract

    bool Archive::extract(
        const fs::path &archive_path,
        const fs::path &output_directory)
    {
        try
        {
            std::ifstream archive(
                archive_path,
                std::ios::binary);

            if (!archive)
            {
                throw std::runtime_error(
                    "Unable to open archive: " +
                    archive_path.string());
            }

            const ArchiveInfo info =
                read_and_validate_header(archive);

            const auto entries =
                read_entries(archive, info);

            fs::create_directories(
                output_directory);

            std::vector<std::byte> solid_original;
            std::size_t solid_offset = 0;

            if ((info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0)
            {
                if (info.header.solid_compressed_size >
                    std::numeric_limits<std::size_t>::max())
                {
                    throw std::runtime_error(
                        "Solid RIPC stream is too large to extract.");
                }

                archive.seekg(
                    static_cast<std::streamoff>(
                        info.header.solid_data_offset));

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to seek to solid RIPC data.");
                }

                std::vector<std::byte> compressed(
                    static_cast<std::size_t>(
                        info.header.solid_compressed_size));

                if (!compressed.empty())
                {
                    archive.read(
                        reinterpret_cast<char *>(
                            compressed.data()),
                        static_cast<std::streamsize>(
                            compressed.size()));
                }

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to read solid RIPC data.");
                }

                std::string ripc_error;

                if (!rip::compression::decompress(
                        compressed,
                        solid_original,
                        &ripc_error))
                {
                    throw std::runtime_error(
                        "Solid RIPC decompression failed: " +
                        ripc_error);
                }

                std::uint64_t expected_size = 0;

                for (const auto &[entry, path] : entries)
                {
                    if (entry.original_size >
                        std::numeric_limits<std::uint64_t>::max() -
                            expected_size)
                    {
                        throw std::runtime_error(
                            "Solid RIPC original size overflow.");
                    }

                    expected_size += entry.original_size;
                }

                if (expected_size != solid_original.size())
                {
                    throw std::runtime_error(
                        "Solid RIPC decompressed size mismatch.");
                }
            }

            for (const auto &[entry, archive_path_string] :
                 entries)
            {
                const fs::path relative_path =
                    fs::path(archive_path_string);

                if (relative_path.empty() ||
                    relative_path.is_absolute() ||
                    relative_path.has_root_name() ||
                    relative_path.has_root_directory())
                {
                    throw std::runtime_error(
                        "Unsafe path in archive: " +
                        archive_path_string);
                }

                for (const auto &component :
                     relative_path)
                {
                    if (component == "..")
                    {
                        throw std::runtime_error(
                            "Unsafe path in archive: " +
                            archive_path_string);
                    }
                }

                const fs::path destination =
                    output_directory /
                    relative_path;

                if (destination.has_parent_path())
                {
                    fs::create_directories(
                        destination.parent_path());
                }

                if ((info.header.flags &
                     ARCHIVE_FLAG_SOLID_RIPC) != 0)
                {
                    if (entry.compression !=
                        COMPRESSION_RIPC)
                    {
                        throw std::runtime_error(
                            "Solid RIPC archive contains a non-RIPC entry.");
                    }

                    if (entry.original_size >
                        solid_original.size() - solid_offset)
                    {
                        throw std::runtime_error(
                            "Solid RIPC entry exceeds decompressed stream: " +
                            archive_path_string);
                    }

                    const std::size_t size =
                        static_cast<std::size_t>(
                            entry.original_size);

                    const std::span<const std::byte> original_data(
                        solid_original.data() + solid_offset,
                        size);

                    if (crc32(original_data) != entry.crc32)
                    {
                        throw std::runtime_error(
                            "CRC-32 verification failed: " +
                            archive_path_string);
                    }

                    std::ofstream output_file(
                        destination,
                        std::ios::binary |
                            std::ios::trunc);

                    if (!output_file)
                    {
                        throw std::runtime_error(
                            "Unable to create extracted file: " +
                            destination.string());
                    }

                    if (!original_data.empty())
                    {
                        output_file.write(
                            reinterpret_cast<const char *>(
                                original_data.data()),
                            static_cast<std::streamsize>(
                                original_data.size()));
                    }

                    if (!output_file)
                    {
                        throw std::runtime_error(
                            "Failed writing extracted file: " +
                            destination.string());
                    }

                    std::cout
                        << "Extracted "
                        << archive_path_string
                        << " ("
                        << entry.original_size
                        << " bytes, CRC OK)\n";

                    solid_offset += size;
                    continue;
                }

                if (entry.compression !=
                        COMPRESSION_STORE &&
                    entry.compression !=
                        COMPRESSION_DEFLATE &&
                    entry.compression !=
                        COMPRESSION_RIPC)
                {
                    throw std::runtime_error(
                        "Unsupported compression method.");
                }

                if (entry.compression ==
                        COMPRESSION_STORE &&
                    entry.original_size !=
                        entry.compressed_size)
                {
                    throw std::runtime_error(
                        "Invalid stored file size for: " +
                        archive_path_string);
                }

                if (entry.compressed_size >
                    std::numeric_limits<std::size_t>::max())
                {
                    throw std::runtime_error(
                        "File is too large to extract: " +
                        archive_path_string);
                }

                archive.seekg(
                    static_cast<std::streamoff>(
                        entry.data_offset));

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to seek to file data: " +
                        archive_path_string);
                }

                std::vector<std::byte> data(
                    static_cast<std::size_t>(
                        entry.compressed_size));

                if (!data.empty())
                {
                    archive.read(
                        reinterpret_cast<char *>(
                            data.data()),
                        static_cast<std::streamsize>(
                            data.size()));
                }

                if (!archive)
                {
                    throw std::runtime_error(
                        "Unable to read file data: " +
                        archive_path_string);
                }

                std::vector<std::byte> original_data;

                if (entry.compression ==
                    COMPRESSION_STORE)
                {
                    original_data =
                        std::move(data);
                }
                else if (entry.compression ==
                         COMPRESSION_DEFLATE)
                {
                    original_data =
                        decompress_deflate(
                            data,
                            entry.original_size);
                }
                else if (entry.compression ==
                         COMPRESSION_RIPC)
                {
                    std::string ripc_error;

                    if (!rip::compression::decompress(
                            data,
                            original_data,
                            &ripc_error))
                    {
                        throw std::runtime_error(
                            "RIPC decompression failed for " +
                            archive_path_string +
                            ": " +
                            ripc_error);
                    }
                }
                else
                {
                    throw std::runtime_error(
                        "Unsupported compression method: " +
                        std::to_string(entry.compression));
                }

                if (original_data.size() !=
                    entry.original_size)
                {
                    throw std::runtime_error(
                        "Decompressed size mismatch: " +
                        archive_path_string);
                }

                const std::uint32_t actual_crc =
                    crc32(original_data);

                if (actual_crc != entry.crc32)
                {
                    throw std::runtime_error(
                        "CRC-32 verification failed: " +
                        archive_path_string);
                }

                std::ofstream output(
                    destination,
                    std::ios::binary |
                        std::ios::trunc);

                if (!output)
                {
                    throw std::runtime_error(
                        "Unable to create extracted file: " +
                        destination.string());
                }

                if (!original_data.empty())
                {
                    output.write(
                        reinterpret_cast<const char *>(
                            original_data.data()),
                        static_cast<std::streamsize>(
                            original_data.size()));
                }

                if (!output)
                {
                    throw std::runtime_error(
                        "Failed writing extracted file: " +
                        destination.string());
                }

                std::cout
                    << "Extracted "
                    << archive_path_string
                    << " ("
                    << entry.original_size
                    << " bytes, CRC OK)\n";
            }

            if ((info.header.flags &
                 ARCHIVE_FLAG_SOLID_RIPC) != 0 &&
                solid_offset != solid_original.size())
            {
                throw std::runtime_error(
                    "Solid RIPC stream contains trailing data.");
            }

            std::cout
                << '\n'
                << "Extracted "
                << entries.size()
                << " files to "
                << output_directory
                << '\n';

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr
                << "rip: "
                << e.what()
                << '\n';

            return false;
        }
    }

} // namespace rip