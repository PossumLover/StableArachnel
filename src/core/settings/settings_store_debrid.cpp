#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace arachnel::core {
namespace {
QString keyPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/torbox.key");
}
}

void SettingsStore::loadTorboxKey()
{
    QFile file(keyPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray bytes = file.readAll();
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return;
    bytes = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
#endif
    m_torboxApiKey = QString::fromUtf8(bytes).trimmed();
    emit debridChanged();
}

bool SettingsStore::saveTorboxKey(const QString& key)
{
    QByteArray bytes = key.toUtf8();
#ifdef Q_OS_WIN
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Sprout TorBox", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;
    bytes = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
#endif
    QDir().mkpath(QFileInfo(keyPath()).absolutePath());
    QSaveFile file(keyPath());
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    return file.write(bytes) == bytes.size() && file.commit();
}

void SettingsStore::setTorboxEnabled(bool enabled)
{
    if (m_torboxEnabled == enabled)
        return;
    m_torboxEnabled = enabled;
    save();
    emit debridChanged();
}

void SettingsStore::setTorboxApiKey(const QString& key)
{
    const QString next = key.trimmed();
    if (next == m_torboxApiKey)
        return;
    if (!saveTorboxKey(next)) {
        m_torboxStatus = QCoreApplication::translate("Core", "Could not save the TorBox API key");
        emit debridStatusChanged();
        return;
    }
    m_torboxApiKey = next;
    m_torboxStatus.clear();
    emit debridChanged();
    emit debridStatusChanged();
}

void SettingsStore::checkTorboxConnection()
{
    if (m_torboxChecking)
        return;
    if (m_torboxApiKey.isEmpty()) {
        m_torboxStatus = QCoreApplication::translate("Core", "Enter your TorBox API key first");
        emit debridStatusChanged();
        return;
    }
    m_torboxChecking = true;
    m_torboxStatus.clear();
    emit debridStatusChanged();
    auto* network = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QStringLiteral("https://api.torbox.app/v1/api/user/me")));
    req.setRawHeader("Authorization", "Bearer " + m_torboxApiKey.toUtf8());
    req.setTransferTimeout(30000);
    const QString key = m_torboxApiKey;
    auto* reply = network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, network, reply, key]() {
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_torboxChecking = false;
        if (key == m_torboxApiKey) {
            m_torboxStatus = reply->error() == QNetworkReply::NoError && obj.value(QStringLiteral("success")).toBool()
                ? QCoreApplication::translate("Core", "Connected to TorBox")
                : QCoreApplication::translate("Core", "Could not connect to TorBox. Check your API key and subscription.");
        }
        network->deleteLater();
        emit debridStatusChanged();
    });
}

} // namespace arachnel::core
