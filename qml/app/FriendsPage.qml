import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Item {
    id: root

    signal openGame(string gameId)
    signal openSettings()

    readonly property int pageMargin: MD.Token.spacing.extra_large
    readonly property int cardRadius: MD.Token.shape.corner.extra_large
    readonly property bool emptyState: Core.social.friends.count === 0

    // Ticks once a minute so "Last seen 5 min ago" does not freeze.
    property real now: Date.now()
    Timer {
        interval: 60000
        running: root.visible
        repeat: true
        onTriggered: root.now = Date.now()
    }

    function relativeTime(iso, nowMs) {
        const t = Date.parse(iso || "")
        if (isNaN(t))
            return ""
        const minutes = Math.floor(Math.max(0, nowMs - t) / 60000)
        if (minutes < 1)
            return qsTr("just now")
        if (minutes < 60)
            return qsTr("%1 min ago").arg(minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("%1 h ago").arg(hours)
        const days = Math.floor(hours / 24)
        if (days < 30)
            return qsTr("%1 d ago").arg(days)
        return new Date(t).toLocaleDateString()
    }

    function submitInvite(code) {
        const digits = String(code).replace(/[^0-9]/g, "")
        if (digits.length !== 6)
            return
        Core.acceptFriendInvite(digits)
        emptyAddPin.text = ""
        listAddPin.text = ""
    }

    Item {
        anchors.fill: parent
        visible: root.emptyState

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - root.pageMargin * 2, 880)
            spacing: MD.Token.spacing.extra_large

            ColumnLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.extra_small

                MD.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("You appear as %1").arg(Core.social.displayName)
                    typescale: MD.Token.typescale.headline_small
                }

                MD.Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Change in settings")
                    mdState.type: MD.Enum.BtText
                    onClicked: root.openSettings()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: MD.Token.spacing.large

                MD.ElevationRectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Math.max(createCol.implicitHeight, addCol.implicitHeight)
                                           + MD.Token.spacing.extra_large * 2
                    radius: root.cardRadius
                    color: MD.Token.color.surface_container
                    elevation: MD.Token.elevation.level1

                    ColumnLayout {
                        id: createCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: MD.Token.spacing.extra_large
                        anchors.rightMargin: MD.Token.spacing.extra_large
                        spacing: MD.Token.spacing.medium

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 56
                            implicitHeight: 56
                            radius: MD.Token.shape.corner.large
                            color: MD.Token.color.primary_container

                            MD.Icon {
                                anchors.centerIn: parent
                                name: MD.Token.icon.key
                                size: 28
                                color: MD.Token.color.on_primary_container
                            }
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Create a friend code")
                            typescale: MD.Token.typescale.title_medium
                        }

                        FriendCodePin {
                            Layout.alignment: Qt.AlignHCenter
                            visible: (Core.social.pendingInviteCode || "").length > 0
                            text: Core.social.pendingInviteCode
                            readOnly: true
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            visible: (Core.social.pendingInviteCode || "").length === 0
                            text: qsTr("Share it with someone on another device.")
                            wrapMode: Text.WordWrap
                            color: MD.Token.color.on_surface_variant
                            typescale: MD.Token.typescale.body_medium
                        }

                        MD.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: (Core.social.pendingInviteCode || "").length
                                  ? qsTr("New code")
                                  : qsTr("Create code")
                            icon.name: MD.Token.icon.key
                            mdState.type: MD.Enum.BtFilled
                            onClicked: Core.createFriendInvite()
                        }
                    }
                }

                MD.ElevationRectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Math.max(createCol.implicitHeight, addCol.implicitHeight)
                                           + MD.Token.spacing.extra_large * 2
                    radius: root.cardRadius
                    color: MD.Token.color.surface_container
                    elevation: MD.Token.elevation.level1

                    ColumnLayout {
                        id: addCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: MD.Token.spacing.extra_large
                        anchors.rightMargin: MD.Token.spacing.extra_large
                        spacing: MD.Token.spacing.medium

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 56
                            implicitHeight: 56
                            radius: MD.Token.shape.corner.large
                            color: MD.Token.color.secondary_container

                            MD.Icon {
                                anchors.centerIn: parent
                                name: MD.Token.icon.person_add
                                size: 28
                                color: MD.Token.color.on_secondary_container
                            }
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Add a friend")
                            typescale: MD.Token.typescale.title_medium
                        }

                        FriendCodePin {
                            id: emptyAddPin
                            Layout.alignment: Qt.AlignHCenter
                            onAccepted: root.submitInvite(emptyAddPin.text)
                        }

                        MD.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Add friend")
                            icon.name: MD.Token.icon.person_add
                            mdState.type: MD.Enum.BtFilledTonal
                            enabled: emptyAddPin.complete
                            onClicked: root.submitInvite(emptyAddPin.text)
                        }
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        visible: !root.emptyState
        contentWidth: width
        contentHeight: listCol.implicitHeight + root.pageMargin
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: listCol
            width: parent.width
            spacing: MD.Token.spacing.large

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: root.pageMargin
                Layout.rightMargin: root.pageMargin
                Layout.topMargin: root.pageMargin
                spacing: MD.Token.spacing.medium

                RowLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.medium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        MD.Label {
                            text: qsTr("Friends")
                            typescale: MD.Token.typescale.headline_small
                        }

                        MD.Label {
                            text: qsTr("You appear as %1").arg(Core.social.displayName)
                            color: MD.Token.color.on_surface_variant
                            typescale: MD.Token.typescale.body_medium
                        }
                    }

                    FriendCodePin {
                        visible: (Core.social.pendingInviteCode || "").length > 0
                        text: Core.social.pendingInviteCode
                        readOnly: true
                    }

                    MD.Button {
                        text: (Core.social.pendingInviteCode || "").length
                              ? qsTr("New code")
                              : qsTr("Create code")
                        icon.name: MD.Token.icon.key
                        mdState.type: MD.Enum.BtText
                        onClicked: Core.createFriendInvite()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.small

                    FriendCodePin {
                        id: listAddPin
                        onAccepted: root.submitInvite(listAddPin.text)
                    }

                    MD.Button {
                        text: qsTr("Add")
                        icon.name: MD.Token.icon.person_add
                        mdState.type: MD.Enum.BtFilledTonal
                        enabled: listAddPin.complete
                        onClicked: root.submitInvite(listAddPin.text)
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            MD.ElevationRectangle {
                Layout.fillWidth: true
                Layout.leftMargin: root.pageMargin
                Layout.rightMargin: root.pageMargin
                implicitHeight: friendsCol.implicitHeight + MD.Token.spacing.small * 2
                radius: MD.Token.shape.corner.large
                color: MD.Token.color.surface_container
                elevation: MD.Token.elevation.level0

                ColumnLayout {
                    id: friendsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: MD.Token.spacing.small
                    spacing: 0

                    Repeater {
                        model: Core.social.friends

                        ColumnLayout {
                            id: friendItem
                            required property string friendId
                            required property string nickname
                            required property bool online
                            required property string currentGameId
                            required property string currentGameTitle
                            required property string currentGameCoverUrl
                            required property string lastSeenAt
                            required property string suggestedGameId
                            required property string suggestedGameTitle
                            required property string suggestedAt
                            required property int index

                            readonly property bool playing: online && currentGameId.length > 0
                            readonly property bool gameInstalled:
                                playing && Core.isEntryPlayable(currentGameId)

                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: MD.Token.spacing.small
                                Layout.rightMargin: MD.Token.spacing.extra_small
                                Layout.topMargin: MD.Token.spacing.small
                                Layout.bottomMargin: MD.Token.spacing.small
                                spacing: MD.Token.spacing.medium

                                // Their game's cover while they play; the initial otherwise.
                                Item {
                                    Layout.preferredWidth: friendItem.playing ? 72 : MD.Token.spacing.extra_large
                                    Layout.preferredHeight: friendItem.playing ? 34 : MD.Token.spacing.extra_large
                                    Layout.alignment: Qt.AlignVCenter

                                    Rectangle {
                                        anchors.fill: parent
                                        visible: !friendItem.playing || cover.status !== Image.Ready
                                        radius: friendItem.playing ? MD.Token.shape.corner.small
                                                                   : MD.Token.shape.corner.full
                                        color: friendItem.online ? MD.Token.color.primary_container
                                                                 : MD.Token.color.surface_container_high

                                        MD.Label {
                                            anchors.centerIn: parent
                                            text: friendItem.nickname.length
                                                  ? friendItem.nickname.charAt(0).toUpperCase() : "?"
                                            typescale: MD.Token.typescale.title_small
                                            color: friendItem.online ? MD.Token.color.on_primary_container
                                                                     : MD.Token.color.on_surface_variant
                                        }
                                    }

                                    Image {
                                        id: cover
                                        anchors.fill: parent
                                        visible: friendItem.playing && status === Image.Ready
                                        source: friendItem.playing ? friendItem.currentGameCoverUrl : ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        smooth: true
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    MD.Label {
                                        Layout.fillWidth: true
                                        text: friendItem.nickname
                                        elide: Text.ElideRight
                                        typescale: MD.Token.typescale.title_small
                                    }

                                    MD.Label {
                                        Layout.fillWidth: true
                                        text: friendItem.playing
                                              ? qsTr("Playing %1").arg(friendItem.currentGameTitle || qsTr("a game"))
                                              : friendItem.online
                                                ? qsTr("Online")
                                                : (friendItem.lastSeenAt.length
                                                   ? qsTr("Last seen %1").arg(root.relativeTime(friendItem.lastSeenAt, root.now))
                                                   : qsTr("Offline"))
                                        elide: Text.ElideRight
                                        color: friendItem.online ? MD.Token.color.primary
                                                                 : MD.Token.color.on_surface_variant
                                        typescale: MD.Token.typescale.body_small
                                    }
                                }

                                MD.Button {
                                    visible: friendItem.playing
                                    text: friendItem.gameInstalled ? qsTr("Play") : qsTr("View")
                                    mdState.type: friendItem.gameInstalled ? MD.Enum.BtFilledTonal : MD.Enum.BtText
                                    onClicked: friendItem.gameInstalled
                                               ? Core.launchGame(friendItem.currentGameId)
                                               : root.openGame(friendItem.currentGameId)
                                }

                                MD.IconButton {
                                    mdState.type: MD.Enum.IBtStandard
                                    icon.name: MD.Token.icon.share
                                    onClicked: suggestDialog.openFor(friendItem.friendId, friendItem.nickname)
                                }

                                MD.IconButton {
                                    mdState.type: MD.Enum.IBtStandard
                                    icon.name: MD.Token.icon.edit
                                    onClicked: renameDialog.openFor(friendItem.friendId, friendItem.nickname)
                                }

                                MD.IconButton {
                                    mdState.type: MD.Enum.IBtStandard
                                    icon.name: MD.Token.icon.delete
                                    onClicked: Core.removeFriendById(friendItem.friendId)
                                }
                            }

                            // What they last suggested to you - the relay delivers it, the
                            // page never showed it.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: MD.Token.spacing.extra_large + MD.Token.spacing.medium
                                Layout.rightMargin: MD.Token.spacing.extra_small
                                Layout.bottomMargin: MD.Token.spacing.small
                                visible: friendItem.suggestedGameId.length > 0
                                spacing: MD.Token.spacing.small

                                MD.Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Suggested %1 · %2")
                                          .arg(friendItem.suggestedGameTitle || qsTr("a game"))
                                          .arg(root.relativeTime(friendItem.suggestedAt, root.now))
                                    elide: Text.ElideRight
                                    color: MD.Token.color.on_surface_variant
                                    typescale: MD.Token.typescale.body_small
                                }

                                MD.Button {
                                    text: qsTr("View")
                                    mdState.type: MD.Enum.BtText
                                    onClicked: root.openGame(friendItem.suggestedGameId)
                                }
                            }

                            MD.Divider {
                                Layout.fillWidth: true
                                Layout.leftMargin: MD.Token.spacing.extra_large + MD.Token.spacing.medium
                                visible: friendItem.index < Core.social.friends.count - 1
                            }
                        }
                    }
                }
            }
        }
    }

    MD.Dialog {
        id: renameDialog
        property string friendId: ""
        title: qsTr("Rename friend")
        modal: true
        width: Math.min(400, root.width > 0 ? root.width - 48 : 400)

        function openFor(id, nickname) {
            friendId = id
            renameField.text = nickname
            open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        function commit() {
            const name = renameField.text.trim()
            if (name.length > 0)
                Core.renameFriendById(friendId, name)
            close()
        }

        ColumnLayout {
            width: renameDialog.width - renameDialog.horizontalPadding * 2
            spacing: MD.Token.spacing.medium

            MD.TextField {
                id: renameField
                Layout.fillWidth: true
                onAccepted: renameDialog.commit()
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                MD.Button {
                    text: qsTr("Cancel")
                    mdState.type: MD.Enum.BtText
                    onClicked: renameDialog.close()
                }
                MD.Button {
                    text: qsTr("Save")
                    mdState.type: MD.Enum.BtFilledTonal
                    onClicked: renameDialog.commit()
                }
            }
        }
    }

    MD.Dialog {
        id: suggestDialog
        property string friendId: ""
        property string friendName: ""
        title: qsTr("Suggest a game to %1").arg(friendName)
        modal: true
        width: Math.min(440, root.width > 0 ? root.width - 48 : 440)

        function openFor(id, name) {
            friendId = id
            friendName = name
            open()
        }

        ListView {
            width: suggestDialog.width - suggestDialog.horizontalPadding * 2
            height: Math.min(contentHeight, 360)
            clip: true
            model: Core.library

            delegate: MD.ItemDelegate {
                required property string gameId
                required property string title
                width: ListView.view.width
                text: title
                onClicked: {
                    Core.suggestGameToFriend(suggestDialog.friendId, gameId)
                    suggestDialog.close()
                }
            }
        }
    }
}
