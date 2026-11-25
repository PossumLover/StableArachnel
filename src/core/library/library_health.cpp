#include "library_health.h"

#include "install_heuristics.h"
#include "library_model.h"
#include "online_fix_overlay.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVariantMap>
#include <QtEndian>

#if defined(Q_OS_WIN)
#include <filesystem>
#endif

namespace arachnel::core {
namespace {

constexpr int kDirectoryBudget = 4000;
constexpr int kSizeBudget = 100000;
constexpr int kPakHits = 3;
constexpr qint64 kInventoryBytes = 32 * 1024 * 1024;
constexpr qint64 kLaunchMarkerBytes = 1024 * 1024;

QJsonObject readObject(const QString& path, qint64 limit)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > limit)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.read(limit + 1);
    return bytes.size() <= limit ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
}

QString launchExecutable(const LibraryHealthInput& input, const QString& markerPath)
{
    const QDir root(input.installPath);
    const auto usablePath = [&root](const QString& path) -> QString {
        if (path.trimmed().isEmpty())
            return {};
        const QFileInfo info(root.absoluteFilePath(path.trimmed()));
        if (!info.isFile() || isExcludedGameExecutable(info.fileName()))
            return {};
        return info.absoluteFilePath();
    };
    const QString override = usablePath(input.executableOverride);
    if (!override.isEmpty())
        return override;

    // launchInfo can repair the install. The recorded launch targets let this check stay read-only.
    const QJsonObject marker = readObject(markerPath, kLaunchMarkerBytes);
    auto recordedOptions = input.launchOptions;
    const QJsonArray markerOptions = recordedOptions.isEmpty()
        ? marker.value(QStringLiteral("launchOptions")).toArray() : QJsonArray{};
    for (const QJsonValue& entry : markerOptions) {
        const QJsonObject option = entry.toObject();
        QString path = option.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            path = option.value(QStringLiteral("executable")).toString();
        recordedOptions.append({option.value(QStringLiteral("id")).toString(), path,
                                option.value(QStringLiteral("isDefault")).toBool()});
    }
    QString firstOption;
    QString defaultOption;
    for (const auto& option : recordedOptions) {
        const QString path = usablePath(option.executable);
        if (path.isEmpty())
            continue;
        if (!input.launchOptionId.isEmpty() && option.id == input.launchOptionId)
            return path;
        if (firstOption.isEmpty())
            firstOption = path;
        if (defaultOption.isEmpty() && option.isDefault)
            defaultOption = path;
    }
    const QJsonObject launch = marker.value(QStringLiteral("launch")).toObject();
    const QString launchType = launch.value(QStringLiteral("type")).toString();
    if (launchType != QStringLiteral("steam") && launchType != QStringLiteral("missing")) {
        const QString path = usablePath(launch.value(QStringLiteral("path")).toString());
        if (!path.isEmpty())
            return path;
    }
    if (!defaultOption.isEmpty())
        return defaultOption;
    if (!firstOption.isEmpty())
        return firstOption;
    return findGameExecutableInTree(input.installPath, input.title, kDirectoryBudget);
}

bool isPeExecutable(const QString& path)
{
    if (!path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(64);
    if (header.size() != 64 || !header.startsWith("MZ"))
        return false;
    const quint32 offset = qFromLittleEndian<quint32>(header.constData() + 0x3c);
    return offset >= 64 && offset <= file.size() - 4 && file.seek(offset)
        && file.read(4) == QByteArray("PE\0\0", 4);
}

bool isElfBinary(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(18);
    if (header.size() != 18 || !header.startsWith("\x7f" "ELF")
        || (header[4] != 1 && header[4] != 2) || (header[5] != 1 && header[5] != 2))
        return false;
    const auto* type = header.constData() + 16;
    const quint16 value = header[5] == 1 ? qFromLittleEndian<quint16>(type)
                                        : qFromBigEndian<quint16>(type);
    return value == 2 || value == 3;  // ET_EXEC or ET_DYN
}

bool hasMixedFiles(const QList<QFileInfo>& entries)
{
    QSet<QString> windowsStems;
    for (const QFileInfo& entry : entries) {
        const QString suffix = entry.suffix().toLower();
        if (suffix == QStringLiteral("app") && entry.isDir())
            return true;
        if ((suffix == QStringLiteral("exe") || suffix == QStringLiteral("dll")) && entry.isFile())
            windowsStems.insert(entry.completeBaseName().toCaseFolded());
    }
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName().toCaseFolded();
        if (name.endsWith(QStringLiteral(".exe")) || name.endsWith(QStringLiteral(".dll")))
            continue;
        const qsizetype so = name.indexOf(QStringLiteral(".so."));
        const QString stem = so < 0 ? entry.completeBaseName().toCaseFolded() : name.left(so);
        if ((windowsStems.contains(stem) || windowsStems.contains(name))
            && entry.isFile() && isElfBinary(entry.absoluteFilePath()))
            return true;
    }
    return false;
}

QList<QFileInfo> listEntries(const QString& directory, int& budget)
{
    QList<QFileInfo> entries;
    QDirIterator it(directory, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                   | QDir::Hidden | QDir::NoSymLinks);
    while (budget > 0 && it.hasNext()) {
        it.next();
        entries.append(it.fileInfo());
        --budget;
    }
    return entries;
}

QString inventoryPath(const QJsonObject& entry)
{
    const QString path = entry.value(QStringLiteral("path")).toString();
    // The inventory only names files inside the install, even on Windows.
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char(':'))
        || path.contains(QLatin1Char('\\')) || path.contains(QChar::Null)
        || QDir::cleanPath(path) != path || path == QStringLiteral(".")
        || path == QStringLiteral("..") || path.startsWith(QStringLiteral("../")))
        return {};
    return path;
}

bool isPackageFile(const QString& path)
{
    return path.endsWith(QStringLiteral(".pak"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".ucas"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".utoc"), Qt::CaseInsensitive);
}

void addIssue(QVariantList& issues, const QString& code, bool verify = true,
              int count = 0, bool minimum = false)
{
    issues.append(QVariantMap{{QStringLiteral("code"), code},
                              {QStringLiteral("count"), count},
                              {QStringLiteral("minimum"), minimum},
                              {QStringLiteral("verify"), verify}});
}

} // namespace

void queueLibraryHealthSync(const QList<LibraryGame>& games,
                            QHash<QString, LibraryHealthCheckMode>& pending)
{
    // In-place writes and changes in nested directories may leave the cache stamps unchanged.
    for (const auto& game : games) {
        if (!game.installPath.isEmpty())
            pending.insert(game.id, LibraryHealthCheckMode::Refresh);
    }
}

LibraryHealthResult checkLibraryHealth(const LibraryHealthInput& input,
                                       const LibraryHealthResult& cached, LibraryHealthCheckMode mode)
{
    LibraryHealthResult result;
    result.input = input;
    if (input.installPath.isEmpty() || !QFileInfo(input.installPath).isDir())
        return result;

    const QDir root(input.installPath);
    const QString inventory = root.filePath(QStringLiteral(".arachnel-steamidra-files"));
    const QString marker = root.filePath(QStringLiteral(".arachnel-steamidra"));
    result.installMtime = QFileInfo(input.installPath).lastModified();
    result.inventoryMtime = QFileInfo(inventory).lastModified();
    result.launchMarkerMtime = QFileInfo(marker).lastModified();
    if (mode == LibraryHealthCheckMode::UseCache && result.input == cached.input
        && result.installMtime.isValid()
        && result.installMtime == cached.installMtime && result.inventoryMtime == cached.inventoryMtime
        && result.launchMarkerMtime == cached.launchMarkerMtime)
        return cached;

    const QString executable = launchExecutable(input, marker);
    const bool windows = isPeExecutable(executable);
    const QJsonObject manifest = readObject(inventory, kInventoryBytes);
    const QJsonValue depots = manifest.value(QStringLiteral("depots"));
    const bool hasInventory = manifest.value(QStringLiteral("version")).toInt() == 1
        && depots.isArray();

    int budget = kDirectoryBudget;
    const auto rootEntries = windows || hasInventory ? listEntries(input.installPath, budget)
                                                     : QList<QFileInfo>{};
    bool mixed = windows && hasMixedFiles(rootEntries);
    QStringList pakDirs;
    for (const QFileInfo& entry : rootEntries) {
        if (!entry.isDir())
            continue;
        if (windows && !mixed && budget > 0)
            mixed = hasMixedFiles(listEntries(entry.absoluteFilePath(), budget));
        if (hasInventory) {
            const QString paks = entry.absoluteFilePath() + QStringLiteral("/Content/Paks");
            if (QFileInfo(paks).isDir())
                pakDirs.append(paks);
        }
    }
    if (mixed)
        addIssue(result.issues, QStringLiteral("mixed-os"));
    if (onlineFixMissingLoader(scanFixLayout(input.installPath, kDirectoryBudget), executable))
        addIssue(result.issues, QStringLiteral("online-fix-no-loader"), false);
    if (!hasInventory)
        return result;

    QSet<QString> shipped;
#if defined(Q_OS_WIN)
    QMultiHash<QString, QString> shippedPaksByCase;
#endif
    bool wrongSize = false;
    int sizesLeft = kSizeBudget;
    for (const QJsonValue& depot : depots.toArray()) {
        const auto files = depot.toObject().value(QStringLiteral("files")).toArray();
        for (const QJsonValue& value : files) {
            const QJsonObject entry = value.toObject();
            const QString path = inventoryPath(entry);
            if (path.isEmpty() || entry.value(QStringLiteral("dir")).toBool())
                continue;
            if (!pakDirs.isEmpty()) {
                shipped.insert(path);
#if defined(Q_OS_WIN)
                if (isPackageFile(path))
                    shippedPaksByCase.insert(path.toCaseFolded(), path);
#endif
            }
            const qint64 expected = entry.value(QStringLiteral("size")).toInteger(-1);
            if (!wrongSize && expected >= 0 && sizesLeft > 0) {
                --sizesLeft;
                const QFileInfo actual(root.filePath(path));
                wrongSize = actual.isFile() && !actual.isSymLink() && actual.size() != expected;
            }
            if (pakDirs.isEmpty() && (wrongSize || sizesLeft == 0))
                break;
        }
        if (pakDirs.isEmpty() && (wrongSize || sizesLeft == 0))
            break;
    }
    if (wrongSize)
        addIssue(result.issues, QStringLiteral("wrong-size"));

    const auto isShipped = [&](const QFileInfo& entry) {
        const QString path = root.relativeFilePath(entry.absoluteFilePath());
        if (shipped.contains(path))
            return true;
#if defined(Q_OS_WIN)
        // Windows directories can opt into case-sensitive lookup. Ask the filesystem
        // whether a case-only mismatch actually resolves to the enumerated file.
        const QString key = path.toCaseFolded();
        const auto end = shippedPaksByCase.constEnd();
        for (auto it = shippedPaksByCase.constFind(key); it != end && it.key() == key; ++it) {
            std::error_code error;
            if (std::filesystem::equivalent(std::filesystem::path(root.filePath(it.value()).toStdWString()),
                                            std::filesystem::path(entry.absoluteFilePath().toStdWString()),
                                            error))
                return true;
        }
#endif
        return false;
    };

    int stale = 0;
    int pakBudget = kDirectoryBudget;
    for (const QString& dir : pakDirs) {
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                 | QDir::NoSymLinks | QDir::Hidden);
        while (pakBudget > 0 && stale < kPakHits && it.hasNext()) {
            const QFileInfo entry(it.next());
            --pakBudget;
            if (isPackageFile(entry.fileName()) && entry.isFile() && !isShipped(entry))
                ++stale;
        }
        if (pakBudget == 0 || stale == kPakHits)
            break;
    }
    if (stale > 0)
        addIssue(result.issues, QStringLiteral("stale-paks"), true, stale,
                 stale == kPakHits || pakBudget == 0);
    return result;
}

} // namespace arachnel::core
