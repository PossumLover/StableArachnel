import QtQuick
import QtTest
import Qcm.Material as MD

MD.Pane {
    width: 640
    height: 300

    QtObject {
        id: signalSource
        signal ping()
    }

    Component {
        id: pageComponent
        Item {
            id: page
            property int received: 0
            property alias buttonText: button.text
            MD.Button { id: button; text: "Ready" }
            MD.IconButton { icon.name: MD.Token.icon.refresh }
            Connections {
                target: signalSource
                function onPing() {
                    page.received++
                    button.text = "Received"
                }
            }
        }
    }

    Component {
        id: loaderComponent
        ResidentPageLoader {
            sourceComponent: pageComponent
        }
    }

    TestCase {
        name: "ResidentPageLoader"
        when: windowShown

        function test_activation_and_tab_switches() {
            const loader = createTemporaryObject(loaderComponent, parent)
            verify(loader)
            compare(loader.status, Loader.Null)
            loader.requested = true
            compare(loader.status, Loader.Ready)
            const page = loader.item
            verify(page)
            for (let i = 0; i < 20; ++i) {
                loader.requested = false
                gc()
                signalSource.ping()
                compare(loader.status, Loader.Ready)
                compare(loader.item, page)
                compare(page.received, i + 1)
                compare(page.buttonText, "Received")
                loader.requested = true
            }
        }

        function test_background_warm() {
            const loader = createTemporaryObject(loaderComponent, parent)
            verify(loader)
            loader.keepAlive = true
            compare(loader.status, Loader.Ready)
            compare(loader.requested, false)
            gc()
            signalSource.ping()
            compare(loader.item.received, 1)
            compare(loader.item.buttonText, "Received")
        }
    }
}
