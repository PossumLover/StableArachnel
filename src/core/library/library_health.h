#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>

namespace arachnel::core {

struct LibraryHealthLaunchOption {
    QString id;
    QString executable;
    bool isDefault = false;

    bool operator==(const LibraryHealthLaunchOption&) const = default;
};

struct LibraryHealthInput {
    QString installPath;
    QString title;
    QString executableOverride;
    QString launchOptionId;
    QList<LibraryHealthLaunchOption> launchOptions;

    bool operator==(const LibraryHealthInput&) const = default;
};

struct LibraryHealthResult {
    LibraryHealthInput input;
    QDateTime installMtime;
    QDateTime inventoryMtime;
    QDateTime launchMarkerMtime;
    QVariantList issues;
};

enum class LibraryHealthCheckMode { UseCache, Refresh };

struct LibraryGame;
void queueLibraryHealthSync(const QList<LibraryGame>& games,
                            QHash<QString, LibraryHealthCheckMode>& pending);

// Reads small executable headers and inventory metadata. Call only on a worker thread.
LibraryHealthResult checkLibraryHealth(const LibraryHealthInput& input,
                                       const LibraryHealthResult& cached = {},
                                       LibraryHealthCheckMode mode = LibraryHealthCheckMode::UseCache);

} // namespace arachnel::core
