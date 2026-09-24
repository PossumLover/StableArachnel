import QtQuick

import Qcm.Material as MD

// A round icon chip in one of the meadow tones (see MeadowTones), shared by
// stat cards and the settings hub.
MD.ElevationRectangle {
    id: root

    property string iconName: ""
    property int tone: 0
    property int iconSize: Math.round(width / 2)

    MeadowTones { id: tones }

    implicitWidth: 48
    implicitHeight: 48
    radius: MD.Token.shape.corner.full
    color: tones.containerFor(tone)
    elevation: MD.Token.elevation.level0

    MD.Icon {
        anchors.centerIn: parent
        name: root.iconName
        size: root.iconSize
        color: tones.inkFor(root.tone)
    }
}
