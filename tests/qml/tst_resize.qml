import QtQuick
import QtQuick.Window
import QtTest

Item {
    width: 400
    height: 300

    QtObject {
        id: testWindow
        property int visibility: Window.Windowed
        property int lastEdges: 0
        function startSystemResize(edges) { lastEdges = edges; return true }
    }

    WindowChrome { id: chrome }
    WindowResizeEdges { id: edges; anchors.fill: parent; window: testWindow }

    TestCase {
        name: "WindowResize"
        when: windowShown

        function init() {
            testWindow.visibility = Window.Windowed
            testWindow.lastEdges = 0
        }

        function test_edges_data() {
            return [
                { tag: "top", x: 200, y: 1, flags: Qt.TopEdge },
                { tag: "bottom", x: 200, y: 299, flags: Qt.BottomEdge },
                { tag: "left", x: 1, y: 150, flags: Qt.LeftEdge },
                { tag: "right", x: 399, y: 150, flags: Qt.RightEdge },
                { tag: "top-left", x: 1, y: 1, flags: Qt.TopEdge | Qt.LeftEdge },
                { tag: "top-right", x: 399, y: 1, flags: Qt.TopEdge | Qt.RightEdge },
                { tag: "bottom-left", x: 1, y: 299, flags: Qt.BottomEdge | Qt.LeftEdge },
                { tag: "bottom-right", x: 399, y: 299, flags: Qt.BottomEdge | Qt.RightEdge }
            ]
        }

        function test_edges(data) {
            mouseClick(edges, data.x, data.y)
            compare(testWindow.lastEdges, data.flags)
        }

        function test_inactive_states_data() {
            return [
                { tag: "maximized", visibility: Window.Maximized },
                { tag: "fullscreen", visibility: Window.FullScreen },
                { tag: "minimized", visibility: Window.Minimized }
            ]
        }

        function test_inactive_states(data) {
            testWindow.visibility = data.visibility
            verify(!edges.enabled)
            mouseClick(edges, 399, 299)
            compare(testWindow.lastEdges, 0)
        }

        function test_linux_keeps_native_chrome() {
            if (Qt.platform.os !== "windows") {
                verify(!chrome.customTitleBar)
                compare(chrome.flags, Qt.Window)
            }
        }
    }
}
