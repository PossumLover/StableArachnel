import QtQuick
import QtTest
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    width: 400
    height: 620

    CatalogBrowsePanel {
        id: panel
        anchors.fill: parent
        compact: true
        navigation: CatalogGenreNavigation {}
        page: QtObject {
            property var typeFilterLabels: ({"-1": "All"})
            property var sizeFilterLabels: ({"0": "Any size"})
            property var recencyFilterLabels: ({"0": "Any time"})
            property var playModeFilterLabels: ({"0": "Any players"})
        }
    }

    SignalSpy { id: dismissed; target: panel; signalName: "dismissed" }
    CatalogScrollHeader {
        id: toolbar
        visible: false
        width: 560
        hasSelection: true
        listViewMode: false
        showPanelButton: false
        sortOptions: [{mode: 0, label: "Recently uploaded"}, {mode: 2, label: "Title A-Z"}]
        onViewModeChangeRequested: mode => listViewMode = mode === 1
    }
    SignalSpy { id: sortRequests; target: toolbar; signalName: "sortModeRequested" }

    TestCase {
        name: "BrowseGenres"
        when: windowShown

        function test_small_catalog_and_pending_counts() {
            const list = findChild(panel, "genreList")
            const saved = Core.catalogGenreCounts
            Core.catalogGenreCounts = {"": 12, Action: 8, Strategy: 3, Platformer: 1}
            compare(list.model, ["", "Action", "Strategy", "Platformer"])
            Core.catalogPartial = true
            Core.catalogGenreCounts = {}
            tryVerify(() => list.itemAtIndex(0)?.countLabel === "")
            Core.catalogGenreCounts = saved
            Core.catalogPartial = false
        }

        function test_sort_choice_owned_by_page() {
            toolbar.visible = true
            const trigger = findChild(toolbar, "catalogSortTrigger")
            compare(trigger.text, "Recently uploaded")
            mouseClick(trigger)
            const menu = findChild(toolbar, "catalogSortMenu")
            tryCompare(menu, "opened", true)
            mouseClick(menu.itemAt(1))
            compare(sortRequests.count, 1)
            compare(sortRequests.signalArguments[0][0], 2)
            compare(Core.catalog.sortMode, 0)
            Core.catalog.sortMode = 2
            compare(trigger.text, "Title A-Z")
            toolbar.visible = false
        }

        function test_view_segments() {
            toolbar.visible = true
            const grid = findChild(toolbar, "segment0")
            const list = findChild(toolbar, "segment1")
            compare(grid.width, list.width)
            list.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(toolbar.listViewMode, true)
            verify(list.checked)
            grid.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            verify(grid.checked)
            toolbar.visible = false
        }

        function test_select_genre_and_return_to_all_games() {
            const search = findChild(panel, "genreSearch")
            const list = findChild(panel, "genreList")
            compare(list.model, ["", "Action", "Strategy"])
            tryVerify(() => !!list.itemAtIndex(0))
            compare(list.itemAtIndex(0).countLabel, (1200).toLocaleString(Qt.locale(), "f", 0))
            for (const tag of ["Free to Play", "Early Access"]) {
                search.text = tag
                tryCompare(list, "count", 0)
            }
            search.text = ""
            mouseClick(findChild(panel, "moreGenres"))
            compare(list.model, ["", "Action", "Strategy", "Platformer"])
            mouseClick(findChild(panel, "moreGenres"))
            search.text = "PLAT"
            tryCompare(list, "count", 1)
            tryVerify(() => !!list.itemAtIndex(0))
            mouseClick(list.itemAtIndex(0))
            compare(Core.catalogGenreFilter, "Platformer")
            compare(dismissed.count, 1)

            panel.compact = false
            search.text = ""
            compare(list.model, ["", "Action", "Strategy", "Platformer"])
            Core.catalogGenreCounts = {"": 1200, Action: 901, Strategy: 30, Platformer: 2}
            tryVerify(() => list.itemAtIndex(1)?.countLabel === "901")
            tryVerify(() => list.itemAtIndex(0) && list.itemAtIndex(0).text === "All games")
            mouseClick(list.itemAtIndex(0))
            compare(Core.catalogGenreFilter, "")
            compare(list.model, ["", "Action", "Strategy"])
            compare(dismissed.count, 1)
        }
    }
}
