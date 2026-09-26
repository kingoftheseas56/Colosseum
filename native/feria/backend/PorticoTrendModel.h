#pragma once

#include "PorticoTrendTypes.h"

#include <QAbstractListModel>

class PorticoTrendModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        ShelfIdRole = Qt::UserRole + 1,
        TitleRole,
        SourceIdRole,
        SourceLabelRole,
        MediumRole,
        RegionRole,
        PeriodRole,
        CanonicalUrlRole,
        StabilityRole,
        ItemsRole,
        LoadingRole,
        ErrorRole,
        FetchedAtRole,
        StaleRole,
        FallbackReasonRole,
        ProviderIdRole,
        RankedRole,
        PriorityRole
    };

    explicit PorticoTrendModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLoading(const QString &feedId, const QString &sourceId,
                    const QString &title, const QString &sourceLabel,
                    const QString &medium, const QString &stability);
    void setShelf(const PorticoTrend::Shelf &shelf);
    void setError(const QString &feedId, const QString &message);
    QVariantMap shelf(const QString &feedId) const;

private:
    struct Entry {
        PorticoTrend::Shelf shelf;
        bool loading = false;
        QString error;
    };

    int indexOf(const QString &feedId) const;
    QList<Entry> m_entries;
};
