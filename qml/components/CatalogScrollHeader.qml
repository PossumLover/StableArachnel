import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Item {
    id: root

    required property bool hasSelection
    required property bool listViewMode
    required property bool showPanelButton
    property var sortOptions: []

    signal filterRequested()
    signal viewModeChangeRequested(int mode)
    signal refreshRequested()
    signal sortModeRequested(int mode)

    implicitHeight: col.implicitHeight
    ColumnLayout {
        id: col
        width: root.width
        spacing: MD.Token.spacing.small

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: MD.Token.spacing.small
            Layout.rightMargin: MD.Token.spacing.small
            spacing: MD.Token.spacing.small

            Item {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 20
                visible: Core.catalogLoading

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: MD.Util.transparent(MD.Token.color.primary, 0.12)
                }

                MD.LinearIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    implicitHeight: 4
                    strokeWidth: implicitHeight
                    indeterminate: true
                    running: Core.catalogLoading && root.visible
                    color: MD.Token.color.primary
                    trackColor: MD.Util.transparent(color, 0.2)
                }
            }

            MD.Label {
                Layout.fillWidth: true
                text: {
                    if (Core.catalogPartial || (Core.catalogLoading && Core.catalog.count === 0))
                        return qsTr("Loading catalog…")
                    if (Core.catalog.count === 1)
                        return qsTr("1 game")
                    return qsTr("%1 games").arg(Core.catalog.count.toLocaleString(Qt.locale(), "f", 0))
                }
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.label_large
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            CatalogSortButton {
                sortOptions: root.sortOptions
                onSortModeChosen: function (mode) { root.sortModeRequested(mode) }
            }

            MD.Button {
                visible: root.showPanelButton
                text: qsTr("Filters")
                icon.name: MD.Token.icon.filter_list
                mdState.type: Core.catalogActiveFilterCount > 0 ? MD.Enum.BtFilledTonal : MD.Enum.BtText
                onClicked: root.filterRequested()
            }

            ConnectedIconToggle {
                icons: [MD.Token.icon.grid_view, MD.Token.icon.view_list]
                tips: [qsTr("Grid"), qsTr("List")]
                currentIndex: root.listViewMode ? 1 : 0
                onPicked: function (index) { root.viewModeChangeRequested(index) }
            }

            MD.IconButton {
                mdState.type: MD.Enum.IBtStandard
                Accessible.name: qsTr("Refresh")
                icon.name: MD.Token.icon.refresh
                enabled: !Core.catalogLoading && root.hasSelection
                onClicked: root.refreshRequested()
            }
        }

        Item {
            Layout.preferredHeight: MD.Token.spacing.extra_small
        }
    }
}
