import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

FocusScope {
    id: root
    property alias text: editor.text
    property alias placeholderText: editor.placeholderText
    signal accepted()

    function focusEditor() { editor.forceActiveFocus() }

    implicitWidth: 160
    implicitHeight: Math.max(48, content.implicitHeight)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: MD.Token.color.surface_container_high
        border.width: root.activeFocus ? 2 : 0
        border.color: MD.Token.color.primary
        TapHandler { onTapped: editor.forceActiveFocus(Qt.MouseFocusReason) }
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: MD.Token.spacing.medium
        anchors.rightMargin: clearButton.visible ? MD.Token.spacing.extra_small : MD.Token.spacing.medium
        spacing: MD.Token.spacing.medium_small
        MD.Icon {
            objectName: "searchIcon"
            name: MD.Token.icon.search
            size: 20
            color: MD.Token.color.on_surface_variant
            Accessible.ignored: true
        }
        MD.TextFieldEmbed {
            id: editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            focus: true
            clip: true
            color: MD.Token.color.on_surface
            Accessible.name: placeholderText
            onAccepted: root.accepted()

            MD.Label {
                anchors.fill: parent
                visible: !editor.text.length && !editor.preeditText.length
                text: editor.placeholderText
                font: editor.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: MD.Token.color.on_surface_variant
                Accessible.ignored: true
            }
        }
        MD.IconButton {
            id: clearButton
            objectName: "clearSearchButton"
            visible: editor.text.length > 0
            icon.name: MD.Token.icon.close
            Accessible.name: qsTr("Clear search")
            onClicked: { editor.clear(); editor.forceActiveFocus() }
        }
    }
}
