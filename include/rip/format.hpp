#pragma once

#include <cstdint>

namespace rip
{
    // RIP archive format
    constexpr std::uint8_t RIP_FORMAT_MAJOR = 1;
    constexpr std::uint8_t RIP_FORMAT_MINOR = 1;

    // "RIP\x01"
    constexpr char RIP_MAGIC[4] = {'R', 'I', 'P', 1};

    // Archive flags

    enum ArchiveFlags : std::uint32_t
    {
        ARCHIVE_FLAG_NONE = 0,
        ARCHIVE_FLAG_SOLID_RIPC = 1u << 0
    };

    // Compression methods

    enum CompressionMethod : std::uint8_t
{
    COMPRESSION_STORE = 0,
    COMPRESSION_DEFLATE = 1,
    COMPRESSION_RIPC = 2
};

    // -----------------------------------------------------------------------------
    // Logical format structures
    //
    // These are NOT written directly to disk.
    //
    // All fields are serialized explicitly by archive.cpp as little-endian
    // integers. This makes the on-disk format independent of compiler struct
    // layout and packing.
    // -----------------------------------------------------------------------------

    struct ArchiveHeader
    {
        std::uint8_t major_version;
        std::uint8_t minor_version;

        std::uint16_t header_size;

        std::uint32_t flags;

        std::uint64_t entry_count;
        std::uint64_t index_offset;
        std::uint64_t index_size;

        // RIP 1.1 solid-RIPC extension.
        // These fields are zero for ordinary archives.
        std::uint64_t solid_data_offset;
        std::uint64_t solid_compressed_size;
    };

    struct FileEntry
    {
        std::uint64_t data_offset;
        std::uint64_t original_size;
        std::uint64_t compressed_size;

        std::uint32_t crc32;

        std::uint8_t compression;

        std::uint32_t path_size;
    };

    // Explicit serialized sizes.

    // Base RIP 1.0/1.1 header size.
    constexpr std::uint64_t ARCHIVE_HEADER_SIZE = 36;

    // RIP 1.1 solid-RIPC header size.
    constexpr std::uint64_t SOLID_RIPC_HEADER_SIZE = 52;

    constexpr std::uint64_t FILE_ENTRY_SIZE = 33;

} // namespace rip