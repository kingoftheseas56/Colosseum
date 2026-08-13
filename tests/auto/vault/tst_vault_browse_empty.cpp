#include <QtTest>

#include "engine/VaultBrowseEmpty.h"

// Vault Browse face execution plan, Slice 9 — the grid's empty-cause classification (locked
// design §4.5's four distinct causes, minus the deferred "filtered", which classify() never
// produces since no filter control has shipped). Pure logic, no VaultIndex/VaultConfig
// construction needed — the three booleans this test drives are exactly what
// VaultLibrary::browseEmptyCause() computes from rootsDetail()/browseAt()/
// VaultBrowseAway::ownerRootAway() before handing off to classify(). GUILESS, pure Qt6::Core.
class VaultBrowseEmptyTest final : public QObject
{
    Q_OBJECT

private slots:
    void classifyDistinguishesNoRootsEmptyFolderAndAllAway_data();
    void classifyDistinguishesNoRootsEmptyFolderAndAllAway();
    void causeNameMapsEveryEnumValue();
};

void VaultBrowseEmptyTest::classifyDistinguishesNoRootsEmptyFolderAndAllAway_data()
{
    QTest::addColumn<bool>("hasAnyRoots");
    QTest::addColumn<bool>("levelHasRows");
    QTest::addColumn<bool>("levelAway");
    QTest::addColumn<int>("expected"); // VaultBrowseEmpty::Cause, as int for QTest table storage

    // No roots configured at all wins regardless of away/rows — there is nothing to be away OR
    // empty ABOUT yet (design: "no storage configured yet").
    QTest::newRow("no-roots-wins-over-away")
        << false << false << true << static_cast<int>(VaultBrowseEmpty::Cause::NoRoots);
    QTest::newRow("no-roots-plain")
        << false << false << false << static_cast<int>(VaultBrowseEmpty::Cause::NoRoots);

    // Real rows present -> nothing to classify, regardless of away (a mixed away/available level
    // still has SOMETHING to show — the individual away tiles, per VaultBrowseAway's own
    // offlineBrowseAt fallback; this classifier only fires once browseAt() truly returned nothing).
    QTest::newRow("has-rows-is-none")
        << true << true << false << static_cast<int>(VaultBrowseEmpty::Cause::None);
    QTest::newRow("has-rows-is-none-even-if-away")
        << true << true << true << static_cast<int>(VaultBrowseEmpty::Cause::None);

    // Roots exist, this level's rows are empty, owning root is reachable -> genuinely empty.
    QTest::newRow("empty-folder")
        << true << false << false << static_cast<int>(VaultBrowseEmpty::Cause::EmptyFolder);

    // Roots exist, this level's rows are empty, owning root is away -> all away.
    QTest::newRow("all-away")
        << true << false << true << static_cast<int>(VaultBrowseEmpty::Cause::AllAway);
}

void VaultBrowseEmptyTest::classifyDistinguishesNoRootsEmptyFolderAndAllAway()
{
    QFETCH(bool, hasAnyRoots);
    QFETCH(bool, levelHasRows);
    QFETCH(bool, levelAway);
    QFETCH(int, expected);

    const VaultBrowseEmpty::Cause got =
        VaultBrowseEmpty::classify(hasAnyRoots, levelHasRows, levelAway);
    QCOMPARE(static_cast<int>(got), expected);
}

void VaultBrowseEmptyTest::causeNameMapsEveryEnumValue()
{
    // The QML-facing vocabulary — VaultBrowseEmpty.qml's `cause` property keys off these EXACT
    // strings, and so does the Lanista/Quick-Test layer. A typo here silently breaks every
    // consumer, so each value is asserted by its literal string, not just non-empty.
    QCOMPARE(VaultBrowseEmpty::causeName(VaultBrowseEmpty::Cause::NoRoots), QStringLiteral("noRoots"));
    QCOMPARE(VaultBrowseEmpty::causeName(VaultBrowseEmpty::Cause::EmptyFolder), QStringLiteral("emptyFolder"));
    QCOMPARE(VaultBrowseEmpty::causeName(VaultBrowseEmpty::Cause::AllAway), QStringLiteral("allAway"));
    QCOMPARE(VaultBrowseEmpty::causeName(VaultBrowseEmpty::Cause::None), QStringLiteral("none"));
}

QTEST_MAIN(VaultBrowseEmptyTest)
#include "tst_vault_browse_empty.moc"
