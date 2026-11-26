import QtQuick
import Qcm.Material as MD

MD.Button {
    id: root
    required property string gameId
    property string playStatus: ""
    mdState.type: MD.Enum.BtFilledTonal
    text: qsTr("List: %1").arg(menu.nameForStatus(playStatus))
    icon.name: MD.Token.icon.expand_more
    onClicked: menu.open()

    GameListMenu {
        id: menu
        objectName: "gameListMenu"
        y: root.height
        gameId: root.gameId
        playStatus: root.playStatus
    }
}
