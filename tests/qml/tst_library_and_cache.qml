import QtQuick
import QtTest
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    width: 840
    height: 450
    font.family: "Nunito"
    FontLoader { source: "Nunito-Regular.ttf" }
    LibraryToolbar { id: toolbar; width: 800; y: 20 }
    GameOrganizationControl { id: organization; gameId: "game"; width: 600; height: implicitHeight; y: 120 }
    TorBoxCacheBadge { id: badge; cacheHash: "a"; y: 230 }
    DownloadAvailability { id: availability; entryId: "game"; width: 600; y: 300 }
    TestCase {
        name: "LibraryAndCache"
        when: windowShown
        function selectNext(control) {
            verify(control.width > 0 && control.height > 0)
            mouseClick(control, control.width - 15, control.height / 2)
            tryCompare(control.popup, "visible", true)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(control.popup, "visible", false)
        }
        function test_library_controls() {
            const search = findChild(toolbar, "librarySearch")
            mouseClick(search)
            keyClick(Qt.Key_F)
            keyClick(Qt.Key_I)
            keyClick(Qt.Key_S)
            keyClick(Qt.Key_H)
            compare(Core.libraryView.search, "fish")
            const backlog = findChild(toolbar, "libraryFilter_backlog")
            mouseClick(backlog)
            compare(Core.libraryView.playStatus, "backlog")
            mouseClick(backlog)
            compare(backlog.checked, true)
            mouseClick(findChild(toolbar, "libraryFilter_unorganized"))
            compare(Core.libraryView.playStatus, "unorganized")
            mouseClick(findChild(toolbar, "libraryFilter_"))
            compare(Core.libraryView.playStatus, "")
            selectNext(findChild(toolbar, "librarySort"))
            compare(Core.libraryView.sortMode, 1)
            const menu = findChild(organization, "gameListMenu")
            mouseClick(organization)
            tryCompare(menu, "opened", true)
            mouseClick(menu.itemAt(1))
            compare(Core.savedGameId, "game")
            compare(Core.savedGameStatus, "backlog")
            tryCompare(menu, "visible", false)
            organization.playStatus = Core.savedGameStatus
            compare(organization.text, "List: Backlog")
            mouseClick(organization)
            tryCompare(menu, "opened", true)
            compare(menu.itemAt(1).selected, true)
            mouseClick(menu.itemAt(0))
            compare(Core.savedGameStatus, "")
        }
        function test_narrow_search_data() {
            return [{ tag: "ltr", mirrored: false }, { tag: "rtl", mirrored: true }]
        }
        function test_narrow_search(data) {
            toolbar.width = 580
            toolbar.LayoutMirroring.childrenInherit = true
            toolbar.LayoutMirroring.enabled = data.mirrored
            Core.libraryView.search = ""
            const search = findChild(toolbar, "librarySearch")
            const icon = findChild(search, "searchIcon")
            const clear = findChild(search, "clearSearchButton")
            waitForPolish(toolbar)
            mouseClick(icon)
            keyClick(Qt.Key_A)
            compare(Core.libraryView.search, "a")
            const editor = toolbar.Window.window.activeFocusItem
            function caretBetweenIcons() {
                const caret = editor.mapToItem(search, editor.cursorRectangle)
                const left = data.mirrored ? clear : icon
                const right = data.mirrored ? icon : clear
                const start = left.mapToItem(search, left.width, 0).x
                const end = right.mapToItem(search, 0, 0).x
                return start < caret.x && caret.x + caret.width < end
            }
            tryVerify(caretBetweenIcons)
            for (let i = 0; i < 30; ++i)
                keyClick(Qt.Key_W)
            keyClick(Qt.Key_Home)
            verify(caretBetweenIcons())
            keyClick(Qt.Key_End)
            verify(caretBetweenIcons())
            mouseClick(clear)
            compare(Core.libraryView.search, "")
            keyClick(Qt.Key_B)
            compare(Core.libraryView.search, "b")
            Core.libraryView.search = ""
            toolbar.LayoutMirroring.enabled = false
            toolbar.width = 800
        }
        function test_sort_label_fits_narrow_library() {
            // 900px window with the wide navigation drawer and both page margins.
            toolbar.width = 580
            Core.libraryView.sortMode = 1
            const menu = findChild(toolbar, "librarySort")
            tryCompare(menu.contentItem, "text", "Recently played")
            wait(20)
            verify(!menu.contentItem.truncated)
            compare(menu.contentItem.lineCount, 1)
            verify(menu.contentItem.x + menu.contentItem.width <= menu.indicator.x)
            Core.libraryView.sortMode = 0
            toolbar.width = 800
        }
        function test_cache_states() {
            Core.settings.torboxEnabled = true
            Core.settings.torboxApiKey = "test"
            const label = findChild(badge, "cacheStatusLabel")
            for (const state of ["cached", "uncached", "unavailable"]) {
                Core.cacheState = state
                Core.torboxCacheChanged()
                wait(10)
                compare(label.text, state === "cached" ? "Cached on TorBox"
                    : state === "uncached" ? "Not cached on TorBox" : "Cache status unavailable")
                compare(Core.settings.torboxEnabled, true)
            }
            Core.settings.torboxEnabled = false
            verify(!badge.visible)
            badge.directDownload = true
            verify(badge.visible)
            compare(label.text, "Direct download")
            badge.directDownload = false
            Core.settings.torboxApiKey = ""
        }
        function test_sources_loading_after_details_open() {
            Core.settings.torboxEnabled = true
            Core.settings.torboxApiKey = "test"
            Core.offerList = [{sourceName: "Torrent", cacheHash: "a"}, {sourceName: "HTTP", directDownload: true}]
            Core.catalogCountsChanged()
            compare(availability.offers.length, 2)
            verify(availability.visible)
            Core.offerList = []
            Core.catalogCountsChanged()
            verify(!availability.visible)
            Core.settings.torboxEnabled = false
            Core.settings.torboxApiKey = ""
        }
    }
}
