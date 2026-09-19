#pragma once

#include <QMutex>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstdio>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <csignal>
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace arachnel {

extern QMutex g_logMutex;
extern QString g_logDir;
extern QString g_runExePath;
extern QString g_runArgsLine;
extern bool g_isCrashDialogProcess;
extern bool g_shuttingDown;
extern QStringList g_recentLines;
extern QStringList g_breadcrumbs;
extern qint64 g_appStartMs;
#if defined(Q_OS_WIN)
extern DWORD g_mainThreadId;
#else
extern pthread_t g_mainThreadHandle;
#endif

struct CrashReportData {
    QString summary;
    QString details;
    QString issueUrl;
};

QString logDirectory();
QString runLogPath();
QString crashLogPath();
QString latestCrashReportPath();
QString latestHangReportPath();
QString pendingCrashMarkerPath();
QString latestCrashDumpPath();

void appendToFile(const QString& path, const QString& text);
void writeTextFile(const QString& path, const QString& text);
QString readTextFile(const QString& path);
QString readPendingField(const QString& key);
void removePendingMarker();

void rememberRecentLine(const QString& line);
void rememberBreadcrumb(const QString& line);
void writeLine(const QString& line, bool toStderr = true);

CrashReportData buildCrashReport(const QString& summary, const QString& extraDetails,
                                 const QString& stackTrace);
QString buildIssueUrl(const QString& summary, const QString& reportBody);
void handleCrashReport(const CrashReportData& report);
void reportUiHang(int hungSeconds);

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

#if defined(Q_OS_WIN)
bool attachParentConsole();
QString captureStackTraceWindows(CONTEXT* optionalContext);
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info);
#else
QString captureStackTraceUnix();
/** Arm the signal-based backtrace of the main thread. Call from the main thread. */
void installHangStackCapture();
/** Backtrace of the main thread, taken from another thread. */
QString captureMainThreadStackUnix(int timeoutMs);
void linuxSignalHandler(int signal, siginfo_t* info, void* context);
#endif

} // namespace arachnel
