import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Flickable {
    id: root
    property int contentMargin: MD.Token.spacing.large
    property string draftKey: Core.settings.torboxApiKey
    property bool showKey: false
    contentWidth: width
    contentHeight: body.implicitHeight + contentMargin
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    ColumnLayout {
        id: body
        width: root.width
        spacing: MD.Token.spacing.medium

        MD.Card {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.topMargin: MD.Token.spacing.small
            type: MD.Enum.CardFilled
            horizontalPadding: MD.Token.spacing.large
            verticalPadding: MD.Token.spacing.large
            contentItem: ColumnLayout {
                spacing: MD.Token.spacing.medium
                RowLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.medium
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: MD.Token.spacing.extra_small
                        MD.Label {
                            text: qsTr("TorBox")
                            typescale: MD.Token.typescale.title_medium
                        }
                        MD.Label {
                            Layout.fillWidth: true
                            text: qsTr("Download all torrents through TorBox.")
                            typescale: MD.Token.typescale.body_medium
                            color: MD.Token.color.on_surface_variant
                            wrapMode: Text.WordWrap
                        }
                    }
                    MD.Switch {
                        checked: Core.settings.torboxEnabled
                        onToggled: Core.settings.torboxEnabled = checked
                        Accessible.name: qsTr("Use TorBox")
                    }
                }
                AppTextField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: root.draftKey
                    placeholderText: qsTr("API key")
                    echoMode: root.showKey ? TextInput.Normal : TextInput.Password
                    onTextEdited: root.draftKey = text
                }
                MD.CheckBox {
                    text: qsTr("Show API key")
                    checked: root.showKey
                    onToggled: root.showKey = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: MD.Token.spacing.small
                    MD.Button {
                        text: Core.settings.torboxChecking ? qsTr("Checking...") : qsTr("Save and test")
                        mdState.type: MD.Enum.BtFilled
                        enabled: !Core.settings.torboxChecking && root.draftKey.trim().length > 0
                        onClicked: {
                            Core.settings.torboxApiKey = root.draftKey.trim()
                            if (Core.settings.torboxApiKey === root.draftKey.trim())
                                Core.settings.checkTorboxConnection()
                        }
                    }
                    MD.Button {
                        text: qsTr("Remove key")
                        mdState.type: MD.Enum.BtText
                        enabled: !Core.settings.torboxChecking && Core.settings.torboxApiKey.length > 0
                        onClicked: {
                            Core.settings.torboxEnabled = false
                            Core.settings.torboxApiKey = ""
                            root.draftKey = Core.settings.torboxApiKey
                        }
                    }
                }
                MD.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: Core.settings.torboxStatus
                    typescale: MD.Token.typescale.body_medium
                    color: MD.Token.color.on_surface_variant
                    wrapMode: Text.WordWrap
                }
                MD.Button {
                    text: qsTr("Open TorBox settings")
                    mdState.type: MD.Enum.BtText
                    onClicked: Core.openExternalUrl("https://torbox.app/settings")
                }
            }
        }
        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            text: qsTr("A TorBox subscription with API access is required. Enabling TorBox switches unfinished torrents to TorBox. Direct downloads stay direct.")
            typescale: MD.Token.typescale.body_small
            color: MD.Token.color.on_surface_variant
            wrapMode: Text.WordWrap
        }
    }
}
