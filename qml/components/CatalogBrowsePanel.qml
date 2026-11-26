import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Rectangle {
    id: root

    required property var page
    required property CatalogGenreNavigation navigation
    property bool expanded: false
    property bool compact: false
    property bool refining: false
    signal dismissed()
    function pickGenre(key) {
        Core.catalogGenreFilter = key
        if (compact)
            dismissed()
    }

    color: MD.Token.color.surface_container_low
    radius: MD.Token.shape.corner.large

    function genreIcon(key) {
        switch (key) {
        case "Action":
            return MD.Token.icon.bolt
        case "Adventure":
            return MD.Token.icon.explore
        case "RPG":
            return MD.Token.icon.swords
        case "Strategy":
            return MD.Token.icon.chess_pawn
        case "Simulation":
            return MD.Token.icon.tune
        case "Shooter":
            return MD.Token.icon.target
        case "Horror":
            return MD.Token.icon.skull
        case "Casual":
            return MD.Token.icon.weekend
        case "Sports":
            return MD.Token.icon.sports_soccer
        case "Racing":
            return MD.Token.icon.sports_motorsports
        case "Puzzle":
            return MD.Token.icon.extension
        case "Platformer":
            return MD.Token.icon.sports_gymnastics
        case "Fighting":
            return MD.Token.icon.sports_mma
        case "Survival":
            return MD.Token.icon.forest
        case "Roguelike":
            return MD.Token.icon.casino
        case "Visual Novel":
            return MD.Token.icon.menu_book
        case "Card":
            return MD.Token.icon.style
        default:
            return MD.Token.icon.category
        }
    }

    component SectionLabel: MD.Label {
        Layout.fillWidth: true
        typescale: MD.Token.typescale.title_small
        color: MD.Token.color.on_surface_variant
    }

    component Options: ColumnLayout {
        id: options
        required property string heading
        required property var labels
        required property int value
        signal picked(int value)
        Layout.fillWidth: true
        spacing: MD.Token.spacing.extra_small

        SectionLabel { text: options.heading }
        Flow {
            Layout.fillWidth: true
            spacing: MD.Token.spacing.extra_small
            Repeater {
                model: Object.keys(options.labels).map(Number).sort((a, b) => a - b)
                MD.FilterChip {
                    required property int modelData
                    text: options.labels[String(modelData)]
                    checked: options.value === modelData
                    checkable: false
                    onClicked: options.picked(modelData)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: MD.Token.spacing.medium
        spacing: MD.Token.spacing.small

        RowLayout {
            Layout.fillWidth: true
            ConnectedIconToggle {
                Layout.fillWidth: true
                segmentWidth: (width - spacing) / 2
                labels: [qsTr("Genres"), qsTr("Filters")]
                currentIndex: root.refining ? 1 : 0
                onPicked: index => root.refining = index === 1
            }
            MD.IconButton {
                visible: root.compact
                icon.name: MD.Token.icon.close
                Accessible.name: qsTr("Close")
                onClicked: root.dismissed()
            }
        }

        LauncherSearchField {
            id: genreSearch
            objectName: "genreSearch"
            visible: !root.refining && Core.availableCatalogGenres.length > 0
            Layout.fillWidth: true
            placeholderText: qsTr("Find a genre")
        }

        ListView {
            id: genres
            objectName: "genreList"
            Accessible.description: qsTr("Counts cover games in visible sources, before search and other filters.")
            visible: !root.refining
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: MD.Token.spacing.extra_small
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: MD.ScrollBar {}
            model: {
                const query = genreSearch.text.trim().toLocaleLowerCase()
                if (!query)
                    return [""].concat(root.navigation.withSelection(root.expanded ? root.navigation.ranked : root.navigation.primary))
                return root.navigation.ranked.filter(key => Core.catalogGenreLabel(key).toLocaleLowerCase().includes(query))
            }
            delegate: MD.Button {
                id: genreButton
                required property string modelData
                readonly property string countLabel: {
                    const count = Core.catalogGenreCounts[modelData] ?? 0
                    if (root.navigation.total < 0 || (!count && (Core.catalogPartial || Core.catalogLoading)))
                        return ""
                    return count.toLocaleString(Qt.locale(), "f", 0)
                }
                Accessible.name: text + " " + countLabel
                Accessible.checkable: true
                Accessible.checked: Core.catalogGenreFilter === modelData
                width: genres.width - MD.Token.spacing.small
                text: modelData ? Core.catalogGenreLabel(modelData) : qsTr("All games")
                icon.name: modelData ? root.genreIcon(modelData) : MD.Token.icon.apps
                mdState.type: Core.catalogGenreFilter === modelData ? MD.Enum.BtFilledTonal : MD.Enum.BtText
                // Keep the icon and label aligned down the entire list.
                contentItem: RowLayout {
                    spacing: MD.Token.spacing.medium
                    MD.Icon { name: genreButton.icon.name; Accessible.ignored: true }
                    MD.Label {
                        Layout.fillWidth: true
                        text: genreButton.text
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        maximumLineCount: 1
                        typescale: MD.Token.typescale.label_large
                    }
                    MD.Label {
                        text: genreButton.countLabel
                        typescale: MD.Token.typescale.label_medium
                        color: MD.Token.color.on_surface_variant
                        Accessible.ignored: true
                    }
                }
                onClicked: root.pickGenre(modelData)
            }
            MD.Label {
                anchors.centerIn: parent
                visible: genres.count === 0
                text: qsTr("No matching genres")
                color: MD.Token.color.on_surface_variant
            }
        }

        MD.Button {
            objectName: "moreGenres"
            visible: !root.refining && !genreSearch.text.trim()
                     && root.navigation.ranked.length > root.navigation.primary.length
            text: root.expanded ? qsTr("Fewer genres") : qsTr("More genres")
            mdState.type: MD.Enum.BtText
            onClicked: root.expanded = !root.expanded
        }

        Flickable {
            visible: root.refining
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: filters.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: MD.ScrollBar {}
            ColumnLayout {
                id: filters
                width: parent.width - MD.Token.spacing.small
                spacing: MD.Token.spacing.medium
                ColumnLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: qsTr("Sources") }
                    CatalogSourceChips {
                        Layout.fillWidth: true
                        hiddenIds: Core.hiddenCatalogSourceIds
                        onSourceToggled: sourceId => Core.setCatalogSourceHidden(sourceId, !hiddenIds.includes(sourceId))
                    }
                }
                Options {
                    heading: qsTr("Players")
                    labels: root.page.playModeFilterLabels
                    value: Core.catalogPlayModeFilter
                    onPicked: value => Core.catalogPlayModeFilter = value
                }
                Options {
                    heading: qsTr("Download size")
                    labels: root.page.sizeFilterLabels
                    value: Core.catalogSizeFilter
                    onPicked: value => Core.catalogSizeFilter = value
                }
                Options {
                    heading: qsTr("Added")
                    labels: root.page.recencyFilterLabels
                    value: Core.catalogRecencyFilter
                    onPicked: value => Core.catalogRecencyFilter = value
                }
                Options {
                    heading: qsTr("Package type")
                    labels: root.page.typeFilterLabels
                    value: Core.catalogTypeFilter
                    onPicked: value => Core.catalogTypeFilter = value
                }
                MD.FilterChip {
                    text: qsTr("Has add-ons")
                    checkable: false
                    checked: Core.catalogHasAddonsFilter
                    onClicked: Core.catalogHasAddonsFilter = !Core.catalogHasAddonsFilter
                }
                RowLayout {
                    Layout.fillWidth: true
                    MD.Label {
                        Layout.fillWidth: true
                        text: qsTr("Hide adult games")
                        wrapMode: Text.WordWrap
                    }
                    MD.Switch {
                        Accessible.name: qsTr("Hide adult games")
                        checked: Core.settings.hideAdultGames
                        onToggled: Core.settings.hideAdultGames = checked
                    }
                }
                MD.Button {
                    text: qsTr("Clear all filters")
                    enabled: Core.catalogActiveFilterCount > 0
                    mdState.type: MD.Enum.BtText
                    onClicked: Core.clearCatalogFilters()
                }
            }
        }
    }
}
