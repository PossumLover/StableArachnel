#include "process_memory.h"

#include <QByteArray>
#include <QDebug>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#endif

namespace arachnel::core {

qint64 processPrivateMemoryMiB()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)))
        return -1;
    return static_cast<qint64>(counters.PrivateUsage / (1024 * 1024));
#elif defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray& line : status.readAll().split('
')) {
        if (line.startsWith("RssAnon:")) // private and resident: closest to Windows PrivateUsage
            return line.mid(8).trimmed().split(' ').value(0).toLongLong() / 1024;
    }
    return -1;
#else
    return -1;
#endif
}

bool memoryStageLogEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("ARACHNEL_MEMLOG") > 0;
    return enabled;
}

void logMemoryStage(const QString& stage)
{
    if (!memoryStageLogEnabled())
        return;
    qInfo().noquote() << QStringLiteral("[mem] %1: %2 MiB").arg(stage).arg(processPrivateMemoryMiB());
}

} // namespace arachnel::core
