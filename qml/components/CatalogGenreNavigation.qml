import QtQml
import Arachnel.Core 1.0

QtObject {
    readonly property var ranked: Array.from(Core.availableCatalogGenres)
    readonly property int total: Core.catalogGenreCounts[""] ?? -1
    readonly property int threshold: total < 25 ? 1 : Math.max(3, Math.min(25, Math.ceil(total / 1000)))
    readonly property var primary: ranked.filter(key => Core.catalogGenreCounts[key] >= threshold
                                                        || key === Core.catalogGenreFilter)
    function withSelection(keys) {
        const current = Core.catalogGenreFilter
        return current && !keys.includes(current) ? keys.concat([current]) : keys
    }
}
