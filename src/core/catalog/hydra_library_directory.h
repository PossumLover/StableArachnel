#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

namespace arachnel::core {

/**
 * The Hydra download sources listed on Hydra Library (library.hydra.wiki), read from the
 * public API its website uses. Each entry carries the catalog URL to add, so the user can
 * pick a source instead of hunting for its link.
 */
class HydraLibraryDirectory : public QObject
{
    Q_OBJECT

public:
    explicit HydraLibraryDirectory(QObject* parent = nullptr);

    /** Read every page of the list. Ends in loaded() or failed(); a running read restarts. */
    void fetch();

signals:
    void loaded(const QVariantList& sources);
    void failed(const QString& error);

private:
    void fetchPage(int page);
    void handlePage(QNetworkReply* reply);

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QVariantList m_sources;
};

} // namespace arachnel::core
