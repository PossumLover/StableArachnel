import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Flickable {
    id: root

    property int contentMargin: MD.Token.spacing.large
    property bool applying: false

    readonly property var languageOptions: [
        { code: "en", label: qsTr("English") },
        { code: "ru", label: qsTr("Russian") }
    ]

    readonly property string weblateTranslateUrl: "https://hosted.weblate.org/projects/arachnel/application/"

    contentWidth: width
    contentHeight: body.implicitHeight + MD.Token.spacing.extra_large
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    function syncFromStore() {
        applying = true
        Appearance.apply()
        applying = false
    }

    Component.onCompleted: syncFromStore()

    SettingsColumn {
        id: body
        margin: root.contentMargin

        SettingsGroup {
            title: qsTr("Theme")
            description: qsTr("Theme and colors apply across the app.")

            // Theme as two little meadow postcards: day and dusk.
            SettingsRow {
                title: qsTr("Light or dark")
                iconName: MD.Token.icon.dark_mode

                below: RowLayout {
                    Layout.fillWidth: true
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
            }

            SettingsSwitchRow {
                title: qsTr("Reduce motion")
                iconName: MD.Token.icon.animation
                checked: Appearance.reduceMotion
                onToggled: function (on) {
                    if (!root.applying)
                        Appearance.setReduceMotion(on)
                }
            }
        }

        SettingsGroup {
            title: qsTr("Colors")

            SettingsRow {
                title: qsTr("App colour")
                subtitle: qsTr("Each circle shows the colours the app will use.")
                iconName: MD.Token.icon.palette

                below: ThemeColorPicker {
                    Layout.fillWidth: true
                    Layout.topMargin: MD.Token.spacing.small
                    applying: root.applying
                }
            }
        }

        SettingsGroup {
            title: qsTr("Language")

            SettingsRow {
                title: qsTr("Interface language")
                iconName: MD.Token.icon.language

                ConnectedTextToggle {
                    labels: root.languageOptions.map(o => o.label)
                    currentIndex: Math.max(0, root.languageOptions.findIndex(o => o.code === Core.settings.uiLanguage))
                    onPicked: function (index) { Core.settings.uiLanguage = root.languageOptions[index].code }
                }
            }

            SettingsRow {
                title: qsTr("Community translations")
                subtitle: qsTr("Missing your language? Help translate Sprout on Weblate.")
                iconName: MD.Token.icon.translate
                clickable: true
                showChevron: true
                onClicked: Core.openExternalUrl(root.weblateTranslateUrl)
            }
        }
    }
}
