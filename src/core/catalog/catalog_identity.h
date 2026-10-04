#pragma once

#include "catalog_types.h"
#include <QHash>

namespace arachnel::core {

QString catalogTitleKey(const QString& title);
QHash<QString, QString> catalogSteamTitleKeys(const QVector<CatalogEntry>& entries);
QString catalogOfferGroupKey(const CatalogEntry& entry, const QHash<QString, QString>& steamTitles);

} // namespace arachnel::core
