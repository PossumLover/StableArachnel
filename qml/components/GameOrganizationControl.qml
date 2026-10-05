import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

RowLayout {
    id: root
    required property string gameId
    property string playStatus: ""
    spacing: MD.Token.spacing.medium
    MD.Label {
        Layout.fillWidth: true
        text: qsTr("Library list")
        typescale: MD.Token.typescale.body_large
    }
    MD.ComboBox {
        objectName: "gamePlayStatus"
        Layout.preferredWidth: 200
        model: [qsTr("Unorganized"), qsTr("Backlog"), qsTr("Playing"), qsTr("Completed")]
        readonly property var statuses: ["", "backlog", "playing", "completed"]
        currentIndex: Math.max(0, statuses.indexOf(root.playStatus))
        onActivated: Core.setGamePlayStatus(root.gameId, statuses[currentIndex])
        Accessible.name: qsTr("Library list")
    }
}
