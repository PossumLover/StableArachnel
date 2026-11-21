#include "library_snapshot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace arachnel::core {

namespace {

// Only this source expressly identifies the version as a Steam build number.
QString steamBuild(const LibraryGame& game)
{
    static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
    return game.sourceId == QStringLiteral("steamidra") && digits.match(game.version).hasMatch()
        ? game.version : QString();
}

bool shareableGame(const LibraryGame& game)
{
    bool numericId = false;
    const auto id = game.steamAppId.toUInt(&numericId);
    return numericId && id > 0 && !game.installPath.isEmpty() && QFileInfo(game.installPath).isDir();
}

} // namespace

bool writeLibrarySnapshot(const QUrl& fileUrl, const QVector<LibraryGame>& library)
{
    if (!fileUrl.isLocalFile())
        return false;
    QJsonObject games;
    for (const auto& game : library) {
        if (!shareableGame(game))
            continue;
        const auto build = steamBuild(game);
        if (!games.contains(game.steamAppId) || !build.isEmpty())
            games.insert(game.steamAppId, QJsonObject{{"entryId", game.id}, {"build", build}});
    }
    const QJsonObject snapshot{{"format", "sprout-library-v1"}, {"games", games},
        {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    const QByteArray bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    QSaveFile file(fileUrl.toLocalFile());
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QVariantMap readSharedLibrary(const QUrl& fileUrl, const QVector<LibraryGame>& library)
{
    constexpr qint64 limit = 8 * 1024 * 1024;
    QFile file(fileUrl.toLocalFile());
    if (!fileUrl.isLocalFile() || !QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > limit)
        return {{"error", QCoreApplication::translate("FriendsPage", "Could not read this library list.")}};
    const QByteArray bytes = file.read(limit + 1);
    const QJsonObject snapshot = QJsonDocument::fromJson(bytes).object();
    const auto created = QDateTime::fromString(snapshot.value("createdAt").toString(), Qt::ISODate);
    if (bytes.size() > limit || snapshot.value("format") != QStringLiteral("sprout-library-v1")
        || !snapshot.value("games").isObject() || !created.isValid())
        return {{"error", QCoreApplication::translate("FriendsPage", "Choose a library list exported by Sprout.")}};
    const QJsonObject games = snapshot.value("games").toObject();
    QVariantList matches;
    QSet<QString> matched;
    for (const auto& game : library) {
        if (!games.contains(game.steamAppId) || matched.contains(game.steamAppId) || !shareableGame(game))
            continue;
        const auto localBuild = steamBuild(game);
        const auto other = games.value(game.steamAppId).toObject();
        const auto otherId = other.value("entryId").toString();
        if (otherId.isEmpty())
            continue;
        const auto otherBuild = other.value("build").toString();
        bool comparable = false;
        otherBuild.toULongLong(&comparable);
        QString detail;
        if (comparable && !localBuild.isEmpty()) {
            detail = localBuild == otherBuild ? QCoreApplication::translate("FriendsPage", "Same build · %1").arg(localBuild)
                : QCoreApplication::translate("FriendsPage", "Your build %1 · Shared build %2").arg(localBuild, otherBuild);
        }
        matched.insert(game.steamAppId);
        matches.append(QVariantMap{{"peerGameId", otherId}, {"title", game.title}, {"detail", detail}});
    }
    std::stable_sort(matches.begin(), matches.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("title").toString().localeAwareCompare(b.toMap().value("title").toString()) < 0;
    });
    return {{"games", matches}, {"createdAt", created}};
}

} // namespace arachnel::core
