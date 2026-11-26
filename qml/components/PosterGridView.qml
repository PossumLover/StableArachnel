import QtQuick
import Qcm.Material as MD

GridView {
    id: grid

    required property real availableWidth
    required property real minimumPosterWidth
    readonly property real gap: MD.Token.spacing.medium
    readonly property int columns: Math.max(1, Math.floor(width / (minimumPosterWidth + gap)))
    readonly property real posterWidth: Math.max(1, cellWidth - gap)

    // Include the trailing cell gutter so the last poster ends at availableWidth.
    width: Math.max(0, availableWidth) + gap
    // Round down to a binary subpixel so GridView never wraps a full row one card early.
    cellWidth: Math.max(1, Math.floor(width * 64 / columns) / 64)
}
