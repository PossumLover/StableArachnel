pragma ComponentBehavior: Bound
import QtQuick
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Menu {
    id: root
    required property string gameId
    required property string playStatus
    function nameForStatus(status) {
        switch (status) {
        case "backlog": return backlog.text
        case "playing": return playing.text
        case "completed": return completed.text
        default: return unorganized.text
        }
    }
    autoClose: true

    component ListChoice: MD.MenuItem {
        required property string status
        selected: root.playStatus === status
        icon.name: selected ? MD.Token.icon.check : ""
        onTriggered: Core.setGamePlayStatus(root.gameId, status)
    }

    ListChoice { id: unorganized; status: ""; text: qsTr("Unorganized") }
    ListChoice { id: backlog; status: "backlog"; text: qsTr("Backlog") }
    ListChoice { id: playing; status: "playing"; text: qsTr("Playing") }
    ListChoice { id: completed; status: "completed"; text: qsTr("Completed") }
}
