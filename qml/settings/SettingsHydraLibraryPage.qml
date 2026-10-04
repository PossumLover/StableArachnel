import QtQuick
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Flickable {
    id: root

    property int contentMargin: MD.Token.spacing.large
    property int sourcesRev: 0
    property bool loading: false
    property string loadError: ""
    property var librarySources: []
    property string search: ""
    // One check at a time: the validator drops a check when a newer one starts.
    property string addingUrl: ""
    property string validateRequestId: ""
    property var addErrors: ({})

    readonly property var addedUrls: {
        root.sourcesRev
        const urls = {}
        for (const row of Core.sources.manualCatalogs())
            urls[row.catalogUrl] = true
        return urls
    }

    readonly property var visibleSources: {
        const needle = root.search.trim().toLowerCase()
        if (!needle.length)
            return root.librarySources
        return root.librarySources.filter(source =>
            source.title.toLowerCase().includes(needle)
            || source.description.toLowerCase().includes(needle))
    }

    function reload() {
        root.loading = true
        root.loadError = ""
        Core.fetchHydraLibrarySources()
    }

    function setAddError(url, message) {
        const errors = Object.assign({}, root.addErrors)
        if (message.length)
            errors[url] = message
        else
            delete errors[url]
        root.addErrors = errors
    }

    function addSource(source) {
        if (root.addingUrl.length)
            return
        root.setAddError(source.url, "")
        root.addingUrl = source.url
        root.validateRequestId = "library-" + Date.now()
        Core.validateHydraCatalogUrl(root.validateRequestId, source.url)
    }

    function sourceDetails(source) {
        const parts = [qsTr("Games: %1").arg(Number(source.gamesCount).toLocaleString(Qt.locale(), "f", 0))]
        for (const label of source.labels)
            parts.push(label)
        return parts.join(" · ")
    }

    Component.onCompleted: reload()

    Connections {
        target: Core

        function onHydraLibrarySourcesLoaded(sources) {
            root.loading = false
            root.librarySources = sources
        }

        function onHydraLibrarySourcesFailed(error) {
            root.loading = false
            root.loadError = error
        }

        function onHydraCatalogUrlValidated(requestId, ok, count, error) {
            if (requestId !== root.validateRequestId)
                return
            const url = root.addingUrl
            root.addingUrl = ""
            root.validateRequestId = ""
            if (!ok) {
                root.setAddError(url, error.length ? error
                                                   : qsTr("Could not load catalog from this URL."))
                return
            }
            const source = root.librarySources.find(s => s.url === url)
            const id = Core.sources.addSource(source ? source.title : url, url,
                                              source ? source.description : "", "")
            if (!id.length) {
                root.setAddError(url, qsTr("Could not add catalog."))
                return
            }
            Core.prefetchCatalogCounts()
        }
    }

    Connections {
        target: Core.sources
        function onSourcesChanged() {
            root.sourcesRev++
        }
    }

    contentWidth: width
    contentHeight: body.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick

    ColumnLayout {
        id: body
        width: root.width
        spacing: MD.Token.spacing.medium

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            Layout.topMargin: MD.Token.spacing.small
            text: Messages.settingsHydraLibraryHint
            wrapMode: Text.WordWrap
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.body_medium
        }

        AppTextField {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: root.librarySources.length > 0
            placeholderText: qsTr("Search sources")
            leadingIcon: MD.Token.icon.search
            text: root.search
            onTextEdited: root.search = text
        }

        MD.LinearIndicator {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: root.loading
            implicitHeight: 4
            strokeWidth: implicitHeight
            indeterminate: true
            running: root.loading && root.visible
            color: MD.Token.color.primary
            trackColor: MD.Util.transparent(color, 0.2)
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: root.loading
            text: qsTr("Loading sources…")
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.body_small
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: !root.loading && root.loadError.length > 0
            spacing: MD.Token.spacing.small

            MD.Label {
                Layout.fillWidth: true
                text: qsTr("Couldn't load Hydra Library: %1").arg(root.loadError)
                wrapMode: Text.WordWrap
                color: MD.Token.color.error
                typescale: MD.Token.typescale.body_medium
            }

            MD.Button {
                mdState.type: MD.Enum.BtOutlined
                text: qsTr("Try again")
                icon.name: MD.Token.icon.refresh
                onClicked: root.reload()
            }
        }

        MD.Label {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: !root.loading && root.librarySources.length > 0
                     && root.visibleSources.length === 0
            text: qsTr("No sources match.")
            color: MD.Token.color.on_surface_variant
            typescale: MD.Token.typescale.body_medium
        }

        MD.ElevationRectangle {
            Layout.fillWidth: true
            Layout.leftMargin: contentMargin
            Layout.rightMargin: contentMargin
            visible: root.visibleSources.length > 0
            implicitHeight: sourceCol.implicitHeight + MD.Token.spacing.small * 2
            radius: MD.Token.shape.corner.large
            color: MD.Token.color.surface_container
            elevation: MD.Token.elevation.level0

            ColumnLayout {
                id: sourceCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: MD.Token.spacing.small
                spacing: 0

                Repeater {
                    model: root.visibleSources

                    ColumnLayout {
                        id: sourceRow
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        spacing: 0

                        readonly property bool added: !!root.addedUrls[modelData.url]
                        readonly property bool adding: root.addingUrl === modelData.url
                        readonly property string addError: root.addErrors[modelData.url] || ""

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: MD.Token.spacing.small
                            Layout.rightMargin: MD.Token.spacing.small
                            Layout.topMargin: MD.Token.spacing.small
                            Layout.bottomMargin: MD.Token.spacing.small
                            spacing: MD.Token.spacing.medium

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: MD.Token.spacing.extra_small

                                MD.Label {
                                    Layout.fillWidth: true
                                    text: sourceRow.modelData.title
                                    typescale: MD.Token.typescale.body_large
                                    elide: Text.ElideRight
                                }

                                MD.Label {
                                    Layout.fillWidth: true
                                    text: root.sourceDetails(sourceRow.modelData)
                                    color: MD.Token.color.on_surface_variant
                                    typescale: MD.Token.typescale.body_small
                                    elide: Text.ElideRight
                                }

                                MD.Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: sourceRow.modelData.description
                                    color: MD.Token.color.on_surface_variant
                                    typescale: MD.Token.typescale.body_small
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                MD.Label {
                                    Layout.fillWidth: true
                                    visible: sourceRow.addError.length > 0
                                    text: sourceRow.addError
                                    color: MD.Token.color.error
                                    typescale: MD.Token.typescale.body_small
                                    wrapMode: Text.WordWrap
                                }
                            }

                            MD.Button {
                                Layout.alignment: Qt.AlignVCenter
                                mdState.type: sourceRow.added ? MD.Enum.BtText : MD.Enum.BtFilledTonal
                                text: sourceRow.added ? qsTr("Added")
                                                      : (sourceRow.adding ? qsTr("Checking…")
                                                                          : qsTr("Add"))
                                enabled: !sourceRow.added && root.addingUrl.length === 0
                                onClicked: root.addSource(sourceRow.modelData)
                            }
                        }

                        MD.Divider {
                            Layout.fillWidth: true
                            visible: sourceRow.index < root.visibleSources.length - 1
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: MD.Token.spacing.medium
        }
    }
}
