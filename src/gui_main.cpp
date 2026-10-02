#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineView>

#include "rip/archive.hpp"
#include "rip/format.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cwctype>

namespace fs = std::filesystem;

namespace
{

    std::atomic_bool g_archive_operation_active = false;

    class RipWebPage final : public QWebEnginePage
    {
    public:
        explicit RipWebPage(
            QWebEngineProfile *profile,
            QObject *parent = nullptr)
            : QWebEnginePage(
                  profile,
                  parent)
        {
        }

    protected:
        void javaScriptConsoleMessage(
            JavaScriptConsoleMessageLevel level,
            const QString &message,
            int lineNumber,
            const QString &sourceID) override
        {
            const char *label = "LOG";

            switch (level)
            {
            case InfoMessageLevel:
                label = "INFO";
                break;

            case WarningMessageLevel:
                label = "WARN";
                break;

            case ErrorMessageLevel:
                label = "ERROR";
                break;
            }

            qWarning().noquote()
                << "[RIP WebEngine]"
                << label
                << sourceID
                << lineNumber
                << message;

            QWebEnginePage::javaScriptConsoleMessage(
                level,
                message,
                lineNumber,
                sourceID);
        }
    };

    class RipBridge final : public QObject
    {
        Q_OBJECT

    public slots:
        void postMessage(
            const QString &message)
        {
            emit messageFromWeb(message);
        }

    signals:
        void messageFromWeb(
            const QString &message);

        void messageReceived(
            const QString &message);

    public:
        void sendToWeb(
            const QString &message)
        {
            emit messageReceived(message);
        }
    };

    class RipSchemeHandler final :
        public QWebEngineUrlSchemeHandler
    {
    public:
        explicit RipSchemeHandler(
            const QString &root,
            QObject *parent = nullptr)
            : QWebEngineUrlSchemeHandler(parent),
              m_root(QDir::cleanPath(root))
        {
        }

    protected:
        void requestStarted(
            QWebEngineUrlRequestJob *job) override
        {
            qDebug().noquote()
                << "[RIP WebEngine] request:"
                << job->requestUrl();

            QString relative =
                QUrl::fromPercentEncoding(
                    job->requestUrl()
                        .path()
                        .toUtf8());

            while (relative.startsWith('/'))
            {
                relative.remove(0, 1);
            }

            if (relative.isEmpty())
            {
                relative =
                    QStringLiteral("index.html");
            }

            const QString candidate =
                QDir::cleanPath(
                    QDir(m_root).filePath(relative));

            const QString canonicalRoot =
                QFileInfo(m_root)
                    .canonicalFilePath();

            const QString canonicalFile =
                QFileInfo(candidate)
                    .canonicalFilePath();

            qDebug().noquote()
                << "[RIP WebEngine] root:"
                << canonicalRoot
                << "file:"
                << canonicalFile;

            if (canonicalRoot.isEmpty() ||
                canonicalFile.isEmpty() ||
                ((!canonicalFile.startsWith(
                    canonicalRoot +
                    QDir::separator())) &&
                 canonicalFile != canonicalRoot))
            {
                qWarning().noquote()
                    << "[RIP WebEngine] rejected path:"
                    << candidate;

                job->fail(
                    QWebEngineUrlRequestJob::UrlNotFound);
                return;
            }

            auto *file =
                new QFile(canonicalFile);

            if (!file->open(
                    QIODevice::ReadOnly))
            {
                qWarning().noquote()
                    << "[RIP WebEngine] unable to open:"
                    << canonicalFile;

                delete file;

                job->fail(
                    QWebEngineUrlRequestJob::UrlNotFound);
                return;
            }

            const QMimeType mime =
                QMimeDatabase()
                    .mimeTypeForFile(
                        QFileInfo(canonicalFile));

            QByteArray mimeType =
                mime.name().toUtf8();

            if (mimeType.isEmpty())
            {
                mimeType =
                    "application/octet-stream";
            }

            job->reply(
                mimeType,
                file);
        }

    private:
        QString m_root;
    };

    QMainWindow *g_window = nullptr;
    QWebEngineView *g_webview = nullptr;
    RipBridge *g_bridge = nullptr;

    fs::path g_current_directory;
    fs::path g_current_archive;
    fs::path g_selected_path;

    std::wstring g_requested_preset = L"strong";

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

    fs::path path_from_qstring(
        const QString &path)
    {
#ifdef _WIN32
        return fs::path(
            path.toStdWString());
#else
        return fs::path(
            path.toStdString());
#endif
    }

    QString qstring_from_path(
        const fs::path &path)
    {
#ifdef _WIN32
        return QString::fromStdWString(
            path.wstring());
#else
        return QString::fromStdString(
            path.string());
#endif
    }

    QString qstring_from_wstring(
        const std::wstring &value)
    {
        return QString::fromStdWString(
            value);
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
        const QFileInfo info(
            qstring_from_path(path));

        if (!info.exists())
        {
            return false;
        }

        modified =
            info.lastModified()
                .toString(
                    QStringLiteral("yyyy-MM-dd"))
                .toStdWString();

        created =
            info.birthTime()
                .toString(
                    QStringLiteral("yyyy-MM-dd"))
                .toStdWString();

        if (created.empty())
        {
            created = modified;
        }

        return true;
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

                    swprintf(
                        buffer,
                        sizeof(buffer) /
                            sizeof(*buffer),
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

    void send_web_message(
        const std::wstring &message)
    {
        if (!g_bridge)
        {
            return;
        }

        const QString value =
            qstring_from_wstring(message);

        if (QThread::currentThread() ==
            g_bridge->thread())
        {
            g_bridge->sendToWeb(value);
            return;
        }

        QMetaObject::invokeMethod(
            g_bridge,
            [bridge = g_bridge, value]()
            {
                if (bridge)
                {
                    bridge->sendToWeb(value);
                }
            },
            Qt::QueuedConnection);
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
        if (!g_window)
        {
            return;
        }

        g_window->setWindowTitle(
            QStringLiteral("RIP - ") +
            qstring_from_wstring(text));
    }

    void show_error_dialog(
        const QString &title,
        const QString &text)
    {
        if (g_window)
        {
            QMessageBox::critical(
                g_window,
                title,
                text);
        }
    }

    void show_info_dialog(
        const QString &title,
        const QString &text)
    {
        if (g_window)
        {
            QMessageBox::information(
                g_window,
                title,
                text);
        }
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

                return QString::fromStdWString(a.name)
                           .compare(
                               QString::fromStdWString(b.name),
                               Qt::CaseInsensitive) < 0;
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

        case rip::COMPRESSION_RIPC:
            return L"RIPC";

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
        QString defaultName =
            QStringLiteral("archive.rip");

        if (!g_selected_path.empty())
        {
            const QFileInfo info(
                qstring_from_path(
                    g_selected_path));

            defaultName = info.fileName();

            if (info.suffix().isEmpty())
            {
                defaultName +=
                    QStringLiteral(".rip");
            }
            else
            {
                defaultName =
                    info.completeBaseName() +
                    QStringLiteral(".rip");
            }
        }

        const QString file =
            QFileDialog::getSaveFileName(
                g_window,
                QStringLiteral("Save RIP Archive"),
                defaultName,
                QStringLiteral(
                    "RIP archives (*.rip);;All files (*)"));

        if (file.isEmpty())
        {
            return false;
        }

        output =
            path_from_qstring(file);

        return true;
    }

    bool pick_folder(
        fs::path &output)
    {
        const QString directory =
            QFileDialog::getExistingDirectory(
                g_window,
                QStringLiteral(
                    "Select extraction directory"),
                QDir::homePath(),
                QFileDialog::ShowDirsOnly);

        if (directory.isEmpty())
        {
            return false;
        }

        output =
            path_from_qstring(directory);

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

        QString extension =
            qstring_from_path(
                g_selected_path
            ).section(
                QLatin1Char('.'),
                -1,
                -1
            ).toLower();

        if (extension != QStringLiteral("rip"))
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

        std::wstring stageText;

        switch (stage)
        {
        case rip::ArchiveProgressStage::Preparing:
            stageText = L"Preparing archive";
            break;

        case rip::ArchiveProgressStage::Compressing:
            stageText = L"Compressing";
            break;

        case rip::ArchiveProgressStage::Writing:
            stageText = L"Writing archive";
            break;

        case rip::ArchiveProgressStage::Finalizing:
            stageText = L"Finalizing";
            break;
        }

        const std::wstring json =
            L"{\"type\":\"archiveProgress\"," +
            std::wstring(L"\"processed\":") +
            std::to_wstring(processed) +
            L",\"total\":" +
            std::to_wstring(total) +
            L",\"percent\":" +
            std::to_wstring(
                std::clamp(
                    percent,
                    0,
                    99)) +
            L",\"file\":\"" +
            json_escape(file.wstring()) +
            L"\",\"stage\":\"" +
            json_escape(stageText) +
            L"\"}";

        send_web_message(json);
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

        if (!fs::exists(source, error) ||
            error)
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

                if (!fs::exists(
                        output,
                        error))
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

                QMetaObject::invokeMethod(
                    qApp,
                    [success, final_output]()
                    {
                        g_archive_operation_active =
                            false;

                        if (success)
                        {
                            g_current_archive =
                                final_output;

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
                    },
                    Qt::QueuedConnection);
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
            show_error_dialog(
                QStringLiteral("RIP"),
                QStringLiteral(
                    "The archive test failed."));

            send_error(
                L"Archive test failed.");

            return;
        }

        show_info_dialog(
            QStringLiteral("RIP Archive Test"),
            QStringLiteral(
                "The archive is valid."));

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
        std::uint64_t ripc_count = 0;

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
            else if (entry.compression ==
                     rip::COMPRESSION_RIPC)
            {
                ++ripc_count;
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
            << stored_count
            << L"\n"
            << L"RIPC: "
            << ripc_count;

        show_info_dialog(
            QStringLiteral("RIP Archive Information"),
            qstring_from_wstring(message.str()));
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

            const QMessageBox::StandardButton answer =
                QMessageBox::question(
                    g_window,
                    QStringLiteral("Confirm Delete"),
                    QStringLiteral(
                        "Delete this item?\n\n") +
                        qstring_from_path(
                            g_selected_path),
                    QMessageBox::Yes |
                        QMessageBox::No,
                    QMessageBox::No);

            if (answer != QMessageBox::Yes)
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
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(
                qstring_from_path(path)));
    }

    // -----------------------------------------------------------------------------
    // Host messages
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
    // Qt WebEngine initialization
    // -----------------------------------------------------------------------------

    void install_bridge_script(
        QWebEngineProfile *profile)
    {
        QFile channelFile(
            QStringLiteral(
                ":/qtwebchannel/qwebchannel.js"));

        if (!channelFile.open(
                QIODevice::ReadOnly))
        {
            throw std::runtime_error(
                "Unable to load qwebchannel.js.");
        }

        const QString bridgeCode =
            QStringLiteral(R"JS(
(function() {
    if (window.__ripQtBridgeInstalled) {
        return;
    }

    window.__ripQtBridgeInstalled = true;

    let bridge = null;
    const queuedMessages = [];
    const listeners = [];

    const dispatch = (message) => {
        const event = {
            data: message
        };

        for (const listener of listeners) {
            try {
                listener(event);
            }
            catch {
                // Keep one broken listener from stopping the bridge.
            }
        }
    };

    window.chrome =
        window.chrome || {};

    window.chrome.webview = {
        postMessage(message) {
            const value =
                String(message);

            if (bridge) {
                bridge.postMessage(value);
            }
            else {
                queuedMessages.push(value);
            }
        },

        addEventListener(type, listener) {
            if (
                type === "message" &&
                typeof listener === "function"
            ) {
                listeners.push(listener);
            }
        }
    };

    new QWebChannel(
        qt.webChannelTransport,
        (channel) => {
            bridge =
                channel.objects.ripHost;

            if (!bridge) {
                return;
            }

            if (bridge.messageReceived) {
                bridge.messageReceived.connect(
                    dispatch
                );
            }

            while (queuedMessages.length > 0) {
                bridge.postMessage(
                    queuedMessages.shift()
                );
            }
        }
    );
})();
)JS");

        const QString source =
            QString::fromUtf8(
                channelFile.readAll()) +
            "\n" +
            bridgeCode;

        QWebEngineScript script;

        script.setName(
            QStringLiteral("rip-qt-bridge"));

        script.setInjectionPoint(
            QWebEngineScript::DocumentCreation);

        script.setWorldId(
            QWebEngineScript::MainWorld);

        script.setRunsOnSubFrames(false);

        script.setSourceCode(
            source);

        profile->scripts()->insert(
            script);
    }

} // namespace

#include "gui_main.moc"

int main(
    int argc,
    char *argv[])
{
    QWebEngineUrlScheme scheme(
        QByteArrayLiteral("rip"));

    scheme.setSyntax(
        QWebEngineUrlScheme::Syntax::Host);

    scheme.setDefaultPort(
        QWebEngineUrlScheme::PortUnspecified);

    scheme.setFlags(
        QWebEngineUrlScheme::SecureScheme |
        QWebEngineUrlScheme::LocalScheme |
        QWebEngineUrlScheme::LocalAccessAllowed |
        QWebEngineUrlScheme::CorsEnabled |
        QWebEngineUrlScheme::FetchApiAllowed);

    QWebEngineUrlScheme::registerScheme(
        scheme);

    QApplication application(
        argc,
        argv);

    QApplication::setApplicationName(
        QStringLiteral("RIP"));

    QApplication::setApplicationDisplayName(
        QStringLiteral("RIP Archive Utility"));

    QApplication::setOrganizationName(
        QStringLiteral("Cooper-Src"));

    const QString webRoot =
        QDir(
            QCoreApplication::applicationDirPath())
            .filePath(
                QStringLiteral("gui"));

    auto *window =
        new QMainWindow();

    window->resize(
        1280,
        820);

    window->setMinimumSize(
        900,
        600);

    window->setWindowTitle(
        QStringLiteral("RIP - Loading..."));

    QWebEngineProfile *profile =
        QWebEngineProfile::defaultProfile();

    auto *view =
        new QWebEngineView(
            window);

    auto *page =
        new RipWebPage(
            profile,
            view);

    view->setPage(page);

    view->setZoomFactor(1.0);

    view->setContextMenuPolicy(
        Qt::NoContextMenu);

    window->setCentralWidget(
        view);

    g_current_directory =
        path_from_qstring(
            QDir::homePath());

    g_window = window;
    g_webview = view;

    RipBridge bridge;

    g_bridge = &bridge;

    auto *schemeHandler =
        new RipSchemeHandler(
            webRoot,
            profile);

    profile->installUrlSchemeHandler(
        QByteArrayLiteral("rip"),
        schemeHandler);

    install_bridge_script(
        profile);

    QWebChannel channel;

    channel.registerObject(
        QStringLiteral("ripHost"),
        &bridge);

    view->page()->setWebChannel(
        &channel);

    QObject::connect(
        &bridge,
        &RipBridge::messageFromWeb,
        [](const QString &message)
        {
            handle_web_message(
                message.toStdWString());
        });

    QObject::connect(
        view,
        &QWebEngineView::loadStarted,
        []
        {
            qDebug().noquote()
                << "[RIP WebEngine] load started";
        });

    QObject::connect(
        view,
        &QWebEngineView::loadProgress,
        [](int progress)
        {
            qDebug()
                << "[RIP WebEngine] load progress:"
                << progress;
        });

    QObject::connect(
        view,
        &QWebEngineView::urlChanged,
        [](const QUrl &url)
        {
            qDebug().noquote()
                << "[RIP WebEngine] URL:"
                << url;
        });

    QObject::connect(
        view,
        &QWebEngineView::loadFinished,
        [](bool ok)
        {
            qDebug()
                << "[RIP WebEngine] load finished:"
                << ok;

            if (!ok)
            {
                show_error_dialog(
                    QStringLiteral("RIP"),
                    QStringLiteral(
                        "Unable to load the RIP GUI.\n\n"
                        "Run rip-gui from a terminal to see "
                        "the WebEngine diagnostics."));
            }
        });

    view->setUrl(
        QUrl(
            QStringLiteral(
                "rip://app/index.html?native=1")));

    window->show();

    const int exitCode =
        application.exec();

    g_bridge = nullptr;
    g_webview = nullptr;
    g_window = nullptr;

    return exitCode;
}
