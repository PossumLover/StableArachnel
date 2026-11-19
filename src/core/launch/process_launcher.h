#pragma once

#include "launch_resolver.h"

#include <QString>
#include <functional>

namespace arachnel::core {

class ProcessLauncher
{
public:
#ifdef Q_OS_WIN
    static void awaitDetachedGame(const QString& program, QObject* context, int timeoutMs,
                                  std::function<void(qint64)> done);
#endif
    /**
     * Launch detached. When logFilePath is set, the child's stdout/stderr are
     * redirected there so Proton/Wine diagnostics are captured for the UI log.
     */
    static bool launch(const ResolvedLaunch& launch, QString* errorOut = nullptr,
                       qint64* processIdOut = nullptr, const QString& logFilePath = {});
};

} // namespace arachnel::core
