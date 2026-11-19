#include "file_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QThread>
#include <QTemporaryDir>
#include <filesystem>
#include <unordered_map>

#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace arachnel::core {
namespace {

void clearReadOnlyAttribute(const QString& filePath)
{
#if defined(Q_OS_WIN)
    SetFileAttributesW(reinterpret_cast<LPCWSTR>(filePath.utf16()), FILE_ATTRIBUTE_NORMAL);
#else
    QFile::setPermissions(filePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                        | QFile::ReadUser | QFile::WriteUser);
#endif
}

bool tryRemoveFile(const QString& path)
{
    clearReadOnlyAttribute(path);
    return QFile::remove(path);
}

bool tryRemoveDirectoryOnce(const QString& path)
{
    // Clear read-only on nested files so removeRecursively can succeed on Windows.
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        clearReadOnlyAttribute(it.next());
    }

    QDir dir(path);
    return dir.removeRecursively();
}

void reportProgress(const FileProgressCallback& onProgress, qint64* copiedInOut, qint64 totalHint,
                    qint64 delta)
{
    if (copiedInOut)
        *copiedInOut += delta;
    if (!onProgress)
        return;
    const qint64 done = copiedInOut ? *copiedInOut : delta;
    const qint64 total = totalHint > 0 ? totalHint : done;
    onProgress(done, total);
}

} // namespace

qint64 pathByteSize(const QString& path)
{
    if (path.isEmpty())
        return 0;
    QFileInfo info(path);
    if (!info.exists())
        return 0;
    if (info.isFile())
        return info.size();

    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

bool removePathRecursive(const QString& path, QString* errorOut)
{
    if (path.isEmpty())
        return true;

    QFileInfo info(path);
    if (!info.exists())
        return true;

    constexpr int kAttempts = 5;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (attempt > 0)
            QThread::msleep(static_cast<unsigned long>(100 * attempt));

        info.refresh();
        if (!info.exists())
            return true;

        const bool ok = info.isFile() ? tryRemoveFile(path) : tryRemoveDirectoryOnce(path);
        if (ok)
            return true;
    }

    if (errorOut) {
        *errorOut = info.isFile()
                        ? QCoreApplication::translate("Core", "Failed to delete file: %1").arg(path)
                        : QCoreApplication::translate("Core", "Failed to delete folder: %1").arg(path);
    }
    return false;
}

bool copyPathRecursive(const QString& src, const QString& dst, QString* errorOut,
                       const FileProgressCallback& onProgress, qint64* copiedInOut, qint64 totalHint)
{
    QFileInfo srcInfo(src);
    if (!srcInfo.exists()) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("Core", "Source not found: %1").arg(src);
        return false;
    }

    if (srcInfo.isFile()) {
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (QFile::exists(dst) && !QFile::remove(dst)) {
            if (errorOut)
                *errorOut = QCoreApplication::translate("Core", "Failed to replace: %1").arg(dst);
            return false;
        }
        if (QFile::copy(src, dst)) {
            reportProgress(onProgress, copiedInOut, totalHint, srcInfo.size());
            return true;
        }
        if (errorOut)
            *errorOut = QCoreApplication::translate("Core", "Failed to copy: %1").arg(src);
        return false;
    }

    QDir srcDir(src);
    QDir dstDir(dst);
    if (!dstDir.exists() && !QDir().mkpath(dst)) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("Core", "Failed to create folder: %1").arg(dst);
        return false;
    }

    const QStringList entries =
        srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    const QString cleanSrc = QDir::cleanPath(src);
    const QString cleanDst = QDir::cleanPath(dst);
    for (const QString& entry : entries) {
        const QString srcPath = srcDir.absoluteFilePath(entry);
        const QString dstPath = dstDir.absoluteFilePath(entry);
        // Never copy a destination that lives inside the source tree into itself.
        if (QDir::cleanPath(srcPath).compare(cleanDst, Qt::CaseInsensitive) == 0)
            continue;
        if (cleanDst.startsWith(cleanSrc + QLatin1Char('/'), Qt::CaseInsensitive)
            && QDir::cleanPath(srcPath)
                   .startsWith(cleanDst + QLatin1Char('/'), Qt::CaseInsensitive))
            continue;
        if (!copyPathRecursive(srcPath, dstPath, errorOut, onProgress, copiedInOut, totalHint))
            return false;
    }
    return true;
}

bool movePathRecursive(const QString& src, const QString& dst, QString* errorOut,
                       const FileProgressCallback& onProgress, QString* cleanupWarning)
{
    namespace fs = std::filesystem;
    auto fail = [&](const QString& error) {
        if (errorOut) *errorOut = error;
        return false;
    };
    const QFileInfo source(src), target(dst);
    if (src.isEmpty() || dst.isEmpty() || !source.isDir() || source.isSymLink() || source.isJunction()
        || target.exists() || target.isSymLink())
        return fail(QCoreApplication::translate("Core", "Missing source or destination already exists: %1").arg(dst));
    const QString root = source.canonicalFilePath();
    if (!QDir().mkpath(target.absolutePath()))
        return fail(QCoreApplication::translate("Core", "Cannot create destination folder: %1").arg(dst));
    const QString parent = QFileInfo(target.absolutePath()).canonicalFilePath();
    const QString rel = QDir(root).relativeFilePath(parent);
    if (root.isEmpty() || parent.isEmpty() || (!QDir::isAbsolutePath(rel)
        && rel != QStringLiteral("..") && !rel.startsWith(QStringLiteral("../"))))
        return fail(QCoreApplication::translate("Core", "Destination is inside the source folder"));
    if (QDir().rename(src, dst))
        return true;

    // Only this staging directory belongs to us on failure. Never merge into or
    // clean up another install, even if the destination appears during the copy.
    QTemporaryDir stage(QDir(parent).filePath(QStringLiteral(".sprout-move-XXXXXX")));
    if (!stage.isValid()) return fail(stage.errorString());
    const QString staged = stage.filePath(QStringLiteral("install"));
    const auto from = source.filesystemAbsoluteFilePath();
    const auto to = QFileInfo(staged).filesystemAbsoluteFilePath();
    try {
        fs::create_directory(to);
        qint64 total = pathByteSize(src), copied = 0;
        size_t copiedEntries = 0;
        std::unordered_map<fs::path, fs::file_time_type> copiedAt;
        for (const auto& entry : fs::recursive_directory_iterator(from)) {
            const auto output = to / entry.path().lexically_relative(from);
#ifdef Q_OS_WIN
            if (QFileInfo(entry.path()).isJunction())
                return fail(QCoreApplication::translate("Core", "Cannot move a directory junction: %1")
                                .arg(QFileInfo(entry.path()).absoluteFilePath()));
#endif
            const auto type = entry.symlink_status();
            if (fs::is_symlink(type)) {
                fs::copy_symlink(entry.path(), output);
            } else if (fs::is_directory(type)) {
                fs::create_directory(output);
            } else if (fs::is_regular_file(type)) {
                const auto timestamp = entry.last_write_time();
                fs::copy_file(entry.path(), output);
                const auto size = entry.file_size();
                if (fs::file_size(output) != size || timestamp != entry.last_write_time())
                    return fail(QCoreApplication::translate("Core", "Incomplete copy: %1").arg(src));
                fs::last_write_time(output, timestamp);
                copiedAt.emplace(entry.path(), timestamp);
                copied += static_cast<qint64>(size);
                if (onProgress) onProgress(copied, total);
            } else {
                return fail(QCoreApplication::translate("Core", "Unsupported file in %1").arg(src));
            }
            ++copiedEntries;
        }
        // Rewalk the source before committing: a save or update during a long
        // copy must not disappear when we remove the original installation.
        size_t verified = 0;
        for (const auto& entry : fs::recursive_directory_iterator(from)) {
            const auto output = to / entry.path().lexically_relative(from);
            const auto type = entry.symlink_status();
            const auto stamp = copiedAt.find(entry.path());
            if (type.type() != fs::symlink_status(output).type()
                || (fs::is_regular_file(type) && (entry.file_size() != fs::file_size(output)
                    || stamp == copiedAt.end() || stamp->second != entry.last_write_time()))
                || (fs::is_symlink(type) && fs::read_symlink(entry.path()) != fs::read_symlink(output)))
                return fail(QCoreApplication::translate("Core", "Game files changed while moving: %1").arg(src));
            ++verified;
        }
        if (verified != copiedEntries)
            return fail(QCoreApplication::translate("Core", "Game files changed while moving: %1").arg(src));
    } catch (const fs::filesystem_error& e) {
        return fail(QString::fromLocal8Bit(e.what()));
    }
    if (!QDir().rename(staged, dst))
        return fail(QCoreApplication::translate("Core", "Cannot publish moved game: %1").arg(dst));
    std::error_code ec;
    fs::remove_all(from, ec);
    if (ec) {
        const QString warning = QCoreApplication::translate("Core", "Game moved, but some files remain in %1").arg(src);
        if (cleanupWarning) *cleanupWarning = warning;
        qWarning().noquote() << warning;
    }
    return true;
}

QString relocatePathPrefix(const QString& path, const QString& oldRoot, const QString& newRoot)
{
    const QString normalizedPath = QDir::fromNativeSeparators(path);
    const QString normalizedOld = QDir::fromNativeSeparators(oldRoot);
    const QString normalizedNew = QDir::fromNativeSeparators(newRoot);

#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (!normalizedOld.isEmpty() && (normalizedPath.compare(normalizedOld, sensitivity) == 0
        || normalizedPath.startsWith(normalizedOld + QLatin1Char('/'), sensitivity)))
        return normalizedNew + normalizedPath.mid(normalizedOld.size());
    return path;
}

bool rewritePathPrefixInFile(const QString& filePath, const QString& oldRoot, const QString& newRoot)
{
    if (filePath.isEmpty() || oldRoot.isEmpty() || !QFileInfo::exists(filePath))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray data = file.readAll();
    file.close();

    const QByteArray oldFwd = QDir::fromNativeSeparators(oldRoot).toUtf8();
    const QByteArray newFwd = QDir::fromNativeSeparators(newRoot).toUtf8();
    const QByteArray oldNat = QDir::toNativeSeparators(oldRoot).toUtf8();
    const QByteArray newNat = QDir::toNativeSeparators(newRoot).toUtf8();

    const QByteArray before = data;
    data.replace(oldFwd, newFwd);
    if (oldNat != oldFwd)
        data.replace(oldNat, newNat);
    if (data == before)
        return true;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(data);
    return true;
}

int peImageBits(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0x40)
        return 0;

    if (!file.seek(0x3C))
        return 0;
    const QByteArray lfanew = file.read(4);
    if (lfanew.size() < 4)
        return 0;
    const quint32 peOffset = quint32(static_cast<uchar>(lfanew.at(0)))
                             | (quint32(static_cast<uchar>(lfanew.at(1))) << 8)
                             | (quint32(static_cast<uchar>(lfanew.at(2))) << 16)
                             | (quint32(static_cast<uchar>(lfanew.at(3))) << 24);

    if (peOffset < 0x40 || peOffset + 6 > quint32(file.size()) || !file.seek(peOffset))
        return 0;
    if (file.read(4) != QByteArray("PE\0\0", 4))
        return 0;

    const QByteArray machine = file.read(2);
    if (machine.size() < 2)
        return 0;
    const quint16 machineType = quint16(static_cast<uchar>(machine.at(0)))
                                | (quint16(static_cast<uchar>(machine.at(1))) << 8);
    if (machineType == 0x8664) // IMAGE_FILE_MACHINE_AMD64
        return 64;
    if (machineType == 0x14c) // IMAGE_FILE_MACHINE_I386
        return 32;
    return 0;
}

QByteArray firstJsonObject(const QByteArray& data)
{
    const int start = data.indexOf('{');
    if (start < 0)
        return {};
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (int i = start; i < data.size(); ++i) {
        const char c = data.at(i);
        if (inString) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
            inString = true;
        else if (c == '{')
            ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0)
                return data.mid(start, i - start + 1);
        }
    }
    return {};
}

bool healUnityScriptingAssembliesFile(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    file.close();
    if (raw.trimmed().isEmpty())
        return false;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    const bool leftoverBytes = parseError.error != QJsonParseError::NoError || !doc.isObject();
    if (leftoverBytes) {
        const QByteArray first = firstJsonObject(raw);
        if (first.isEmpty())
            return false;
        doc = QJsonDocument::fromJson(first, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return false;
    }

    QJsonObject root = doc.object();
    QJsonArray names = root.value(QStringLiteral("names")).toArray();
    const QJsonArray types = root.value(QStringLiteral("types")).toArray();
    if (names.isEmpty() || names.size() != types.size())
        return false;

    const QDir managed(QFileInfo(jsonPath).absolutePath());
    const bool hasWin64 =
        QFileInfo::exists(managed.filePath(QStringLiteral("Facepunch.Steamworks.Win64.dll")));
    bool swappedSteamworks = false;
    if (hasWin64) {
        for (int i = 0; i < names.size(); ++i) {
            if (names.at(i).toString() == QLatin1String("Facepunch.Steamworks.Posix.dll")) {
                names[i] = QStringLiteral("Facepunch.Steamworks.Win64.dll");
                swappedSteamworks = true;
            }
        }
        if (swappedSteamworks)
            root.insert(QStringLiteral("names"), names);
    }

    if (!leftoverBytes && !swappedSteamworks)
        return false;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

int healUnityScriptingAssemblies(const QString& installPath)
{
    if (installPath.isEmpty() || !QFileInfo::exists(installPath))
        return 0;

    int repaired = 0;
    QDirIterator it(installPath, {QStringLiteral("ScriptingAssemblies.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    int seen = 0;
    while (it.hasNext() && seen < 32) {
        it.next();
        ++seen;
        const QString rel = QDir(installPath).relativeFilePath(it.filePath());
        if (rel.count(QLatin1Char('/')) + rel.count(QLatin1Char('\\')) > 4)
            continue;
        if (healUnityScriptingAssembliesFile(it.filePath()))
            ++repaired;
    }
    return repaired;
}

#ifndef Q_OS_WIN
namespace {
// Both paths are on the installation filesystem. POSIX rename replaces a
// regular file atomically; if it fails, both files are still there.
bool mergeLayoutEntry(const QString& src, const QString& dst)
{
    const QFileInfo from(src), to(dst);
    if (to.isSymLink() || from.isSymLink())
        return false;
    if (to.exists()) {
        if (from.isDir() && to.isDir()) {
            const QDir dir(src);
            const auto entries = dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                | QDir::NoDotAndDotDot);
            for (const QString& name : entries)
                mergeLayoutEntry(dir.filePath(name), QDir(dst).filePath(name));
            return QDir().rmdir(src); // succeeds only if every child was moved
        }
        if (!from.isFile() || !to.isFile() || from.lastModified() <= to.lastModified())
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(from.filesystemFilePath(), to.filesystemFilePath(), ec);
    return !ec;
}
}
#endif

int healWindowsInstallLayout(const QString& installPath)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(installPath);
    return 0;
#else
    if (installPath.isEmpty() || !QFileInfo::exists(installPath))
        return 0;

    QStringList broken;
    QDirIterator it(installPath, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System
                                     | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileName().contains(QLatin1Char('\\')))
            broken.append(it.filePath());
    }
    std::sort(broken.begin(), broken.end(), [](const QString& a, const QString& b) {
        return a.size() > b.size();
    });

    int moved = 0;
    for (const QString& src : broken) {
        const QFileInfo info(src);
        if (!info.exists())
            continue;
        QStringList parts = info.fileName().split(QLatin1Char('\\'));
        if (parts.contains(QString()) || parts.contains(QStringLiteral(".."))
            || parts.contains(QStringLiteral(".")))
            continue;
        const QString leaf = parts.takeLast();
        QDir parent = info.dir();
        bool valid = true;
        for (const QString& part : parts) {
            if (QFileInfo(parent.filePath(part)).isSymLink()
                || (!parent.exists(part) && !parent.mkdir(part)) || !parent.cd(part)) {
                valid = false;
                break;
            }
        }
        if (valid && mergeLayoutEntry(src, parent.filePath(leaf)))
            ++moved;
    }

    const QString marker = QDir(installPath).filePath(QStringLiteral(".arachnel-steamidra"));
    if (QFileInfo::exists(marker)) {
        QFile file(marker);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            file.close();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                QJsonObject launch = root.value(QStringLiteral("launch")).toObject();
                QString path = launch.value(QStringLiteral("path")).toString();
                if (path.contains(QLatin1Char('\\'))) {
                    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
                    launch.insert(QStringLiteral("path"), path);
                    root.insert(QStringLiteral("launch"), launch);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                        file.close();
                    }
                }
            } else {
                QByteArray raw = data;
                raw.replace(QByteArray("\\\\"), QByteArray("/"));
                raw.replace('\\', '/');
                if (raw != data && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    file.write(raw);
                    file.close();
                }
            }
        }
    }
    return moved;
#endif
}

bool writeTextFile(const QString& path, const QString& text, QString* errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(text.toUtf8()) < 0) {
        const QString why = QStringLiteral("%1: %2").arg(path, file.errorString());
        qWarning().noquote() << "cannot write" << why;
        if (errorOut)
            *errorOut = why;
        return false;
    }
    return true;
}

} // namespace arachnel::core
