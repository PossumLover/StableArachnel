import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    id: root
    required property string gameId
    property bool active: true
    property int revision: 0
    property bool expanded: false
    readonly property var info: {
        root.revision
        return Core.gameAchievements(root.gameId)
    }
    readonly property var rows: root.info.rows ?? []
    padding: MD.Token.spacing.large
    radius: MD.Token.shape.corner.large
    backgroundColor: MD.Token.color.surface_container

    function refresh(force) {
        if (root.active && root.gameId.length)
            Core.refreshGameAchievements(root.gameId, !!force)
    }
    onGameIdChanged: {
        expanded = false
        refresh(false)
    }
    onActiveChanged: refresh(false)
    Component.onCompleted: refresh(false)
    Connections {
        target: Core
        function onGameAchievementsChanged(gameId) {
            if (gameId === root.gameId)
                root.revision++
        }
    }
    Timer {
        interval: 15000
        repeat: true
        running: root.active && Core.gameRunning && Core.runningGameId === root.gameId
        onTriggered: root.refresh(false)
    }

    contentItem: ColumnLayout {
        spacing: MD.Token.spacing.medium
        RowLayout {
            Layout.fillWidth: true
            MD.Label {
                Layout.fillWidth: true
                text: qsTr("Achievements")
                typescale: MD.Token.typescale.title_medium
            }
            MD.Label {
                visible: root.rows.length > 0
                text: qsTr("%1/%2 unlocked").arg(root.info.unlocked ?? 0).arg(root.info.total ?? 0)
                typescale: MD.Token.typescale.body_medium
            }
            MD.Button {
                objectName: "refreshAchievements"
                text: qsTr("Refresh")
                mdState.type: MD.Enum.BtText
                enabled: !(root.info.loading ?? false)
                onClicked: root.refresh(true)
            }
        }
        MD.Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: {
                if (root.info.loading ?? false)
                    return qsTr("Loading achievements...")
                if ((root.info.error ?? "").length)
                    return root.info.error
                if (!root.rows.length)
                    return qsTr("No achievement details available for this game.")
                if (!(root.info.localFileFound ?? false))
                    return qsTr("No supported achievement save found yet. Play the game, then refresh.")
                return ""
            }
            typescale: MD.Token.typescale.body_small
            color: MD.Token.color.on_surface_variant
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: root.expanded ? root.rows : root.rows.slice(0, 5)
            delegate: RowLayout {
                required property var modelData
                objectName: "achievement-" + modelData.name
                Layout.fillWidth: true
                spacing: MD.Token.spacing.medium
                Image {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    source: modelData.icon
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    opacity: modelData.unlocked ? 1 : 0.6
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.extra_small
                    MD.Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        typescale: MD.Token.typescale.title_small
                        wrapMode: Text.WordWrap
                    }
                    MD.Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: modelData.description
                        typescale: MD.Token.typescale.body_small
                        color: MD.Token.color.on_surface_variant
                        wrapMode: Text.WordWrap
                    }
                }
                MD.Label {
                    text: modelData.unlocked ? qsTr("Unlocked") : qsTr("Locked")
                    typescale: MD.Token.typescale.label_medium
                    color: modelData.unlocked ? MD.Token.color.primary : MD.Token.color.on_surface_variant
                }
            }
        }
        MD.Button {
            objectName: "expandAchievements"
            visible: root.rows.length > 5
            text: root.expanded ? qsTr("Show less") : qsTr("Show all (%1)").arg(root.rows.length)
            mdState.type: MD.Enum.BtText
            onClicked: root.expanded = !root.expanded
        }
    }
}
