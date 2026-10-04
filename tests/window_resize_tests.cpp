#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QTest>
#include <QWindow>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ResizeEvents : public QAbstractNativeEventFilter
{
public:
    explicit ResizeEvents(QWindow& window)
        : handle(reinterpret_cast<HWND>(window.winId()))
    {
        qApp->installNativeEventFilter(this);
    }
    ~ResizeEvents() override { qApp->removeNativeEventFilter(this); }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* event = static_cast<MSG*>(message);
        if (event->hwnd == handle && event->message == WM_ENTERSIZEMOVE) {
            ++entered;
            // End the native modal loop without injecting desktop mouse input.
            PostMessageW(handle, WM_CANCELMODE, 0, 0);
        }
        return false;
    }

    HWND handle;
    int entered = 0;
};

class WindowResizeTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(SPROUT_WINDOW_CHROME_QML)));
        QScopedPointer<QObject> chrome(component.create());
        QVERIFY2(chrome, qPrintable(component.errorString()));
        QVERIFY(chrome->property("customTitleBar").toBool());
        flags = Qt::WindowFlags(chrome->property("flags").toInt());
    }

    void nativeFrameKeepsTheCustomTitleBar()
    {
        QWindow window;
        window.setFlags(flags);
        const auto handle = reinterpret_cast<HWND>(window.winId());
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        QVERIFY(style & WS_THICKFRAME);
        QVERIFY(style & WS_MAXIMIZEBOX);
        QVERIFY(style & WS_MINIMIZEBOX);
        QVERIFY(style & WS_SYSMENU);
        QVERIFY(!(style & WS_CAPTION));
        QVERIFY(GetSystemMenu(handle, FALSE));
    }

    void everyEdgeStartsSystemResize_data()
    {
        QTest::addColumn<int>("edges");
        QTest::newRow("left") << int(Qt::LeftEdge);
        QTest::newRow("right") << int(Qt::RightEdge);
        QTest::newRow("top") << int(Qt::TopEdge);
        QTest::newRow("bottom") << int(Qt::BottomEdge);
        QTest::newRow("top-left") << int(Qt::TopEdge | Qt::LeftEdge);
        QTest::newRow("top-right") << int(Qt::TopEdge | Qt::RightEdge);
        QTest::newRow("bottom-left") << int(Qt::BottomEdge | Qt::LeftEdge);
        QTest::newRow("bottom-right") << int(Qt::BottomEdge | Qt::RightEdge);
    }

    void everyEdgeStartsSystemResize()
    {
        QFETCH(int, edges);
        QWindow window;
        window.setFlags(flags);
        window.resize(850, 600);
        window.show();
        QTRY_VERIFY(window.isExposed());
        ResizeEvents events(window);
        QVERIFY(window.startSystemResize(Qt::Edges(edges)));
        QTRY_COMPARE(events.entered, 1);
    }

    void minimumSizeAndRestore()
    {
        QWindow window;
        window.setFlags(flags);
        window.setMinimumSize(QSize(520, 340));
        window.setGeometry(50, 50, 620, 440);
        window.show();
        QTRY_VERIFY(window.isExposed());
        MINMAXINFO sizes{};
        SendMessageW(reinterpret_cast<HWND>(window.winId()), WM_GETMINMAXINFO, 0,
                     reinterpret_cast<LPARAM>(&sizes));
        QVERIFY(sizes.ptMinTrackSize.x >= qRound(520 * window.devicePixelRatio()));
        QVERIFY(sizes.ptMinTrackSize.y >= qRound(340 * window.devicePixelRatio()));
        const auto original = window.geometry();
        window.showMaximized();
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        window.showNormal();
        QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
        QTRY_COMPARE(window.geometry(), original);
    }

private:
    Qt::WindowFlags flags;
};

QTEST_MAIN(WindowResizeTests)
#include "window_resize_tests.moc"
