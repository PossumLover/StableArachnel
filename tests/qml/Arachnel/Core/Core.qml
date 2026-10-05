pragma Singleton
import QtQml

QtObject {
    property QtObject libraryView: QtObject {
        property string search: ""
        property string playStatus: ""
        property int sortMode: 0
    }
    property string savedGameStatus: ""
    property string savedGameId: ""
    function setGamePlayStatus(id, status) { savedGameId = id; savedGameStatus = status }
    property string cacheState: "checking"
    signal torboxCacheChanged()
    signal catalogCountsChanged()
    function torboxCacheStatus(hash) { return cacheState }
    function checkTorboxCache(id) {}
    property var offerList: []
    function installOffersForEntry(id) { return offerList }
    property bool gameRunning: false
    property string runningGameId: ""
    property int achievementRefreshCount: 0
    property var achievementInfo: ({})
    signal gameAchievementsChanged(string gameId)
    function gameAchievements(gameId) { return achievementInfo }
    function refreshGameAchievements(gameId, force) { achievementRefreshCount++ }
    property int catalogActiveFilterCount: 0
    property int catalogTypeFilter: -1
    property int catalogSizeFilter: 0
    property int catalogRecencyFilter: 0
    property bool catalogHasAddonsFilter: false
    property string catalogGenreFilter: ""
    property int catalogPlayModeFilter: 0
    property var hiddenCatalogSourceIds: []
    function catalogGenreLabel(genre) { return genre }
    property QtObject settings: QtObject {
        signal debridChanged()
        property string torboxApiKey: ""
        property bool torboxEnabled: false
        property bool torboxChecking: false
        property string torboxStatus: ""
        function checkTorboxConnection() { torboxStatus = "Connected to TorBox" }
    }
    function openExternalUrl(url) {}
}
