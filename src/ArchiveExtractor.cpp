#include "ArchiveExtractor.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFileInfo>

namespace {

bool copyData(archive *source, archive *target, QString *errorMessage)
{
    const void *buffer = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;

    while (true) {
        const int readResult = archive_read_data_block(source, &buffer, &size, &offset);
        if (readResult == ARCHIVE_EOF) {
            return true;
        }
        if (readResult != ARCHIVE_OK) {
            if (errorMessage) {
                *errorMessage = QString::fromLocal8Bit(archive_error_string(source));
            }
            return false;
        }

        const int writeResult = archive_write_data_block(target, buffer, size, offset);
        if (writeResult != ARCHIVE_OK) {
            if (errorMessage) {
                *errorMessage = QString::fromLocal8Bit(archive_error_string(target));
            }
            return false;
        }
    }
}

QString safeDestinationPath(const QString &destinationPath, const QString &entryPath)
{
    const QString normalizedEntry = QDir::cleanPath(entryPath);
    if (normalizedEntry.startsWith("../") || normalizedEntry == ".." || QFileInfo(normalizedEntry).isAbsolute()) {
        return {};
    }

    const QString fullPath = QDir(destinationPath).filePath(normalizedEntry);
    const QString canonicalDestination = QDir(destinationPath).canonicalPath();
    const QString targetParent = QFileInfo(fullPath).absoluteDir().absolutePath();
    QDir().mkpath(targetParent);

    const QString cleanedFullPath = QDir::cleanPath(fullPath);
    if (!cleanedFullPath.startsWith(canonicalDestination + QDir::separator())) {
        return {};
    }

    return cleanedFullPath;
}

} // namespace

bool ArchiveExtractor::extractArchive(const QString &archivePath, const QString &destinationPath, QString *errorMessage)
{
    QDir destination(destinationPath);
    if (!destination.exists() && !destination.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create destination directory: %1").arg(destinationPath);
        }
        return false;
    }

    archive *reader = archive_read_new();
    archive *writer = archive_write_disk_new();
    if (!reader || !writer) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot initialize archive reader/writer");
        }
        archive_read_free(reader);
        archive_write_free(writer);
        return false;
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    archive_write_disk_set_standard_lookup(writer);

    const QByteArray archivePathBytes = QFile::encodeName(archivePath);
    if (archive_read_open_filename(reader, archivePathBytes.constData(), 10240) != ARCHIVE_OK) {
        if (errorMessage) {
            *errorMessage = QString::fromLocal8Bit(archive_error_string(reader));
        }
        archive_read_free(reader);
        archive_write_free(writer);
        return false;
    }

    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const QString entryPath = QString::fromUtf8(archive_entry_pathname(entry));
        const QString outputPath = safeDestinationPath(destinationPath, entryPath);
        if (outputPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unsafe path inside archive: %1").arg(entryPath);
            }
            archive_read_close(reader);
            archive_read_free(reader);
            archive_write_free(writer);
            return false;
        }

        const QByteArray outputPathBytes = QFile::encodeName(outputPath);
        archive_entry_set_pathname(entry, outputPathBytes.constData());

        int result = archive_write_header(writer, entry);
        if (result != ARCHIVE_OK) {
            if (archive_entry_size(entry) == 0) {
                archive_read_data_skip(reader);
                continue;
            }
            if (errorMessage) {
                *errorMessage = QString::fromLocal8Bit(archive_error_string(writer));
            }
            archive_read_close(reader);
            archive_read_free(reader);
            archive_write_free(writer);
            return false;
        }

        if (archive_entry_size(entry) > 0 && !copyData(reader, writer, errorMessage)) {
            archive_read_close(reader);
            archive_read_free(reader);
            archive_write_free(writer);
            return false;
        }

        result = archive_write_finish_entry(writer);
        if (result != ARCHIVE_OK) {
            if (errorMessage) {
                *errorMessage = QString::fromLocal8Bit(archive_error_string(writer));
            }
            archive_read_close(reader);
            archive_read_free(reader);
            archive_write_free(writer);
            return false;
        }
    }

    archive_read_close(reader);
    archive_read_free(reader);
    archive_write_free(writer);
    return true;
}

bool ArchiveExtractor::extractTarXz(const QString &archivePath, const QString &destinationPath, QString *errorMessage)
{
    return extractArchive(archivePath, destinationPath, errorMessage);
}
