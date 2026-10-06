#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "crash_log.h"
#include "library_store.h"
#include "plugin_host.h"

namespace arachnel {
void logDiagnostic(const QString&) {}
}

using namespace arachnel::core;

// Plugins answer with string literals, whose characters live in the plugin's image.
// Reinstalling a plugin unloads it, and Sprout reinstalls every plugin after an app
// update; v0.2.12 then crashed saving library.json, because the library still held the
// plugin's name. Nothing the host keeps from a plugin may point into it.
class PluginBoundaryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("SproutTests"));
        QCoreApplication::setApplicationName(QStringLiteral("SproutPluginBoundaryTests"));
        // The host also reads %APPDATA%\Arachnel\plugins; keep a real install out of this.
        QVERIFY(m_appData.isValid());
        qputenv("APPDATA", QFile::encodeName(m_appData.path()));
    }

    void pluginDataOutlivesThePlugin()
    {
        const QString library = QStringLiteral(BOUNDARY_TEST_PLUGIN);
        const QString dir = PluginHost::pluginSearchRoots().value(0) + QStringLiteral("/boundary-test");
        QDir(dir).removeRecursively();
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(QFile::copy(library, dir + QLatin1Char('/') + QFileInfo(library).fileName()));
        QFile manifest(dir + QStringLiteral("/plugin.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"id":"boundary-test","name":"Boundary Test","library":"boundary_test_plugin","apiVersion":4})");
        manifest.close();

        PluginHost host;
        host.scan();
        QVERIFY2(host.hasPlugin(QStringLiteral("boundary-test")), qPrintable(host.lastLoadRejectReason()));
        ISourcePlugin* plugin = host.plugin(QStringLiteral("boundary-test"));
        QVERIFY(plugin);

        // What an install keeps: the source's name, its launch options and launch setup.
        const SourcePluginInfo info = host.pluginInfos().value(0);
        LibraryGame game;
        game.id = QStringLiteral("steam-480");
        game.title = QStringLiteral("Spacewar");
        game.sourceId = info.id;
        game.sourceName = info.name;
        game.launchOptions = plugin->launchOptions(game);
        const LaunchInfo launch = plugin->launchInfo(game);
        const std::optional<CatalogEntry> entry = plugin->entryById(game.id);
        QVERIFY(entry);
        const std::optional<QString> update = plugin->detectUpdate(game, *entry);
        const QVector<CatalogEntry> catalog = plugin->catalog();
        const InstallAnalysis analysis = plugin->analyzeFileNames({});
        OwnedDownloadProgress progress;
        const InstallResult result = plugin->startOwnedDownload(
            InstallContext{}, [&progress](const OwnedDownloadProgress& p) { progress = p; });

        LibraryStore store;
        store.setGames({game});

        host.shutdownPlugins();

        // The library save after a catalog refresh is what crashed.
        store.setGames(store.games());
        QCOMPARE(store.games().first().sourceName, QStringLiteral("Boundary Test Source"));
        QCOMPARE(store.games().first().launchOptions.first().title, QStringLiteral("DirectX 11"));
        QCOMPARE(info.id, QStringLiteral("boundary-test"));
        QCOMPARE(info.description, QStringLiteral("Answers with string literals"));
        QCOMPARE(info.capabilities,
                 QStringList({QStringLiteral("owns_download"), QStringLiteral("launch_options")}));
        QCOMPARE(launch.executable, QStringLiteral("/games/spacewar/spacewar.exe"));
        QCOMPARE(launch.arguments, QStringList{QStringLiteral("-windowed")});
        QCOMPARE(launch.wineDllOverrides, QStringLiteral("steam_api64=n,b"));
        QCOMPARE(launch.environmentExtras.value(QStringLiteral("SteamAppId")), QStringLiteral("480"));
        QCOMPARE(entry->title, QStringLiteral("Spacewar"));
        QCOMPARE(entry->magnetUris.first(),
                 QStringLiteral("magnet:?xt=urn:btih:ffffffffffffffffffffffffffffffffffffffff"));
        QCOMPARE(entry->addons.first().title, QStringLiteral("Soundtrack"));
        QCOMPARE(entry->addons.first().screenshotUrls.first(),
                 QStringLiteral("https://cdn.example/soundtrack-1.jpg"));
        QCOMPARE(catalog.first().genres, QStringLiteral("Action"));
        QCOMPARE(*update, QStringLiteral("build 42"));
        QCOMPARE(analysis.detail, QStringLiteral("Looks portable"));
        QCOMPARE(progress.status, QStringLiteral("downloading"));
        QCOMPARE(progress.detail, QStringLiteral("Downloading depots"));
        QCOMPARE(result.installPath, QStringLiteral("/games/spacewar"));

        const QJsonObject saved{{QStringLiteral("status"), progress.status},
                                {QStringLiteral("launch"), launch.executable},
                                {QStringLiteral("dll"), launch.wineDllOverrides},
                                {QStringLiteral("addon"), entry->addons.first().id},
                                {QStringLiteral("method"), analysis.methodId},
                                {QStringLiteral("error"), result.error},
                                {QStringLiteral("capabilities"), QJsonArray::fromStringList(info.capabilities)}};
        QVERIFY(QJsonDocument(saved).toJson().contains("steam-481"));
        QDir(dir).removeRecursively();
    }

private:
    QTemporaryDir m_appData;
};

QTEST_GUILESS_MAIN(PluginBoundaryTests)
#include "plugin_boundary_tests.moc"
