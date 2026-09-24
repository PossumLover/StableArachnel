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

            ToneBadge {
                Layout.alignment: Qt.AlignVCenter
                iconName: root.iconName
                tone: root.tone
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
