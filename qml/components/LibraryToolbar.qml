import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

RowLayout {
    spacing: MD.Token.spacing.medium
    AppTextField {
        objectName: "librarySearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search library")
        text: Core.libraryView.search
        onTextEdited: Core.libraryView.search = text
    }
    MD.ComboBox {
        objectName: "libraryStatusFilter"
        Layout.preferredWidth: 180
        model: [qsTr("All games"), qsTr("Backlog"), qsTr("Playing"), qsTr("Completed"), qsTr("Unorganized")]
        readonly property var statuses: ["", "backlog", "playing", "completed", "unorganized"]
        currentIndex: Math.max(0, statuses.indexOf(Core.libraryView.playStatus))
        onActivated: Core.libraryView.playStatus = statuses[currentIndex]
        Accessible.name: qsTr("Filter library")
    }
    MD.ComboBox {
        objectName: "librarySort"
        Layout.preferredWidth: 180
        model: [qsTr("Title"), qsTr("Recently played"), qsTr("Most played")]
        currentIndex: Core.libraryView.sortMode
        onActivated: Core.libraryView.sortMode = currentIndex
        Accessible.name: qsTr("Sort library")
    }
}
