#include "engine/TankoyomiSeriesMatcher.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &label) {
        qInfo().noquote() << (ok ? "ok" : "FAIL") << label;
        if (!ok) ++failures;
    };
    const auto row = [](const QString &id, const QString &title) {
        return QVariantMap{{"id", id}, {"title", title}, {"url", "https://source.example/" + id}};
    };
    TankoyomiSeriesQuery plain;
    plain.title = "One Piece";
    const auto match = [&](const QVariantList &rows, const QStringList &decorators = QStringList{}) {
        return TankoyomiSeriesMatcher::match(plain, rows, decorators);
    };
    check(match({row("right", "ONE---PIECE")}).accepted, "case and separator folding is exact");
    const auto exact = match({row("wrong", "Other Series"), row("right", "One Piece")});
    check(exact.accepted && exact.row.value("id") == QStringLiteral("right"), "unrelated first row never beats exact identity");
    check(!match({row("wrong", "Other Series")}).accepted, "unrelated only result is rejected");
    check(!match({row("spinoff", "One Piece Party")}).accepted, "One Piece Party is not One Piece");
    check(!match({row("official", "Official One Piece")}).accepted, "undeclared decorator is not guessed");
    check(match({row("official", "Official One Piece")}, {"official"}).accepted, "WeebCentral declared official wrapper is accepted");
    check(match({row("official", "One Piece (Official)")}, {"official"}).accepted, "declared trailing decorator is accepted");
    check(!match({row("wrong", "Unofficial One Piece")}, {"official"}).accepted, "decorator matching respects token boundaries");
    const auto priority = match({row("decorated", "Official One Piece"), row("exact", "One Piece")}, {"official"});
    check(priority.accepted && priority.row.value("id") == QStringLiteral("exact"), "exact tier has priority over decorated tier");
    const auto ambiguous = match({row("one", "One Piece"), row("two", "One Piece")});
    check(!ambiguous.accepted && ambiguous.reason.contains("ambiguous"), "distinct exact identities are ambiguous");
    check(!match({row("one", "Official One Piece"), row("two", "One Piece Official")}, {"official"}).accepted,
          "distinct decorated identities are ambiguous");
    check(match({row("same", "One Piece"), row("same", "One Piece")}).accepted, "duplicate rows of one identity do not create ambiguity");
    check(!match({row("", "One Piece")}).accepted, "missing provider identity is rejected");
    TankoyomiSeriesQuery alias;
    alias.title = "JoJo's Bizarre Adventure";
    check(TankoyomiSeriesMatcher::match(alias, {row("jojo", "JoJos Bizarre Adventure")}).accepted,
          "established apostrophe folding is preserved");
    alias.title = "Canonical"; alias.aliases = {"Alternate Name"};
    check(TankoyomiSeriesMatcher::match(alias, {row("alias", "alternate-name")}).accepted, "explicit discovery alias is accepted");
    TankoyomiSeriesQuery color;
    color.title = "One Piece (Color)";
    color.discoveryTitle = "One Piece Colored";
    color.aliases = {"One Piece Digital Colored Comics", "One Piece Full Colour"};
    color.requiredTitleMarkers = {"colored", "full color", "full colour"};
    check(TankoyomiSeriesMatcher::match(color, {row("color", "Official One Piece Colored")}, {"official"}).accepted,
          "colored query retains provider decoration and required edition identity");
    check(TankoyomiSeriesMatcher::match(color, {row("alias-color", "One Piece Digital Colored Comics")}).accepted,
          "colored alias identity is accepted");
    check(TankoyomiSeriesMatcher::match(color, {row("uk-color", "One Piece Full Colour")}).accepted,
          "required markers are alternatives rather than all-required");
    check(!TankoyomiSeriesMatcher::match(color, {row("plain", "One Piece")}).accepted,
          "colored edition never falls onto ordinary One Piece");
    plain.requiredTitleMarkers = {"colored"};
    check(!TankoyomiSeriesMatcher::match(plain, {row("plain", "One Piece")}).accepted,
          "exact title cannot override an absent required marker");
    check(!TankoyomiSeriesMatcher::match(TankoyomiSeriesQuery{}, {row("one", "One Piece")}).accepted,
          "empty identity query fails closed");
    qInfo() << (failures ? "TANKOYOMI_SERIES_MATCHER_FAIL" : "TANKOYOMI_SERIES_MATCHER_OK");
    return failures ? 1 : 0;
}
