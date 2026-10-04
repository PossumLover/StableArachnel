#pragma once

#include "achievement_files.h"
#include <QObject>
#include <QJsonArray>
#include <QVariantMap>

class QNetworkAccessManager;

namespace arachnel::core {

class AchievementService : public QObject {
    Q_OBJECT
public:
    explicit AchievementService(QObject* parent = nullptr, QNetworkAccessManager* network = nullptr,
                                const QString& cacheDirectory = {});
    void refresh(const QString& gameId, const QString& appId, const QString& language,
                 const AchievementLocations& locations, bool force = false);
    QVariantMap info(const QString& gameId) const;
signals:
    void changed(const QString& gameId);
private:
    struct State {
        QString appId;
        QString language;
        QJsonArray metadata;
        QHash<QString, qint64> unlocks;
        qint64 fetchedAt = 0;
        qint64 retryAt = 0;
        quint64 generation = 0;
        bool loading = false;
        bool localFileFound = false;
        QString error;
    };
    QString cachePath(const QString& gameId) const;
    void save(const QString& gameId);
    void load(const QString& gameId);
    QNetworkAccessManager* m_network;
    QString m_cacheDirectory;
    QHash<QString, State> m_states;
    quint64 m_generation = 0;
};

} // namespace arachnel::core
