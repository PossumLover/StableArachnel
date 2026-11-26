#include "online_fix_package.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace arachnel::core {
namespace {

QString trPackage(const char* message)
{
    return QCoreApplication::translate("OnlineFixPackage", message);
}

QString backupPath(QString install)
{
#ifdef Q_OS_WIN
    install = install.toCaseFolded();
#endif
    const auto key = QCryptographicHash::hash(install.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/backups/online-fix-packages/") + QString::fromLatin1(key);
}

bool safeRelative(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(u'\\') || path.contains(u':'))
        return false;
    for (const QString& part : path.split(u'/')) {
        if (part.isEmpty() || part.startsWith(u'.') || part.endsWith(u'.') || part.endsWith(u' ')
            || part.endsWith(QStringLiteral(".arachnel-off"), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

bool linked(const QFileInfo& item)
{
    return item.isSymLink() || item.isJunction();
}

bool safeTarget(const QDir& root, const QString& relative)
{
    if (!safeRelative(relative))
        return false;
    QString path = root.path();
    for (const QString& part : relative.split(u'/')) {
        path += u'/' + part;
        if (linked(QFileInfo(path)))
            return false;
    }
    return true;
}

bool containsPath(const QDir& root, const QString& absolute)
{
    const QString rel = root.relativeFilePath(absolute);
    return !QDir::isAbsolutePath(rel) && rel != QStringLiteral("..")
        && !rel.startsWith(QStringLiteral("../"));
}

struct Package {
    QStringList files;
    QStringList directories;
    QString ini;
    QSet<QString> names;
};

QString collectFiles(const QDir& root, const QDir& dir, Package& package)
{
    if (!dir.isReadable())
        return trPackage("Cannot read folder %1.").arg(dir.path());
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                           | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString rel = root.relativeFilePath(entry.absoluteFilePath());
        if (!safeRelative(rel) || linked(entry) || package.names.contains(rel.toCaseFolded()))
            return trPackage("Unsupported or ambiguous package path: %1").arg(rel);
        package.names.insert(rel.toCaseFolded());
        if (entry.isDir()) {
            package.directories.append(rel);
            const auto error = collectFiles(root, QDir(entry.absoluteFilePath()), package);
            if (!error.isEmpty())
                return error;
        } else if (entry.isFile()) {
            package.files.append(rel);
            if (entry.fileName().compare(QStringLiteral("OnlineFix.ini"), Qt::CaseInsensitive) == 0) {
                if (!package.ini.isEmpty())
                    return trPackage("Choose a folder containing just one Online Fix package.");
                package.ini = rel;
            }
        } else {
            return trPackage("Unsupported file in package: %1").arg(rel);
        }
    }
    return {};
}

QString validatePackage(const Package& package, const QDir& source, const QDir& install,
                        const QString& steamAppId)
{
    if (package.ini.isEmpty())
        return trPackage("The selected folder contains no OnlineFix.ini.");
    const QDir layer = QFileInfo(source.filePath(package.ini)).dir();
    const QDir target = QFileInfo(install.filePath(package.ini)).dir();
    if (!target.exists())
        return trPackage("The package layout does not match the installed game folder.");
    QFile list(layer.filePath(QStringLiteral("dlllist.txt")));
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text))
        return trPackage("The package is missing a readable dlllist.txt beside OnlineFix.ini.");
    bool hasCore = false;
    while (!list.atEnd()) {
        const QString name = QString::fromUtf8(list.readLine()).trimmed();
        if (name.isEmpty())
            continue;
        if (!safeRelative(name) || name.contains(u'/') || !QFileInfo(layer.filePath(name)).isFile())
            return trPackage("Missing or invalid DLL in dlllist.txt: %1").arg(name);
        static const QRegularExpression core(QStringLiteral("^OnlineFix(?:32|64)?\\.dll$"),
                                              QRegularExpression::CaseInsensitiveOption);
        hasCore |= core.match(name).hasMatch();
    }
    if (!hasCore || !QFileInfo(layer.filePath(QStringLiteral("winmm.dll"))).isFile())
        return trPackage("The package needs its Online Fix DLL and winmm.dll loader.");

    QSettings incoming(source.filePath(package.ini), QSettings::IniFormat);
    QSettings installed(install.filePath(package.ini), QSettings::IniFormat);
    incoming.setFallbacksEnabled(false);
    installed.setFallbacksEnabled(false);
    const QString steamKey = QStringLiteral("Main/RealAppId");
    const QString gdkKey = QStringLiteral("Main/ProductId");
    const QString app = incoming.value(steamKey).toString().trimmed();
    const QString product = incoming.value(gdkKey).toString().trimmed();
    const QString currentApp = installed.value(steamKey).toString().trimmed();
    const QString currentProduct = installed.value(gdkKey).toString().trimmed();
    if (!product.isEmpty() && app.isEmpty()) {
        if (currentProduct.compare(product, Qt::CaseInsensitive) != 0 || !currentApp.isEmpty())
            return trPackage("This GDK update requires an installed OnlineFix.ini with ProductId %1 at the same path.").arg(product);
    } else {
        static const QRegularExpression numeric(QStringLiteral("^[1-9][0-9]*$"));
        if (!numeric.match(app).hasMatch() || !product.isEmpty() || !currentProduct.isEmpty()
            || (!currentApp.isEmpty() && currentApp != app)
            || (steamAppId.isEmpty() ? currentApp != app : steamAppId != app))
            return trPackage("The package's Steam App ID could not be matched to this installation.");
    }
    if (!target.entryList({QStringLiteral("*.arachnel-off"), QStringLiteral("SteamFix*.dll"),
                           QStringLiteral("unsteam.dll")}, QDir::Files).isEmpty())
        return trPackage("Restore the active Online Fix layout before importing a package here.");
    return {};
}

bool copyAtomically(const QString& from, const QString& to)
{
    QFile input(from);
    QSaveFile output(to);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return false;
    char buf[64 * 1024];
    qint64 count;
    while ((count = input.read(buf, sizeof(buf))) > 0) {
        if (output.write(buf, count) != count)
            return false;
    }
    return count == 0 && output.setPermissions(input.permissions()) && output.commit();
}

// The journal is published before the first game file changes. Replaying it is idempotent.
QString undo(const QDir& install, const QDir& backup, const QJsonArray& files,
             const QJsonArray& directories, int count)
{
    for (int i = 0; i < count; ++i) {
        const auto item = files.at(i).toObject();
        const QString rel = item.value(QStringLiteral("path")).toString();
        const QString target = install.filePath(rel);
        const auto original = item.value(QStringLiteral("original"));
        if (!safeTarget(install, rel) || !original.isBool()
            || (QFileInfo::exists(target) && !QFileInfo(target).isFile()))
            return trPackage("Invalid restore path: %1").arg(rel);
        if (QFileInfo::exists(target + QStringLiteral(".arachnel-off")))
            return trPackage("Enable Online Fix again before restoring %1.").arg(rel);
        if (original.toBool() && !QFileInfo(backup.filePath(QString::number(i))).isReadable())
            return trPackage("Cannot read the backup of %1.").arg(rel);
    }
    for (int i = count - 1; i >= 0; --i) {
        const auto item = files.at(i).toObject();
        const QString rel = item.value(QStringLiteral("path")).toString();
        const QString target = install.filePath(rel);
        if (item.value(QStringLiteral("original")).toBool()) {
            if (!QDir().mkpath(QFileInfo(target).absolutePath())
                || !copyAtomically(backup.filePath(QString::number(i)), target))
                return trPackage("Could not restore %1.").arg(rel);
        } else if (QFileInfo::exists(target) && !QFile::remove(target)) {
            return trPackage("Could not remove imported file %1.").arg(rel);
        }
    }
    for (auto i = directories.size(); i > 0; --i) {
        const QString rel = directories.at(i - 1).toString();
        if (safeTarget(install, rel))
            install.rmdir(rel); // Preserve any files added later.
    }
    return {};
}

QString finishRestore(const QDir& backup)
{
    // Detach the journal atomically before cleaning it up, so a retry never sees half a backup.
    QTemporaryDir trash(backup.path() + QStringLiteral("-restored-XXXXXX"));
    if (!trash.isValid() || !QDir().rename(backup.path(), trash.filePath(QStringLiteral("backup"))))
        return trPackage("Files restored. Retry Restore to retire the backup at %1.").arg(backup.path());
    return {};
}

} // namespace

bool hasOnlineFixPackageBackup(const QString& installPath)
{
    const QString root = QFileInfo(installPath).canonicalFilePath();
    return !installPath.isEmpty() && !root.isEmpty() && QFileInfo(backupPath(root)).isDir();
}

QString importOnlineFixPackage(const QString& installPath, const QString& steamAppId,
                               const QString& packagePath)
{
    if (installPath.isEmpty() || packagePath.isEmpty() || !QFileInfo(installPath).isDir()
        || !QFileInfo(packagePath).isDir())
        return trPackage("Choose an extracted package folder and an installed game.");
    const QDir install(QFileInfo(installPath).canonicalFilePath());
    const QDir source(QFileInfo(packagePath).canonicalFilePath());
    const QDir backup(backupPath(install.path()));
    if (containsPath(install, source.path()) || containsPath(source, install.path())
        || containsPath(install, backup.path()) || containsPath(source, backup.path()))
        return trPackage("Choose separate game and package folders.");
    if (!QDir().mkpath(QFileInfo(backup.path()).absolutePath()))
        return trPackage("Cannot create the backup folder.");
    QLockFile lock(backup.path() + QStringLiteral(".lock"));
    if (!lock.tryLock())
        return trPackage("Another package operation is in progress.");
    if (backup.exists())
        return trPackage("Restore the previous import before applying another package.");

    Package package;
    QString error = collectFiles(source, source, package);
    if (!error.isEmpty())
        return error;
    for (const QString& rel : package.files) {
        const QFileInfo target(install.filePath(rel));
        if (!safeTarget(install, rel) || (target.exists() && !target.isFile()))
            return trPackage("Cannot replace this game path: %1").arg(rel);
    }
    error = validatePackage(package, source, install, steamAppId);
    if (!error.isEmpty())
        return error;

    QJsonArray directories;
    for (const QString& rel : package.directories) {
        const QFileInfo target(install.filePath(rel));
        if (!safeTarget(install, rel) || (target.exists() && !target.isDir()))
            return trPackage("Cannot use this game folder: %1").arg(rel);
        if (!target.exists())
            directories.append(rel);
    }
    QTemporaryDir staging(backup.path() + QStringLiteral("-staging-XXXXXX"));
    if (!staging.isValid())
        return trPackage("Cannot create the backup folder.");
    QJsonArray files;
    for (const QString& rel : package.files) {
        const QString target = install.filePath(rel);
        const bool original = QFileInfo::exists(target);
        if (original && !QFile::copy(target, staging.filePath(QString::number(files.size()))))
            return trPackage("Could not back up %1.").arg(rel);
        files.append(QJsonObject{{QStringLiteral("path"), rel}, {QStringLiteral("original"), original}});
    }
    const QByteArray journal = QJsonDocument(QJsonObject{{QStringLiteral("files"), files},
        {QStringLiteral("directories"), directories}}).toJson();
    QSaveFile manifest(staging.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(journal) != journal.size()
        || !manifest.commit() || !QDir().rename(staging.path(), backup.path()))
        return trPackage("Could not save the package recovery journal.");
    staging.setAutoRemove(false);

    int written = 0;
    for (const auto& dir : directories) {
        if (!install.mkdir(dir.toString())) {
            error = trPackage("Could not create %1.").arg(dir.toString());
            break;
        }
    }
    if (error.isEmpty()) {
        for (const QString& rel : package.files) {
            if (!copyAtomically(source.filePath(rel), install.filePath(rel))) {
                error = trPackage("Could not import %1.").arg(rel);
                break;
            }
            ++written;
        }
    }
    if (error.isEmpty())
        return {};
    const QString rollback = undo(install, backup, files, directories, written);
    const QString recovery = rollback.isEmpty() ? finishRestore(backup) : rollback;
    return recovery.isEmpty() ? error : error + u' ' + recovery;
}

QString restoreOnlineFixPackage(const QString& installPath)
{
    if (installPath.isEmpty() || !QFileInfo(installPath).isDir())
        return trPackage("The game installation is unavailable.");
    const QDir install(QFileInfo(installPath).canonicalFilePath());
    const QDir backup(backupPath(install.path()));
    if (!backup.exists())
        return trPackage("No package backup exists for this installation.");
    QLockFile lock(backup.path() + QStringLiteral(".lock"));
    if (!lock.tryLock())
        return trPackage("Another package operation is in progress.");
    QFile manifest(backup.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly))
        return trPackage("Cannot read the restore journal at %1.").arg(backup.path());
    const QJsonObject journal = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    const QJsonArray files = journal.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || !journal.value(QStringLiteral("directories")).isArray())
        return trPackage("Invalid restore journal at %1.").arg(backup.path());
    const QString error = undo(install, backup, files, journal.value(QStringLiteral("directories")).toArray(),
                               files.size());
    return error.isEmpty() ? finishRestore(backup) : error;
}

} // namespace arachnel::core
