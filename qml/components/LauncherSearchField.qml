import QtQuick
import Qcm.Material as MD

MD.TextFieldEmbed {
    id: field
    Accessible.name: placeholderText
    color: MD.Token.color.on_surface
    MD.Label {
        x: field.leftPadding
        width: Math.max(0, field.width - field.leftPadding - field.rightPadding)
        anchors.verticalCenter: parent.verticalCenter
        visible: !field.text.length && !field.preeditText.length
        text: field.placeholderText
        elide: Text.ElideRight
        color: MD.Token.color.on_surface_variant
        typescale: MD.Token.typescale.body_large
    }
    leftPadding: !mirrored || clearButton.visible ? 48 : MD.Token.spacing.large
    rightPadding: mirrored || clearButton.visible ? 48 : MD.Token.spacing.large
    MD.IconButton {
        id: clearButton
        z: 1
        visible: field.text.length > 0
        x: field.mirrored ? 4 : field.width - width - 4
        anchors.verticalCenter: parent.verticalCenter
        icon.name: MD.Token.icon.close
        Accessible.name: qsTr("Clear search")
        onClicked: { field.clear(); field.forceActiveFocus() }
    }

    background: Rectangle {
        implicitHeight: 48
        implicitWidth: 160
        radius: height / 2
        color: MD.Token.color.surface_container_high
        border.width: field.activeFocus ? 2 : 0
        border.color: MD.Token.color.primary
        MD.Icon {
            x: field.mirrored ? parent.width - width - 16 : 16
            anchors.verticalCenter: parent.verticalCenter
            name: MD.Token.icon.search
            size: 20
            color: MD.Token.color.on_surface_variant
        }
    }
}
