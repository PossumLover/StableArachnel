import QtQuick
import QtQuick.Layouts
import Arachnel.Core 1.0
import Qcm.Material as MD

ColumnLayout {
    id: root
    required property string entryId
    property var offers: []
    visible: Core.settings.torboxEnabled && offers.some(offer => (offer.cacheHash || "").length > 0)
    spacing: MD.Token.spacing.small
    function refresh() {
        offers = entryId.length ? Core.installOffersForEntry(entryId) : []
        if (entryId.length)
            Core.checkTorboxCache(entryId)
    }
    onEntryIdChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: Core.settings
        function onDebridChanged() { root.refresh() }
    }
    Connections {
        target: Core
        function onCatalogCountsChanged() { root.refresh() }
    }
    Timer {
        interval: 60000
        repeat: true
        running: root.visible
        onTriggered: Core.checkTorboxCache(root.entryId)
    }
    Repeater {
        model: root.offers.filter(offer => (offer.cacheHash || "").length > 0)
        RowLayout {
            required property var modelData
            Layout.fillWidth: true
            MD.Label {
                Layout.fillWidth: true
                text: modelData.sourceName || modelData.sourceId
                typescale: MD.Token.typescale.body_small
                color: MD.Token.color.on_surface_variant
                elide: Text.ElideRight
            }
            TorBoxCacheBadge { cacheHash: modelData.cacheHash }
        }
    }
}
