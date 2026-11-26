#pragma once

#include "catalog_types.h"
#include <QHash>

namespace arachnel::core {

QString catalogTitleKey(const QString& title);
QHash<QString, QString> catalogSteamTitleKeys(const QVector<CatalogEntry>& entries);
/** Same, over borrowed rows (the merge uses this so it never copies the catalog). */
QHash<QString, QString> catalogSteamTitleKeys(const QVector<const CatalogEntry*>& entries);
QString catalogOfferGroupKey(const CatalogEntry& entry, const QHash<QString, QString>& steamTitles);

} // namespace arachnel::core
