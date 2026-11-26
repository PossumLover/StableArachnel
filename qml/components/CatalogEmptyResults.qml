import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Item {
    id: root

    required property var page
    required property int pageMargin
    signal chooseSources(Item button)
    readonly property bool loading: !page.noSourceSelected && (Core.catalogLoading || Core.catalogPartial)

    Item {
        id: emptyResultsCard
        anchors.centerIn: parent
        width: Math.min(parent.width - pageMargin * 2, 720)
        height: 220
        clip: true

        layer.enabled: true
        layer.effect: MD.RoundClip {
            corners: MD.Util.corners(page.cardRadius)
            size: Qt.vector2d(emptyResultsCard.width, emptyResultsCard.height)
        }

        Rectangle {
            anchors.fill: parent
            color: MD.Token.color.surface_container_high
        }

        MeadowMark {
            id: poppies
            variant: "spray"
            visible: parent.width >= 420
            width: Math.min(parent.width * 0.32, 218)
            height: parent.height
            anchors.bottom: parent.bottom
            anchors.right: parent.right
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: MD.Token.spacing.extra_large
            anchors.rightMargin: MD.Token.spacing.extra_large + (poppies.visible ? poppies.width : 0)
            spacing: MD.Token.spacing.small

            MD.Label {
                Layout.fillWidth: true
                text: page.noSourceSelected
                      ? qsTr("Select sources")
                      : root.loading ? qsTr("Loading catalog…") : qsTr("Nothing found")
                typescale: MD.Token.typescale.title_large
            }

            MD.Label {
                Layout.fillWidth: true
                visible: !root.loading
                text: page.noSourceSelected
                      ? Messages.catalogEnableChipsHint
                      : page.searchQuery ? qsTr("Try another search.")
                      : Core.catalogActiveFilterCount > 0 ? qsTr("Try a different genre or clear your filters.")
                      : qsTr("Try another search or refresh the catalog.")
                color: MD.Token.color.on_surface_variant
                typescale: MD.Token.typescale.body_medium
                wrapMode: Text.WordWrap
            }

            MD.Button {
                id: recovery
                visible: !root.loading
                text: page.noSourceSelected ? qsTr("Choose sources") : page.searchQuery ? qsTr("Clear search")
                      : Core.catalogActiveFilterCount > 0 ? qsTr("Clear filters") : qsTr("Refresh catalogs")
                mdState.type: MD.Enum.BtFilledTonal
                onClicked: {
                    if (page.noSourceSelected) root.chooseSources(recovery)
                    else if (page.searchQuery) page.applyCatalogSearch("")
                    else if (Core.catalogActiveFilterCount > 0) Core.clearCatalogFilters()
                    else Core.refreshSelectedCatalogs()
                }
            }
        }
    }
}
