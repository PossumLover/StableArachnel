import Qcm.Material as MD

MD.Label {
    property double durationMs: 0
    property string caption: qsTr("Playtime")
    readonly property string durationText: {
        const minutes = Math.floor(Math.max(0, durationMs) / 60000)
        if (durationMs > 0 && minutes === 0)
            return qsTr("Less than a minute")
        if (minutes < 60)
            return qsTr("%1 min").arg(minutes)
        return qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }
    text: qsTr("%1: %2").arg(caption).arg(durationText)
    typescale: MD.Token.typescale.body_small
    color: MD.Token.color.on_surface_variant
}
