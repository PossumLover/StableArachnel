import QtQuick
import QtTest
import Qcm.Material as MD

MD.Pane {
    width: 640
    height: 300
    font.family: "Nunito"
    FontLoader { source: "Nunito-Regular.ttf" }
    QtObject {
        id: page
        property string searchQuery: ""
        property bool enabled: true
        property int searches: 0
        property var typeFilterLabels: ({})
        property var sizeFilterLabels: ({})
        property var recencyFilterLabels: ({})
        property var playModeFilterLabels: ({})
        function applyCatalogSearch(query) {
            searchQuery = query
            searches++
        }
    }
    CatalogStickyToolbar { id: bar; width: 560; page: page; pageMargin: 24 }
    TestCase {
        name: "CatalogSearch"
        when: windowShown
        function test_debounce_and_sync() {
            bar.focusSearch()
            keyClick(Qt.Key_F)
            keyClick(Qt.Key_I)
            keyClick(Qt.Key_S)
            keyClick(Qt.Key_H)
            keyClick(Qt.Key_Space)
            tryCompare(page, "searchQuery", "fish")
            compare(bar.searchText, "fish ")
            compare(page.searches, 1)
            keyClick(Qt.Key_O)
            keyClick(Qt.Key_N)
            keyClick(Qt.Key_Return)
            compare(page.searchQuery, "fish on")
            wait(350)
            compare(page.searches, 2)
            page.searchQuery = "rising"
            compare(bar.searchText, "rising")
            wait(350)
            compare(page.searches, 2)
            bar.searchText = "other"
            page.enabled = false
            wait(350)
            compare(page.searches, 2)
        }
    }
}
