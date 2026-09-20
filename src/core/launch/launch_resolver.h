#pragma once

#include "library_model.h"
#include "plugin_interface.h"
#include "settings_store.h"

#include <QProcessEnvironment>
#include <QStringList>

namespace arachnel::core {

class ProtonManager;

struct ResolvedLaunch {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
};

QStringList splitLaunchArguments(const QString& text);

/**
 * The executable resolveLaunch() will run: the per-game override unless it names an
 * excluded helper, else the plugin's executable. Anything that must agree with the
 * launch - where the fix layer goes, which process the watcher tracks - asks this
 * rather than deciding again.
 */
QString chooseLaunchExecutable(const LaunchInfo& pluginInfo, const LibraryGame& game,
                               bool* fromOverride = nullptr);

ResolvedLaunch resolveLaunch(const LaunchInfo& pluginInfo, const LibraryGame& game,
                             const SettingsStore& settings, ProtonManager* protonManager = nullptr);

} // namespace arachnel::core
