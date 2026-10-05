#include <QtTest>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <libtorrent/extensions.hpp>
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
    void cleanupTestCase() {
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
};
} // namespace arachnel::core

QTEST_GUILESS_MAIN(arachnel::core::TorrentSessionTests)
#include "torrent_shutdown_tests.moc"
