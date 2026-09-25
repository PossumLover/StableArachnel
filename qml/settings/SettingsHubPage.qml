import QtQuick
import QtQuick.Layouts

import Qcm.Material as MD

Flickable {
    id: root

    property int contentMargin: MD.Token.spacing.large
    readonly property bool onLinux: Qt.platform.os === "linux"

    signal openSection(string sectionId)

    readonly property var sectionModel: {
        const sections = [
            {
                id: "plugins",
                icon: MD.Token.icon.extension,
                tone: 0,
                title: qsTr("Plugins"),
                subtitle: qsTr("Install plugins to browse and play games.")
            },
            {
                id: "sources",
                icon: MD.Token.icon.library_books,
                tone: 1,
                title: qsTr("Hydra catalogs"),
                subtitle: qsTr("JSON catalog URLs")
            },
            {
                id: "friends",
                icon: MD.Token.icon.groups,
                tone: 2,
                title: qsTr("Friends"),
                subtitle: qsTr("Invite codes and relay presence")
            },
            {
                id: "storage",
                icon: MD.Token.icon.hard_drive,
                tone: 3,
                title: qsTr("Storage"),
                subtitle: qsTr("Library and download folders")
            },
            {
                id: "updates",
                icon: MD.Token.icon.update,
                tone: 1,
                title: qsTr("Updates"),
                subtitle: qsTr("Game and launcher updates")
            },
            {
                id: "launch",
                icon: MD.Token.icon.rocket_launch,
                tone: 0,
                title: qsTr("Launch"),
                subtitle: qsTr("Launch options and Proton on Linux")
            },
            {
                id: "appearance",
                icon: MD.Token.icon.local_florist,
                tone: 2,
                title: qsTr("Appearance"),
                subtitle: qsTr("Theme, colors, and language")
            },
            {
                id: "about",
                icon: MD.Token.icon.info,
                tone: 3,
                title: qsTr("About"),
                subtitle: qsTr("Version and app data")
            }
        ]
        if (root.onLinux)
            return sections
        return sections.filter(function (entry) { return entry.id !== "launch" })
    }

    contentWidth: width
    contentHeight: body.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    ColumnLayout {
        id: body
        width: root.width
        spacing: MD.Token.spacing.small

        GridLayout {
            id: grid
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.topMargin: MD.Token.spacing.small
            Layout.bottomMargin: MD.Token.spacing.medium
            columns: root.width > 520 ? 2 : 1
            columnSpacing: MD.Token.spacing.small
            rowSpacing: MD.Token.spacing.small
            uniformCellWidths: true

            Repeater {
                model: root.sectionModel

                MD.Card {
                    id: sectionCard
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    type: MD.Enum.CardFilled
                    verticalPadding: MD.Token.spacing.medium
                    horizontalPadding: MD.Token.spacing.medium
                    onClicked: root.openSection(sectionCard.modelData.id)

                    contentItem: RowLayout {
                        spacing: MD.Token.spacing.medium

                        ToneBadge {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 44
                            implicitHeight: 44
                            iconName: sectionCard.modelData.icon
                            tone: sectionCard.modelData.tone
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2

                            MD.Label {
                                Layout.fillWidth: true
                                text: sectionCard.modelData.title
                                typescale: MD.Token.typescale.title_small
                                elide: Text.ElideRight
                            }

                            MD.Label {
                                Layout.fillWidth: true
                                text: sectionCard.modelData.subtitle
                                color: MD.Token.color.on_surface_variant
                                typescale: MD.Token.typescale.body_small
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: MD.Token.spacing.medium
            Layout.bottomMargin: MD.Token.spacing.large
            spacing: MD.Token.spacing.extra_small

            MeadowMark {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 150
                Layout.preferredHeight: 128
                variant: "vignette"
                opacity: 0.9
            }

            MD.Label {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.application.version.length ? qsTr("Sprout v%1").arg(Qt.application.version) : "Sprout"
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.label_medium
            }
        }
    }
}
