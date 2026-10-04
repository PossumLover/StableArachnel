#include <QtTest>

#include "catalog_parser.h"
#include "hydra_catalog_client.h"
#include "job_store.h"
#include "job_orchestrator.h"
#include "http_download_session.h"
#include "torrent_session.h"
#include "transport_stubs.h"
#include "proton_manager.h"
#include "settings_store.h"
#include "torbox_download_session.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

using namespace arachnel::core;

// Proton isn't exercised by this standalone network test target.
QString ProtonManager::resolveProtonId(const QString&, const QString&, const QStringList&) const
{
    Q_UNREACHABLE();
}

namespace {
const QString hash(40, QLatin1Char('a'));
const QString magnet = QStringLiteral("magnet:?xt=urn:btih:") + hash;

struct Response {
    QByteArray body;
    int status = 200;
    QByteArray range;
    bool split = false;
};

Response json(const QJsonValue& data)
{
    return {QJsonDocument(QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("data"), data}}).toJson()};
}

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest& req, Response response, QObject* parent)
        : QNetworkReply(parent), m_response(std::move(response))
    {
        setRequest(req);
        setUrl(req.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
        if (!m_response.range.isEmpty())
            setRawHeader("Content-Range", m_response.range);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this]() {
            if (m_aborted)
                return;
            emit metaDataChanged();
            const int first = m_response.split ? m_response.body.size() / 2 : m_response.body.size();
            m_available = m_response.body.left(first);
            emit readyRead();
            QTimer::singleShot(20, this, [this, first]() {
                if (m_aborted)
                    return;
                m_available += m_response.body.mid(first);
                emit readyRead();
                setFinished(true);
                emit finished();
            });
        });
    }
    void abort() override
    {
        m_aborted = true;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_available.size() + QNetworkReply::bytesAvailable(); }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char* data, qint64 size) override
    {
        const auto count = qMin<qint64>(size, m_available.size());
        if (count == 0)
            return isFinished() ? -1 : 0;
        memcpy(data, m_available.constData(), count);
        m_available.remove(0, count);
        return count;
    }
private:
    Response m_response;
    QByteArray m_available;
    bool m_aborted = false;
};

class FakeNetwork : public QNetworkAccessManager
{
public:
    std::function<Response(const QNetworkRequest&, const QByteArray&)> handler;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& req, QIODevice* body) override
    {
        requests.append(req);
        return new FakeReply(req, handler(req, body ? body->readAll() : QByteArray()), this);
    }
};

void torboxFixture(FakeNetwork& network, const QByteArray& content, const QString& name,
                   bool ignoreRange = false, bool badRange = false, bool split = false)
{
    network.handler = [=](const QNetworkRequest& req, const QByteArray&) -> Response {
        const QString path = req.url().path();
        if (path.endsWith(QStringLiteral("createtorrent")))
            return json(QJsonObject{{QStringLiteral("torrent_id"), 7}});
        if (path.endsWith(QStringLiteral("mylist")))
            return json(QJsonObject{{QStringLiteral("id"), 7}, {QStringLiteral("download_finished"), true},
                {QStringLiteral("download_present"), true},
                {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("id"), 0},
                    {QStringLiteral("name"), name}, {QStringLiteral("size"), content.size()}}}}});
        if (path.endsWith(QStringLiteral("requestdl")))
            return json(QStringLiteral("https://cdn.example/file"));
        const auto range = req.rawHeader("Range");
        if (!range.isEmpty() && !ignoreRange) {
            const auto offset = range.mid(6).split('-').first().toLongLong();
            return {content.mid(offset), 206,
                    "bytes " + QByteArray::number(badRange ? offset + 1 : offset) + '-'
                        + QByteArray::number(content.size() - 1) + '/' + QByteArray::number(content.size()), split};
        }
        return {content, 200, {}, split};
    };
}

QString outputPath(const QTemporaryDir& dir, const QString& name)
{
    return dir.path() + QStringLiteral("/torbox-") + hash + QLatin1Char('/') + name;
}
}

class NetworkTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("SproutNetworkTests"));
        QCoreApplication::setOrganizationName(QStringLiteral("SproutTests"));
    }
    void hydraPaginationAndLazyLinks()
    {
        FakeNetwork network;
        network.handler = [](const QNetworkRequest& req, const QByteArray& body) -> Response {
            if (req.url().path() == QStringLiteral("/download-sources"))
                return {R"({"id":"source","fingerprint":"fp"})"};
            if (req.url().path() == QStringLiteral("/catalogue/search")) {
                const int skip = QJsonDocument::fromJson(body).object().value(QStringLiteral("skip")).toInt();
                QJsonArray edges;
                for (int i = skip; i < qMin(skip + 100, 101); ++i)
                    edges.append(QJsonObject{{QStringLiteral("shop"), QStringLiteral("steam")},
                        {QStringLiteral("objectId"), QString::number(i + 1)},
                        {QStringLiteral("title"), QStringLiteral("Example %1").arg(i)}});
                return {QJsonDocument(QJsonObject{{QStringLiteral("count"), 101}, {QStringLiteral("edges"), edges}}).toJson()};
            }
            return {QJsonDocument(QJsonArray{
                QJsonObject{{QStringLiteral("downloadSourceId"), QStringLiteral("other")}, {QStringLiteral("uris"), QJsonArray{QStringLiteral("magnet:wrong-source")}}},
                QJsonObject{{QStringLiteral("downloadSourceId"), QStringLiteral("source")}, {QStringLiteral("uploadDate"), QStringLiteral("2026-01-02")}, {QStringLiteral("uris"), QJsonArray{QStringLiteral("magnet:unavailable")}}, {QStringLiteral("unavailableUris"), QJsonArray{QStringLiteral("magnet:unavailable")}}},
                QJsonObject{{QStringLiteral("downloadSourceId"), QStringLiteral("source")}, {QStringLiteral("uploadDate"), QStringLiteral("2026-01-01")}, {QStringLiteral("uris"), QJsonArray{magnet}}}}).toJson()};
        };
        HydraCatalogClient client(nullptr, QUrl(QStringLiteral("https://hydra.example")), &network);
        QSignalSpy loaded(&client, &HydraCatalogClient::loaded);
        client.load(QUrl(QStringLiteral("https://hydralinks.cloud/sources/example.json")));
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(network.requests.size(), 3);
        auto entries = parseCatalogFeed(loaded.first().first().toByteArray(), QStringLiteral("test"));
        QCOMPARE(entries.size(), 101);
        prepareCatalogEntry(entries[0]);
        QVERIFY(entries[0].magnetUris.first().startsWith(QStringLiteral("hydra:")));
        QSignalSpy resolved(&client, &HydraCatalogClient::resolved);
        client.resolve(entries[0].magnetUris.first());
        QTRY_COMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().first().toString(), magnet);
        QCOMPARE(QUrlQuery(network.requests.last().url()).queryItemValue(QStringLiteral("downloadSourceIds[]")), QStringLiteral("source"));
    }
    void hydraCancelSuppressesCompletion()
    {
        FakeNetwork network;
        network.handler = [](const auto&, const auto&) { return Response{R"({"id":"source","fingerprint":"fp"})"}; };
        HydraCatalogClient client(nullptr, QUrl(QStringLiteral("https://hydra.example")), &network);
        QSignalSpy loaded(&client, &HydraCatalogClient::loaded);
        QSignalSpy failed(&client, &HydraCatalogClient::failed);
        client.load(QUrl(QStringLiteral("https://hydralinks.cloud/sources/example.json")));
        client.cancel();
        QTest::qWait(50);
        QCOMPARE(loaded.size(), 0);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(network.requests.size(), 1);
    }
    void hydraResolvesDirectDownload()
    {
        FakeNetwork network;
        network.handler = [](const auto&, const auto&) {
            return Response{QJsonDocument(QJsonArray{QJsonObject{
                {QStringLiteral("downloadSourceId"), QStringLiteral("source")},
                {QStringLiteral("uris"), QJsonArray{QStringLiteral("https://cdn.example/game.zip")}}}}).toJson()};
        };
        HydraCatalogClient client(nullptr, QUrl(QStringLiteral("https://hydra.example")), &network);
        QSignalSpy resolved(&client, &HydraCatalogClient::resolved);
        client.resolve(QStringLiteral("hydra://steam/1?source=source"));
        QTRY_COMPARE(resolved.size(), 1);
        QCOMPARE(resolved.first().first().toString(), QStringLiteral("https://cdn.example/game.zip"));
    }
    void torboxRoutingNeverFallsBackToPeers()
    {
        transport_test::reset();
        SettingsStore settings;
        settings.setTorboxApiKey({});
        settings.setTorboxEnabled(true);
        JobStore store;
        store.setJobs({});
        JobModel jobs;
        TorrentSession torrent;
        HttpDownloadSession http;
        JobOrchestrator orchestrator(&settings, &store, &torrent, &http, &jobs);
        QSignalSpy failed(&orchestrator, &JobOrchestrator::downloadFailed);
        CatalogEntry entry;
        entry.id = QStringLiteral("torrent");
        entry.title = QStringLiteral("Torrent");
        entry.magnetUris = {magnet};
        const QString id = orchestrator.startCatalogDownload(entry, JobKind::Download);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(store.jobById(id)->torboxDownload);
        orchestrator.retryJob(id);
        QTRY_COMPARE(failed.size(), 2);
        QVERIFY(transport_test::torrentStarts.isEmpty());
        entry.id = QStringLiteral("direct");
        entry.magnetUris = {QStringLiteral("https://cdn.example/game.zip")};
        const QString direct = orchestrator.startCatalogDownload(entry, JobKind::Download);
        QCOMPARE(transport_test::httpStarts, QStringList{direct});
        QVERIFY(!store.jobById(direct)->torboxDownload);
        orchestrator.shutdownDownloads();
    }
    void torboxMigratesRestoredAndActiveTorrents()
    {
        transport_test::reset();
        SettingsStore settings;
        settings.setTorboxApiKey({});
        settings.setTorboxEnabled(true);
        JobStore store;
        JobEntry old;
        old.id = QStringLiteral("paused-native");
        old.status = QStringLiteral("paused");
        old.magnetUri = magnet;
        store.setJobs({old});
        JobModel jobs;
        TorrentSession torrent;
        HttpDownloadSession http;
        JobOrchestrator orchestrator(&settings, &store, &torrent, &http, &jobs);
        orchestrator.restoreJobs();
        QVERIFY(store.jobById(old.id)->torboxDownload);
        QCOMPARE(store.jobById(old.id)->status, QStringLiteral("paused"));
        QVERIFY(transport_test::torrentStarts.isEmpty());
        QSignalSpy failed(&orchestrator, &JobOrchestrator::downloadFailed);
        orchestrator.toggleJobPause(old.id);
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(transport_test::torrentStarts.isEmpty());
        settings.setTorboxEnabled(false);
        CatalogEntry entry;
        entry.id = QStringLiteral("active-native");
        entry.magnetUris = {magnet};
        const QString active = orchestrator.startCatalogDownload(entry, JobKind::Download);
        QCOMPARE(transport_test::torrentStarts, QStringList{active});
        settings.setTorboxEnabled(true);
        QVERIFY(transport_test::torrentStops.contains(active));
        QVERIFY(store.jobById(active)->torboxDownload);
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(transport_test::torrentStarts, QStringList{active});
        orchestrator.shutdownDownloads();
    }
    void torboxResume_data()
    {
        QTest::addColumn<bool>("ignoreRange");
        QTest::addColumn<bool>("badRange");
        QTest::newRow("range-supported") << false << false;
        QTest::newRow("range-ignored") << true << false;
        QTest::newRow("invalid-range") << false << true;
    }
    void torboxResume()
    {
        QFETCH(bool, ignoreRange);
        QFETCH(bool, badRange);
        QTemporaryDir dir;
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("test-key"));
        const QByteArray content(512 * 1024, 'x');
        FakeNetwork network;
        torboxFixture(network, content, QStringLiteral("Example/data.bin"), ignoreRange, badRange);
        const QString path = outputPath(dir, QStringLiteral("Example/data.bin"));
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile part(path + QStringLiteral(".sprout-part"));
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(content.left(12345));
        part.close();
        TorBoxDownloadSession session(&settings, nullptr, QUrl(QStringLiteral("https://torbox.example")), &network);
        QSignalSpy done(&session, &TorBoxDownloadSession::finished);
        QSignalSpy failed(&session, &TorBoxDownloadSession::failed);
        session.addJob(QStringLiteral("job"), magnet, dir.path());
        if (badRange) {
            QTRY_COMPARE(failed.size(), 1);
            QCOMPARE(done.size(), 0);
            QVERIFY(!QFileInfo::exists(path));
        } else {
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(failed.size(), 0);
            QFile output(path);
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll(), content);
            QCOMPARE(network.requests.last().rawHeader("Range"), QByteArray("bytes=12345-"));
            QVERIFY(!network.requests.last().hasRawHeader("Authorization"));
        }
    }
    void torboxPauseAndResume()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("test-key"));
        FakeNetwork network;
        const QByteArray content(512 * 1024, 'p');
        torboxFixture(network, content, QStringLiteral("data.bin"), false, false, true);
        TorBoxDownloadSession session(&settings, nullptr, QUrl(QStringLiteral("https://torbox.example")), &network);
        QSignalSpy done(&session, &TorBoxDownloadSession::finished);
        bool paused = false;
        connect(&session, &TorBoxDownloadSession::progress, &session, [&](const QString& id, int, qint64 bytes, qint64) {
            if (!paused && bytes > 0) {
                paused = true;
                session.setPaused(id, true);
            }
        });
        session.addJob(QStringLiteral("job"), magnet, dir.path());
        QTRY_VERIFY(paused);
        QTest::qWait(50);
        QCOMPARE(done.size(), 0);
        session.setPaused(QStringLiteral("job"), false);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(network.requests.last().rawHeader("Range"), QByteArray("bytes=262144-"));
    }
    void torboxRejectsTraversal()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("test-key"));
        FakeNetwork network;
        torboxFixture(network, "content", QStringLiteral("../escape.bin"));
        TorBoxDownloadSession session(&settings, nullptr, QUrl(QStringLiteral("https://torbox.example")), &network);
        QSignalSpy failed(&session, &TorBoxDownloadSession::failed);
        session.addJob(QStringLiteral("job"), magnet, dir.path());
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/escape.bin")));
    }
    void torboxPreservesMultipleFileLayout()
    {
        QTemporaryDir dir;
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("test-key"));
        FakeNetwork network;
        network.handler = [](const QNetworkRequest& req, const QByteArray&) -> Response {
            const QString path = req.url().path();
            if (path.endsWith(QStringLiteral("mylist"))) {
                QJsonArray files;
                for (int i = 0; i < 3; ++i)
                    files.append(QJsonObject{{QStringLiteral("id"), i},
                        {QStringLiteral("name"), QStringLiteral("Game/folder%1/data.bin").arg(i)},
                        {QStringLiteral("size"), 4}});
                return json(QJsonArray{QJsonObject{{QStringLiteral("id"), 7},
                    {QStringLiteral("download_finished"), true}, {QStringLiteral("download_present"), true},
                    {QStringLiteral("files"), files}}});
            }
            if (path.endsWith(QStringLiteral("requestdl")))
                return json(QStringLiteral("https://cdn.example/") + QUrlQuery(req.url()).queryItemValue(QStringLiteral("file_id")));
            return {QByteArray("data")};
        };
        const QString existing = outputPath(dir, QStringLiteral("Game/folder0/data.bin"));
        const QString completePart = outputPath(dir, QStringLiteral("Game/folder1/data.bin.sprout-part"));
        for (const QString& path : {existing, completePart}) {
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("data"), qint64(4));
        }
        TorBoxDownloadSession session(&settings, nullptr, QUrl(QStringLiteral("https://torbox.example")), &network);
        QSignalSpy done(&session, &TorBoxDownloadSession::finished);
        session.addJob(QStringLiteral("job"), magnet, dir.path(), 7);
        QTRY_COMPARE(done.size(), 1);
        for (int i = 0; i < 3; ++i) {
            QFile file(outputPath(dir, QStringLiteral("Game/folder%1/data.bin").arg(i)));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("data"));
        }
        QCOMPARE(network.requests.size(), 4);
        QCOMPARE(network.requests.last().url(), QUrl(QStringLiteral("https://cdn.example/2")));
    }
    void torboxKeepsTransportAndRemoteId()
    {
        JobStore store;
        JobEntry job;
        job.id = QStringLiteral("persisted");
        job.torboxDownload = true;
        job.torboxTorrentId = 123456789;
        job.magnetUri = magnet;
        job.status = QStringLiteral("paused");
        store.setJobs({job});
        JobStore loaded;
        loaded.load();
        QCOMPARE(loaded.jobs().size(), 1);
        QVERIFY(loaded.jobs().first().torboxDownload);
        QCOMPARE(loaded.jobs().first().torboxTorrentId, job.torboxTorrentId);
        QCOMPARE(loaded.jobs().first().status, QStringLiteral("paused"));
    }
    void keyStoredOutsideSettings()
    {
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("unique-test-secret"));
        settings.setTorboxEnabled(true);
        SettingsStore loaded;
        loaded.load();
        QCOMPARE(loaded.torboxApiKey(), QStringLiteral("unique-test-secret"));
        QVERIFY(loaded.torboxEnabled());
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/settings.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("unique-test-secret"));
    }
};

QTEST_GUILESS_MAIN(NetworkTests)
#include "network_tests.moc"
