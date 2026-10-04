import QtQml

QtObject {
    readonly property bool customTitleBar: Qt.platform.os === "windows"
    // FramelessWindowHint removes WS_THICKFRAME and disables Windows system resizing.
    readonly property int flags: customTitleBar
        ? (Qt.Window | Qt.CustomizeWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint)
        : Qt.Window
}
