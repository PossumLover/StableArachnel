import QtQuick
import QtTest
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    width: 640
    height: 1000
    font.family: "Nunito"
    FontLoader { source: "Nunito-Regular.ttf" }
    GameAchievementsPanel { id: panel; width: 600; gameId: "game" }
    PlaytimeLabel { id: playtime; visible: false }
    TestCase {
        name: "GameActivity"
        when: windowShown
        function test_playtime_and_achievements() {
            playtime.durationMs = 7260000
            compare(playtime.durationText, "2 h 1 min")
            playtime.durationMs = 1
            compare(playtime.durationText, "Less than a minute")
            const rows = []
            for (let i = 0; i < 8; i++)
                rows.push({name: "A" + i, title: "Achievement " + i, description: "Example", icon: "", unlocked: i < 2})
            Core.achievementInfo = {rows: rows, total: 8, unlocked: 2, localFileFound: true}
            Core.gameAchievementsChanged("game")
            wait(20)
            verify(findChild(panel, "achievement-A4") !== null)
            verify(findChild(panel, "achievement-A5") === null)
            mouseClick(findChild(panel, "expandAchievements"))
            wait(20)
            verify(findChild(panel, "achievement-A7") !== null)
            const before = Core.achievementRefreshCount
            mouseClick(findChild(panel, "refreshAchievements"))
            compare(Core.achievementRefreshCount, before + 1)
        }
    }
}
