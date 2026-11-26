#include "online_fix_package.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace arachnel::core;

namespace {
bool put(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly)
        && file.write(data) == data.size();
}

QByteArray get(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Fixture {
    QTemporaryDir source;
    QTemporaryDir game;

    bool make(const QString& prefix, const QByteArray& identity)
    {
        const QByteArray ini = "[Main]\r\n" + identity + "\r\n[Hashes]\r\n0=example\r\n";
        return put(source.filePath(prefix + "OnlineFix.ini"), ini)
            && put(source.filePath(prefix + "OnlineFix64.dll"), "sample core")
            && put(source.filePath(prefix + "winmm.dll"), "sample loader")
            && put(source.filePath(prefix + "dlllist.txt"), "OnlineFix64.dll\r\n");
    }
};
}

class OnlineFixPackageTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void steamRoundTrip()
    {
        Fixture f;
        QVERIFY(f.make({}, "RealAppId=5118500"));
        const QString rel = "Safari-TV_Data/Plugins/x86_64/steam_api64.dll";
        QVERIFY(put(f.source.filePath(rel), "nested"));
        QVERIFY(put(f.game.filePath("winmm.dll"), "original"));
        QVERIFY(put(f.game.filePath("save"), "unrelated"));
        auto apply = [&] { return importOnlineFixPackage(f.game.path(), "5118500", f.source.path()); };
        QCOMPARE(apply(), QString());
        QVERIFY(hasOnlineFixPackageBackup(f.game.path()));
        QVERIFY(!apply().isEmpty());
        QCOMPARE(get(f.game.filePath(rel)), "nested");
        QCOMPARE(get(f.game.filePath("OnlineFix.ini")), get(f.source.filePath("OnlineFix.ini")));
        QCOMPARE(get(f.game.filePath("winmm.dll")), "sample loader");
        QVERIFY(put(f.game.filePath("winmm.dll"), "later edit"));
        QCOMPARE(restoreOnlineFixPackage(f.game.path()), QString());
        QCOMPARE(get(f.game.filePath("winmm.dll")), "original");
        QCOMPARE(get(f.game.filePath("save")), "unrelated");
        QVERIFY(!QFileInfo::exists(f.game.filePath("OnlineFix.ini")));
        QVERIFY(!QFileInfo::exists(f.game.filePath("Safari-TV_Data")));
        QVERIFY(!hasOnlineFixPackageBackup(f.game.path()));
        QCOMPARE(apply(), QString());
        QCOMPARE(restoreOnlineFixPackage(f.game.path()), QString());
    }

    void gdkNested()
    {
        Fixture f;
        const QString prefix = "Dungeons/Binaries/WinGDK/";
        const QString ini = f.game.filePath(prefix + "OnlineFix.ini");
        QVERIFY(f.make(prefix, "ProductId=9P5786PJB9RP"));
        QVERIFY(put(f.source.filePath("GDK_Helper.bat"), "fixture"));
        auto apply = [&] { return importOnlineFixPackage(f.game.path(), {}, f.source.path()); };
        QVERIFY(!apply().isEmpty());
        QVERIFY(put(ini, "[Main]\nProductId=OTHER\n"));
        QVERIFY(!apply().isEmpty());
        QVERIFY(!QFileInfo::exists(f.game.filePath("GDK_Helper.bat")));
        const QByteArray original = "[Main]\nProductId=9P5786PJB9RP\n";
        QVERIFY(put(ini, original));
        QCOMPARE(apply(), QString());
        QCOMPARE(get(f.game.filePath("GDK_Helper.bat")), "fixture");
        QCOMPARE(get(f.game.filePath(prefix + "OnlineFix64.dll")), "sample core");
        QCOMPARE(restoreOnlineFixPackage(f.game.path()), QString());
        QCOMPARE(get(ini), original);
        QVERIFY(!QFileInfo::exists(f.game.filePath("GDK_Helper.bat")));
    }

    void rejectMismatchOrPartialPackage()
    {
        Fixture f;
        QVERIFY(f.make({}, "RealAppId=3296060\nFakeAppId=480"));
        auto apply = [&](const QString& id) {
            return importOnlineFixPackage(f.game.path(), id, f.source.path());
        };
        QVERIFY(!apply("480").isEmpty());
        QVERIFY(!apply({}).isEmpty());
        QVERIFY(!QFileInfo::exists(f.game.filePath("OnlineFix.ini")));
        QVERIFY(put(f.game.filePath("OnlineFix.ini"), "[Main]\nRealAppId=999\n"));
        QVERIFY(!apply("3296060").isEmpty());
        QVERIFY(put(f.game.filePath("OnlineFix.ini"), "[Main]\nRealAppId=3296060\n"));
        QCOMPARE(apply({}), QString());
        QCOMPARE(restoreOnlineFixPackage(f.game.path()), QString());
        QVERIFY(put(f.source.filePath("dlllist.txt"), "OnlineFix64.dll\nmissing.dll\n"));
        QVERIFY(!apply("3296060").isEmpty());
        QVERIFY(!hasOnlineFixPackageBackup(f.game.path()));
        QVERIFY(!QFileInfo::exists(f.game.filePath("winmm.dll")));
    }

    void rejectOverlapAndConflictingPaths()
    {
        Fixture f;
        QVERIFY(f.make({}, "RealAppId=123"));
        QVERIFY(!importOnlineFixPackage(f.source.path(), "123", f.source.path()).isEmpty());
        QVERIFY(put(f.game.filePath("OnlineFix64.dll.arachnel-off"), "parked"));
        auto apply = [&] { return importOnlineFixPackage(f.game.path(), "123", f.source.path()); };
        QVERIFY(!apply().isEmpty());
        QVERIFY(QFile::remove(f.game.filePath("OnlineFix64.dll.arachnel-off")));
        QVERIFY(QDir().mkpath(f.game.filePath("winmm.dll")));
        QVERIFY(!apply().isEmpty());
        QVERIFY(!hasOnlineFixPackageBackup(f.game.path()));
    }

    void restoreAfterToggle()
    {
        Fixture f;
        QVERIFY(f.make({}, "RealAppId=123"));
        QCOMPARE(importOnlineFixPackage(f.game.path(), "123", f.source.path()), QString());
        const QString dll = f.game.filePath("OnlineFix64.dll");
        QVERIFY(QFile::rename(dll, dll + ".arachnel-off"));
        QVERIFY(!restoreOnlineFixPackage(f.game.path()).isEmpty());
        QVERIFY(hasOnlineFixPackageBackup(f.game.path()));
        QCOMPARE(get(f.game.filePath("winmm.dll")), "sample loader");
        QVERIFY(QFile::rename(dll + ".arachnel-off", dll));
        QCOMPARE(restoreOnlineFixPackage(f.game.path()), QString());
        QVERIFY(!QFileInfo::exists(dll));
    }

    void failedReadRollsBack()
    {
        Fixture f;
        QVERIFY(f.make({}, "RealAppId=123"));
        QVERIFY(put(f.game.filePath("dlllist.txt"), "original list"));
        const QString blocked = f.source.filePath("zz-last.dat");
        QVERIFY(put(blocked, "unreadable"));
#ifdef Q_OS_WIN
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(blocked.utf16()), GENERIC_READ,
                                          0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
#else
        QVERIFY(QFile::setPermissions(blocked, {}));
        if (QFileInfo(blocked).isReadable())
            QSKIP("This user can read files with no permission bits.");
#endif
        const auto error = importOnlineFixPackage(f.game.path(), "123", f.source.path());
#ifdef Q_OS_WIN
        CloseHandle(handle);
#endif
        QVERIFY(!error.isEmpty());
        QCOMPARE(get(f.game.filePath("dlllist.txt")), "original list");
        QVERIFY(!QFileInfo::exists(f.game.filePath("OnlineFix.ini")));
        QVERIFY(!hasOnlineFixPackageBackup(f.game.path()));
    }
};

QTEST_GUILESS_MAIN(OnlineFixPackageTests)
#include "online_fix_package_tests.moc"
