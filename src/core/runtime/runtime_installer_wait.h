#pragma once

#include <QDeadlineTimer>
#include <QProcess>
#include <atomic>

namespace arachnel::core {
// Called on the installer worker. Check cancellation even if the installer
// stops producing output and never exits on its own.
inline bool waitForRuntimeInstaller(QProcess& process, const std::atomic_bool* cancelled,
                                    QDeadlineTimer deadline)
{
    while (process.state() != QProcess::NotRunning) {
        if ((cancelled && cancelled->load()) || deadline.hasExpired()) {
            process.kill();
            process.waitForFinished(3000);
            return false;
        }
        process.waitForFinished(200);
    }
    return true;
}
}
