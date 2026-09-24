import QtQuick
import QtQuick.Layouts

import Qcm.Material as MD

Item {
    id: root

    property string title: ""
    property string value: ""
    property string iconName: ""
    // 0 sage (primary), 1 olive (secondary), 2 dusty rose (tertiary), 3 apricot sun.
    property int tone: 0

    readonly property color _chipColor: tone === 1 ? MD.Token.color.secondary_container
                                       : tone === 2 ? MD.Token.color.tertiary_container
                                       : tone === 3 ? (MD.Token.isDarkTheme ? "#6B4A30" : "#FBDDB9")
                                       : MD.Token.color.primary_container
    readonly property color _chipInk: tone === 1 ? MD.Token.color.on_secondary_container
                                     : tone === 2 ? MD.Token.color.on_tertiary_container
                                     : tone === 3 ? (MD.Token.isDarkTheme ? "#FCD7B0" : "#6E4318")
                                     : MD.Token.color.on_primary_container

    implicitHeight: 88
    Layout.fillWidth: true
    Layout.minimumHeight: 88

    MD.ElevationRectangle {
        anchors.fill: parent
        radius: MD.Token.shape.corner.extra_large
        color: MD.Token.color.surface_container
        elevation: MD.Token.elevation.level1

        RowLayout {
            anchors.fill: parent
            anchors.margins: MD.Token.spacing.medium
            spacing: MD.Token.spacing.medium

            MD.ElevationRectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignVCenter
                radius: MD.Token.shape.corner.full
                color: root._chipColor
                elevation: MD.Token.elevation.level0

                MD.Icon {
                    anchors.centerIn: parent
                    name: root.iconName
                    size: 24
                    color: root._chipInk
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                MD.Label {
                    Layout.fillWidth: true
                    text: root.value
                    typescale: MD.Token.typescale.headline_small
                    elide: Text.ElideRight
                }

                MD.Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: MD.Token.color.on_surface_variant
                    typescale: MD.Token.typescale.label_large
                    elide: Text.ElideRight
                }
            }
        }
    }
}
