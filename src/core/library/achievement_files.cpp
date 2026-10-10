#include "achievement_files.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace arachnel::core {

QStringList achievementFileCandidates(const QString& appId, const AchievementLocations& locations)
{
    static const QRegularExpression numericId(QStringLiteral("^[0-9]+$"));
    if (!numericId.match(appId).hasMatch())
        return {};
    QStringList files;
    auto addRoaming = [&](const QString& root) {
        if (root.isEmpty())
            return;
        for (const auto& folder : {QStringLiteral("Goldberg SteamEmu Saves"), QStringLiteral("GSE Saves")})
            files.append(root + QLatin1Char('/') + folder + QLatin1Char('/') + appId + QStringLiteral("/achievements.json"));
        files.append(root + QStringLiteral("/Steam/CODEX/") + appId + QStringLiteral("/achievements.ini"));
    };
    auto addPublic = [&](const QString& root) {
        if (root.isEmpty())
            return;
        files.append(root + QStringLiteral("/OnlineFix/") + appId + QStringLiteral("/Stats/Achievements.ini"));
        files.append(root + QStringLiteral("/OnlineFix/") + appId + QStringLiteral("/Achievements.ini"));
        for (const auto& folder : {QStringLiteral("CODEX"), QStringLiteral("RUNE")})
            files.append(root + QStringLiteral("/Steam/") + folder + QLatin1Char('/') + appId + QStringLiteral("/achievements.ini"));
    };
    addRoaming(locations.roamingPath);
    addPublic(locations.publicDocumentsPath);
    if (!locations.prefixPath.isEmpty()) {
        QDir users(locations.prefixPath + QStringLiteral("/drive_c/users"));
        for (const auto& user : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString root = users.filePath(user);
            addRoaming(root + QStringLiteral("/AppData/Roaming"));
            if (user.compare(QStringLiteral("Public"), Qt::CaseInsensitive) == 0)
                addPublic(root + QStringLiteral("/Documents"));
        }
    }
    if (!locations.installPath.isEmpty()) {
        for (const auto& suffix : {QStringLiteral("/achievements.json"), QStringLiteral("/achievements.ini"),
                                  QStringLiteral("/SteamData/User/Stats/achievements.ini")})
            files.append(locations.installPath + suffix);
        files.append(locations.installPath + QStringLiteral("/steam_settings/") + appId
            + QStringLiteral("/achievements.json"));
    }
    files.removeDuplicates();
    return files;
}

QHash<QString, qint64> readAchievementUnlocks(const QString& filePath, bool* valid)
{
    *valid = false;
    QHash<QString, qint64> unlocks;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return unlocks;
    const QByteArray bytes = file.readAll();
    auto timeMs = [](const QJsonValue& value) {
        const qint64 seconds = value.toVariant().toLongLong();
        return seconds > 0 && seconds < 100000000000LL ? seconds * 1000 : qint64(0);
    };
    if (QFileInfo(filePath).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray()))
            return unlocks;
        *valid = doc.isObject() ? doc.object().isEmpty() : doc.array().isEmpty();
        auto append = [&](const QString& name, const QJsonObject& achievement) {
            *valid |= !name.isEmpty() && achievement.contains(QStringLiteral("earned"));
            const auto earned = achievement.value(QStringLiteral("earned"));
            if (!name.isEmpty() && (earned.toBool() || earned.toInt() == 1))
                unlocks.insert(name, timeMs(achievement.value(QStringLiteral("earned_time"))));
        };
        if (doc.isObject()) {
            const auto object = doc.object();
            for (auto it = object.begin(); it != object.end(); ++it)
                append(it.key(), it.value().toObject());
        } else {
            for (const auto& value : doc.array()) {
                const auto achievement = value.toObject();
                append(achievement.value(QStringLiteral("name")).toString(), achievement);
            }
        }
        return unlocks;
    }
    QString section;
    QHash<QString, QHash<QString, QString>> sections;
    QString text = QString::fromUtf8(bytes);
    text.remove(QChar(0xfeff));
    for (const auto& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';')) || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            section = trimmed.mid(1, trimmed.size() - 2);
            continue;
        }
        const int equals = trimmed.indexOf(QLatin1Char('='));
        if (equals > 0 && !section.isEmpty())
            sections[section].insert(trimmed.left(equals).trimmed().toLower(), trimmed.mid(equals + 1).trimmed());
    }
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        const auto& values = it.value();
        if (!values.contains(QStringLiteral("achieved")))
            continue;
        *valid = true;
        const QString achieved = values.value(QStringLiteral("achieved")).toLower();
        if (achieved != QStringLiteral("1") && achieved != QStringLiteral("true"))
            continue;
        QString timestamp = values.value(QStringLiteral("timestamp"));
        if (timestamp.isEmpty())
            timestamp = values.value(QStringLiteral("timeunlocked"));
        if (timestamp.isEmpty())
            timestamp = values.value(QStringLiteral("unlocktime"));
        unlocks.insert(it.key(), timeMs(timestamp));
    }
    return unlocks;
}

} // namespace arachnel::core
