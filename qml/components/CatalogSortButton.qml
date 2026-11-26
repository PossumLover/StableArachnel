import QtQuick
import QtQuick.Controls

import Arachnel.Core 1.0
import Qcm.Material as MD

Item {
    id: root

    property var sortOptions: []
    property bool compact: false
    readonly property string activeLabel: sortOptions.find(option => option.mode === Core.catalog.sortMode)?.label ?? qsTr("Sort")

    FontMetrics { id: metrics; font: btn.font }
    implicitWidth: compact ? btn.implicitWidth
        : Math.ceil(sortOptions.reduce((w, option) => Math.max(w, metrics.advanceWidth(option.label)), 0))
          + btn.leftPadding + btn.rightPadding + btn.spacing + btn.icon.width
    implicitHeight: btn.implicitHeight

    MD.Button {
        id: btn
        objectName: "catalogSortTrigger"
        anchors.fill: parent
        text: root.activeLabel
        iconStyle: root.compact ? MD.Enum.IconOnly : MD.Enum.IconAndText
        Accessible.name: qsTr("Sort: %1").arg(root.activeLabel)
        mdState.type: MD.Enum.BtText
        icon.name: MD.Token.icon.sort
        onClicked: sortMenu.open()

        MD.Menu {
            id: sortMenu
            objectName: "catalogSortMenu"
            y: parent.height
            autoClose: true

            Repeater {
                model: root.sortOptions

                MD.MenuItem {
                    required property var modelData
                    text: modelData.label
                    checked: Core.catalog.sortMode === modelData.mode
                    selected: checked
                    icon.name: checked ? MD.Token.icon.check : ""
                    onTriggered: root.sortModeChosen(modelData.mode)
                }
            }
        }
    }

    signal sortModeChosen(int mode)
}
