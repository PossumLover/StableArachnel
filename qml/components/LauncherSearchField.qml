import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

MD.TextFieldEmbed {
    id: field
    Accessible.name: placeholderText
    color: MD.Token.color.on_surface
    readonly property real leadingSpace: MD.Token.spacing.medium + searchIcon.implicitWidth + content.spacing
    readonly property real trailingSpace: clearButton.visible
        ? MD.Token.spacing.extra_small + clearButton.implicitWidth + content.spacing : MD.Token.spacing.medium
    leftPadding: mirrored ? trailingSpace : leadingSpace
    rightPadding: mirrored ? leadingSpace : trailingSpace

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: MD.Token.spacing.medium
        anchors.rightMargin: clearButton.visible ? MD.Token.spacing.extra_small : MD.Token.spacing.medium
        spacing: MD.Token.spacing.small
        MD.Icon {
            id: searchIcon
            name: MD.Token.icon.search
            size: 20
            color: MD.Token.color.on_surface_variant
        }
        MD.Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            opacity: !field.text.length && !field.preeditText.length ? 1 : 0
            text: field.placeholderText
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.body_large
            Accessible.ignored: true
        }
        MD.IconButton {
            id: clearButton
            visible: field.text.length > 0
            icon.name: MD.Token.icon.close
            Accessible.name: qsTr("Clear search")
            onClicked: { field.clear(); field.forceActiveFocus() }
        }
    }

    background: Rectangle {
        implicitHeight: 48
        implicitWidth: 160
        radius: height / 2
        color: MD.Token.color.surface_container_high
        border.width: field.activeFocus ? 2 : 0
        border.color: MD.Token.color.primary
    }
}
