import QtQuick

import Qcm.Material as MD

// Painted art for empty states and decoration (replaces the old spider web).
//   "vignette"  a little round meadow scene - sun, hills, a flower, two birds
//   "spray"     flowering stems, for a faint corner decoration
//   "emblem"    the app emblem
Item {
    id: root

    property string variant: "vignette"

    implicitWidth: 200
    implicitHeight: variant === "vignette" ? 170 : 200

    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
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
