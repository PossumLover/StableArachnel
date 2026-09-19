#pragma once

#include "library_model.h"
#include "plugin_interface.h"
#include "settings_store.h"

#include <QMap>
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
 * Steam-style launch options, as people are used to writing them in Steam:
 *
 *     WINEDLLOVERRIDES="winhttp=n,b" mangohud %command% -skipLauncher
 *
 * `%command%` stands for the real program and its arguments: leading VAR=value
 * tokens become environment variables, tokens between them and `%command%` become a
 * wrapper the game is run under, and tokens after it are appended to the game's own
 * arguments. Without a `%command%` the whole string is arguments and nothing else,
 * which is how Arachnel has always treated it.
 */
struct LaunchOptions {
    QMap<QString, QString> environment;
    QStringList wrapper;
    QStringList arguments;
};

LaunchOptions parseLaunchOptions(const QString& text);

ResolvedLaunch resolveLaunch(const LaunchInfo& pluginInfo, const LibraryGame& game,
                             const SettingsStore& settings, ProtonManager* protonManager = nullptr);

} // namespace arachnel::core
