#include "library_view_model.h"
#include "library_model.h"
#include <QCollator>
#include <QDateTime>
#include <QSettings>

namespace arachnel::core {
LibraryViewModel::LibraryViewModel(LibraryModel* library, QObject* parent, bool persistPreferences)
    : QSortFilterProxyModel(parent), m_persistPreferences(persistPreferences)
{
    if (m_persistPreferences)
        m_sortMode = qBound(0, QSettings().value(QStringLiteral("library/sortMode"), 0).toInt(), 2);
    setSourceModel(library);
    setDynamicSortFilter(true);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &LibraryViewModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LibraryViewModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LibraryViewModel::countChanged);
}

void LibraryViewModel::setSearch(const QString& search)
{
    const QString normalized = search.trimmed();
    if (m_search == normalized)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_search = normalized;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
    emit filtersChanged();
}

void LibraryViewModel::setPlayStatus(const QString& status)
{
    if (!status.isEmpty() && status != QStringLiteral("unorganized")
        && status != QStringLiteral("backlog") && status != QStringLiteral("playing")
        && status != QStringLiteral("completed"))
        return;
    if (m_status == status)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_status = status;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
    emit filtersChanged();
}

void LibraryViewModel::setSortMode(int mode)
{
    if (mode < 0 || mode > 2 || m_sortMode == mode)
        return;
    m_sortMode = mode;
    if (m_persistPreferences)
        QSettings().setValue(QStringLiteral("library/sortMode"), mode);
    invalidate();
    emit filtersChanged();
}

bool LibraryViewModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const auto index = sourceModel()->index(row, 0, parent);
    const QString status = index.data(LibraryModel::PlayStatusRole).toString();
    return (m_status.isEmpty() || (m_status == QStringLiteral("unorganized") ? status.isEmpty() : status == m_status))
        && index.data(LibraryModel::TitleRole).toString().contains(m_search, Qt::CaseInsensitive);
}

bool LibraryViewModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (m_sortMode == 1) {
        const auto a = QDateTime::fromString(left.data(LibraryModel::LastPlayedAtRole).toString(), Qt::ISODate);
        const auto b = QDateTime::fromString(right.data(LibraryModel::LastPlayedAtRole).toString(), Qt::ISODate);
        if (a.isValid() != b.isValid())
            return a.isValid();
        if (a != b)
            return a > b;
    } else if (m_sortMode == 2) {
        const auto a = left.data(LibraryModel::PlaytimeMsRole).toLongLong();
        const auto b = right.data(LibraryModel::PlaytimeMsRole).toLongLong();
        if (a != b)
            return a > b;
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    const int comparison = collator.compare(left.data(LibraryModel::TitleRole).toString(),
                                            right.data(LibraryModel::TitleRole).toString());
    return comparison != 0 ? comparison < 0
        : left.data(LibraryModel::GameIdRole).toString() < right.data(LibraryModel::GameIdRole).toString();
}
} // namespace arachnel::core
