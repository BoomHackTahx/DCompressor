#include <QApplication>
#include <QAction>
#include <QDateTime>
#include <QFileDialog>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QStyle>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ArchiveEntry {
    QString name;
    QString type;
    qint64 size;
    QString modified;
};

using ArchiveReader = std::unique_ptr<archive, decltype(&archive_read_free)>;
using ArchiveWriter = std::unique_ptr<archive, decltype(&archive_write_free)>;

fs::path toFilesystemPath(const QString& path)
{
#ifdef _WIN32
    return fs::path(path.toStdWString());
#else
    return fs::path(path.toStdString());
#endif
}

QString fromFilesystemPath(const fs::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromStdString(path.string());
#endif
}

QString archiveError(archive* handle, const QString& fallback)
{
    const char* message = archive_error_string(handle);
    return message ? QString::fromLocal8Bit(message) : fallback;
}

bool readEntries(const QString& path, std::vector<ArchiveEntry>& entries, QString& error)
{
    ArchiveReader reader(archive_read_new(), archive_read_free);
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());

#ifdef _WIN32
    const std::wstring nativePath = path.toStdWString();
    if (archive_read_open_filename_w(reader.get(), nativePath.c_str(), 10240) != ARCHIVE_OK) {
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    if (archive_read_open_filename(reader.get(), encodedPath.constData(), 10240) != ARCHIVE_OK) {
#endif
        error = archiveError(reader.get(), QStringLiteral("Impossible d’ouvrir l’archive."));
        return false;
    }

    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* rawName = archive_entry_pathname_utf8(entry);
        if (!rawName) rawName = archive_entry_pathname(entry);
        const auto fileType = archive_entry_filetype(entry);
        entries.push_back({
            QString::fromUtf8(rawName ? rawName : ""),
            fileType == AE_IFDIR ? QStringLiteral("Dossier") : QStringLiteral("Fichier"),
            static_cast<qint64>(archive_entry_size(entry)),
            QDateTime::fromSecsSinceEpoch(archive_entry_mtime(entry)).toString(Qt::ISODate)
        });
        archive_read_data_skip(reader.get());
    }

    if (result != ARCHIVE_EOF) {
        error = archiveError(reader.get(), QStringLiteral("Erreur pendant la lecture de l’archive."));
        return false;
    }
    return true;
}

bool addPath(archive* writer, const fs::path& path, const fs::path& base, QString& error)
{
    std::error_code filesystemError;
    const auto relativePath = fs::relative(path, base, filesystemError);
    if (filesystemError) {
        error = QString::fromLocal8Bit(filesystemError.message().c_str());
        return false;
    }

    archive_entry* rawEntry = archive_entry_new();
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(rawEntry, archive_entry_free);
    const std::u8string entryName = relativePath.generic_u8string();
    archive_entry_set_pathname_utf8(entry.get(), reinterpret_cast<const char*>(entryName.c_str()));

    const auto status = fs::symlink_status(path, filesystemError);
    if (filesystemError) {
        error = QString::fromLocal8Bit(filesystemError.message().c_str());
        return false;
    }

    if (fs::is_symlink(status)) {
        error = QStringLiteral("Les liens symboliques ne sont pas encore pris en charge : %1")
                    .arg(QString::fromUtf8(reinterpret_cast<const char*>(entryName.c_str())));
        return false;
    }

    if (fs::is_directory(status)) {
        archive_entry_set_filetype(entry.get(), AE_IFDIR);
        archive_entry_set_perm(entry.get(), 0755);
        archive_entry_set_size(entry.get(), 0);
        if (archive_write_header(writer, entry.get()) != ARCHIVE_OK) {
            error = archiveError(writer, QStringLiteral("Impossible d’écrire l’entrée d’archive."));
            return false;
        }
        for (const auto& child : fs::directory_iterator(path)) {
            if (!addPath(writer, child.path(), base, error)) {
                return false;
            }
        }
        return true;
    }

    if (!fs::is_regular_file(status)) {
        return true;
    }

    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_entry_set_size(entry.get(), static_cast<la_int64_t>(fs::file_size(path)));
    std::error_code timeError;
    const auto modified = fs::last_write_time(path, timeError);
    if (!timeError) {
        const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            modified - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        archive_entry_set_mtime(entry.get(), std::chrono::system_clock::to_time_t(systemTime), 0);
    }

    if (archive_write_header(writer, entry.get()) != ARCHIVE_OK) {
        error = archiveError(writer, QStringLiteral("Impossible d’écrire l’entrée d’archive."));
        return false;
    }

    QFile input(fromFilesystemPath(path));
    if (!input.open(QIODevice::ReadOnly)) {
        error = input.errorString();
        return false;
    }
    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 bytesRead = input.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            error = input.errorString();
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        if (archive_write_data(writer, buffer.constData(), static_cast<size_t>(bytesRead)) < 0) {
            error = archiveError(writer, QStringLiteral("Erreur pendant l’écriture des données."));
            return false;
        }
    }
    return true;
}

bool createArchive(const QString& outputPath, const QStringList& inputPaths, QString& error)
{
    ArchiveWriter writer(archive_write_new(), archive_write_free);
    const QString suffix = QFileInfo(outputPath).suffix().toLower();
    const QString completeSuffix = QFileInfo(outputPath).completeSuffix().toLower();

    if (suffix == QStringLiteral("zip")) {
        archive_write_set_format_zip(writer.get());
    } else if (suffix == QStringLiteral("7z")) {
        archive_write_set_format_7zip(writer.get());
    } else {
        archive_write_set_format_pax_restricted(writer.get());
        if (completeSuffix.endsWith(QStringLiteral("tar.gz")) || suffix == QStringLiteral("tgz")) {
            archive_write_add_filter_gzip(writer.get());
        } else if (completeSuffix.endsWith(QStringLiteral("tar.bz2")) || suffix == QStringLiteral("tbz2")) {
            archive_write_add_filter_bzip2(writer.get());
        } else if (completeSuffix.endsWith(QStringLiteral("tar.xz")) || suffix == QStringLiteral("txz")) {
            archive_write_add_filter_xz(writer.get());
        } else if (completeSuffix.endsWith(QStringLiteral("tar.lzma"))) {
            archive_write_add_filter_lzma(writer.get());
        } else if (suffix == QStringLiteral("tar")) {
            archive_write_add_filter_none(writer.get());
        } else {
            error = QStringLiteral("Format non pris en charge. Choisissez .zip, .7z, .tar, .tar.gz, .tar.bz2, .tar.xz ou .tar.lzma.");
            return false;
        }
    }

#ifdef _WIN32
    const std::wstring nativeOutputPath = outputPath.toStdWString();
    if (archive_write_open_filename_w(writer.get(), nativeOutputPath.c_str()) != ARCHIVE_OK) {
#else
    const QByteArray encodedPath = QFile::encodeName(outputPath);
    if (archive_write_open_filename(writer.get(), encodedPath.constData()) != ARCHIVE_OK) {
#endif
        error = archiveError(writer.get(), QStringLiteral("Impossible de créer l’archive."));
        return false;
    }

    for (const QString& inputPath : inputPaths) {
        const fs::path path = toFilesystemPath(inputPath);
        const fs::path base = path.parent_path().empty() ? fs::current_path() : path.parent_path();
        if (!addPath(writer.get(), path, base, error)) {
            return false;
        }
    }

    if (archive_write_close(writer.get()) != ARCHIVE_OK) {
        error = archiveError(writer.get(), QStringLiteral("Impossible de finaliser l’archive."));
        return false;
    }
    return true;
}

bool extractArchive(const QString& archivePath, const QString& destination, QString& error,
                    qint64& extractedCount, QStringList& extractedPaths,
                    const QString& selectedEntry = {},
                    bool includeDescendants = false)
{
    extractedCount = 0;
    extractedPaths.clear();
    ArchiveReader reader(archive_read_new(), archive_read_free);
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());

#ifdef _WIN32
    const std::wstring nativeArchivePath = archivePath.toStdWString();
    if (archive_read_open_filename_w(reader.get(), nativeArchivePath.c_str(), 10240) != ARCHIVE_OK) {
#else
    const QByteArray encodedArchivePath = QFile::encodeName(archivePath);
    if (archive_read_open_filename(reader.get(), encodedArchivePath.constData(), 10240) != ARCHIVE_OK) {
#endif
        error = archiveError(reader.get(), QStringLiteral("Impossible d’ouvrir l’archive."));
        return false;
    }

    ArchiveWriter diskWriter(archive_write_disk_new(), archive_write_free);
    archive_write_disk_set_options(diskWriter.get(), ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS);

    archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    bool extractedEntry = false;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* rawName = archive_entry_pathname_utf8(entry);
        if (!rawName) rawName = archive_entry_pathname(entry);
        const auto* utf8Name = reinterpret_cast<const char8_t*>(rawName ? rawName : "");
        const fs::path entryPath{std::u8string(utf8Name)};
        if (entryPath.empty() || entryPath.is_absolute() || entryPath.has_root_name() ||
            entryPath.has_root_directory() || std::any_of(entryPath.begin(), entryPath.end(),
                [](const fs::path& component) { return component == ".."; })) {
            error = QStringLiteral("Chemin d’entrée non sûr dans l’archive : %1")
                        .arg(QString::fromLocal8Bit(rawName ? rawName : ""));
            return false;
        }
        if (!selectedEntry.isEmpty()) {
            QString entryName = QString::fromUtf8(rawName ? rawName : "");
            while (entryName.endsWith(QLatin1Char('/'))) entryName.chop(1);
            const bool isSelected = entryName == selectedEntry ||
                (includeDescendants && entryName.startsWith(selectedEntry + QLatin1Char('/')));
            if (!isSelected) {
                if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) {
                    error = archiveError(reader.get(), QStringLiteral("Erreur pendant la lecture de l’archive."));
                    return false;
                }
                continue;
            }
        }
        const fs::path outputPath = toFilesystemPath(destination) / entryPath;
    #ifdef _WIN32
        archive_entry_copy_pathname_w(entry, outputPath.c_str());
    #else
        const std::string outputName = outputPath.string();
        archive_entry_set_pathname(entry, outputName.c_str());
    #endif

        if (archive_write_header(diskWriter.get(), entry) != ARCHIVE_OK) {
            error = archiveError(diskWriter.get(), QStringLiteral("Impossible de créer un fichier extrait."));
            return false;
        }

        const void* block = nullptr;
        size_t blockSize = 0;
        la_int64_t blockOffset = 0;
        while ((result = archive_read_data_block(reader.get(), &block, &blockSize, &blockOffset)) == ARCHIVE_OK) {
            if (archive_write_data_block(diskWriter.get(), block, blockSize, blockOffset) != ARCHIVE_OK) {
                error = archiveError(diskWriter.get(), QStringLiteral("Erreur pendant l’extraction."));
                return false;
            }
        }
        if (result != ARCHIVE_EOF && result != ARCHIVE_OK) {
            error = archiveError(reader.get(), QStringLiteral("Archive endommagée ou illisible."));
            return false;
        }
        if (archive_write_finish_entry(diskWriter.get()) != ARCHIVE_OK) {
            error = archiveError(diskWriter.get(), QStringLiteral("Impossible de finaliser un fichier extrait."));
            return false;
        }
        std::error_code outputError;
        const auto outputStatus = fs::symlink_status(outputPath, outputError);
        if (outputError || !fs::exists(outputStatus)) {
            error = QStringLiteral("L’entrée n’a pas été créée à l’emplacement attendu : %1")
                        .arg(fromFilesystemPath(outputPath));
            return false;
        }
        extractedEntry = true;
        ++extractedCount;
        extractedPaths.push_back(fromFilesystemPath(outputPath));
    }

    if (result != ARCHIVE_EOF) {
        error = archiveError(reader.get(), QStringLiteral("Erreur pendant l’extraction."));
        return false;
    }
    if (!selectedEntry.isEmpty() && !extractedEntry) {
        error = QStringLiteral("L’entrée sélectionnée est introuvable dans l’archive.");
        return false;
    }
    if (archive_write_close(diskWriter.get()) != ARCHIVE_OK) {
        error = archiveError(diskWriter.get(), QStringLiteral("Impossible de finaliser l’extraction."));
        return false;
    }
    return true;
}

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("DCompressor"));
        resize(1040, 680);
        setMinimumSize(760, 520);
        setStyleSheet(QStringLiteral(R"(
            QMainWindow { background: #f3f6f3; color: #1e2923; }
            QToolBar {
                background: #ffffff; border: none; border-bottom: 1px solid #dce5de;
                spacing: 8px; padding: 10px 22px;
            }
            QToolButton {
                background: transparent; border: 1px solid transparent; border-radius: 7px;
                color: #34443a; padding: 8px 12px; font-size: 13px; font-weight: 600;
            }
            QToolButton:hover { background: #f0f5f1; border-color: #e1e9e3; }
            QToolButton:pressed { background: #e5eee7; }
            QToolButton#primaryAction { background: #26734d; color: #ffffff; }
            QToolButton#primaryAction:hover { background: #1f6241; }
            QLabel#productTitle { color: #1c2b22; font-size: 25px; font-weight: 700; }
            QLabel#archiveSubtitle { color: #718078; font-size: 13px; }
            QLabel#archiveSummary {
                background: #e8f2eb; border: 1px solid #d4e6d9; border-radius: 12px;
                color: #286343; padding: 7px 12px; font-size: 12px; font-weight: 600;
            }
            QLabel#emptyIcon { color: #629477; }
            QLabel#emptyTitle { color: #24352b; font-size: 21px; font-weight: 650; }
            QLabel#emptyDescription { color: #718078; font-size: 14px; }
            QPushButton#emptyPrimary {
                background: #26734d; border: none; border-radius: 7px; color: #ffffff;
                padding: 10px 18px; font-size: 13px; font-weight: 600;
            }
            QPushButton#emptyPrimary:hover { background: #1f6241; }
            QPushButton#emptySecondary {
                background: transparent; border: 1px solid #d4dfd7; border-radius: 7px;
                color: #34443a; padding: 10px 18px; font-size: 13px; font-weight: 600;
            }
            QPushButton#emptySecondary:hover { background: #edf2ee; }
            QTableWidget {
                background: #ffffff; alternate-background-color: #f8faf8;
                border: 1px solid #dce5de; border-radius: 8px; color: #29372f;
                font-size: 13px; outline: none; selection-background-color: #e2f0e6;
                selection-color: #1e5136;
            }
            QTableWidget::item { border: none; padding: 9px 12px; }
            QHeaderView::section {
                background: #ffffff; border: none; border-bottom: 1px solid #e4eae5;
                color: #718078; padding: 12px; font-size: 11px; font-weight: 700;
            }
            QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }
            QScrollBar::handle:vertical { background: #cbd7ce; border-radius: 4px; min-height: 28px; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
            QStatusBar { background: #ffffff; border-top: 1px solid #dce5de; color: #68776e; }
        )"));

        auto* toolbar = addToolBar(QStringLiteral("Archives"));
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(18, 18));
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* openButton = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                               QStringLiteral("Ouvrir une archive"));
        toolbar->widgetForAction(openButton)->setObjectName(QStringLiteral("primaryAction"));
        toolbar->addSeparator();
        auto* createButton = toolbar->addAction(style()->standardIcon(QStyle::SP_FileIcon),
                                                 QStringLiteral("Depuis des fichiers"));
        auto* createFolderButton = toolbar->addAction(style()->standardIcon(QStyle::SP_DirIcon),
                                                       QStringLiteral("Depuis un dossier"));
        toolbar->addSeparator();
        extractAction_ = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                             QStringLiteral("Extraire"));
        extractAction_->setEnabled(false);
        extractAction_->setToolTip(QStringLiteral("Extraire la sélection ou toute l’archive"));

        auto* content = new QWidget(this);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(28, 24, 28, 18);
        layout->setSpacing(18);

        auto* heading = new QHBoxLayout();
        auto* headingText = new QVBoxLayout();
        headingText->setSpacing(4);
        title_ = new QLabel(QStringLiteral("Vos archives"), content);
        title_->setObjectName(QStringLiteral("productTitle"));
        subtitle_ = new QLabel(QStringLiteral("Ouvrez une archive ou créez-en une nouvelle."), content);
        subtitle_->setObjectName(QStringLiteral("archiveSubtitle"));
        headingText->addWidget(title_);
        headingText->addWidget(subtitle_);
        heading->addLayout(headingText);
        heading->addStretch();
        summary_ = new QLabel(QStringLiteral("AUCUNE ARCHIVE"), content);
        summary_->setObjectName(QStringLiteral("archiveSummary"));
        heading->addWidget(summary_, 0, Qt::AlignVCenter);
        layout->addLayout(heading);

        emptyState_ = new QWidget(content);
        auto* emptyLayout = new QVBoxLayout(emptyState_);
        emptyLayout->setContentsMargins(20, 34, 20, 34);
        emptyLayout->setSpacing(12);
        emptyLayout->addStretch();
        auto* emptyIcon = new QLabel(emptyState_);
        emptyIcon->setObjectName(QStringLiteral("emptyIcon"));
        emptyIcon->setPixmap(style()->standardIcon(QStyle::SP_DirOpenIcon).pixmap(52, 52));
        emptyIcon->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(emptyIcon);
        auto* emptyTitle = new QLabel(QStringLiteral("Aucune archive ouverte"), emptyState_);
        emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
        emptyTitle->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(emptyTitle);
        auto* emptyDescription = new QLabel(
            QStringLiteral("Parcourez vos fichiers ou composez une archive à partir de vos éléments."), emptyState_);
        emptyDescription->setObjectName(QStringLiteral("emptyDescription"));
        emptyDescription->setAlignment(Qt::AlignCenter);
        emptyLayout->addWidget(emptyDescription);
        auto* emptyActions = new QHBoxLayout();
        emptyActions->setSpacing(10);
        emptyActions->addStretch();
        auto* browseButton = new QPushButton(QStringLiteral("Ouvrir une archive"), emptyState_);
        browseButton->setObjectName(QStringLiteral("emptyPrimary"));
        auto* newArchiveButton = new QPushButton(QStringLiteral("Créer une archive"), emptyState_);
        newArchiveButton->setObjectName(QStringLiteral("emptySecondary"));
        emptyActions->addWidget(browseButton);
        emptyActions->addWidget(newArchiveButton);
        emptyActions->addStretch();
        emptyLayout->addLayout(emptyActions);
        emptyLayout->addStretch();
        layout->addWidget(emptyState_, 1);

        table_ = new QTableWidget(0, 4, content);
        table_->setHorizontalHeaderLabels({QStringLiteral("Nom"), QStringLiteral("Type"),
                                            QStringLiteral("Taille"), QStringLiteral("Modifié le")});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setHighlightSections(false);
        table_->verticalHeader()->setVisible(false);
        table_->verticalHeader()->setDefaultSectionSize(44);
        table_->setShowGrid(false);
        table_->setAlternatingRowColors(true);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setFocusPolicy(Qt::NoFocus);
        table_->hide();
        layout->addWidget(table_, 1);
        setCentralWidget(content);
        statusBar()->showMessage(QStringLiteral("Prêt"));

        connect(openButton, &QAction::triggered, this, [this] { openArchive(); });
        connect(createButton, &QAction::triggered, this, [this] { makeArchive(); });
        connect(createFolderButton, &QAction::triggered, this, [this] { makeArchiveFromFolder(); });
        connect(extractAction_, &QAction::triggered, this, [this] { extractCurrent(); });
        connect(browseButton, &QPushButton::clicked, this, [this] { openArchive(); });
        connect(newArchiveButton, &QPushButton::clicked, this, [this] { makeArchive(); });
    }

private:
    void openArchive()
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir une archive"));
        if (path.isEmpty()) return;
        loadArchive(path);
    }

    void makeArchive()
    {
        const QStringList inputs = QFileDialog::getOpenFileNames(this, QStringLiteral("Ajouter des fichiers"));
        if (inputs.isEmpty()) return;
        const QString output = QFileDialog::getSaveFileName(this, QStringLiteral("Créer une archive"), {},
            QStringLiteral("Archives (*.zip *.7z *.tar *.tar.gz *.tar.bz2 *.tar.xz *.tar.lzma *.tgz *.tbz2 *.txz)"));
        if (output.isEmpty()) return;
        QString error;
        const bool success = createArchive(output, inputs, error);
        showOperationResult(success, success ? QStringLiteral("Archive créée.") : error);
        if (success) loadArchive(output);
    }

    void makeArchiveFromFolder()
    {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Choisir le dossier à archiver"));
        if (folder.isEmpty()) return;
        const QString output = QFileDialog::getSaveFileName(this, QStringLiteral("Créer une archive"), {},
            QStringLiteral("Archives (*.zip *.7z *.tar *.tar.gz *.tar.bz2 *.tar.xz *.tar.lzma *.tgz *.tbz2 *.txz)"));
        if (output.isEmpty()) return;
        QString error;
        const bool success = createArchive(output, {folder}, error);
        showOperationResult(success, success ? QStringLiteral("Archive créée.") : error);
        if (success) loadArchive(output);
    }

    void extractCurrent()
    {
        if (currentArchive_.isEmpty()) {
            showOperationResult(false, QStringLiteral("Ouvrez d’abord une archive."));
            return;
        }
        const QString destination = QFileDialog::getExistingDirectory(this, QStringLiteral("Choisir le dossier de destination"));
        if (destination.isEmpty()) return;
        QString selectedEntry;
        bool includeDescendants = false;
        const int selectedRow = table_->currentRow();
        if (selectedRow >= 0) {
            selectedEntry = table_->item(selectedRow, 0)->text();
            while (selectedEntry.endsWith(QLatin1Char('/'))) selectedEntry.chop(1);
            includeDescendants = table_->item(selectedRow, 1)->text() == QStringLiteral("Dossier");
        }
        QString error;
        qint64 extractedCount = 0;
        QStringList extractedPaths;
        const bool success = extractArchive(currentArchive_, destination, error, extractedCount, extractedPaths,
                                            selectedEntry, includeDescendants);
        if (success) {
            const QString countMessage = extractedCount == 1
                ? QStringLiteral("1 entrée extraite")
                : QStringLiteral("%1 entrées extraites").arg(extractedCount);
            QMessageBox::information(this, QStringLiteral("Extraction terminée"),
                                     QStringLiteral("%1 :\n%2")
                                         .arg(countMessage, extractedPaths.join(QLatin1Char('\n'))));
        } else {
            showOperationResult(false, error);
        }
    }

    void loadArchive(const QString& path)
    {
        std::vector<ArchiveEntry> entries;
        QString error;
        if (!readEntries(path, entries, error)) {
            showOperationResult(false, error);
            return;
        }
        currentArchive_ = path;
        title_->setText(QFileInfo(path).fileName());
        subtitle_->setText(path);
        summary_->setText(QStringLiteral("%1 ENTRÉES").arg(entries.size()));
        emptyState_->hide();
        table_->show();
        extractAction_->setEnabled(true);
        table_->setRowCount(static_cast<int>(entries.size()));
        qint64 totalSize = 0;
        for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
            const auto& entry = entries[static_cast<size_t>(row)];
            table_->setItem(row, 0, new QTableWidgetItem(entry.name));
            table_->setItem(row, 1, new QTableWidgetItem(entry.type));
            table_->setItem(row, 2, new QTableWidgetItem(entry.type == QStringLiteral("Dossier")
                ? QString() : QLocale().formattedDataSize(entry.size)));
            table_->setItem(row, 3, new QTableWidgetItem(entry.modified));
            totalSize += entry.size;
        }
        statusBar()->showMessage(QStringLiteral("%1 entrées, %2 décompressés")
            .arg(entries.size()).arg(QLocale().formattedDataSize(totalSize)));
    }

    void showOperationResult(bool success, const QString& message)
    {
        if (success) {
            statusBar()->showMessage(message, 5000);
        } else {
            QMessageBox::warning(this, QStringLiteral("DCompressor"), message);
        }
    }

    QTableWidget* table_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* summary_ = nullptr;
    QWidget* emptyState_ = nullptr;
    QAction* extractAction_ = nullptr;
    QString currentArchive_;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    MainWindow window;
    window.show();
    return application.exec();
}