import QtQuick
import Qcm.Material as MD

Row {
    id: root
    property var icons: []
    property var tips: []
    property var labels: []
    property int currentIndex: 0
    property real segmentWidth: 48
    signal picked(int index)
    spacing: 2

    Repeater {
        model: Math.max(root.icons.length, root.labels.length)
        MD.SegmentedButton {
            id: segment
            required property int index
            objectName: "segment" + index
            width: root.segmentWidth
            text: root.labels[index] ?? ""
            checked: index === root.currentIndex
            checkable: false // The owner supplies the single selection.
            focusPolicy: Qt.StrongFocus
            Accessible.checkable: true
            Accessible.checked: checked
            Accessible.name: root.tips[index] ?? text
            position: index === 0 ? MD.Enum.PosFirst : MD.Enum.PosLast
            mdState.corners: position === MD.Enum.PosFirst
                ? MD.Util.corners(height / 2, 4, height / 2, 4)
                : MD.Util.corners(4, height / 2, 4, height / 2)
            contentItem: Item {
                MD.Icon {
                    anchors.centerIn: parent
                    visible: !segment.text
                    name: root.icons[segment.index] ?? ""
                    color: segment.mdState.textColor
                    Accessible.ignored: true
                }
                MD.Label {
                    anchors.centerIn: parent
                    visible: !!segment.text
                    text: segment.text
                    typescale: MD.Token.typescale.label_large
                    color: segment.mdState.textColor
                    Accessible.ignored: true
                }
            }
            onClicked: root.picked(index)
            MD.FocusIndicator { anchors.fill: parent; corners: segment.mdState.corners; active: segment.visualFocus }
            MD.ToolTip {
                text: root.tips[segment.index] ?? ""
                visible: segment.hovered && !!text
            }
        }
    }
}
