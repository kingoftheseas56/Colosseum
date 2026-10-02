#pragma once

#include <QSortFilterProxyModel>
#include <QStringList>

class PorticoShelfFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString lens READ lens WRITE setLens NOTIFY lensChanged)
    Q_PROPERTY(QStringList enabledSources READ enabledSources WRITE setEnabledSources NOTIFY enabledSourcesChanged)

public:
    explicit PorticoShelfFilterModel(QObject *parent = nullptr);

    QString lens() const { return m_lens; }
    QStringList enabledSources() const { return m_enabledSources; }

    void setLens(const QString &lens);
    void setEnabledSources(const QStringList &sources);

signals:
    void lensChanged();
    void enabledSourcesChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_lens = QStringLiteral("all");
    QStringList m_enabledSources;
};
