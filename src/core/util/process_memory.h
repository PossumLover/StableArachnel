#pragma once

#include <QString>
#include <QtGlobal>

namespace arachnel::core {

/** Committed private memory of this process in MiB (Windows PrivateUsage, Linux RssAnon), or -1. */
qint64 processPrivateMemoryMiB();

/** True when ARACHNEL_MEMLOG=1: memory is logged at catalog load stages and every 2.5 s. */
bool memoryStageLogEnabled();

/** With ARACHNEL_MEMLOG=1, logs "[mem] <stage>: <MiB>" so a run shows where memory lands. */
void logMemoryStage(const QString& stage);

} // namespace arachnel::core
