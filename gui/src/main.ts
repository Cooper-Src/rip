import "./style.css";

type IconKey =
    | "folder"
    | "vscode"
    | "markdown"
    | "vlc"
    | "browser"
    | "exe"
    | "disc";

type ViewMode =
    | "filesystem"
    | "archive";

interface FileEntry {
    name: string;
    path: string;
    kind: "folder" | "file";
    icon: IconKey;
    size: number | null;
    modified: string;
    created: string;
}

interface ArchiveEntry {
    name: string;
    originalSize: number;
    compressedSize: number;
    method: string;
}

interface DirectoryMessage {
    type: "directory";
    path: string;
    entries: FileEntry[];
}

interface ArchiveMessage {
    type: "archive";
    path: string;
    major: number;
    minor: number;
    fileSize: number;
    entries: ArchiveEntry[];
}

interface ArchiveProgressMessage {
    type: "archiveProgress";
    processed: number;
    total: number;
    percent: number;
    file: string;
    stage: string;
}

interface ArchiveCompleteMessage {
    type: "archiveComplete";
    success: boolean;
}

interface StatusMessage {
    type: "status" | "error";
    text: string;
}

type HostMessage =
    | DirectoryMessage
    | ArchiveMessage
    | StatusMessage
    | ArchiveProgressMessage
    | ArchiveCompleteMessage;

interface WebViewBridge {
    postMessage(message: string): void;

    addEventListener(
        type: "message",
        listener: (event: MessageEvent<string>) => void
    ): void;
}

declare global {
    interface Window {
        chrome?: {
            webview?: WebViewBridge;
        };
    }
}

const nativeHost =
    new URLSearchParams(
        window.location.search
    ).get("native") === "1";

document.body.classList.toggle(
    "native-host",
    nativeHost
);

let mode: ViewMode = "filesystem";

let currentPath = "C:\\";
let archivePath = "";
let archiveDirectory = "";

let filesystemEntries: FileEntry[] = [];
let archiveEntries: ArchiveEntry[] = [];

let selectedIndex = -1;

const app =
    document.querySelector<HTMLDivElement>(
        "#app"
    );

if (!app) {
    throw new Error(
        "Unable to find #app."
    );
}

const formatSize = (
    size: number | null
): string => {
    if (size === null) {
        return "";
    }

    return size
        .toLocaleString("en-US")
        .replaceAll(",", " ");
};

const formatSaved = (
    original: number,
    packed: number
): string => {
    if (original <= 0) {
        return "0.0%";
    }

    return `${(
        ((original - packed) / original) *
        100
    ).toFixed(1)}%`;
};

const icon = (
    type: IconKey
): string => {
    switch (type) {
        case "folder":
            return `
                <svg viewBox="0 0 24 24">
                    <path
                        d="M3 6.5h7l1.8 2H21v10.8H3z"
                        fill="#f4c542"
                        stroke="#c38d1f"
                        stroke-width=".7"
                    />
                    <path
                        d="M3 8.5h18"
                        stroke="#e0a92d"
                        stroke-width="1"
                    />
                </svg>
            `;

        case "vscode":
            return `
                <svg viewBox="0 0 24 24">
                    <path
                        d="M17.8 3.5 21 5.2v13.6l-3.2 1.7-8.6-6.7
                           -3.1 2.4-1.9-1.3 3-3.5-3-3.5 1.9-1.3
                           3.1 2.4z"
                        fill="#1687d9"
                    />
                    <path
                        d="m9.7 8.1 5.5-3v13.8l-5.5-3"
                        fill="#36a6ee"
                    />
                </svg>
            `;

        case "markdown":
            return `
                <svg viewBox="0 0 24 24">
                    <path
                        d="M5 2.5h9l5 5v14H5z"
                        fill="#f8f8f8"
                        stroke="#a7adb4"
                    />
                    <path
                        d="M14 2.5v5h5"
                        fill="none"
                        stroke="#a7adb4"
                    />
                    <rect
                        x="7"
                        y="13"
                        width="10"
                        height="4.5"
                        fill="#7b8189"
                    />
                    <text
                        x="8"
                        y="16.4"
                        font-size="3.5"
                        fill="white"
                        font-family="Segoe UI, sans-serif"
                        font-weight="700"
                    >
                        MD
                    </text>
                </svg>
            `;

        case "vlc":
            return `
                <svg viewBox="0 0 24 24">
                    <path
                        d="M12 2 6.1 20h11.8z"
                        fill="#e87521"
                    />
                    <path
                        d="M7.8 14.5h8.4M8.8 11.5h6.4"
                        stroke="white"
                        stroke-width="2"
                    />
                    <path
                        d="M8.5 20h7"
                        stroke="#d14f13"
                        stroke-width="1.5"
                    />
                </svg>
            `;

        case "browser":
            return `
                <svg viewBox="0 0 24 24">
                    <circle
                        cx="12"
                        cy="12"
                        r="9"
                        fill="#dcefff"
                        stroke="#73a9d8"
                    />
                    <path
                        d="M3.5 11.5h17M12 3c2.6 3.1 2.7 14.8 0 18"
                        stroke="#78a6cb"
                        stroke-width="1"
                        fill="none"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="2.4"
                        fill="#4b9ed8"
                    />
                </svg>
            `;

        case "exe":
            return `
                <svg viewBox="0 0 24 24">
                    <rect
                        x="3"
                        y="3"
                        width="18"
                        height="18"
                        fill="#f4f6f8"
                        stroke="#919aa3"
                    />
                    <rect
                        x="6"
                        y="6"
                        width="12"
                        height="8"
                        fill="#4d87c4"
                    />
                </svg>
            `;

        case "disc":
            return `
                <svg viewBox="0 0 24 24">
                    <path
                        d="M5 2.5h14v19H5z"
                        fill="#f7f7f7"
                        stroke="#a6adb5"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="5"
                        fill="#aeb5bc"
                    />
                    <circle
                        cx="12"
                        cy="12"
                        r="1.8"
                        fill="#f4f4f4"
                    />
                </svg>
            `;
    }
};

app.innerHTML = `
    <div class="app">

        <header class="titlebar">

            <div class="titlebar-left">

                <div class="rip-title-icon">
                    <span>RIP</span>
                </div>

                <div
                    id="window-path"
                    class="titlebar-path"
                >
                    C:\\
                </div>

            </div>

            <div class="window-buttons">
                <button class="window-button">
                    <span class="minimize-glyph"></span>
                </button>

                <button class="window-button">
                    <span class="maximize-glyph"></span>
                </button>

                <button class="window-button close">
                    <span class="close-glyph"></span>
                </button>
            </div>

        </header>

        <section class="toolbar">

            <button
                class="tool"
                data-action="add"
            >
                <span class="tool-icon add-icon">+</span>
                <span class="tool-label">Add</span>
            </button>

            <button
                class="tool"
                data-action="extract"
            >
                <span class="tool-icon extract-icon"></span>
                <span class="tool-label">Extract</span>
            </button>

            <button
                class="tool"
                data-action="test"
            >
                <span class="tool-icon test-icon"></span>
                <span class="tool-label">Test</span>
            </button>

            <button
                class="tool"
                data-action="delete"
            >
                <span class="tool-icon delete-icon">×</span>
                <span class="tool-label">Delete</span>
            </button>

            <button
                class="tool"
                data-action="info"
            >
                <span class="tool-icon info-icon">i</span>
                <span class="tool-label">Info</span>
            </button>

            <div class="toolbar-spacer"></div>

        </section>

        <section class="address-bar">

            <button
                id="up-button"
                class="up-button"
            >
                <span class="folder-arrow-icon">
                    ↑
                </span>
            </button>

            <div class="address-input">

                <span class="address-drive-icon">
                    ▼
                </span>

                <span
                    id="address-path"
                    class="address-path"
                >
                    C:\\
                </span>

                <span class="address-chevron">
                    
                </span>

            </div>

        </section>

        <main class="file-area">

            <div
                id="column-header"
                class="column-header"
            ></div>

            <div
                id="file-list"
                class="file-list"
            ></div>

        </main>

                <footer class="status-bar">

            <div
                id="status-text"
                class="status-text"
            >
                Loading...
            </div>

            <div class="status-pane"></div>
            <div class="status-pane"></div>
            <div class="status-pane"></div>

        </footer>

        <!-- New Archive Dialog -->

        <div
            id="new-archive-dialog"
            class="modal-backdrop hidden"
        >
            <div
                class="archive-dialog"
                role="dialog"
                aria-modal="true"
                aria-labelledby="archive-dialog-title"
            >

                <div class="dialog-titlebar">

                    <div>
                        <div
                            id="archive-dialog-title"
                            class="dialog-title"
                        >
                            Create RIP Archive
                        </div>

                        <div
                            id="dialog-source"
                            class="dialog-subtitle"
                        >
                            No source selected
                        </div>
                    </div>

                    <button
                        id="dialog-close"
                        class="dialog-close"
                        aria-label="Close"
                    >
                        ×
                    </button>

                </div>

                <div class="dialog-body">

                    <label class="dialog-field">
                        <span>Source</span>

                        <input
                            id="dialog-source-input"
                            type="text"
                            readonly
                        >
                    </label>

                    <label class="dialog-field">
                        <span>Compression preset</span>

                        <select id="dialog-preset">
                            <option value="deflate">
                                Deflate
                            </option>

                            <option
                                value="strong"
                                selected
                            >
                                Strong
                            </option>

                            <option value="maximum">
                                Maximum
                            </option>

                            <option value="ultra">
                                Ultra
                            </option>
                        </select>
                    </label>

                    <div class="preset-description">

                        <div
    id="preset-description-title"
    class="preset-description-title"
></div>

                        <div
                            id="preset-description"
                            class="preset-description-text"
                        >
                            DEFLATE now, with room for
                            additional codecs in the
                            expanded compression engine.
                        </div>

                    </div>

                    <div class="dialog-note">
    The archive will be created next to
    the selected file or folder.
</div>

                </div>

                <div class="dialog-footer">

                    <button
                        id="dialog-cancel"
                        class="dialog-button"
                    >
                        Cancel
                    </button>

                    <button
                        id="dialog-create"
                        class="dialog-button primary"
                    >
                        Create
                    </button>

                </div>

            </div>
        </div>

    </div>

    <div
    id="archive-progress-dialog"
    class="modal-backdrop hidden"
>
    <div
        class="archive-progress-dialog"
        role="dialog"
        aria-modal="true"
    >

        <div class="dialog-titlebar">

            <div>
                <div class="dialog-title">
                    Creating RIP Archive
                </div>

                <div
                    id="archive-progress-source"
                    class="dialog-subtitle"
                >
                    Preparing...
                </div>
            </div>

        </div>

        <div class="progress-body">

            <div class="progress-file-label">
                Current file
            </div>

            <div
                id="archive-progress-file"
                class="progress-file"
            >
                Preparing archive...
            </div>

            <div class="progress-method-row">

                <span>Stage</span>

                <strong id="archive-progress-stage">
                    Preparing
                </strong>

            </div>

            <div class="progress-track">
                <div
                    id="archive-progress-bar"
                    class="progress-bar"
                ></div>
            </div>

            <div class="progress-meta">

                <span
                    id="archive-progress-count"
                >
                    0 / 0 files
                </span>

                <strong
                    id="archive-progress-percent"
                >
                    0%
                </strong>

            </div>

        </div>

        <div class="progress-status-bar">

            <span
                id="archive-progress-status"
            >
                Preparing archive...
            </span>

        </div>

    </div>
</div>
`;

const webview =
    window.chrome?.webview;

const sendHostMessage = (
    message: string
): void => {
    if (webview) {
        webview.postMessage(message);
    }
};

const statusText =
    document.querySelector<HTMLDivElement>(
        "#status-text"
    );

const addressPath =
    document.querySelector<HTMLSpanElement>(
        "#address-path"
    );

const windowPath =
    document.querySelector<HTMLDivElement>(
        "#window-path"
    );

const columnHeader =
    document.querySelector<HTMLDivElement>(
        "#column-header"
    );

const fileList =
    document.querySelector<HTMLDivElement>(
        "#file-list"
    );

const setStatus = (
    text: string
): void => {
    if (statusText) {
        statusText.textContent = text;
    }
};

const setColumns = (
    archive: boolean
): void => {
    if (!columnHeader) {
        return;
    }

    if (!archive) {
        columnHeader.innerHTML = `
            <div class="column name-column">Name</div>
            <div class="column size-column">Size</div>
            <div class="column date-column">Modified</div>
            <div class="column date-column">Created</div>
            <div class="column">Comment</div>
            <div class="column size-column">Folders</div>
            <div class="column size-column">Files</div>
            <div class="column rest-column"></div>
        `;

        return;
    }

    columnHeader.innerHTML = `
        <div class="column name-column">Name</div>
        <div class="column size-column">Original</div>
        <div class="column size-column">Packed</div>
        <div class="column size-column">Saved</div>
        <div class="column">Method</div>
        <div class="column"></div>
        <div class="column"></div>
        <div class="column rest-column"></div>
    `;
};

const updateToolbar = (): void => {
    const selectedFilesystemEntry =
        mode === "filesystem" &&
        selectedIndex >= 0
            ? filesystemEntries[selectedIndex]
            : undefined;

    const selectedRip =
        selectedFilesystemEntry !== undefined &&
        selectedFilesystemEntry.kind === "file" &&
        selectedFilesystemEntry.name
            .toLowerCase()
            .endsWith(".rip");

    const archiveAvailable =
        mode === "archive" ||
        selectedRip;

    const enabled = {
        add:
    mode === "filesystem" &&
    selectedIndex >= 0 &&
    !selectedRip,
        extract: archiveAvailable,
        test: archiveAvailable,
        delete:
            mode === "filesystem" &&
            selectedIndex >= 0,
        info: archiveAvailable
    };

    document
        .querySelectorAll<HTMLButtonElement>(
            "[data-action]"
        )
        .forEach((button) => {
            const action =
                button.dataset.action as keyof typeof enabled;

            button.disabled =
                !enabled[action];
        });
};

const newArchiveDialog =
    document.querySelector<HTMLDivElement>(
        "#new-archive-dialog"
    );

const dialogSource =
    document.querySelector<HTMLDivElement>(
        "#dialog-source"
    );

const dialogSourceInput =
    document.querySelector<HTMLInputElement>(
        "#dialog-source-input"
    );

const dialogPreset =
    document.querySelector<HTMLSelectElement>(
        "#dialog-preset"
    );

const presetDescription =
    document.querySelector<HTMLDivElement>(
        "#preset-description"
    );

const presetDescriptionTitle =
    document.querySelector<HTMLDivElement>(
        "#preset-description-title"
    );

const presetDescriptions: Record<
    string,
    {
        title: string;
        description: string;
    }
> = {
    deflate: {
        title: "Deflate",
        description:
            "Use DEFLATE compression for every compressible file."
    },

    strong: {
        title: "Strong",
        description:
            "Compare DEFLATE with additional codecs as the multi-codec engine expands."
    },

    maximum: {
        title: "Maximum",
        description:
            "Use several compression algorithms and keep the smallest result."
    },

    ultra: {
        title: "Ultra",
        description:
            "Spend significantly more CPU time searching for the smallest possible archive."
    },

    custom: {
        title: "Custom",
        description:
            "Choose which compression algorithms and levels RIP should use."
    }
};

const openNewArchiveDialog = (): void => {
    if (
        mode !== "filesystem" ||
        selectedIndex < 0
    ) {
        setStatus(
            "Select a file or folder first"
        );

        return;
    }

    const entry =
        filesystemEntries[selectedIndex];

    if (!entry) {
        return;
    }

    if (dialogSource) {
        dialogSource.textContent =
            entry.name;
    }

    if (dialogSourceInput) {
        dialogSourceInput.value =
            entry.path;
    }

    if (dialogPreset) {
        dialogPreset.value =
            "strong";
    }

    const defaultPreset =
    presetDescriptions.strong;

if (dialogPreset) {
    dialogPreset.value = "strong";
}

if (presetDescriptionTitle) {
    presetDescriptionTitle.textContent =
        defaultPreset.title;
}

if (presetDescription) {
    presetDescription.textContent =
        defaultPreset.description;
}

    newArchiveDialog?.classList.remove(
        "hidden"
    );

    dialogPreset?.focus();
};

const closeNewArchiveDialog = (): void => {
    newArchiveDialog?.classList.add(
        "hidden"
    );
};

dialogPreset?.addEventListener(
    "change",
    () => {
        const value =
            dialogPreset.value;

        const preset =
            presetDescriptions[value];

        if (!preset) {
            return;
        }

        if (presetDescriptionTitle) {
            presetDescriptionTitle.textContent =
                preset.title;
        }

        if (presetDescription) {
            presetDescription.textContent =
                preset.description;
        }
    }
);

document
    .querySelector<HTMLButtonElement>(
        "#dialog-close"
    )
    ?.addEventListener(
        "click",
        closeNewArchiveDialog
    );

document
    .querySelector<HTMLButtonElement>(
        "#dialog-cancel"
    )
    ?.addEventListener(
        "click",
        closeNewArchiveDialog
    );

document
    .querySelector<HTMLButtonElement>(
        "#dialog-create"
    )
    ?.addEventListener(
        "click",
        () => {
            if (
                mode !== "filesystem" ||
                selectedIndex < 0
            ) {
                return;
            }

            const preset =
                dialogPreset?.value ??
                "strong";

            sendHostMessage(
    `preset\t${preset}`
);

closeNewArchiveDialog();

showArchiveProgress();

sendHostMessage(
    "action\tadd"
);
        }
    );

newArchiveDialog?.addEventListener(
    "click",
    (event) => {
        if (
            event.target ===
            newArchiveDialog
        ) {
            closeNewArchiveDialog();
        }
    }
);

const showArchiveProgress = (): void => {
    archiveProgressDialog?.classList.remove(
        "hidden"
    );

    if (archiveProgressBar) {
        archiveProgressBar.style.width =
            "0%";
    }

    if (archiveProgressPercent) {
        archiveProgressPercent.textContent =
            "0%";
    }

    if (archiveProgressCount) {
        archiveProgressCount.textContent =
            "0 / 0 files";
    }

    if (archiveProgressStage) {
        archiveProgressStage.textContent =
            "Preparing";
    }

    if (archiveProgressFile) {
        archiveProgressFile.textContent =
            "Preparing archive...";
    }

    if (archiveProgressStatus) {
        archiveProgressStatus.textContent =
            "Preparing archive...";
    }
};

const hideArchiveProgress = (): void => {
    archiveProgressDialog?.classList.add(
        "hidden"
    );
};

const updateArchiveProgress = (
    progress: ArchiveProgressMessage
): void => {
    showArchiveProgress();

    if (archiveProgressSource) {
        archiveProgressSource.textContent =
            progress.file;
    }

    if (archiveProgressFile) {
        archiveProgressFile.textContent =
            progress.file;
    }

    if (archiveProgressStage) {
        archiveProgressStage.textContent =
            progress.stage;
    }

    if (archiveProgressBar) {
        archiveProgressBar.style.width =
            `${progress.percent}%`;
    }

    if (archiveProgressPercent) {
        archiveProgressPercent.textContent =
            `${progress.percent}%`;
    }

    if (archiveProgressCount) {
        archiveProgressCount.textContent =
            `${progress.processed} / ${progress.total} files`;
    }

    if (archiveProgressStatus) {
        archiveProgressStatus.textContent =
            progress.stage;
    }

    setStatus(
        `${progress.stage} · ${progress.percent}%`
    );
};

const archiveProgressDialog =
    document.querySelector<HTMLDivElement>(
        "#archive-progress-dialog"
    );

const archiveProgressSource =
    document.querySelector<HTMLDivElement>(
        "#archive-progress-source"
    );

const archiveProgressFile =
    document.querySelector<HTMLDivElement>(
        "#archive-progress-file"
    );

const archiveProgressStage =
    document.querySelector<HTMLElement>(
        "#archive-progress-stage"
    );

const archiveProgressBar =
    document.querySelector<HTMLDivElement>(
        "#archive-progress-bar"
    );

const archiveProgressCount =
    document.querySelector<HTMLSpanElement>(
        "#archive-progress-count"
    );

const archiveProgressPercent =
    document.querySelector<HTMLElement>(
        "#archive-progress-percent"
    );

const archiveProgressStatus =
    document.querySelector<HTMLSpanElement>(
        "#archive-progress-status"
    );

const renderFilesystem = (
    entries: FileEntry[]
): void => {
    if (!fileList) {
        return;
    }

    mode = "filesystem";
    filesystemEntries = entries;

    setColumns(false);

    if (addressPath) {
        addressPath.textContent =
            currentPath;
    }

    if (windowPath) {
        windowPath.textContent =
            currentPath;
    }

    fileList.innerHTML =
        entries
            .map(
                (entry, index) => `
                    <div
                        class="file-row"
                        data-index="${index}"
                        tabindex="0"
                    >

                        <div class="cell name-cell">

                            <span class="file-icon">
                                ${icon(entry.icon)}
                            </span>

                            <span class="name-text">
                                ${entry.name}
                            </span>

                        </div>

                        <div class="cell size-cell">
                            ${
                                entry.size === null
                                    ? ""
                                    : formatSize(
                                          entry.size
                                      )
                            }
                        </div>

                        <div class="cell date-cell">
                            ${entry.modified}
                        </div>

                        <div class="cell date-cell">
                            ${entry.created}
                        </div>

                        <div class="cell"></div>
                        <div class="cell size-cell"></div>
                        <div class="cell size-cell"></div>
                        <div class="cell"></div>

                    </div>
                `
            )
            .join("");

    selectedIndex = -1;

    fileList
        .querySelectorAll<HTMLElement>(
            ".file-row"
        )
        .forEach((row) => {

            const index =
                Number(row.dataset.index);

            row.addEventListener(
                "click",
                () => {
                    selectedIndex =
                        index;

                    fileList
                        .querySelectorAll(
                            ".file-row"
                        )
                        .forEach((item) =>
                            item.classList.remove(
                                "selected"
                            )
                        );

                    row.classList.add(
                        "selected"
                    );

                    sendHostMessage(
                        `select\t${entries[index].path}`
                    );

                    updateToolbar();

                    setStatus(
                        `1 / ${entries.length} object(s) selected`
                    );
                }
            );

            row.addEventListener(
                "dblclick",
                () => {
                    const entry =
                        entries[index];

                    if (!entry) {
                        return;
                    }

                    if (entry.kind === "folder") {
                        sendHostMessage(
                            `directory\t${entry.path}`
                        );

                        return;
                    }

                    if (
                        entry.name
                            .toLowerCase()
                            .endsWith(".rip")
                    ) {
                        sendHostMessage(
                            `archive\t${entry.path}`
                        );

                        return;
                    }

                    sendHostMessage(
                        `open-file\t${entry.path}`
                    );
                }
            );
        });

    setStatus(
        `0 / ${entries.length} object(s) selected`
    );

    updateToolbar();
};

const archiveIconForName = (
    name: string
): IconKey => {
    const lower =
        name.toLowerCase();

    if (
        lower.endsWith(".md") ||
        lower.endsWith(".markdown")
    ) {
        return "markdown";
    }

    if (
        lower.endsWith(".mp4") ||
        lower.endsWith(".mkv") ||
        lower.endsWith(".avi")
    ) {
        return "vlc";
    }

    if (
        lower.endsWith(".html") ||
        lower.endsWith(".htm")
    ) {
        return "browser";
    }

    if (
        lower.endsWith(".exe") ||
        lower.endsWith(".dll")
    ) {
        return "exe";
    }

    if (
        lower.endsWith(".iso") ||
        lower.endsWith(".img")
    ) {
        return "disc";
    }

    if (
        lower.endsWith(".cpp") ||
        lower.endsWith(".hpp") ||
        lower.endsWith(".c") ||
        lower.endsWith(".h") ||
        lower.endsWith(".js") ||
        lower.endsWith(".ts") ||
        lower.endsWith(".tsx") ||
        lower.endsWith(".jsx") ||
        lower.endsWith(".py") ||
        lower.endsWith(".json") ||
        lower.endsWith(".css")
    ) {
        return "vscode";
    }

    return "disc";
};

const renderArchive = (
    path: string,
    entries: ArchiveEntry[],
    major: number,
    minor: number
): void => {
    if (!fileList) {
        return;
    }

    mode = "archive";
    archivePath = path;
    archiveEntries = entries;
    archiveDirectory = "";

    renderArchiveDirectory(
        major,
        minor
    );
};

const renderArchiveDirectory = (
    major?: number,
    minor?: number
): void => {
    if (!fileList) {
        return;
    }

    mode = "archive";

    setColumns(true);

    /*
     * Normalize the current virtual directory.
     *
     * ""                  -> archive root
     * "src"               -> /src
     * "node_modules/react" -> /node_modules/react
     */
    const currentDirectory =
        archiveDirectory
            .replace(/^\/+/, "")
            .replace(/\/+$/, "");

    const directoryPrefix =
        currentDirectory.length > 0
            ? `${currentDirectory}/`
            : "";

    /*
     * Build the visible contents of the current directory.
     *
     * A real archive only stores files, so folders are generated
     * from the path prefixes.
     */
    const folders = new Set<string>();
    const visibleFiles: ArchiveEntry[] = [];

    for (const entry of archiveEntries) {
        const normalized =
            entry.name.replace(
                /^\/+/,
                ""
            );

        if (
            !normalized.startsWith(
                directoryPrefix
            )
        ) {
            continue;
        }

        const relative =
            normalized.slice(
                directoryPrefix.length
            );

        if (!relative) {
            continue;
        }

        const slash =
            relative.indexOf("/");

        if (slash !== -1) {
            folders.add(
                relative.slice(0, slash)
            );

            continue;
        }

        visibleFiles.push(entry);
    }

    /*
     * Turn synthetic folders into rows.
     */
    const folderRows =
        [...folders]
            .sort((a, b) =>
                a.localeCompare(
                    b,
                    undefined,
                    {
                        sensitivity: "base"
                    }
                )
            )
            .map((folder) => ({
                kind: "folder" as const,
                name: folder,
                path:
                    directoryPrefix +
                    folder
            }));

    /*
     * Files are sorted after folders, like a normal archive manager.
     */
    visibleFiles.sort((a, b) =>
        a.name.localeCompare(
            b.name,
            undefined,
            {
                sensitivity: "base"
            }
        )
    );

    const rows = [
        ...folderRows.map(
            (folder) => ({
                type: "folder" as const,
                name: folder.name,
                path: folder.path
            })
        ),

        ...visibleFiles.map(
            (entry) => ({
                type: "file" as const,
                entry
            })
        )
    ];

    fileList.innerHTML =
        rows
            .map((row, index) => {
                if (row.type === "folder") {
                    return `
                        <div
                            class="file-row"
                            data-index="${index}"
                            data-kind="folder"
                            tabindex="0"
                        >

                            <div class="cell name-cell">

                                <span class="file-icon">
                                    ${icon("folder")}
                                </span>

                                <span class="name-text">
                                    ${row.name}
                                </span>

                            </div>

                            <div class="cell size-cell"></div>
                            <div class="cell size-cell"></div>
                            <div class="cell size-cell"></div>

                            <div class="cell">
                                <span class="method-label">
                                    Folder
                                </span>
                            </div>

                            <div class="cell"></div>
                            <div class="cell"></div>
                            <div class="cell"></div>

                        </div>
                    `;
                }

                const entry =
                    row.entry;

                const saved =
                    entry.originalSize >
                    entry.compressedSize
                        ? formatSaved(
                              entry.originalSize,
                              entry.compressedSize
                          )
                        : "—";

                return `
                    <div
                        class="file-row"
                        data-index="${index}"
                        data-kind="file"
                        tabindex="0"
                    >

                        <div class="cell name-cell">

                            <span class="file-icon">
                                ${icon(
                                    archiveIconForName(
                                        entry.name
                                    )
                                )}
                            </span>

                            <span class="name-text">
                                ${entry.name.split("/").pop() ?? entry.name}
                            </span>

                        </div>

                        <div class="cell size-cell">
                            ${formatSize(
                                entry.originalSize
                            )}
                        </div>

                        <div class="cell size-cell">
                            ${formatSize(
                                entry.compressedSize
                            )}
                        </div>

                        <div class="cell size-cell">
                            ${saved}
                        </div>

                        <div class="cell">
                            <span
                                class="
                                    method-label
                                    ${entry.method.toLowerCase()}
                                "
                            >
                                ${entry.method}
                            </span>
                        </div>

                        <div class="cell"></div>
                        <div class="cell"></div>
                        <div class="cell"></div>

                    </div>
                `;
            })
            .join("");

    selectedIndex = -1;

    const updateStatus = (): void => {
        setStatus(
            `${rows.length} item(s) · RIP ${
                major ?? 1
            }.${minor ?? 0}`
        );
    };

    fileList
        .querySelectorAll<HTMLElement>(
            ".file-row"
        )
        .forEach((row) => {
            const index =
                Number(
                    row.dataset.index
                );

            row.addEventListener(
                "click",
                () => {
                    selectedIndex =
                        index;

                    fileList
                        .querySelectorAll(
                            ".file-row"
                        )
                        .forEach(
                            (item) =>
                                item.classList.remove(
                                    "selected"
                                )
                        );

                    row.classList.add(
                        "selected"
                    );

                    updateToolbar();

                    setStatus(
                        `1 / ${rows.length} object(s) selected`
                    );
                }
            );

            row.addEventListener(
                "dblclick",
                () => {
                    const selected =
                        rows[index];

                    if (!selected) {
                        return;
                    }

                    if (
                        selected.type ===
                        "folder"
                    ) {
                        archiveDirectory =
                            selected.path;

                        renderArchiveDirectory(
                            major,
                            minor
                        );

                        return;
                    }

                    /*
                     * Files don't need opening here yet.
                     * Later this can become "Open" or
                     * "Extract selected".
                     */
                }
            );
        });

    /*
     * Archive address bar.
     */
    if (addressPath) {
        addressPath.textContent =
            currentDirectory.length > 0
                ? `${archivePath}\\${currentDirectory}`
                : archivePath;
    }

    if (windowPath) {
        windowPath.textContent =
            currentDirectory.length > 0
                ? `${archivePath}\\${currentDirectory}`
                : archivePath;
    }

    updateStatus();
    updateToolbar();
};

const handleHostMessage = (
    event: MessageEvent<string>
): void => {
    let message: HostMessage;

    try {
        message =
            JSON.parse(event.data) as HostMessage;
    } catch {
        return;
    }

    if (
        message.type ===
        "directory"
    ) {
        currentPath =
            message.path;

        renderFilesystem(
            message.entries
        );

        return;
    }

    if (
        message.type ===
        "archive"
    ) {
        renderArchive(
            message.path,
            message.entries,
            message.major,
            message.minor
        );

        return;
    }

if (
    message.type ===
    "archiveProgress"
) {
    updateArchiveProgress(
        message
    );

    return;
}

if (
    message.type ===
    "archiveComplete"
) {
    hideArchiveProgress();

    if (message.success) {
        setStatus(
            "Archive created successfully"
        );
    }
    else {
        setStatus(
            "Archive creation failed"
        );
    }

    return;
}

    if (
        message.type ===
            "status" ||
        message.type ===
            "error"
    ) {
        setStatus(
            message.text
        );

        return;
    }
};

if (webview) {
    webview.addEventListener(
        "message",
        handleHostMessage
    );

    sendHostMessage(
        "ready"
    );
}
else {
    // Browser-development fallback.
    renderFilesystem([]);
    setStatus(
        "Run rip-gui.exe to browse the real filesystem"
    );
}

document
    .querySelectorAll<HTMLButtonElement>(
        "[data-action]"
    )
    .forEach((button) => {
        button.addEventListener(
            "click",
            () => {
                const action =
                    button.dataset.action;

                if (!action) {
                    return;
                }

                if (action === "add") {
                    openNewArchiveDialog();
                    return;
                }

                sendHostMessage(
                    `action\t${action}`
                );
            }
        );
    });

document
    .querySelector<HTMLButtonElement>(
        "#up-button"
    )
    ?.addEventListener(
        "click",
        () => {
            if (
                mode === "archive" &&
                archiveDirectory.length > 0
            ) {
                const parts =
                    archiveDirectory
                        .split("/")
                        .filter(Boolean);

                parts.pop();

                archiveDirectory =
                    parts.join("/");

                renderArchiveDirectory();

                return;
            }

            sendHostMessage("up");
        }
    );