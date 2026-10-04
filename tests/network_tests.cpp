#include <QtTest>

#include "catalog_parser.h"
#include "catalog_identity.h"
#include "catalog_filter_service.h"
#include "library_store.h"
#include "achievement_service.h"
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
    void catalogReleaseTitlesGroupTogether()
    {
        const QString expected = catalogTitleKey(QStringLiteral("How to Fish"));
        for (const auto& title : {QStringLiteral("How to Fish - V 1.0.8 / Build 24902533"),
                                 QStringLiteral("How to Fish [P] {RUS + ENG + 14]"),
                                 QStringLiteral("How to Fish Free Download [v1.0.9/Build-24911270]")})
            QCOMPARE(catalogTitleKey(title), expected);
        QVERIFY(catalogTitleKey(QStringLiteral("Game 2")) != catalogTitleKey(QStringLiteral("Game")));
        QVERIFY(catalogTitleKey(QStringLiteral("Game Remastered")) != catalogTitleKey(QStringLiteral("Game")));
        QCOMPARE(catalogTitleKey(QStringLiteral("V Rising")), QStringLiteral("vrising"));

        CatalogEntry original;
        original.title = QStringLiteral("How to Fish");
        original.steamAppId = QStringLiteral("1234");
        CatalogEntry release;
        release.title = QStringLiteral("How to Fish - V 1.0.8 / Build 24902533");
        QCOMPARE(catalogOfferGroupKey(release, catalogSteamTitleKeys({original})), QStringLiteral("steam:1234"));
        auto conflicting = original;
        conflicting.steamAppId = QStringLiteral("5678");
        const auto keys = catalogSteamTitleKeys({original, conflicting});
        QCOMPARE(catalogOfferGroupKey(release, keys), QStringLiteral("title:howtofish"));
        QCOMPARE(catalogOfferGroupKey(original, keys), QStringLiteral("steam:1234"));
        QCOMPARE(catalogOfferGroupKey(conflicting, keys), QStringLiteral("steam:5678"));
    }
    void catalogSearchKeepsLatestQueryDuringReload()
    {
        QVector<CatalogEntry> cache;
        for (int i = 0; i < 5000; ++i) {
            CatalogEntry entry;
            entry.id = QString::number(i);
            entry.title = i % 2 ? QStringLiteral("How to Fish") : QStringLiteral("V Rising");
            entry.sourceId = QStringLiteral("source");
            prepareCatalogEntry(entry);
            cache.append(entry);
        }
        QReadWriteLock lock;
        CatalogModel model;
        CatalogFilterService filters(&model);
        filters.setCache(&cache);
        filters.setCacheLock(&lock);
        filters.rebuildFilterTable();
        filters.applyFilter(QStringLiteral("fish"));
        filters.applyFilter(QStringLiteral("rising"));
        QTRY_COMPARE(model.count(), 2500);
        QCOMPARE(model.data(model.index(0), CatalogModel::TitleRole).toString(), QStringLiteral("V Rising"));
        {
            QWriteLocker locker(&lock);
            for (auto& entry : cache) {
                entry.title = QStringLiteral("Other game");
                prepareCatalogEntry(entry);
            }
        }
        filters.rebuildFilterTable();
        filters.applyFilter(filters.activeQuery());
        QTRY_COMPARE(model.count(), 0);
        filters.applyFilter(QStringLiteral("other"));
        QTRY_COMPARE(model.count(), 5000);
        QThreadPool::globalInstance()->waitForDone();
    }
    void hydraDownloadRowsRetainSteamIdentity()
    {
        const auto entries = parseCatalogFeed(R"({"downloads":[{"title":"Example","steamAppId":123,"uris":[]}]})", QStringLiteral("source"));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().steamAppId, QStringLiteral("123"));
    }
    void playtimeCheckpointsSurviveRestart()
    {
        LibraryStore store;
        LibraryGame game;
        game.id = QStringLiteral("playtime-game");
        game.playtimeMs = 60000;
        store.setGames({game});
        store.recordPlaytime(game.id, 30000, 30000, false);
        store.recordPlaytime(game.id, 30000, 60000, false);
        store.recordPlaytime(game.id, 10000, 70000, true);
        LibraryStore restored;
        restored.load();
        QCOMPARE(restored.gameById(game.id)->playtimeMs, 130000);
        QCOMPARE(restored.gameById(game.id)->lastSessionMs, 70000);
        restored.recordPlaytime(game.id, 30000, 30000, false);
        restored.recordPlaytime(game.id, -30000, 0, true);
        QCOMPARE(restored.gameById(game.id)->playtimeMs, 130000);
        QCOMPARE(restored.gameById(game.id)->lastSessionMs, 70000);
        restored.removeGame(game.id);
    }
    void achievementFilesAndProtonPaths()
    {
        QTemporaryDir dir;
        AchievementLocations locations;
        locations.prefixPath = dir.path() + QStringLiteral("/pfx");
        const QString roaming = locations.prefixPath + QStringLiteral("/drive_c/users/steamuser/AppData/Roaming");
        QVERIFY(QDir().mkpath(roaming));
        const auto paths = achievementFileCandidates(QStringLiteral("123"), locations);
        const QString goldberg = roaming + QStringLiteral("/GSE Saves/123/achievements.json");
        QVERIFY(paths.contains(goldberg));
        QVERIFY(achievementFileCandidates(QStringLiteral("../123"), locations).isEmpty());
        QVERIFY(QDir().mkpath(QFileInfo(goldberg).absolutePath()));
        QFile file(goldberg);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"FIRST":{"earned":true,"earned_time":1700000000},"LOCKED":{"earned":false}})");
        file.close();
        bool valid = false;
        auto unlocks = readAchievementUnlocks(goldberg, &valid);
        QVERIFY(valid);
        QCOMPARE(unlocks.size(), 1);
        QCOMPARE(unlocks.value(QStringLiteral("FIRST")), 1700000000000LL);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"([{"name":"FIRST","earned":true,"earned_time":1700000000}])");
        file.close();
        QCOMPARE(readAchievementUnlocks(goldberg, &valid).size(), 1);
        const QString ini = dir.path() + QStringLiteral("/Achievements.ini");
        QFile iniFile(ini);
        QVERIFY(iniFile.open(QIODevice::WriteOnly));
        iniFile.write("\xEF\xBB\xBF[FIRST]\r\nAchieved=true\r\nTimeUnlocked=1700000000\r\n[SECOND]\nAchieved=1\nUnlockTime=1700000001\n[LOCKED]\nAchieved=false\n");
        iniFile.close();
        unlocks = readAchievementUnlocks(ini, &valid);
        QVERIFY(valid);
        QCOMPARE(unlocks.size(), 2);
        QCOMPARE(unlocks.value(QStringLiteral("FIRST")), 1700000000000LL);
        QCOMPARE(unlocks.value(QStringLiteral("SECOND")), 1700000001000LL);
    }
    void achievementMetadataCachingAndFailures()
    {
        QTemporaryDir dir;
        FakeNetwork network;
        network.handler = [](const auto&, const auto&) -> Response {
            return {QJsonDocument(QJsonArray{
                QJsonObject{{QStringLiteral("name"), QStringLiteral("FIRST")},
                    {QStringLiteral("displayName"), QStringLiteral("First step")},
                    {QStringLiteral("description"), QStringLiteral("Visible")},
                    {QStringLiteral("icon"), QStringLiteral("https://icons.example/1")},
                    {QStringLiteral("hidden"), true}},
                QJsonObject{{QStringLiteral("name"), QStringLiteral("SECRET")},
                    {QStringLiteral("displayName"), QStringLiteral("Spoiler")},
                    {QStringLiteral("description"), QStringLiteral("Spoiler")},
                    {QStringLiteral("hidden"), true}}}).toJson()};
        };
        AchievementLocations locations;
        locations.installPath = dir.path();
        QFile file(dir.path() + QStringLiteral("/achievements.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"FIRST":{"earned":true,"earned_time":1700000000}})");
        file.close();
        AchievementService service(nullptr, &network, dir.path() + QStringLiteral("/cache"));
        service.refresh(QStringLiteral("game"), QStringLiteral("123"), QStringLiteral("en"), locations);
        QTRY_VERIFY(!service.info(QStringLiteral("game")).value(QStringLiteral("loading")).toBool());
        auto info = service.info(QStringLiteral("game"));
        QCOMPARE(info.value(QStringLiteral("unlocked")).toInt(), 1);
        QCOMPARE(info.value(QStringLiteral("total")).toInt(), 2);
        QVERIFY(info.value(QStringLiteral("localFileFound")).toBool());
        const auto rows = info.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("First step"));
        QVERIFY(rows.last().toMap().value(QStringLiteral("description")).toString().isEmpty());
        service.refresh(QStringLiteral("game"), QStringLiteral("123"), QStringLiteral("en"), locations);
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(file.remove());
        AchievementService restored(nullptr, &network, dir.path() + QStringLiteral("/cache"));
        restored.refresh(QStringLiteral("game"), QStringLiteral("123"), QStringLiteral("en"), locations);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(restored.info(QStringLiteral("game")).value(QStringLiteral("unlocked")).toInt(), 1);
        QVERIFY(!restored.info(QStringLiteral("game")).value(QStringLiteral("localFileFound")).toBool());
        network.handler = [](const auto&, const auto&) { return Response{R"({"error":"offline"})", 503}; };
        restored.refresh(QStringLiteral("game"), QStringLiteral("123"), QStringLiteral("ru"), locations);
        QTRY_VERIFY(!restored.info(QStringLiteral("game")).value(QStringLiteral("loading")).toBool());
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(restored.info(QStringLiteral("game")).value(QStringLiteral("total")).toInt(), 2);
        QVERIFY(!restored.info(QStringLiteral("game")).value(QStringLiteral("error")).toString().isEmpty());
        restored.refresh(QStringLiteral("game"), QStringLiteral("123"), QStringLiteral("ru"), locations);
        QCOMPARE(network.requests.size(), 2);
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
#ifndef Q_OS_WIN
        const QFileInfo keyFile(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/torbox.key"));
        const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QVERIFY(!(keyFile.permissions() & publicPermissions));
#endif
    }
    void torboxRejectsSymlinks()
    {
#ifdef Q_OS_WIN
        QSKIP("Unix symlink test");
#else
        QTemporaryDir dir;
        QTemporaryDir outside;
        const QString root = outputPath(dir, {});
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QFile::link(outside.path(), root + QStringLiteral("linked")));
        SettingsStore settings;
        settings.setTorboxApiKey(QStringLiteral("test-key"));
        FakeNetwork network;
        torboxFixture(network, "content", QStringLiteral("linked/escape.bin"));
        TorBoxDownloadSession session(&settings, nullptr, QUrl(QStringLiteral("https://torbox.example")), &network);
        QSignalSpy failed(&session, &TorBoxDownloadSession::failed);
        session.addJob(QStringLiteral("job"), magnet, dir.path());
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QVERIFY(!QFileInfo::exists(outside.path() + QStringLiteral("/escape.bin")));
#endif
    }
};

QTEST_GUILESS_MAIN(NetworkTests)
#include "network_tests.moc"
