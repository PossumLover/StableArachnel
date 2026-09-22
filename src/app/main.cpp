#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QPixmapCache>
#include <QQmlApplicationEngine>

#include <cstdio>
#include <cstdlib>
#include <QQmlError>
#include <QStyleHints>
#include <QString>
#include <QTimer>
#include <cstdio>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

#if !defined(Q_OS_WIN)
#include <QApplication>
#else
#include <QGuiApplication>
#endif

#include "core/facade/core_controller.h"
#include "core/settings/settings_store.h"
#include "core/i18n/translation_service.h"
#include "crash_log.h"
#include "deep_link.h"
#include "settings_identity.h"

#ifndef QT_QML_MATERIAL_IMPORT_PATH
#define QT_QML_MATERIAL_IMPORT_PATH ""
#endif

namespace {

void configureQmlEngine(QQmlApplicationEngine& engine)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir + QStringLiteral("/qml"));
    engine.addImportPath(appDir + QStringLiteral("/qml_modules"));

    const QByteArray materialPathEnv = qgetenv("QT_QML_MATERIAL_IMPORT_PATH");
    const QString materialPath = materialPathEnv.isEmpty()
        ? QStringLiteral(QT_QML_MATERIAL_IMPORT_PATH)
        : QString::fromLocal8Bit(materialPathEnv);
    if (!materialPath.isEmpty()) {
        const QString resolved = QDir::isAbsolutePath(materialPath)
            ? materialPath
            : (appDir + QLatin1Char('/') + materialPath);
        engine.addImportPath(resolved);
    }
}

void wireEngineLogging(QQmlApplicationEngine& engine, QCoreApplication& app)
{
    QObject::connect(
        &engine,
        &QQmlEngine::warnings,
        &app,
        [](const QList<QQmlError>& errors) {
            for (const QQmlError& error : errors)
                arachnel::logQmlWarning(error.url(), error.line(), error.column(),
                                        error.description());
        });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            fprintf(stderr, "Failed to load arachnel QML entry point\n");
            fflush(stderr);
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
}

void applyTranslations(QQmlApplicationEngine& engine, QCoreApplication& app)
{
    auto& core = arachnel::core::CoreController::instance();
    auto& translations = arachnel::core::TranslationService::instance();
    translations.setEngine(&engine);
    translations.applyLanguage(core.settings()->uiLanguage());

    QObject::connect(core.settings(), &arachnel::core::SettingsStore::uiLanguageChanged, &app,
                     [&translations, &core]() {
                         translations.applyLanguage(core.settings()->uiLanguage());
                         core.jobs()->refreshLocalizedText();
                     });
}

#if defined(Q_OS_UNIX)
/**
 * Raise RLIMIT_NOFILE to the hard limit.
 *
 * Source plugins open a descriptor per file they write, and a large depot
 * download can hold hundreds at once - a steamidra download of one game was
 * seen holding 499. At the usual 1024 soft limit the process then runs out of
 * descriptors, and because *core* is the one that can no longer open a file,
 * the symptom is that settings, the library and crash reports silently stop
 * being written while the leak itself stays invisible.
 */
void raiseFileDescriptorLimit()
{
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
        return;
    if (limit.rlim_cur >= limit.rlim_max)
        return;
    const rlim_t previous = limit.rlim_cur;
    limit.rlim_cur = limit.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &limit) == 0) {
        arachnel::logDiagnostic(
            QStringLiteral("Open-file limit raised from %1 to %2")
                .arg(static_cast<qulonglong>(previous))
                .arg(static_cast<qulonglong>(limit.rlim_max)));
    }
}
#endif

} // namespace

int main(int argc, char* argv[])
{
#if !defined(Q_OS_WIN)
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif

    arachnel::configureApplicationIdentity();

    const bool crashDialogMode = arachnel::isCrashDialogMode(argc, argv);

    arachnel::installCrashLogging();
    arachnel::logRunStarted(argc, argv);
#if defined(Q_OS_UNIX)
    raiseFileDescriptorLimit();
#endif

    const QIcon windowIcon = []() {
        QIcon icon;
        for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
            icon.addFile(QStringLiteral(":/icons/%1.png").arg(size), QSize(size, size));
        }
        return icon;
    }();
    if (!windowIcon.isNull())
        app.setWindowIcon(windowIcon);

    arachnel::core::registerCoreTypes();
    QPixmapCache::setCacheLimit(24 * 1024);

    arachnel::SingleInstanceGuard* singleInstance = nullptr;
    if (!crashDialogMode) {
        arachnel::registerGameDeepLinkProtocol();

        singleInstance = new arachnel::SingleInstanceGuard(&app);
        const QString launchLink = arachnel::findDeepLinkArgument(app.arguments());

        // `--launch <gameId>`: launch a game through the running instance's real
        // launch path, from a shell. For scripted debugging (scripts/dev/). It is
        // deliberately NOT a URL verb: arachnel:// links reach this binary from any
        // web page via xdg-open, and a page must not be able to start games. The
        // payload scheme below can only come from this flag or a local process of
        // the same user on the single-instance socket.
        static const QString kCliLaunchPrefix = QStringLiteral("arachnel-cli:launch/");
        QString cliLaunchId;
        {
            const QStringList args = app.arguments();
            const int at = args.indexOf(QStringLiteral("--launch"));
            if (at >= 0 && at + 1 < args.size())
                cliLaunchId = args.at(at + 1).trimmed();
        }

        if (!singleInstance->tryBecomePrimary()) {
            singleInstance->forwardToPrimary(cliLaunchId.isEmpty() ? launchLink
                                                                   : kCliLaunchPrefix + cliLaunchId);
            return 0;
        }

        QObject::connect(singleInstance, &arachnel::SingleInstanceGuard::messageReceived, &app,
                         [](const QString& url) {
                             if (url.startsWith(kCliLaunchPrefix)) {
                                 const QString id = url.mid(kCliLaunchPrefix.size()).trimmed();
                                 if (!id.isEmpty()) {
                                     arachnel::logDiagnostic(
                                         QStringLiteral("[cli] launch requested: %1").arg(id));
                                     arachnel::core::CoreController::instance().launchGame(id);
                                 }
                                 return;
                             }
                             arachnel::core::CoreController::instance().requestDeepLink(url);
                         });

        if (!cliLaunchId.isEmpty()) {
            // Started cold with --launch: give the library and plugins time to load.
            QTimer::singleShot(8000, &app, [cliLaunchId]() {
                arachnel::logDiagnostic(QStringLiteral("[cli] launch requested: %1").arg(cliLaunchId));
                arachnel::core::CoreController::instance().launchGame(cliLaunchId);
            });
        }

        if (!launchLink.isEmpty())
            arachnel::core::CoreController::instance().requestDeepLink(launchLink);
    }

    int exitCode = 1;
    {
        QQmlApplicationEngine engine;
        configureQmlEngine(engine);
        wireEngineLogging(engine, app);

        if (!crashDialogMode) {
            if (auto* guiApp = qobject_cast<QGuiApplication*>(&app))
                guiApp->setQuitOnLastWindowClosed(true);
            QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
                arachnel::markApplicationShuttingDown();
            });
        } else {
            arachnel::core::CoreController::setCrashReporterMode(true);
            if (auto* guiApp = qobject_cast<QGuiApplication*>(&app))
                guiApp->setQuitOnLastWindowClosed(true);
            QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
                arachnel::markApplicationShuttingDown();
            });
        }

        if (crashDialogMode)
            engine.loadFromModule(QStringLiteral("arachnel"), QStringLiteral("CrashReportWindow"));
        else
            engine.loadFromModule(QStringLiteral("arachnel"), QStringLiteral("Main"));

        if (!crashDialogMode) {
            applyTranslations(engine, app);
            QTimer::singleShot(0, &app, []() { arachnel::startHangWatchdog(); });
            const QStringList args = app.arguments();
            const int updateAt = args.indexOf(QStringLiteral("--update"));
            if (updateAt >= 0 && updateAt + 1 < args.size()) {
                const QString entryId = args.at(updateAt + 1);
                QTimer::singleShot(12000, [entryId]() {
                    arachnel::core::CoreController::instance().updateCatalogEntry(entryId);
                });
            }
        }

        exitCode = app.exec();

        // Tear down QML while Core is still alive, then shut Core down, then
        // destroy the engine. Destroying QQmlEngine while plugins/sessions are
        // mid-teardown caused free(): invalid size on Linux (NixOS AppImage).
        arachnel::markApplicationShuttingDown();
        const QList<QObject*> roots = engine.rootObjects();
        for (QObject* root : roots)
            delete root;
        engine.clearComponentCache();

        if (!crashDialogMode)
            arachnel::core::CoreController::instance().prepareShutdown();

        // Leave without unwinding the QQmlEngine. prepareShutdown() has already flushed
        // jobs, settings and library, stopped the sessions and unloaded the plugins, so
        // nothing below this point can still persist anything - but running
        // ~QQmlEngine afterwards reliably aborts with "free(): invalid size" inside
        // libQt6Qml, tearing down QML objects whose native plugin code is already gone.
        // The comment above was an earlier attempt at fixing that by ordering alone; it
        // is not enough. Every close became a crash (and, with core dumps armed, a
        // 294 MB core), so stop unwinding once the work is done and let the kernel
        // reclaim the rest.
        arachnel::logRunFinished(exitCode);
        std::fflush(nullptr);
        std::_Exit(exitCode);
    }

    arachnel::logRunFinished(exitCode);
    return exitCode;
}
