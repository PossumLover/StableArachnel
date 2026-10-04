#pragma once

#include <QHash>
#include <QStringList>

namespace arachnel::core {

struct AchievementLocations {
    QString installPath;
    QString prefixPath;
    QString roamingPath;
    QString publicDocumentsPath;
};

QStringList achievementFileCandidates(const QString& appId, const AchievementLocations& locations);
QHash<QString, qint64> readAchievementUnlocks(const QString& filePath, bool* valid);

} // namespace arachnel::core
