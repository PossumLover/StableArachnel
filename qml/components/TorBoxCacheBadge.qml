import QtQuick
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    id: root
    property string cacheHash: ""
    property bool directDownload: false
    property int revision: 0
    readonly property string status: {
        root.revision
        return Core.torboxCacheStatus(root.cacheHash)
    }
    visible: directDownload || (cacheHash.length > 0 && Core.settings.torboxEnabled)
    padding: MD.Token.spacing.small
    radius: MD.Token.shape.corner.small
    backgroundColor: status === "cached" ? MD.Token.color.secondary_container : MD.Token.color.surface_container_high
    Connections {
        target: Core
        function onTorboxCacheChanged() { root.revision++ }
    }
    contentItem: MD.Label {
        objectName: "cacheStatusLabel"
        text: {
            if (root.directDownload)
                return qsTr("Direct download")
            if (!Core.settings.torboxApiKey.length)
                return qsTr("TorBox key needed")
            if (root.status === "cached")
                return qsTr("Cached on TorBox")
            if (root.status === "uncached")
                return qsTr("Not cached on TorBox")
            if (root.status === "unavailable")
                return qsTr("Cache status unavailable")
            return qsTr("Checking TorBox cache...")
        }
        color: root.status === "cached" ? MD.Token.color.on_secondary_container : MD.Token.color.on_surface_variant
        typescale: MD.Token.typescale.label_medium
    }
}
