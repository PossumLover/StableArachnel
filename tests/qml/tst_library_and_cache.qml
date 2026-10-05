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
            selectNext(findChild(toolbar, "libraryStatusFilter"))
            compare(Core.libraryView.playStatus, "backlog")
            selectNext(findChild(toolbar, "librarySort"))
            compare(Core.libraryView.sortMode, 1)
            selectNext(findChild(organization, "gamePlayStatus"))
            compare(Core.savedGameId, "game")
            compare(Core.savedGameStatus, "backlog")
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
