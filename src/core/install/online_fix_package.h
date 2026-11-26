#pragma once

#include <QString>

namespace arachnel::core {

bool hasOnlineFixPackageBackup(const QString& installPath);
// Empty result means success. The source is the extracted package root.
QString importOnlineFixPackage(const QString& installPath, const QString& steamAppId,
                               const QString& packagePath);
QString restoreOnlineFixPackage(const QString& installPath);

} // namespace arachnel::core
