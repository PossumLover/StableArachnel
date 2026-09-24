import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

import Arachnel.Core 1.0
import Qcm.Material as MD

Flickable {
    id: root

    property int contentMargin: MD.Token.spacing.large
    property bool applying: false
    readonly property bool meadow: Appearance.paletteType === MD.Enum.PaletteMeadow

    readonly property var languageOptions: [
        { code: "en", label: qsTr("English") },
        { code: "ru", label: qsTr("Russian") }
    ]

    readonly property string weblateTranslateUrl: "https://hosted.weblate.org/projects/arachnel/application/"

    contentWidth: width
    contentHeight: body.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    function syncFromStore() {
        applying = true
        Appearance.apply()
        paletteListView.currentIndex = Appearance.paletteType
        applying = false
    }

    Component.onCompleted: syncFromStore()

    ColumnLayout {
        id: body
        width: root.width
        spacing: MD.Token.spacing.medium

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.topMargin: MD.Token.spacing.small
            text: qsTr("Theme and colors apply across the app.")
            color: MD.Token.color.on_surface_variant
            wrapMode: Text.WordWrap
            typescale: MD.Token.typescale.body_medium
        }

        // Theme as two little meadow postcards: day and dusk.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            spacing: MD.Token.spacing.small

            Repeater {
                model: [
                    { dark: false, title: qsTr("Daylight"), icon: MD.Token.icon.wb_sunny },
                    { dark: true, title: qsTr("Dusk"), icon: MD.Token.icon.dark_mode }
                ]

                MD.Card {
                    id: themeCard
                    required property var modelData
                    readonly property bool selected: MD.Token.isDarkTheme === modelData.dark

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    type: MD.Enum.CardOutlined
                    horizontalPadding: MD.Token.spacing.small
                    verticalPadding: MD.Token.spacing.small
                    onClicked: {
                        if (root.applying || selected)
                            return
                        Appearance.setThemeMode(modelData.dark ? MD.Enum.Dark : MD.Enum.Light)
                    }

                    contentItem: ColumnLayout {
                        spacing: MD.Token.spacing.small

                        // The hills art has an open sky; paint one behind it.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            radius: MD.Token.shape.corner.medium
                            gradient: Gradient {
                                GradientStop { position: 0; color: themeCard.modelData.dark ? "#2B2E3A" : "#F7E2CF" }
                                GradientStop { position: 1; color: themeCard.modelData.dark ? "#4A3A39" : "#E9C9BF" }
                            }

                            MD.Image {
                                anchors.fill: parent
                                radius: parent.radius
                                fillMode: Image.PreserveAspectCrop
                                horizontalAlignment: Image.AlignLeft
                                verticalAlignment: Image.AlignBottom
                                source: themeCard.modelData.dark ? "qrc:/art/hills-dark.png" : "qrc:/art/hills-light.png"
                                sourceSize.height: 180
                            }

                            Rectangle {
                                x: parent.width * 0.72
                                y: 12
                                width: 18
                                height: 18
                                radius: 9
                                color: themeCard.modelData.dark ? "#E8DCC4" : "#FCC58C"
                                opacity: 0.9
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: MD.Token.spacing.extra_small
                            spacing: MD.Token.spacing.small

                            MD.Icon {
                                name: themeCard.modelData.icon
                                size: 20
                                color: themeCard.selected ? MD.Token.color.primary : MD.Token.color.on_surface_variant
                            }

                            MD.Label {
                                Layout.fillWidth: true
                                text: themeCard.modelData.title
                                typescale: MD.Token.typescale.title_small
                                color: themeCard.selected ? MD.Token.color.on_surface : MD.Token.color.on_surface_variant
                            }

                            MD.Icon {
                                visible: themeCard.selected
                                name: MD.Token.icon.check_circle
                                size: 20
                                color: MD.Token.color.primary
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: MD.Token.shape.corner.medium
                        color: "transparent"
                        border.width: 2
                        border.color: MD.Token.color.primary
                        visible: themeCard.selected
                    }
                }
            }
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            text: qsTr("Palette")
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.label_large
        }

        MD.HorizontalListView {
            id: paletteListView
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            expand: true
            spacing: MD.Token.spacing.small
            implicitHeight: 40
            model: MD.PaletteModel {}

            MD.ActionGroup {
                id: paletteActionGroup
            }

            delegate: MD.InputChip {
                required property int index
                required property var model

                action: MD.Action {
                    T.ActionGroup.group: paletteActionGroup
                    icon.name: ""
                    checkable: true
                    checked: paletteListView.currentIndex === index
                    text: model.name
                    onTriggered: {
                        if (root.applying)
                            return
                        paletteListView.currentIndex = index
                        Appearance.setPaletteType(index)
                    }
                }
            }
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            text: qsTr("Primary")
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.label_large
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: root.meadow
            text: qsTr("Meadow brings its own sage, olive and rose. Pick another palette to choose a primary color.")
            color: MD.Token.color.on_surface_variant
            wrapMode: Text.WordWrap
            typescale: MD.Token.typescale.body_small
        }

        Grid {
            visible: !root.meadow
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            spacing: MD.Token.spacing.medium
            rows: 2
            columns: 6

            Repeater {
                model: AccentColors.palette

                MD.ColorRadio {
                    required property var modelData
                    size: 32
                    color: modelData.color
                    checked: Appearance.accentColor === modelData.color
                    onClicked: {
                        if (root.applying)
                            return
                        Appearance.setAccentColor(modelData.color)
                    }
                }
            }
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.topMargin: MD.Token.spacing.small
            text: qsTr("Language")
            typescale: MD.Token.typescale.label_large
        }

        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            spacing: MD.Token.spacing.small

            Repeater {
                model: root.languageOptions

                MD.InputChip {
                    required property var modelData

                    action: MD.Action {
                        checkable: true
                        checked: Core.settings.uiLanguage === modelData.code
                        text: modelData.label
                        onTriggered: Core.settings.uiLanguage = modelData.code
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.bottomMargin: MD.Token.spacing.medium
            spacing: MD.Token.spacing.small

            MD.Label {
                Layout.fillWidth: true
                text: qsTr("Community translations")
                typescale: MD.Token.typescale.title_small
            }

            MD.Label {
                Layout.fillWidth: true
                textFormat: Text.StyledText
                linkColor: MD.Token.color.primary
                text: Messages.settingsWeblateHint.arg(root.weblateTranslateUrl)
                wrapMode: Text.WordWrap
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.body_small
                onLinkActivated: link => Core.openExternalUrl(link)
            }

            MD.Button {
                text: qsTr("Help translate")
                icon.name: MD.Token.icon.language
                mdState.type: MD.Enum.BtText
                onClicked: Core.openExternalUrl(root.weblateTranslateUrl)
            }
        }
    }
}
