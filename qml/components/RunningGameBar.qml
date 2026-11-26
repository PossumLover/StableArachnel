import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

// The running game, pinned in the nav drawer above Settings. In the collapsed rail it stacks
// to the poster over the stop button, so it still reads as "a game is running".
MD.Pane {
    id: root

    property bool compact: false
    property string title: ""
    property string coverUrl: ""

    padding: MD.Token.spacing.small
    radius: MD.Token.shape.corner.medium
    backgroundColor: MD.Token.color.surface_container_high
    clip: true

    contentItem: GridLayout {
        columns: root.compact ? 1 : 3
        columnSpacing: MD.Token.spacing.small
        rowSpacing: MD.Token.spacing.small

        GamePoster {
            inputEnabled: false
            Layout.preferredWidth: 32
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignCenter
            source: root.coverUrl
            seed: root.title
            fallbackText: root.title.charAt(0)
            cornerRadius: MD.Token.shape.corner.small
        }

        ColumnLayout {
            visible: !root.compact
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            MD.Label {
                Layout.fillWidth: true
                text: qsTr("Playing now")
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.label_medium
                elide: Text.ElideRight
            }

            MD.Label {
                Layout.fillWidth: true
                text: root.title
                color: MD.Token.color.on_surface
                typescale: MD.Token.typescale.title_small
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        MD.IconButton {
            id: stopButton
            Layout.alignment: Qt.AlignCenter
            icon.name: MD.Token.icon.stop
            mdState.type: MD.Enum.IBtFilledTonal
            mdState.backgroundColor: MD.Token.color.error_container
            mdState.textColor: MD.Token.color.on_error_container
            Accessible.name: qsTr("Stop %1").arg(root.title)
            MD.ToolTip {
                visible: stopButton.hovered || stopButton.visualFocus
                text: stopButton.Accessible.name
            }
            onClicked: Core.stopRunningGame()
        }
    }
}
