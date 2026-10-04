#include "window_frame.h"

#include <QWindow>

#if defined(Q_OS_WIN)
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QPlatformSurfaceEvent>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

class WindowFrame : public QObject, public QAbstractNativeEventFilter
{
public:
    explicit WindowFrame(QWindow* window)
        : QObject(window), m_window(window)
    {
        m_handle = reinterpret_cast<HWND>(window->winId());
        qApp->installNativeEventFilter(this);
        window->installEventFilter(this);
        refreshFrame();
    }

    ~WindowFrame() override { qApp->removeNativeEventFilter(this); }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override
    {
        const auto* event = static_cast<MSG*>(message);
        if (event->hwnd != m_handle
            || !(GetWindowLongPtrW(event->hwnd, GWL_STYLE) & WS_THICKFRAME))
            return false;

        if (event->message == WM_NCHITTEST && !IsZoomed(m_handle) && !IsIconic(m_handle)) {
            RECT frame{};
            if (!GetWindowRect(m_handle, &frame))
                return false;
            const auto x = static_cast<short>(LOWORD(event->lParam));
            const auto y = static_cast<short>(HIWORD(event->lParam));
            const auto dpi = GetDpiForWindow(m_handle);
            const auto border = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
                + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            // The painted top border still needs native resize hit testing.
            if (y >= frame.top && y < frame.top + border
                && x >= frame.left && x < frame.right) {
                *result = x < frame.left + border ? HTTOPLEFT
                    : x >= frame.right - border ? HTTOPRIGHT : HTTOP;
                return true;
            }
            return false;
        }
        if (event->message != WM_NCCALCSIZE || !event->wParam)
            return false;

        auto* size = reinterpret_cast<NCCALCSIZE_PARAMS*>(event->lParam);
        const auto frame = size->rgrc[0];
        DefWindowProcW(event->hwnd, event->message, event->wParam, event->lParam);
        // Keep the resize border outside maximized content, but paint over it when restored.
        if (IsZoomed(event->hwnd)) {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST), &monitor))
                size->rgrc[0] = monitor.rcWork;
        } else {
            size->rgrc[0].top = frame.top;
        }
        *result = 0;
        return true;
    }

    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::PlatformSurface) {
            const auto* surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
                m_handle = reinterpret_cast<HWND>(m_window->winId());
                refreshFrame();
            } else {
                m_handle = nullptr;
            }
        }
        return false;
    }

private:
    void refreshFrame()
    {
        SetWindowPos(m_handle, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    QWindow* m_window;
    HWND m_handle = nullptr;
};

} // namespace
#endif

namespace arachnel {

void configureWindowFrame(QWindow* window)
{
#if defined(Q_OS_WIN)
    if (window && window->property("customTitleBar").toBool())
        new WindowFrame(window);
#else
    Q_UNUSED(window);
#endif
}

} // namespace arachnel
