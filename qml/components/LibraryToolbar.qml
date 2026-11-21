import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

RowLayout {
    component LibraryMenu: MD.ComboBox {
        id: menu
        leftPadding: mirrored ? 36 : MD.Token.spacing.medium
        rightPadding: mirrored ? MD.Token.spacing.medium : 36
        contentItem: MD.Label {
            text: menu.displayText
            typescale: MD.Token.typescale.body_large
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
            color: menu.down || menu.hovered ? MD.Token.color.on_secondary_container : MD.Token.color.on_surface
        }
        background: Rectangle {
            implicitHeight: 48
            radius: height / 2
            color: menu.down || menu.hovered ? MD.Token.color.secondary_container : MD.Token.color.surface_container_high
            border.color: MD.Token.color.primary
            border.width: menu.visualFocus ? 2 : 0
        }
    }

    spacing: MD.Token.spacing.small
    LauncherSearchField {
        objectName: "librarySearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search library")
        text: Core.libraryView.search
        onTextChanged: { if (Core.libraryView.search !== text) Core.libraryView.search = text }
    }
    LibraryMenu {
        objectName: "libraryStatusFilter"
        Layout.preferredWidth: 180
        model: [qsTr("All games"), qsTr("Backlog"), qsTr("Playing"), qsTr("Completed"), qsTr("Unorganized")]
        readonly property var statuses: ["", "backlog", "playing", "completed", "unorganized"]
        currentIndex: Math.max(0, statuses.indexOf(Core.libraryView.playStatus))
        onActivated: Core.libraryView.playStatus = statuses[currentIndex]
        Accessible.name: qsTr("Filter library")
    }
    LibraryMenu {
        objectName: "librarySort"
        Layout.preferredWidth: 180
        model: [qsTr("Title"), qsTr("Recently played"), qsTr("Most played")]
        currentIndex: Core.libraryView.sortMode
        onActivated: Core.libraryView.sortMode = currentIndex
        Accessible.name: qsTr("Sort library")
    }
}
