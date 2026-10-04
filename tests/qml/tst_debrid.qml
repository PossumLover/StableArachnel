import QtQuick
import QtTest
import Arachnel.Core 1.0
import Qcm.Material as MD

MD.Pane {
    id: window
    visible: true
    width: 640
    height: 560
    font.family: "Nunito"

    FontLoader { source: "Nunito-Regular.ttf" }
    SettingsDebridPage { id: page; anchors.fill: parent }

    TestCase {
        name: "DebridInteraction"
        when: windowShown

        function test_mouse_setup() {
            const toggle = findChild(page, "torboxSwitch")
            const field = findChild(page, "torboxKeyField")
            const show = findChild(page, "torboxShowKey")
            const save = findChild(page, "torboxSaveKey")
            const remove = findChild(page, "torboxRemoveKey")
            verify(toggle && field && show && save && remove)
            mouseClick(toggle)
            compare(Core.settings.torboxEnabled, true)
            mouseClick(field)
            verify(field.activeFocus)
            for (const character of "test-key")
                keyClick(character.toUpperCase().charCodeAt(0))
            compare(field.text, "test-key")
            compare(field.echoMode, TextInput.Password)
            mouseClick(show)
            compare(field.echoMode, TextInput.Normal)
            mouseClick(save)
            compare(Core.settings.torboxApiKey, "test-key")
            compare(Core.settings.torboxStatus, "Connected to TorBox")
            mouseClick(remove)
            compare(Core.settings.torboxEnabled, false)
            compare(Core.settings.torboxApiKey, "")
            compare(field.text, "")
        }
    }
}
