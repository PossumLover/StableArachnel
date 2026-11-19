import QtQuick

import Qcm.Material as MD

// Meadow illustrations for empty states and decoration.
//   "vignette"  a glasshouse in an arched landscape
//   "spray"     poppies and a butterfly for the edge of a card
//   "emblem"    the app emblem
Item {
    id: root

    property string variant: "vignette"

    implicitWidth: 200
    implicitHeight: variant === "vignette" ? 170 : 200

    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        verticalAlignment: root.variant === "spray" ? Image.AlignBottom : Image.AlignVCenter
        smooth: true
        mipmap: true
        source: {
            const mode = MD.Token.isDarkTheme ? "dark" : "light"
            if (root.variant === "emblem")
                return "qrc:/art/emblem-small.png"
            if (root.variant === "spray")
                return "qrc:/art/hero-spray-" + mode + ".png"
            return "qrc:/art/vignette-" + mode + ".png"
        }
    }
}
