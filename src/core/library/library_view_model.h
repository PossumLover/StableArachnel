#pragma once

#include <QSortFilterProxyModel>

namespace arachnel::core {
class LibraryModel;

class LibraryViewModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filtersChanged)
    Q_PROPERTY(QString playStatus READ playStatus WRITE setPlayStatus NOTIFY filtersChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY filtersChanged)
public:
    explicit LibraryViewModel(LibraryModel* library, QObject* parent = nullptr,
                              bool persistPreferences = false);
    int count() const { return rowCount(); }
    QString search() const { return m_search; }
    QString playStatus() const { return m_status; }
    int sortMode() const { return m_sortMode; }
    void setSearch(const QString& search);
    void setPlayStatus(const QString& status);
    void setSortMode(int mode);
signals:
    void countChanged();
    void filtersChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
private:
    QString m_search;
    QString m_status;
    int m_sortMode = 0;
    bool m_persistPreferences;
};
} // namespace arachnel::core
