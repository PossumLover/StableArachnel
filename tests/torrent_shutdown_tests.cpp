#include <QtTest>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <libtorrent/extensions.hpp>
#include "torrent_metadata_fetcher.h"
#include "torrent_session.h"
#include "../src/core/torrent/torrent_session_internal.h"
} // namespace arachnel::core

namespace arachnel::core {
struct ShutdownLatch {
    QSemaphore release;
    std::atomic_bool started{false};
    std::atomic_bool done{false};
};

struct SlowExtension : lt::plugin {
    explicit SlowExtension(std::shared_ptr<ShutdownLatch> latch) : latch(std::move(latch)) {}
    ~SlowExtension() override {
        latch->started = true;
        latch->release.tryAcquire(1, 2000);
        latch->done = true;
    }
    std::shared_ptr<ShutdownLatch> latch;
};

class TorrentSessionTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("SproutTests"));
        QCoreApplication::setApplicationName(QStringLiteral("SproutShutdownTests"));
    }
    void unusedEngineStaysDormant() {
        TorrentSession session;
        QVERIFY(!session.m_impl);
        QVERIFY(session.isAvailable());
        session.shutdown();
        QVERIFY(!session.isAvailable());
        QVERIFY(!session.addJob(QStringLiteral("late"), {}, {}));
        session.shutdown();
    }
    void slowEngineShutdownKeepsEventLoopAlive() {
        TorrentSession session;
        lt::settings_pack settings;
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
        session.m_impl = std::make_unique<TorrentSession::Impl>(settings);
        auto latch = std::make_shared<ShutdownLatch>();
        session.m_impl->session.add_extension(std::make_shared<SlowExtension>(latch));
        session.m_impl->session.get_settings();
        QTemporaryDir dir;
        QVERIFY(session.addJob(QStringLiteral("test"),
            QStringLiteral("magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), dir.path()));
        QElapsedTimer elapsed; elapsed.start();
        session.shutdown();
        QVERIFY2(elapsed.elapsed() < 500, "Shutdown waited for the engine on the caller thread");
        QVERIFY(!session.m_impl);
        QVERIFY(!session.isAvailable());
        bool heartbeat = false;
        QTimer::singleShot(0, this, [&]() { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QTRY_VERIFY(latch->started.load());
        QVERIFY(!latch->done.load());
        latch->release.release();
        QTRY_VERIFY(latch->done.load());
        session.shutdown();
        QVERIFY(!session.addJob(QStringLiteral("late"), {}, dir.path()));
    }
    void magnetProbeCancelKeepsEventLoopAlive() {
        // A tracker that answers "started" but never "stopped", so the probe's engine has to
        // wait for it while shutting down. Turning TorBox on cancels the probe from the UI.
        QTcpServer tracker;
        QVERIFY(tracker.listen(QHostAddress::LocalHost));
        int announces = 0;
        connect(&tracker, &QTcpServer::newConnection, this, [&]() {
            while (QTcpSocket* socket = tracker.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [&announces, socket]() {
                    if (socket->readAll().contains("event=stopped"))
                        return;
                    ++announces;
                    socket->write("HTTP/1.0 200 OK\r\n\r\nd8:intervali1800e5:peers0:e");
                    socket->disconnectFromHost();
                });
            }
        });
        const QString magnet =
            QStringLiteral("magnet:?xt=urn:btih:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                           "&tr=http://127.0.0.1:%1/announce").arg(tracker.serverPort());
        MagnetMetadataProbe probe;
        QSignalSpy finished(&probe, &MagnetMetadataProbe::finished);
        QVERIFY(probe.start(magnet, 60000));
        QTRY_VERIFY(announces > 0);
        QTest::qWait(300);
        QElapsedTimer elapsed; elapsed.start();
        probe.cancel();
        QVERIFY2(elapsed.elapsed() < 500, "cancel() waited for the engine on the caller thread");
        QVERIFY(!probe.busy());
        QVERIFY(probe.start(magnet, 60000));
        QVERIFY(probe.busy());
        probe.cancel();
        QTest::qWait(300);
        QCOMPARE(finished.size(), 0);
    }
    void magnetProbeCancelDropsQueuedResult() {
        // Cancel after a probe has queued its result but before it is delivered, as when
        // TorBox is turned on just as a probe ends.
        struct CancelOnDelivery : QObject {
            MagnetMetadataProbe* probe = nullptr;
            bool armed = true;
            bool eventFilter(QObject*, QEvent* event) override {
                if (armed && event->type() == QEvent::MetaCall) {
                    armed = false;
                    probe->cancel();
                }
                return false;
            }
        };
        MagnetMetadataProbe probe;
        QSignalSpy finished(&probe, &MagnetMetadataProbe::finished);
        CancelOnDelivery filter;
        filter.probe = &probe;
        probe.installEventFilter(&filter);
        // A zero timeout ends the probe with an empty result on its first poll.
        const QString first = QStringLiteral("magnet:?xt=urn:btih:cccccccccccccccccccccccccccccccccccccccc");
        QVERIFY(probe.start(first, 0));
        QTRY_VERIFY(!filter.armed);
        QTest::qWait(100);
        QCOMPARE(finished.size(), 0);
        QVERIFY(!probe.busy());
        const QString second = QStringLiteral("magnet:?xt=urn:btih:dddddddddddddddddddddddddddddddddddddddd");
        QVERIFY(probe.start(second, 0));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toString(), second);
        probe.cancel();
    }
    void cleanupTestCase() {
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
};
} // namespace arachnel::core

QTEST_GUILESS_MAIN(arachnel::core::TorrentSessionTests)
#include "torrent_shutdown_tests.moc"
