#include "catalog_disk_cache.h"
#include "file_utils.h"
#include "http_download_session.h"
#include "presence_service.h"
#include "runtime_installer_wait.h"
#include <QtConcurrent>

#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <filesystem>
#ifdef Q_OS_WIN
#include "process_launcher.h"
#include "process_tracker.h"
#include <QProcess>
namespace arachnel {
void logDiagnostic(const QString&) {}
}
#endif

using namespace arachnel::core;

namespace {
bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}

QByteArray contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Split the response across event loop turns so the tests exercise readyRead,
// not just a body delivered in the finished callback.
QString serve(QTcpServer& server, const QByteArray& body, bool truncate = false)
{
    if (!server.listen(QHostAddress::LocalHost)) return {};
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, body, truncate] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, body, truncate] {
            socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n");
            socket->write(body.first(body.size() / 2));
            QTimer::singleShot(30, socket, [socket, body, truncate] {
                if (!truncate) socket->write(body.sliced(body.size() / 2));
                socket->disconnectFromHost();
            });
        }, Qt::SingleShotConnection);
    });
    return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
}
}

class ReliabilityTests : public QObject {
    Q_OBJECT
private slots:
    void slowInstallerCanBeCancelled()
    {
        struct State { std::atomic_bool started{false}, cancelled{false}; };
        auto state = std::make_shared<State>();
        auto worker = QtConcurrent::run([state, exe = QCoreApplication::applicationFilePath()] {
            QProcess process;
            process.start(exe, {"--idle-test-child"});
            if (!process.waitForStarted(3000)) return false;
            state->started = true;
            return !waitForRuntimeInstaller(process, &state->cancelled, QDeadlineTimer(8000))
                && process.state() == QProcess::NotRunning;
        });
        QTRY_VERIFY_WITH_TIMEOUT(state->started.load(), 3000);
        QTimer::singleShot(50, this, [state] { state->cancelled = true; });
        QTRY_VERIFY_WITH_TIMEOUT(worker.isFinished(), 3000);
        QVERIFY(worker.result());
    }
#ifdef Q_OS_WIN
    void pendingLaunch_data()
    {
        QTest::addColumn<bool>("starts");
        QTest::newRow("slow-game") << true;
        QTest::newRow("never-starts") << false;
    }
    void pendingLaunch()
    {
        QFETCH(bool, starts);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString exe = temp.filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), exe));
        QProcess child;
        qint64 observed = -1;
        ProcessLauncher::awaitDetachedGame(exe, this, starts ? 6000 : 500,
                                          [&](qint64 pid) { observed = pid; });
        if (starts) QTimer::singleShot(800, &child, [&] { child.start(exe, {"--idle-test-child"}); });
        QTRY_VERIFY_WITH_TIMEOUT(observed >= 0, 7000);
        if (starts) {
            QVERIFY(observed > 0);
            QCOMPARE(observed, child.processId());
            ProcessTracker::release(observed);
            child.kill();
            QVERIFY(child.waitForFinished(3000));
        } else {
            QCOMPARE(observed, 0); // same-named parent never qualifies
        }
    }
#endif
    void cacheKeyIncludesEveryTailByte()
    {
        for (int length = 32; length < 64; ++length) {
            QByteArray payload(length, 'a');
            const auto before = CatalogDiskCache::payloadFingerprint(payload);
            for (int i = 32; i < length; ++i) {
                payload[i] = 'b';
                QVERIFY(CatalogDiskCache::payloadFingerprint(payload) != before);
                payload[i] = 'a';
            }
        }
    }

    void moveRefusesOccupiedDestination()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto src = temp.filePath("src"), dst = temp.filePath("dst");
        QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
        QVERIFY(writeFile(src + "/.marker", "new"));
        QVERIFY(writeFile(dst + "/.marker", "old"));
        QVERIFY(!movePathRecursive(src, dst));
        QCOMPARE(contents(src + "/.marker"), "new");
        QCOMPARE(contents(dst + "/.marker"), "old");
    }

#ifdef Q_OS_LINUX
    void crossVolumeMovePreservesInstall()
    {
        QTemporaryDir source, dest(QStringLiteral("/dev/shm/sprout-test-XXXXXX"));
        if (!dest.isValid()) QSKIP("No writable /dev/shm");
        QVERIFY(source.isValid());
        const auto src = source.filePath("game"), dst = dest.filePath("game");
        QVERIFY(QDir().mkpath(src + "/.hidden"));
        QVERIFY(writeFile(src + "/.hidden/marker", "owned"));
        std::filesystem::create_symlink(".hidden/marker", QFileInfo(src + "/link").filesystemFilePath());
        QVERIFY(movePathRecursive(src, dst));
        QVERIFY(!QFileInfo::exists(src));
        QCOMPARE(contents(dst + "/.hidden/marker"), "owned");
        QVERIFY(QFileInfo(dst + "/link").isSymLink());
        QCOMPARE(contents(dst + "/link"), "owned");
    }

    void repairMergesCollidingDirectories()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto canonical = temp.filePath("game/assets"), incoming = temp.filePath("game\\assets");
        QVERIFY(QDir().mkpath(canonical) && QDir().mkpath(incoming));
        QVERIFY(writeFile(canonical + "/one", "first"));
        QVERIFY(writeFile(incoming + "/.two", "second"));
        QVERIFY(healWindowsInstallLayout(temp.path()) > 0);
        QCOMPARE(contents(canonical + "/one"), "first");
        QCOMPARE(contents(canonical + "/.two"), "second");
    }

    void failedSourceCleanupKeepsPublishedGame()
    {
        namespace fs = std::filesystem;
        QTemporaryDir source, dest(QStringLiteral("/dev/shm/sprout-test-XXXXXX"));
        if (!dest.isValid()) QSKIP("No writable /dev/shm");
        QVERIFY(source.isValid());
        const auto src = source.filePath("game"), dst = dest.filePath("game");
        QVERIFY(QDir().mkpath(src));
        QVERIFY(writeFile(src + "/save", "progress"));
        const auto parent = QFileInfo(source.path()).filesystemFilePath();
        fs::permissions(parent, fs::perms::owner_read | fs::perms::owner_exec);
        auto restore = qScopeGuard([&] { fs::permissions(parent, fs::perms::owner_all); });
        if (QFileInfo(source.path()).isWritable()) QSKIP("Source deletion is permitted");
        QString warning;
        QVERIFY(movePathRecursive(src, dst, nullptr, {}, &warning));
        QVERIFY(!warning.isEmpty());
        QCOMPARE(contents(dst + "/save"), "progress");
    }
#endif

    void directDownload_data()
    {
        QTest::addColumn<bool>("truncate");
        QTest::addColumn<bool>("unwritable");
        QTest::newRow("complete") << false << false;
        QTest::newRow("truncated") << true << false;
        QTest::newRow("commit-failure") << false << true;
    }
    void directDownload()
    {
        QFETCH(bool, truncate);
        QFETCH(bool, unwritable);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto target = temp.filePath("archive");
        if (unwritable) QVERIFY(QDir().mkdir(target));
        else QVERIFY(writeFile(target, "previous"));
        const QByteArray body(2 * 1024 * 1024 + 37, 'a');
        QTcpServer server;
        const auto base = serve(server, body, truncate);
        QVERIFY(!base.isEmpty());
        HttpDownloadSession http;
        QSignalSpy done(&http, &HttpDownloadSession::httpFinished), failed(&http, &HttpDownloadSession::httpFailed);
        QVERIFY(http.addJob("1", base + "/archive", {}, temp.path()));
        QTRY_COMPARE(done.size() + failed.size(), 1);
        QCOMPARE(done.size(), int(!truncate && !unwritable));
        if (unwritable) QVERIFY(QFileInfo(target).isDir());
        else QCOMPARE(contents(target), truncate ? QByteArray("previous") : body);
    }

    void presenceResponse_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<bool>("valid");
        QTest::newRow("invalid-json") << QByteArray("<html>Error</html>") << false;
        QTest::newRow("missing-roster") << QByteArray("{}") << false;
        QTest::newRow("bad-row") << QByteArray(R"({"friends":[{}]})") << false;
        QTest::newRow("empty-roster") << QByteArray(R"({"friends":[]})") << true;
    }
    void presenceResponse()
    {
        QFETCH(QByteArray, payload);
        QFETCH(bool, valid);
        QTcpServer server;
        PresenceService presence;
        presence.setRelayBaseUrl(serve(server, payload));
        QSignalSpy roster(&presence, &PresenceService::friendsPresenceReceived);
        QSignalSpy state(&presence, &PresenceService::relayStateChanged);
        presence.refresh();
        QTRY_COMPARE(state.size(), 1);
        QCOMPARE(state.first().first().toBool(), valid);
        QCOMPARE(roster.size(), int(valid));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--idle-test-child"))) {
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    ReliabilityTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "reliability_tests.moc"
