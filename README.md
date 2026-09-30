# RIP

<p align="center">
  <strong>RIP Archive Utility</strong><br>
  A lightweight custom archive format and archive manager built for Windows ARM64 and x64.
</p>

<p align="center">
  <a href="https://github.com/Cooper-Src/rip/actions/workflows/release.yml">
    <img src="https://github.com/Cooper-Src/rip/actions/workflows/release.yml/badge.svg" alt="Release build">
  </a>
  <a href="https://github.com/Cooper-Src/rip/releases">
    <img src="https://img.shields.io/github/v/release/Cooper-Src/rip?display_name=tag" alt="Latest release">
  </a>
  <a href="https://github.com/Cooper-Src/rip/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/Cooper-Src/rip" alt="License">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" alt="C++23">
  <img src="https://img.shields.io/badge/Platform-Windows%20ARM64%20%7C%20x64-0078D4?logo=windows&logoColor=white" alt="Windows ARM64 and x64">
</p>

RIP is a custom archive utility written in C++23. It provides both a command-line interface and a lightweight native Windows GUI for creating, inspecting, testing, and extracting `.rip` archives.

The project also includes **RIPC**, a custom compression codec designed specifically for RIP. Archive creation uses adaptive compression so each file can use STORE, DEFLATE, or RIPC depending on which representation is smaller.

## Features

- Custom `.rip` archive format
- C++23 implementation
- Native Windows ARM64 and x64 support
- Command-line interface
- WebView2-based Windows GUI
- Archive creation, listing, testing, inspection, and extraction
- CRC32 integrity validation
- STORE, DEFLATE, and RIPC compression methods
- Adaptive per-file compression selection
- Hierarchical archive browsing in the GUI
- Archive creation progress reporting
- Automated Windows ARM64 and x64 release packaging
- Automated release tests through GitHub Actions

## Download

Prebuilt Windows releases are published on the [Releases](https://github.com/Cooper-Src/rip/releases) page.

Each architecture-specific release package includes:

- `rip.exe` — command-line archive utility
- `rip-gui.exe` — graphical archive manager
- GUI assets required by `rip-gui.exe`

RIP supports **Windows on ARM64 and x64**.

## Command-line usage

### Create an archive

```powershell
rip create archive.rip C:\Path\To\Folder
```

or:

```powershell
rip create archive.rip C:\Path\To\file.txt
```

### List an archive

```powershell
rip list archive.rip
```

### Test an archive

```powershell
rip test archive.rip
```

This validates the archive structure, decompresses entries, and checks their CRC32 values.

### Extract an archive

```powershell
rip extract archive.rip C:\Output\Folder
```

### Show help

```powershell
rip --help
```

## GUI

Run:

```powershell
rip-gui.exe
```

The GUI provides a lightweight file-manager-style interface for working with RIP archives.

Current functionality includes:

- Browse the Windows filesystem
- Create RIP archives
- Open and browse RIP archives
- Extract archives
- Test archives
- View archive information
- Delete files and archives
- Show archive creation progress

## Building from source

### Requirements

- Windows 11
- Visual Studio 2026 Build Tools
- C++23 toolchain
- CMake 3.25 or newer
- Git

The GUI also uses the Microsoft WebView2 SDK. The CMake build downloads the required SDK automatically.

### ARM64

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A ARM64
cmake --build build --config Release --parallel
```

### x64

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
```

RIP also includes convenience build scripts under `scripts\` for ARM64 and x64 builds.

## Testing

The repository contains standalone tests for the compression and token systems.

Build the test targets with:

```powershell
cmake --build build --config Release --target rip-token-codec-test
cmake --build build --config Release --target rip-token-huffman-test
cmake --build build --config Release --target rip-compression-test
```

The release workflow also runs these tests automatically before publishing a release.

## RIPC

RIPC is RIP's custom compression codec.

It currently combines LZ-style matching with token encoding and Huffman entropy coding. RIP chooses RIPC only when it produces a smaller representation than the other available methods.

RIPC is still actively developed, so its format and implementation may evolve before a stable standalone format is declared.

## Archive format

RIP archives use a custom binary format with explicit serialization rather than relying on compiler struct layout.

The archive stores:

- Format version
- Archive flags
- Entry count
- Index location and size
- File offsets and sizes
- CRC32 checksums
- Compression method
- Archive paths

The format is intended to be simple, deterministic, and straightforward to validate.

## Releases

Tagged releases are built automatically by GitHub Actions.

Creating a release:

```powershell
git tag v1.0.2
git push origin v1.0.2
```

The release workflow:

1. Builds RIP natively for Windows ARM64 and x64.
2. Runs the compression and token tests on both architectures.
3. Packages the CLI, GUI, and GUI assets.
4. Creates a GitHub Release.
5. Uploads both architecture-specific ZIPs automatically.

x64 builds can also be produced directly from source using the x64 CMake configuration above.

## Project status

RIP is an actively developed project. The archive format, GUI, and RIPC codec are still evolving as new functionality and validation are added.

## Dependencies

RIP uses:

- [zlib-ng](https://github.com/zlib-ng/zlib-ng) for DEFLATE-compatible compression support.
- [Microsoft WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) for the Windows GUI.

Their respective licenses and terms apply to those third-party components.

## License

RIP is licensed under the [MIT License](LICENSE).
