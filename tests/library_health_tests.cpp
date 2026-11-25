#include "library_health.h"
#include "library_model.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

#if defined(Q_OS_WIN)
#include "steam_shortcut_service.h"

// The overlay's Windows Steam discovery is unrelated to the on-disk health probe.
QString arachnel::core::findSteamInstallPath()
{
    return {};
}
#endif

using namespace arachnel::core;

namespace {

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray peHeader()
{
    QByteArray header(0x46, '\0');
    header.replace(0, 2, "MZ");
    header[0x3c] = 0x40;
    header.replace(0x40, 6, QByteArray("PE\0\0\x64\x86", 6));
    return header;
}

QByteArray elfHeader()
{
    QByteArray header(18, '\0');
    header.replace(0, 4, "\x7f" "ELF");
    header[4] = 2;
    header[5] = 1;
    header[16] = 3;
    return header;
}

QVariantMap findIssue(const LibraryHealthResult& result, const QString& code)
{
    for (const auto& issue : result.issues) {
        const QVariantMap fields = issue.toMap();
        if (fields.value(QStringLiteral("code")).toString() == code)
            return fields;
    }
    return {};
}

bool hasIssue(const LibraryHealthResult& result, const QString& code, bool verify = true)
{
    const auto issue = findIssue(result, code);
    return !issue.isEmpty() && issue.value(QStringLiteral("verify")).toBool() == verify
        && !issue.contains(QStringLiteral("message"));
}

} // namespace

class LibraryHealthTests : public QObject {
    Q_OBJECT

private slots:
    void mixedOs()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString exe = root.filePath(QStringLiteral("Game.exe"));
        const QString code = QStringLiteral("mixed-os");
        const LibraryHealthInput input{root.path(), QStringLiteral("Game"), exe, {}, {}};
        QVERIFY(writeFile(exe, "not a PE image"));
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Other/Game.app")));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code));
        QVERIFY(writeFile(exe, peHeader()));
        QVERIFY(hasIssue(checkLibraryHealth(input), code));

        QVERIFY(QDir(root.path()).rmdir(QStringLiteral("Other/Game.app")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Other/UnityPlayer.so")), elfHeader()));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Other/UnityPlayer.dll")), {}));
        QVERIFY(hasIssue(checkLibraryHealth(input), code));

        QVERIFY(writeFile(root.filePath(QStringLiteral(".arachnel-steamidra")),
                          R"({"launch":{"type":"exe","path":"Game.exe"}})"));
        QVERIFY(hasIssue(checkLibraryHealth({root.path(), {}, {}, {}, {}}), code));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game.x86_64")), elfHeader()));
        QVERIFY(writeFile(root.filePath(QStringLiteral(".arachnel-steamidra")),
                          R"({"launch":{"type":"native","path":"Game.x86_64"}})"));
        QVERIFY(!hasIssue(checkLibraryHealth({root.path(), {}, {}, {}, {}}), code));
    }

    void storedLaunchOptions()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game.exe")), peHeader()));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game.x86_64")), elfHeader()));
        const QString code = QStringLiteral("mixed-os");
        LibraryHealthInput input{root.path(), QStringLiteral("Game"), {}, {}, {}};
        const auto heuristic = checkLibraryHealth(input);
        QVERIFY(hasIssue(heuristic, code));

        input.launchOptions = {
            {QStringLiteral("windows"), root.filePath(QStringLiteral("Game.exe")), false},
            {QStringLiteral("native"), QStringLiteral("Game.x86_64"), true},
        };
        const auto nativeDefault = checkLibraryHealth(input, heuristic);
        QVERIFY(!hasIssue(nativeDefault, code));
        input.launchOptions[1].isDefault = false;
        const auto firstOption = checkLibraryHealth(input, nativeDefault);
        QVERIFY(hasIssue(firstOption, code));
        input.launchOptionId = QStringLiteral("native");
        QVERIFY(!hasIssue(checkLibraryHealth(input, firstOption), code));
    }

    void onlineFixWithoutLoader()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const LibraryHealthInput input{root.path(), {}, {}, {}, {}};
        const QString code = QStringLiteral("online-fix-no-loader");
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Game/Binaries/Win64")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game/Binaries/Win64/OnlineFix64.dll")), {}));
        QVERIFY(hasIssue(checkLibraryHealth(input), code, false));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game/Binaries/Win64/winmm.dll")), {}));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code, false));
    }

    void wrongSizeAfterLibrarySync()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const LibraryHealthInput input{root.path(), {}, {}, {}, {}};
        const QString code = QStringLiteral("wrong-size");
        QVERIFY(writeFile(root.filePath(QStringLiteral(".arachnel-steamidra-files")),
                          R"({"version":1,"depots":[{"files":[{"path":"game.dat","size":3,"dir":false}]}]})"));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code));
        const QString data = root.filePath(QStringLiteral("game.dat"));
        QVERIFY(writeFile(data, "abc"));
        const LibraryHealthResult healthy = checkLibraryHealth(input);
        QVERIFY(!hasIssue(healthy, code));
        QVERIFY(writeFile(data, "a"));
        // A same-path write leaves the install directory's mtime alone.
        QVERIFY(!hasIssue(checkLibraryHealth(input, healthy), code));
        LibraryGame game;
        game.id = QStringLiteral("game");
        game.installPath = root.path();
        QHash<QString, LibraryHealthCheckMode> pending;
        queueLibraryHealthSync({game}, pending);
        QVERIFY(pending.contains(game.id));
        const auto afterSync = checkLibraryHealth(input, healthy, pending.value(game.id));
        QVERIFY(hasIssue(afterSync, code));
    }

    void stalePaks()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const LibraryHealthInput input{root.path(), {}, {}, {}, {}};
        const QString code = QStringLiteral("stale-paks");
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Game/Content/Paks")));
        const QString shipped = root.filePath(QStringLiteral("Game/Content/Paks/shipped.pak"));
        const QString extra = root.filePath(QStringLiteral("Game/Content/Paks/stale.ucas"));
        QVERIFY(writeFile(shipped, {}));
        QVERIFY(writeFile(extra, {}));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code));
        QVERIFY(writeFile(root.filePath(QStringLiteral(".arachnel-steamidra-files")),
                          R"({"version":1,"depots":[{"files":[{"path":"Game/Content/Paks/shipped.pak","size":0,"dir":false}]}]})"));
        const auto issue = findIssue(checkLibraryHealth(input), code);
        QCOMPARE(issue.value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(issue.value(QStringLiteral("minimum")).toBool(), false);
        QVERIFY(QFile::remove(extra));
        QVERIFY(!hasIssue(checkLibraryHealth(input), code));
    }

    void stalePaksCaseMismatch()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("Game/Content/Paks")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("Game/Content/Paks/base.pak")), {}));
        QVERIFY(writeFile(root.filePath(QStringLiteral(".arachnel-steamidra-files")),
                          R"({"version":1,"depots":[{"files":[{"path":"Game/Content/Paks/Base.pak","size":0,"dir":false}]}]})"));

        bool caseSensitive = true;
#if defined(Q_OS_WIN)
        caseSensitive = !QFile::exists(root.filePath(QStringLiteral("Game/Content/Paks/Base.pak")));
#endif
        const auto result = checkLibraryHealth({root.path(), {}, {}, {}, {}});
        QCOMPARE(hasIssue(result, QStringLiteral("stale-paks")), caseSensitive);
    }
};

QTEST_GUILESS_MAIN(LibraryHealthTests)
#include "library_health_tests.moc"
