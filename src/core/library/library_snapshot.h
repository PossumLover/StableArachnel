#pragma once

#include "library_model.h"

#include <QUrl>

namespace arachnel::core {

bool writeLibrarySnapshot(const QUrl& fileUrl, const QVector<LibraryGame>& library);
QVariantMap readSharedLibrary(const QUrl& fileUrl, const QVector<LibraryGame>& library);

} // namespace arachnel::core
