pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

ColumnLayout {
    component LibraryMenu: MD.ComboBox {
        id: menu
        leftPadding: mirrored ? 36 : MD.Token.spacing.medium
        rightPadding: mirrored ? MD.Token.spacing.medium : 36
        TextMetrics {
            id: labelMetrics
            font: menu.contentItem.font
            text: menu.displayText
        }
        Layout.minimumWidth: Math.max(184, Math.ceil(labelMetrics.advanceWidth) + leftPadding + rightPadding + 8)
        Layout.preferredWidth: Layout.minimumWidth
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

    spacing: MD.Token.spacing.medium
    RowLayout {
        Layout.fillWidth: true
        spacing: MD.Token.spacing.small
        LauncherSearchField {
            objectName: "librarySearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Search library")
            text: Core.libraryView.search
            onTextChanged: { if (Core.libraryView.search !== text) Core.libraryView.search = text }
        }
        LibraryMenu {
            objectName: "librarySort"
            model: [qsTr("Title"), qsTr("Recently played"), qsTr("Most played")]
            currentIndex: Core.libraryView.sortMode
            onActivated: Core.libraryView.sortMode = currentIndex
            Accessible.name: qsTr("Sort library")
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: MD.Token.spacing.extra_small
        Repeater {
            model: [
                { status: "", label: qsTr("All games") },
                { status: "backlog", label: qsTr("Backlog") },
                { status: "playing", label: qsTr("Playing") },
                { status: "completed", label: qsTr("Completed") },
                { status: "unorganized", label: qsTr("Unorganized") }
            ]
            MD.FilterChip {
                id: filterChip
                required property var modelData
                objectName: "libraryFilter_" + modelData.status
                text: modelData.label
                checked: Core.libraryView.playStatus === modelData.status
                onClicked: {
                    Core.libraryView.playStatus = modelData.status
                    // Clicking toggles checked and drops the binding; the filter owns the state.
                    checked = Qt.binding(() => Core.libraryView.playStatus === filterChip.modelData.status)
                }
            }
        }
    }
}
