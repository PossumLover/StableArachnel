import QtQuick

Item {
    id: root

    required property var window
    property int margin: 6
    // Corners are a bit larger than the edges so a diagonal resize is easy to hit.
    property int corner: 12
    // Only a normal window can be resized; maximized and fullscreen edges shouldn't promise it.
    enabled: root.window.visibility === Window.Windowed

    function resize(edge) {
        if (root.enabled)
            root.window.startSystemResize(edge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: margin
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: resize(Qt.TopEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: margin
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: resize(Qt.BottomEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: margin
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: resize(Qt.LeftEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: margin
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: resize(Qt.RightEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: corner
        height: corner
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: resize(Qt.LeftEdge | Qt.TopEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: corner
        height: corner
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: resize(Qt.RightEdge | Qt.TopEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: corner
        height: corner
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: resize(Qt.LeftEdge | Qt.BottomEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: corner
        height: corner
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: resize(Qt.RightEdge | Qt.BottomEdge)
    }
}
