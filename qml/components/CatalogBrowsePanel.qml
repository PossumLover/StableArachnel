import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Arachnel.Core 1.0
import Qcm.Material as MD

Rectangle {
    id: root

    required property var page
    property bool compact: false
    property bool refining: false
    signal dismissed()

    color: MD.Token.color.surface_container_low
    radius: MD.Token.shape.corner.large

    function genreIcon(key) {
        switch (key) {
        case "Action":
            return MD.Token.icon.sports_esports
        case "Adventure":
            return MD.Token.icon.explore
        case "RPG":
            return MD.Token.icon.swords
        case "Strategy":
            return MD.Token.icon.psychology
        case "Simulation":
            return MD.Token.icon.precision_manufacturing
        case "Shooter":
            return MD.Token.icon.ads_click
        case "Horror":
            return MD.Token.icon.dark_mode
        case "Indie":
            return MD.Token.icon.extension
        case "Casual":
            return MD.Token.icon.mood
        case "Sports":
            return MD.Token.icon.sports
        case "Racing":
            return MD.Token.icon.directions_car
        case "Puzzle":
            return MD.Token.icon.grid_4x4
        case "Platformer":
            return MD.Token.icon.stairs
        case "Fighting":
            return MD.Token.icon.sports_mma
        case "Survival":
            return MD.Token.icon.forest
        case "Open World":
            return MD.Token.icon.landscape
        case "Roguelike":
            return MD.Token.icon.replay
        case "Visual Novel":
            return MD.Token.icon.menu_book
        case "Card":
            return MD.Token.icon.style
        case "Early Access":
            return MD.Token.icon.rocket_launch
        case "Free to Play":
            return MD.Token.icon.money_off
        case "Massively Multiplayer":
            // `public` is a reserved word the Qt 6.8 QML parser refuses as a
            // member name; bracket access reads the same icon on every version.
            return MD.Token.icon["public"]
        case "VR":
            return MD.Token.icon.view_in_ar
        default:
            return MD.Token.icon.sports_esports
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
            MD.Button {
                Accessible.checkable: true
                Accessible.checked: !root.refining
                text: qsTr("Genres")
                mdState.type: root.refining ? MD.Enum.BtText : MD.Enum.BtFilledTonal
                onClicked: root.refining = false
            }
            MD.Button {
                Accessible.checkable: true
                Accessible.checked: root.refining
                text: qsTr("Filters")
                mdState.type: root.refining ? MD.Enum.BtFilledTonal : MD.Enum.BtText
                onClicked: root.refining = true
            }
            Item { Layout.fillWidth: true }
            MD.IconButton {
                visible: root.compact
                icon.name: MD.Token.icon.close
                Accessible.name: qsTr("Close")
                onClicked: root.dismissed()
            }
        }

        AppTextField {
            id: genreSearch
            objectName: "genreSearch"
            visible: !root.refining && Core.availableCatalogGenres.length > 0
            Layout.fillWidth: true
            placeholderText: qsTr("Find a genre")
            leadingIcon: MD.Token.icon.search
        }

        ListView {
            id: genres
            objectName: "genreList"
            visible: !root.refining
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: MD.Token.spacing.extra_small
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: MD.ScrollBar {}
            model: {
                const query = genreSearch.text.trim().toLocaleLowerCase()
                const keys = Core.availableCatalogGenres
                if (!query)
                    return [""].concat(Array.from(keys))
                return Array.from(keys).filter(key => Core.catalogGenreLabel(key).toLocaleLowerCase().includes(query))
            }
            delegate: MD.Button {
                id: genreButton
                required property string modelData
                Accessible.checkable: true
                Accessible.checked: Core.catalogGenreFilter === modelData
                width: genres.width - MD.Token.spacing.small
                text: modelData ? Core.catalogGenreLabel(modelData) : qsTr("All games")
                icon.name: modelData ? root.genreIcon(modelData) : MD.Token.icon.apps
                mdState.type: Core.catalogGenreFilter === modelData ? MD.Enum.BtFilledTonal : MD.Enum.BtText
                // Keep the icon and label aligned down the entire list.
                contentItem: RowLayout {
                    spacing: MD.Token.spacing.medium
                    MD.Icon { name: genreButton.icon.name }
                    MD.Label {
                        Layout.fillWidth: true
                        text: genreButton.text
                        elide: Text.ElideRight
                        typescale: MD.Token.typescale.label_large
                    }
                }
                onClicked: {
                    Core.catalogGenreFilter = modelData
                    if (root.compact)
                        root.dismissed()
                }
            }
            MD.Label {
                anchors.centerIn: parent
                visible: genres.count === 0
                text: qsTr("No matching genres")
                color: MD.Token.color.on_surface_variant
            }
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
