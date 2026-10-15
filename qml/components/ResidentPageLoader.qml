import QtQuick

Loader {
    property bool requested: false
    property bool keepAlive: false

    active: requested || keepAlive
    // Qt 6.11 can lose QML member data between asynchronous incubation frames.
    asynchronous: false
    visible: status === Loader.Ready
    onStatusChanged: {
        if (status === Loader.Loading || status === Loader.Ready)
            keepAlive = true
    }
}
