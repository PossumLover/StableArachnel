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
        page: QtObject {
            property var typeFilterLabels: ({"-1": "All"})
            property var sizeFilterLabels: ({"0": "Any size"})
            property var recencyFilterLabels: ({"0": "Any time"})
            property var playModeFilterLabels: ({"0": "Any players"})
        }
    }

    SignalSpy { id: dismissed; target: panel; signalName: "dismissed" }

    TestCase {
        name: "BrowseGenres"
        when: windowShown

        function test_select_genre_and_return_to_all_games() {
            const search = findChild(panel, "genreSearch")
            const list = findChild(panel, "genreList")
            search.text = "STRAT"
            tryCompare(list, "count", 1)
            tryVerify(() => !!list.itemAtIndex(0))
            mouseClick(list.itemAtIndex(0))
            compare(Core.catalogGenreFilter, "Strategy")
            compare(dismissed.count, 1)

            panel.compact = false
            search.text = ""
            tryCompare(list, "count", 4)
            tryVerify(() => list.itemAtIndex(0) && list.itemAtIndex(0).text === "All games")
            mouseClick(list.itemAtIndex(0))
            compare(Core.catalogGenreFilter, "")
            compare(dismissed.count, 1)
        }
    }
}
