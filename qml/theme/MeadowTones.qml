import QtQuick

import Qcm.Material as MD

// The four meadow accent tones as container/ink colour pairs:
//   0 sage (primary)  1 olive (secondary)  2 dusty rose (tertiary)  3 apricot sun
QtObject {
    function containerFor(tone) {
        return tone === 1 ? MD.Token.color.secondary_container
             : tone === 2 ? MD.Token.color.tertiary_container
             : tone === 3 ? (MD.Token.isDarkTheme ? "#6B4A30" : "#FBDDB9")
             : MD.Token.color.primary_container
    }

    function inkFor(tone) {
        return tone === 1 ? MD.Token.color.on_secondary_container
             : tone === 2 ? MD.Token.color.on_tertiary_container
             : tone === 3 ? (MD.Token.isDarkTheme ? "#FCD7B0" : "#6E4318")
             : MD.Token.color.on_primary_container
    }
}
