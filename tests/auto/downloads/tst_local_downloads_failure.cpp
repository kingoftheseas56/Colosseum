#include <QtTest>

#include "engine/LocalDownloads.h"

#include <QVariantList>
#include <QVariantMap>

namespace {

class FakeVolumeOwner final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    void setJobs(const QVariantList& jobs) { m_jobs = jobs; }

    Q_INVOKABLE QVariantList activeVolumeJobs() const { return m_jobs; }

    void fail(const QString& id, const QString& reason)
    {
        emit failed(id, reason);
    }

signals:
    void failed(const QString& id, const QString& reason);

private:
    QVariantList m_jobs;
};

QVariantMap retainedFailure(const QVariantList& rows, const QString& id)
{
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id
                && row.value(QStringLiteral("canDismiss")).toBool()) {
            return row;
        }
    }
    return {};
}

} // namespace

class tst_local_downloads_failure final : public QObject
{
    Q_OBJECT

private slots:
    void failedVolumeUsesHumanTitleFromFailedActiveJob();
    void emptyVolumeTitleNeverLeaksRoutingId();
};

void tst_local_downloads_failure::failedVolumeUsesHumanTitleFromFailedActiveJob()
{
    const QString id = QStringLiteral("tankoban:01J76XY7E9FNDZ1DBBM6PBJPFK:volume:2");
    FakeVolumeOwner owner;
    owner.setJobs({QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("seriesTitle"), QStringLiteral("Berserk")},
        {QStringLiteral("label"), QStringLiteral("Vol. 2")},
        {QStringLiteral("state"), QStringLiteral("failed")},
        {QStringLiteral("done"), 0.0},
        {QStringLiteral("total"), 0.0}
    }});

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr, &owner);
    owner.fail(id, QStringLiteral("CBZ validation failed: cannot open CBZ: file open failed"));

    const QVariantMap row = retainedFailure(downloads.activeJobs(), id);
    QVERIFY2(!row.isEmpty(), "the failed volume must be retained as a dismissible failure row");
    QVERIFY(row.value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Berserk")));
    QVERIFY(row.value(QStringLiteral("title")).toString().endsWith(QStringLiteral("Vol. 2")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral("tankoban:")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral(":volume:")));
}

void tst_local_downloads_failure::emptyVolumeTitleNeverLeaksRoutingId()
{
    const QString id = QStringLiteral("tankoban:01J76XY7EF75DJNQCV04HTPDZK:volume:1");
    FakeVolumeOwner owner;
    owner.setJobs({QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), QString()},
        {QStringLiteral("state"), QStringLiteral("failed")}
    }});

    LocalDownloads downloads(nullptr, nullptr, nullptr, nullptr, &owner);
    owner.fail(id, QStringLiteral("CBZ validation failed"));

    const QVariantMap row = retainedFailure(downloads.activeJobs(), id);
    QVERIFY2(!row.isEmpty(), "the empty-title failure must still be retained");
    QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Vol. 1"));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral("tankoban:")));
    QVERIFY(!row.value(QStringLiteral("title")).toString().contains(QStringLiteral(":volume:")));
}

QTEST_GUILESS_MAIN(tst_local_downloads_failure)
#include "tst_local_downloads_failure.moc"
