#include <windows.h>

#include <shobjidl.h>
#include <shellapi.h>

#include <wrl.h>
#include <WebView2.h>

#include "rip/archive.hpp"
#include "rip/format.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace fs = std::filesystem;

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{

    constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"RIPArchiveUtilityWindow";

    constexpr wchar_t WEB_HOST_NAME[] =
        L"rip.local";

    constexpr UINT WM_RIP_ARCHIVE_PROGRESS =
        WM_APP + 10;

    constexpr UINT WM_RIP_ARCHIVE_COMPLETE =
        WM_APP + 11;

    std::atomic_bool g_archive_operation_active =
        false;

    struct ArchiveProgressMessage
    {
        std::uint64_t processed{};
        std::uint64_t total{};

        std::wstring file;
        std::wstring stage;

        int percent{};
    };

    struct ArchiveCompleteMessage
    {
        bool success{};

        std::wstring output;
    };

    struct DirectoryEntry
    {
        std::wstring name;
        std::wstring path;

        bool is_directory{};

        std::uint64_t size{};

        std::wstring modified;
        std::wstring created;

        std::wstring icon;
    };

    HWND g_window = nullptr;

    ComPtr<ICoreWebView2Environment> g_environment;
    ComPtr<ICoreWebView2Controller> g_controller;
    ComPtr<ICoreWebView2> g_webview;

    fs::path g_current_directory =
        L"C:\\";

    fs::path g_current_archive;

    fs::path g_selected_path;

    std::wstring g_requested_preset =
        L"strong";

    // -----------------------------------------------------------------------------
    // Basic helpers
    // -----------------------------------------------------------------------------

    std::wstring get_executable_directory()
    {
        std::wstring buffer(MAX_PATH, L'\0');

        for (;;)
        {
            const DWORD length =
                GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()));

            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size() - 1)
            {
                buffer.resize(length);

                return fs::path(buffer)
                    .parent_path()
                    .wstring();
            }

            buffer.resize(
                buffer.size() * 2);
        }
    }

    std::wstring get_web_root()
    {
        return fs::path(
                   get_executable_directory()) /
               L"gui";
    }

    std::wstring get_webview_user_data()
    {
        wchar_t buffer[4096]{};

        const DWORD length =
            GetEnvironmentVariableW(
                L"LOCALAPPDATA",
                buffer,
                static_cast<DWORD>(
                    std::size(buffer)));

        if (length == 0 ||
            length >= std::size(buffer))
        {
            return (fs::temp_directory_path() /
                    L"RIP-WebView2")
                .wstring();
        }

        return (fs::path(buffer) /
                L"RIP" /
                L"WebView2")
            .wstring();
    }

    std::wstring json_escape(
        const std::wstring &value)
    {
        std::wstring result;

        result.reserve(
            value.size() + 16);

        for (const wchar_t character : value)
        {
            switch (character)
            {
            case L'\\':
                result += L"\\\\";
                break;

            case L'"':
                result += L"\\\"";
                break;

            case L'\b':
                result += L"\\b";
                break;

            case L'\f':
                result += L"\\f";
                break;

            case L'\n':
                result += L"\\n";
                break;

            case L'\r':
                result += L"\\r";
                break;

            case L'\t':
                result += L"\\t";
                break;

            default:
                if (character < 0x20)
                {
                    wchar_t buffer[8]{};

                    swprintf_s(
                        buffer,
                        L"\\u%04x",
                        static_cast<unsigned>(
                            character));

                    result += buffer;
                }
                else
                {
                    result += character;
                }

                break;
            }
        }

        return result;
    }

    std::wstring file_time_to_date(
        const FILETIME &file_time)
    {
        FILETIME local_time{};

        if (!FileTimeToLocalFileTime(
                &file_time,
                &local_time))
        {
            return {};
        }

        SYSTEMTIME system_time{};

        if (!FileTimeToSystemTime(
                &local_time,
                &system_time))
        {
            return {};
        }

        wchar_t buffer[32]{};

        swprintf_s(
            buffer,
            L"%04u-%02u-%02u",
            system_time.wYear,
            system_time.wMonth,
            system_time.wDay);

        return buffer;
    }

    std::wstring get_extension(
        const std::wstring &name)
    {
        const auto position =
            name.find_last_of(L'.');

        if (position == std::wstring::npos)
        {
            return {};
        }

        std::wstring extension =
            name.substr(position);

        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](wchar_t value)
            {
                return static_cast<wchar_t>(
                    towlower(value));
            });

        return extension;
    }

    std::wstring icon_for_file(
        const std::wstring &name)
    {
        const std::wstring extension =
            get_extension(name);

        if (extension == L".md" ||
            extension == L".markdown")
        {
            return L"markdown";
        }

        if (extension == L".mp4" ||
            extension == L".mkv" ||
            extension == L".avi")
        {
            return L"vlc";
        }

        if (extension == L".html" ||
            extension == L".htm")
        {
            return L"browser";
        }

        if (extension == L".exe" ||
            extension == L".dll")
        {
            return L"exe";
        }

        if (extension == L".iso" ||
            extension == L".img")
        {
            return L"disc";
        }

        if (extension == L".cpp" ||
            extension == L".hpp" ||
            extension == L".c" ||
            extension == L".h" ||
            extension == L".js" ||
            extension == L".ts" ||
            extension == L".tsx" ||
            extension == L".jsx" ||
            extension == L".py" ||
            extension == L".json" ||
            extension == L".css")
        {
            return L"vscode";
        }

        return L"disc";
    }

    bool get_file_dates(
        const fs::path &path,
        std::wstring &modified,
        std::wstring &created)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};

        if (!GetFileAttributesExW(
                path.c_str(),
                GetFileExInfoStandard,
                &data))
        {
            return false;
        }

        modified =
            file_time_to_date(
                data.ftLastWriteTime);

        created =
            file_time_to_date(
                data.ftCreationTime);

        return true;
    }

    void send_web_message(
        const std::wstring &message)
    {
        if (!g_webview)
        {
            return;
        }

        g_webview->PostWebMessageAsString(
            message.c_str());
    }

    void send_status(
        const std::wstring &text)
    {
        send_web_message(
            L"{\"type\":\"status\",\"text\":\"" +
            json_escape(text) +
            L"\"}");
    }

    void send_error(
        const std::wstring &text)
    {
        send_web_message(
            L"{\"type\":\"error\",\"text\":\"" +
            json_escape(text) +
            L"\"}");
    }

    void set_window_title(
        const std::wstring &text)
    {
        std::wstring title =
            L"RIP - " + text;

        SetWindowTextW(
            g_window,
            title.c_str());
    }

    // -----------------------------------------------------------------------------
    // Directory enumeration
    // -----------------------------------------------------------------------------

    std::vector<DirectoryEntry> enumerate_directory(
        const fs::path &directory)
    {
        std::vector<DirectoryEntry> entries;

        std::error_code error;

        fs::directory_iterator iterator(
            directory,
            fs::directory_options::skip_permission_denied,
            error);

        const fs::directory_iterator end{};

        while (!error &&
               iterator != end)
        {
            const fs::directory_entry &entry =
                *iterator;

            DirectoryEntry output{};

            output.name =
                entry.path().filename().wstring();

            output.path =
                entry.path().wstring();

            output.is_directory =
                entry.is_directory(error);

            error.clear();

            if (!output.is_directory)
            {
                const auto file_size =
                    entry.file_size(error);

                if (!error)
                {
                    output.size =
                        file_size;
                }

                error.clear();
            }

            get_file_dates(
                entry.path(),
                output.modified,
                output.created);

            output.icon =
                output.is_directory
                    ? L"folder"
                    : icon_for_file(
                          output.name);

            entries.push_back(
                std::move(output));

            iterator.increment(error);
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const DirectoryEntry &a,
               const DirectoryEntry &b)
            {
                if (a.is_directory !=
                    b.is_directory)
                {
                    return a.is_directory >
                           b.is_directory;
                }

                return _wcsicmp(
                           a.name.c_str(),
                           b.name.c_str()) < 0;
            });

        return entries;
    }

    void send_directory()
    {
        const auto entries =
            enumerate_directory(
                g_current_directory);

        std::wstring json =
            L"{\"type\":\"directory\","
            L"\"path\":\"" +
            json_escape(
                g_current_directory.wstring()) +
            L"\",\"entries\":[";

        for (std::size_t index = 0;
             index < entries.size();
             ++index)
        {
            if (index != 0)
            {
                json += L",";
            }

            const DirectoryEntry &entry =
                entries[index];

            json +=
                L"{\"name\":\"" +
                json_escape(entry.name) +
                L"\",\"path\":\"" +
                json_escape(entry.path) +
                L"\",\"kind\":\"" +
                std::wstring(
                    entry.is_directory
                        ? L"folder"
                        : L"file") +
                L"\",\"icon\":\"" +
                json_escape(entry.icon) +
                L"\"";

            if (entry.is_directory)
            {
                json += L",\"size\":null";
            }
            else
            {
                json +=
                    L",\"size\":" +
                    std::to_wstring(
                        entry.size);
            }

            json +=
                L",\"modified\":\"" +
                json_escape(entry.modified) +
                L"\",\"created\":\"" +
                json_escape(entry.created) +
                L"\"}";
        }

        json += L"]}";

        send_web_message(json);

        set_window_title(
            g_current_directory.wstring());

        send_status(
            L"Ready");
    }

    // -----------------------------------------------------------------------------
    // Archive enumeration
    // -----------------------------------------------------------------------------

    const wchar_t *compression_name(
        std::uint8_t method)
    {
        switch (method)
        {
        case rip::COMPRESSION_STORE:
            return L"STORE";

        case rip::COMPRESSION_DEFLATE:
            return L"DEFLATE";

        default:
            return L"UNKNOWN";
        }
    }

    void send_archive()
    {
        rip::ArchiveDetails details{};

        if (!rip::Archive::inspect(
                g_current_archive,
                details))
        {
            send_error(
                L"Unable to open RIP archive.");

            return;
        }

        std::wstring json =
            L"{\"type\":\"archive\","
            L"\"path\":\"" +
            json_escape(
                g_current_archive.wstring()) +
            L"\",\"major\":" +
            std::to_wstring(
                details.major_version) +
            L",\"minor\":" +
            std::to_wstring(
                details.minor_version) +
            L",\"fileSize\":" +
            std::to_wstring(
                details.file_size) +
            L",\"entries\":[";

        for (std::size_t index = 0;
             index < details.entries.size();
             ++index)
        {
            if (index != 0)
            {
                json += L",";
            }

            const auto &entry =
                details.entries[index];

            json +=
                L"{\"name\":\"" +
                json_escape(
                    std::wstring(
                        entry.path.begin(),
                        entry.path.end())) +
                L"\",\"originalSize\":" +
                std::to_wstring(
                    entry.original_size) +
                L",\"compressedSize\":" +
                std::to_wstring(
                    entry.compressed_size) +
                L",\"method\":\"" +
                compression_name(
                    entry.compression) +
                L"\"}";
        }

        json += L"]}";

        send_web_message(json);

        set_window_title(
            g_current_archive.filename().wstring());

        send_status(
            L"Archive opened");
    }

    // -----------------------------------------------------------------------------
    // File dialogs
    // -----------------------------------------------------------------------------

    bool pick_save_archive(
        fs::path &output)
    {
        ComPtr<IFileSaveDialog> dialog;

        HRESULT result =
            CoCreateInstance(
                CLSID_FileSaveDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog));

        if (FAILED(result))
        {
            return false;
        }

        COMDLG_FILTERSPEC filters[] =
            {
                {L"RIP archives",
                 L"*.rip"},
                {L"All files",
                 L"*.*"}};

        dialog->SetFileTypes(
            2,
            filters);

        dialog->SetDefaultExtension(
            L"rip");

        std::wstring default_name =
            L"archive.rip";

        if (!g_selected_path.empty())
        {
            default_name =
                g_selected_path.filename().wstring();

            const fs::path filename(
                default_name);

            if (filename.extension().empty())
            {
                default_name +=
                    L".rip";
            }
            else
            {
                default_name =
                    filename.stem().wstring() +
                    L".rip";
            }
        }

        dialog->SetFileName(
            default_name.c_str());

        dialog->SetOptions(
            FOS_FORCEFILESYSTEM |
            FOS_OVERWRITEPROMPT |
            FOS_PATHMUSTEXIST);

        result =
            dialog->Show(g_window);

        if (FAILED(result))
        {
            return false;
        }

        ComPtr<IShellItem> item;

        result =
            dialog->GetResult(
                &item);

        if (FAILED(result))
        {
            return false;
        }

        PWSTR path = nullptr;

        result =
            item->GetDisplayName(
                SIGDN_FILESYSPATH,
                &path);

        if (FAILED(result) ||
            path == nullptr)
        {
            return false;
        }

        output = path;

        CoTaskMemFree(path);

        return true;
    }

    bool pick_folder(
        fs::path &output)
    {
        ComPtr<IFileOpenDialog> dialog;

        HRESULT result =
            CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog));

        if (FAILED(result))
        {
            return false;
        }

        DWORD options = 0;

        dialog->GetOptions(
            &options);

        dialog->SetOptions(
            options |
            FOS_PICKFOLDERS |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST);

        result =
            dialog->Show(g_window);

        if (FAILED(result))
        {
            return false;
        }

        ComPtr<IShellItem> item;

        result =
            dialog->GetResult(
                &item);

        if (FAILED(result))
        {
            return false;
        }

        PWSTR path = nullptr;

        result =
            item->GetDisplayName(
                SIGDN_FILESYSPATH,
                &path);

        if (FAILED(result) ||
            path == nullptr)
        {
            return false;
        }

        output = path;

        CoTaskMemFree(path);

        return true;
    }

    fs::path get_active_archive()
    {
        if (!g_current_archive.empty())
        {
            return g_current_archive;
        }

        if (g_selected_path.empty())
        {
            return {};
        }

        std::wstring extension =
            g_selected_path.extension().wstring();

        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](wchar_t value)
            {
                return static_cast<wchar_t>(
                    towlower(value));
            });

        if (extension != L".rip")
        {
            return {};
        }

        std::error_code error;

        if (!fs::is_regular_file(
                g_selected_path,
                error))
        {
            return {};
        }

        return g_selected_path;
    }

    // -----------------------------------------------------------------------------
    // Actions
    // -----------------------------------------------------------------------------

    void post_archive_progress(
        std::uint64_t processed,
        std::uint64_t total,
        const fs::path &file,
        rip::ArchiveProgressStage stage)
    {
        if (!g_window)
        {
            return;
        }

        int percent = 0;

        if (stage ==
            rip::ArchiveProgressStage::Preparing)
        {
            percent = 0;
        }
        else if (stage ==
                 rip::ArchiveProgressStage::Compressing)
        {
            if (total > 0)
            {
                percent =
                    5 +
                    static_cast<int>(
                        (processed * 70) /
                        total);
            }
        }
        else if (stage ==
                 rip::ArchiveProgressStage::Writing)
        {
            if (total > 0)
            {
                percent =
                    75 +
                    static_cast<int>(
                        (processed * 20) /
                        total);
            }
        }
        else if (stage ==
                 rip::ArchiveProgressStage::Finalizing)
        {
            percent = 98;
        }

        ArchiveProgressMessage *message =
            new ArchiveProgressMessage;

        message->processed =
            processed;

        message->total =
            total;

        message->file =
            file.wstring();

        message->percent =
            std::clamp(
                percent,
                0,
                99);

        switch (stage)
        {
        case rip::ArchiveProgressStage::Preparing:
            message->stage =
                L"Preparing archive";
            break;

        case rip::ArchiveProgressStage::Compressing:
            message->stage =
                L"Compressing";
            break;

        case rip::ArchiveProgressStage::Writing:
            message->stage =
                L"Writing archive";
            break;

        case rip::ArchiveProgressStage::Finalizing:
            message->stage =
                L"Finalizing";
            break;
        }

        if (!PostMessageW(
                g_window,
                WM_RIP_ARCHIVE_PROGRESS,
                0,
                reinterpret_cast<LPARAM>(
                    message)))
        {
            delete message;
        }
    }

    void create_archive()
    {
        if (g_archive_operation_active.exchange(true))
        {
            send_error(
                L"An archive operation is already running.");

            return;
        }

        if (g_selected_path.empty())
        {
            g_archive_operation_active = false;

            send_error(
                L"No source selected.");

            return;
        }

        const fs::path source =
            g_selected_path;

        std::error_code error;

        if (!fs::exists(source, error) || error)
        {
            g_archive_operation_active = false;

            send_error(
                L"The selected source no longer exists.");

            return;
        }

        fs::path output =
            source.parent_path() /
            (source.stem().wstring() +
             L".rip");

        if (fs::exists(output, error))
        {
            if (error)
            {
                g_archive_operation_active = false;

                send_error(
                    L"Unable to check the archive destination.");

                return;
            }

            const std::wstring base_name =
                source.stem().wstring();

            bool found_name = false;

            for (unsigned int index = 1;
                 index < 1'000'000;
                 ++index)
            {
                output =
                    source.parent_path() /
                    (base_name +
                     L" (" +
                     std::to_wstring(index) +
                     L").rip");

                if (!fs::exists(output, error))
                {
                    if (error)
                    {
                        g_archive_operation_active = false;

                        send_error(
                            L"Unable to check the archive destination.");

                        return;
                    }

                    found_name = true;
                    break;
                }
            }

            if (!found_name)
            {
                g_archive_operation_active = false;

                send_error(
                    L"Unable to find an available archive filename.");

                return;
            }
        }

        const fs::path final_output =
            output;

        post_archive_progress(
            0,
            0,
            source,
            rip::ArchiveProgressStage::Preparing);

        std::thread(
            [source, final_output]()
            {
                const bool success =
                    rip::Archive::create(
                        final_output,
                        source,
                        [](
                            std::uint64_t processed,
                            std::uint64_t total,
                            const fs::path &file,
                            rip::ArchiveProgressStage stage,
                            std::uint8_t)
                        {
                            post_archive_progress(
                                processed,
                                total,
                                file,
                                stage);
                        });

                if (!g_window)
                {
                    g_archive_operation_active = false;
                    return;
                }

                auto *result =
                    new ArchiveCompleteMessage;

                result->success =
                    success;

                result->output =
                    final_output.wstring();

                if (!PostMessageW(
                        g_window,
                        WM_RIP_ARCHIVE_COMPLETE,
                        0,
                        reinterpret_cast<LPARAM>(
                            result)))
                {
                    delete result;

                    g_archive_operation_active = false;
                }
            })
            .detach();
    }

    void extract_archive()
    {
        const fs::path archive =
            get_active_archive();

        if (archive.empty())
        {
            send_error(
                L"Select or open a RIP archive first.");

            return;
        }

        fs::path output;

        if (!pick_folder(output))
        {
            return;
        }

        send_status(
            L"Extracting archive...");

        if (!rip::Archive::extract(
                archive,
                output))
        {
            send_error(
                L"Archive extraction failed.");

            return;
        }

        send_status(
            L"Extraction complete");
    }

    void test_archive()
    {
        const fs::path archive =
            get_active_archive();

        if (archive.empty())
        {
            send_error(
                L"Select or open a RIP archive first.");

            return;
        }

        send_status(
            L"Testing archive...");

        if (!rip::Archive::test(
                archive))
        {
            MessageBoxW(
                g_window,
                L"The archive test failed.",
                L"RIP",
                MB_OK |
                    MB_ICONERROR);

            send_error(
                L"Archive test failed.");

            return;
        }

        MessageBoxW(
            g_window,
            L"The archive is valid.",
            L"RIP Archive Test",
            MB_OK |
                MB_ICONINFORMATION);

        send_status(
            L"Archive test passed");
    }

    void show_archive_info()
    {
        const fs::path archive =
            get_active_archive();

        if (archive.empty())
        {
            send_error(
                L"Select or open a RIP archive first.");

            return;
        }

        rip::ArchiveDetails details{};

        if (!rip::Archive::inspect(
                archive,
                details))
        {
            send_error(
                L"Unable to inspect archive.");

            return;
        }

        std::uint64_t original_total = 0;
        std::uint64_t compressed_total = 0;

        std::uint64_t deflate_count = 0;
        std::uint64_t stored_count = 0;

        for (const auto &entry :
             details.entries)
        {
            original_total +=
                entry.original_size;

            compressed_total +=
                entry.compressed_size;

            if (entry.compression ==
                rip::COMPRESSION_DEFLATE)
            {
                ++deflate_count;
            }
            else if (entry.compression ==
                     rip::COMPRESSION_STORE)
            {
                ++stored_count;
            }
        }

        std::wstringstream message;

        message
            << L"RIP Archive\n\n"
            << L"Archive: "
            << archive.filename().wstring()
            << L"\n"
            << L"Version: "
            << static_cast<unsigned>(
                   details.major_version)
            << L"."
            << static_cast<unsigned>(
                   details.minor_version)
            << L"\n"
            << L"Files: "
            << details.entries.size()
            << L"\n"
            << L"Archive size: "
            << details.file_size
            << L" bytes\n"
            << L"\n"
            << L"Original data: "
            << original_total
            << L" bytes\n"
            << L"Packed data: "
            << compressed_total
            << L" bytes\n"
            << L"\n"
            << L"DEFLATE: "
            << deflate_count
            << L"\n"
            << L"STORE: "
            << stored_count;

        MessageBoxW(
            g_window,
            message.str().c_str(),
            L"RIP Archive Information",
            MB_OK |
                MB_ICONINFORMATION);
    }

    void delete_selected()
    {
        if (g_current_archive.empty())
        {
            if (g_selected_path.empty())
            {
                send_error(
                    L"Select a file or folder first.");

                return;
            }

            if (g_selected_path ==
                g_current_directory)
            {
                send_error(
                    L"You cannot delete the current directory.");

                return;
            }

            const int answer =
                MessageBoxW(
                    g_window,
                    (
                        L"Delete this item?\n\n" +
                        g_selected_path.wstring())
                        .c_str(),
                    L"Confirm Delete",
                    MB_YESNO |
                        MB_ICONWARNING);

            if (answer != IDYES)
            {
                return;
            }

            std::error_code error;

            if (fs::is_directory(
                    g_selected_path,
                    error))
            {
                fs::remove_all(
                    g_selected_path,
                    error);
            }
            else
            {
                fs::remove(
                    g_selected_path,
                    error);
            }

            if (error)
            {
                send_error(
                    L"Unable to delete the selected item.");

                return;
            }

            g_selected_path.clear();

            send_directory();

            send_status(
                L"Item deleted");

            return;
        }

        send_error(
            L"Archive entry deletion will be added after archive rewrite support is implemented.");
    }

    void open_directory(
        const fs::path &path)
    {
        std::error_code error;

        const fs::path normalized =
            fs::absolute(
                path,
                error);

        if (error ||
            !fs::is_directory(
                normalized,
                error))
        {
            send_error(
                L"Unable to open directory.");

            return;
        }

        g_current_directory =
            normalized.lexically_normal();

        g_current_archive.clear();
        g_selected_path.clear();

        send_directory();
    }

    void open_archive(
        const fs::path &path)
    {
        rip::ArchiveDetails details{};

        if (!rip::Archive::inspect(
                path,
                details))
        {
            send_error(
                L"Unable to open RIP archive.");

            return;
        }

        g_current_archive =
            path;

        g_selected_path.clear();

        send_archive();
    }

    void go_up()
    {
        if (!g_current_archive.empty())
        {
            const fs::path parent =
                g_current_archive.parent_path();

            if (!parent.empty())
            {
                open_directory(parent);
            }

            return;
        }

        const fs::path parent =
            g_current_directory.parent_path();

        if (parent.empty() ||
            parent == g_current_directory)
        {
            return;
        }

        open_directory(parent);
    }

    void shell_open(
        const fs::path &path)
    {
        ShellExecuteW(
            g_window,
            L"open",
            path.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
    }

    // -----------------------------------------------------------------------------
    // WebView messages
    // -----------------------------------------------------------------------------

    void handle_web_message(
        const std::wstring &message)
    {
        const std::size_t separator =
            message.find(L'\t');

        const std::wstring command =
            separator == std::wstring::npos
                ? message
                : message.substr(
                      0,
                      separator);

        const std::wstring argument =
            separator == std::wstring::npos
                ? std::wstring{}
                : message.substr(
                      separator + 1);

        if (command == L"ready")
        {
            send_directory();
            return;
        }

        if (command == L"directory")
        {
            open_directory(argument);
            return;
        }

        if (command == L"archive")
        {
            open_archive(argument);
            return;
        }

        if (command == L"select")
        {
            g_selected_path = argument;
            return;
        }

        if (command == L"open-file")
        {
            if (!argument.empty())
            {
                shell_open(argument);
            }

            return;
        }

        if (command == L"up")
        {
            go_up();
            return;
        }

        if (command == L"preset")
        {
            if (!argument.empty())
            {
                g_requested_preset =
                    argument;
            }

            return;
        }

        if (command == L"action")
        {
            if (argument == L"add")
            {
                if (g_current_archive.empty())
                {
                    create_archive();
                }
                else
                {
                    send_error(
                        L"Adding files to an existing archive is the next archive-engine feature.");
                }

                return;
            }

            if (argument == L"extract")
            {
                extract_archive();
                return;
            }

            if (argument == L"test")
            {
                test_archive();
                return;
            }

            if (argument == L"info")
            {
                show_archive_info();
                return;
            }

            if (argument == L"delete")
            {
                delete_selected();
                return;
            }

            return;
        }
    }

    // -----------------------------------------------------------------------------
    // WebView initialization
    // -----------------------------------------------------------------------------

    HRESULT initialize_webview()
    {
        const std::wstring user_data =
            get_webview_user_data();

        fs::create_directories(
            user_data);

        return CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            user_data.c_str(),
            nullptr,
            Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT result,
                   ICoreWebView2Environment *environment)
                    -> HRESULT
                {
                    if (FAILED(result))
                    {
                        MessageBoxW(
                            g_window,
                            L"Unable to initialize WebView2.\n\n"
                            L"Make sure the Microsoft Edge WebView2 Runtime "
                            L"is installed.",
                            L"RIP",
                            MB_OK |
                                MB_ICONERROR);

                        return result;
                    }

                    g_environment =
                        environment;

                    return g_environment
                        ->CreateCoreWebView2Controller(
                            g_window,
                            Callback<
                                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [](HRESULT result,
                                   ICoreWebView2Controller *controller)
                                    -> HRESULT
                                {
                                    if (FAILED(result))
                                    {
                                        return result;
                                    }

                                    g_controller =
                                        controller;

                                    g_controller
                                        ->get_CoreWebView2(
                                            &g_webview);

                                    // ---------------------------------------------------------------------
                                    // High-DPI WebView2 configuration
                                    // ---------------------------------------------------------------------

                                    ComPtr<ICoreWebView2Controller3>
                                        controller3;

                                    if (SUCCEEDED(
                                            g_controller.As(
                                                &controller3)))
                                    {
                                        // Bounds are supplied in physical pixels because our
                                        // Win32 window is per-monitor-DPI-aware.
                                        controller3
                                            ->put_BoundsMode(
                                                COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);

                                        // Let WebView2 automatically track the monitor DPI.
                                        controller3
                                            ->put_ShouldDetectMonitorScaleChanges(
                                                TRUE);
                                    }

                                    g_controller
                                        ->put_ZoomFactor(
                                            1.0);

                                    RECT bounds{};

                                    GetClientRect(
                                        g_window,
                                        &bounds);

                                    g_controller
                                        ->put_Bounds(
                                            bounds);

                                    g_controller
                                        ->put_IsVisible(
                                            TRUE);

                                    ComPtr<ICoreWebView2_3>
                                        webview3;

                                    if (SUCCEEDED(
                                            g_webview.As(
                                                &webview3)))
                                    {
                                        const std::wstring web_root =
                                            get_web_root();

                                        webview3
                                            ->SetVirtualHostNameToFolderMapping(
                                                WEB_HOST_NAME,
                                                web_root.c_str(),
                                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                    }

                                    g_webview
                                        ->add_WebMessageReceived(
                                            Callback<
                                                ICoreWebView2WebMessageReceivedEventHandler>(
                                                [](ICoreWebView2 *,
                                                   ICoreWebView2WebMessageReceivedEventArgs *args)
                                                    -> HRESULT
                                                {
                                                    LPWSTR raw_message =
                                                        nullptr;

                                                    if (SUCCEEDED(
                                                            args
                                                                ->TryGetWebMessageAsString(
                                                                    &raw_message)))
                                                    {
                                                        const std::wstring message =
                                                            raw_message
                                                                ? raw_message
                                                                : L"";

                                                        CoTaskMemFree(
                                                            raw_message);

                                                        handle_web_message(
                                                            message);
                                                    }

                                                    return S_OK;
                                                })
                                                .Get(),
                                            nullptr);

                                    g_webview
                                        ->Navigate(
                                            L"https://rip.local/index.html?native=1");

                                    return S_OK;
                                })
                                .Get());
                })
                .Get());
    }

    // -----------------------------------------------------------------------------
    // Window procedure
    // -----------------------------------------------------------------------------

    LRESULT CALLBACK window_proc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam)
    {
        switch (message)
        {
        case WM_RIP_ARCHIVE_PROGRESS:
        {
            auto *progress =
                reinterpret_cast<ArchiveProgressMessage *>(
                    lparam);

            if (progress)
            {
                std::wstring json =
                    L"{\"type\":\"archiveProgress\","
                    L"\"processed\":" +
                    std::to_wstring(
                        progress->processed) +
                    L",\"total\":" +
                    std::to_wstring(
                        progress->total) +
                    L",\"percent\":" +
                    std::to_wstring(
                        progress->percent) +
                    L",\"file\":\"" +
                    json_escape(
                        progress->file) +
                    L"\",\"stage\":\"" +
                    json_escape(
                        progress->stage) +
                    L"\"}";

                send_web_message(
                    json);

                delete progress;
            }

            return 0;
        }

        case WM_RIP_ARCHIVE_COMPLETE:
        {
            auto *result =
                reinterpret_cast<ArchiveCompleteMessage *>(
                    lparam);

            if (result)
            {
                g_archive_operation_active =
                    false;

                if (result->success)
                {
                    g_current_archive =
                        result->output;

                    g_selected_path.clear();

                    send_web_message(
                        L"{\"type\":\"archiveComplete\","
                        L"\"success\":true}");

                    send_archive();
                }
                else
                {
                    send_web_message(
                        L"{\"type\":\"archiveComplete\","
                        L"\"success\":false}");

                    send_error(
                        L"Failed to create the archive.");
                }

                delete result;
            }

            return 0;
        }

        case WM_SIZE:
        {
            if (g_controller)
            {
                RECT bounds{};

                GetClientRect(
                    hwnd,
                    &bounds);

                g_controller
                    ->put_Bounds(
                        bounds);
            }

            return 0;
        }

        case WM_DPICHANGED:
        {
            if (g_controller)
            {
                RECT bounds{};

                GetClientRect(
                    hwnd,
                    &bounds);

                g_controller
                    ->put_Bounds(
                        bounds);

                ComPtr<ICoreWebView2Controller3>
                    controller3;

                if (SUCCEEDED(
                        g_controller.As(
                            &controller3)))
                {
                    controller3
                        ->put_ShouldDetectMonitorScaleChanges(
                            TRUE);
                }
            }

            return 0;
        }

        case WM_DESTROY:
            g_webview.Reset();
            g_controller.Reset();
            g_environment.Reset();
            g_window = nullptr;

            PostQuitMessage(0);

            return 0;

        default:
            return DefWindowProcW(
                hwnd,
                message,
                wparam,
                lparam);
        }
    }

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command)
{
    // Tell Windows that RIP handles high-DPI scaling itself.
    // This must happen before any HWND is created.
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT result =
        CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED);

    if (FAILED(result))
    {
        MessageBoxW(
            nullptr,
            L"Unable to initialize COM.",
            L"RIP",
            MB_OK |
                MB_ICONERROR);

        return 1;
    }

    WNDCLASSEXW window_class{};

    window_class.cbSize =
        sizeof(window_class);

    window_class.hInstance =
        instance;

    window_class.lpfnWndProc =
        window_proc;

    window_class.lpszClassName =
        WINDOW_CLASS_NAME;

    window_class.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);

    window_class.hIcon =
        LoadIconW(
            nullptr,
            IDI_APPLICATION);

    window_class.hIconSm =
        window_class.hIcon;

    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1);

    if (!RegisterClassExW(
            &window_class))
    {
        CoUninitialize();
        return 1;
    }

    g_window =
        CreateWindowExW(
            0,
            WINDOW_CLASS_NAME,
            L"RIP - C:\\",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            820,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (!g_window)
    {
        CoUninitialize();
        return 1;
    }

    ShowWindow(
        g_window,
        show_command);

    UpdateWindow(
        g_window);

    result =
        initialize_webview();

    if (FAILED(result))
    {
        MessageBoxW(
            g_window,
            L"Failed to initialize WebView2.",
            L"RIP",
            MB_OK |
                MB_ICONERROR);

        DestroyWindow(
            g_window);

        CoUninitialize();

        return 1;
    }

    MSG message{};

    while (
        GetMessageW(
            &message,
            nullptr,
            0,
            0) > 0)
    {
        TranslateMessage(
            &message);

        DispatchMessageW(
            &message);
    }

    CoUninitialize();

    return static_cast<int>(
        message.wParam);
}