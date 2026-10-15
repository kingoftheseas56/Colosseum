#include "BiblioTaxonomy.h"

#include <QHash>
#include <QLatin1String>

namespace {

// Fold a raw provider string to its lookup form: trim, collapse internal
// whitespace, lowercase. This is what the synonym tables are keyed on.
QString foldRaw(const QString &raw)
{
    return raw.simplified().toLower();
}

// One curated synonym table per axis. Keys are folded (see foldRaw); values are
// the stable canonical facet keys. Anything not present maps to nothing, which
// is how unknown / ambiguous / sparse tags stay invisible.

const QHash<QString, QString> &genreTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("sci-fi"), QStringLiteral("science-fiction")},
        {QStringLiteral("scifi"), QStringLiteral("science-fiction")},
        {QStringLiteral("sci fi"), QStringLiteral("science-fiction")},
        {QStringLiteral("science fiction"), QStringLiteral("science-fiction")},
        {QStringLiteral("science-fiction"), QStringLiteral("science-fiction")},
        {QStringLiteral("speculative fiction"), QStringLiteral("science-fiction")},
        {QStringLiteral("fantasy"), QStringLiteral("fantasy")},
        {QStringLiteral("high fantasy"), QStringLiteral("fantasy")},
        {QStringLiteral("epic fantasy"), QStringLiteral("fantasy")},
        {QStringLiteral("mystery"), QStringLiteral("mystery")},
        {QStringLiteral("mysteries"), QStringLiteral("mystery")},
        {QStringLiteral("detective"), QStringLiteral("mystery")},
        {QStringLiteral("thriller"), QStringLiteral("thriller")},
        {QStringLiteral("thrillers"), QStringLiteral("thriller")},
        {QStringLiteral("suspense"), QStringLiteral("thriller")},
        {QStringLiteral("romance"), QStringLiteral("romance")},
        {QStringLiteral("romances"), QStringLiteral("romance")},
        {QStringLiteral("horror"), QStringLiteral("horror")},
        {QStringLiteral("historical fiction"), QStringLiteral("historical-fiction")},
        {QStringLiteral("historical"), QStringLiteral("historical-fiction")},
        {QStringLiteral("literary fiction"), QStringLiteral("literary-fiction")},
        {QStringLiteral("literary"), QStringLiteral("literary-fiction")},
        {QStringLiteral("nonfiction"), QStringLiteral("nonfiction")},
        {QStringLiteral("non-fiction"), QStringLiteral("nonfiction")},
        {QStringLiteral("non fiction"), QStringLiteral("nonfiction")},
        {QStringLiteral("biography"), QStringLiteral("biography")},
        {QStringLiteral("biographies"), QStringLiteral("biography")},
        {QStringLiteral("memoir"), QStringLiteral("biography")},
    };
    return t;
}

const QHash<QString, QString> &audienceTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("children"), QStringLiteral("children")},
        {QStringLiteral("childrens"), QStringLiteral("children")},
        {QStringLiteral("children's"), QStringLiteral("children")},
        {QStringLiteral("kids"), QStringLiteral("children")},
        {QStringLiteral("picture book"), QStringLiteral("children")},
        {QStringLiteral("middle grade"), QStringLiteral("middle-grade")},
        {QStringLiteral("middle-grade"), QStringLiteral("middle-grade")},
        {QStringLiteral("young adult"), QStringLiteral("young-adult")},
        {QStringLiteral("young-adult"), QStringLiteral("young-adult")},
        {QStringLiteral("ya"), QStringLiteral("young-adult")},
        {QStringLiteral("teen"), QStringLiteral("young-adult")},
        {QStringLiteral("teens"), QStringLiteral("young-adult")},
        // "Adult" is a READERSHIP category (spec 2.8 / 4.3): no relationship to
        // explicit content. It is mapped like any other audience tier.
        {QStringLiteral("adult"), QStringLiteral("adult")},
        {QStringLiteral("adults"), QStringLiteral("adult")},
    };
    return t;
}

const QHash<QString, QString> &themeTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("coming of age"), QStringLiteral("coming-of-age")},
        {QStringLiteral("coming-of-age"), QStringLiteral("coming-of-age")},
        {QStringLiteral("survival"), QStringLiteral("survival")},
        {QStringLiteral("revenge"), QStringLiteral("revenge")},
        {QStringLiteral("war"), QStringLiteral("war")},
        {QStringLiteral("friendship"), QStringLiteral("friendship")},
        {QStringLiteral("identity"), QStringLiteral("identity")},
        {QStringLiteral("love"), QStringLiteral("love")},
        {QStringLiteral("power"), QStringLiteral("power")},
        {QStringLiteral("family"), QStringLiteral("family")},
        {QStringLiteral("grief"), QStringLiteral("grief")},
        {QStringLiteral("loss"), QStringLiteral("grief")},
    };
    return t;
}

const QHash<QString, QString> &settingTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("space"), QStringLiteral("space")},
        {QStringLiteral("outer space"), QStringLiteral("space")},
        {QStringLiteral("dystopia"), QStringLiteral("dystopia")},
        {QStringLiteral("dystopian"), QStringLiteral("dystopia")},
        {QStringLiteral("post-apocalyptic"), QStringLiteral("post-apocalyptic")},
        {QStringLiteral("post apocalyptic"), QStringLiteral("post-apocalyptic")},
        {QStringLiteral("postapocalyptic"), QStringLiteral("post-apocalyptic")},
        {QStringLiteral("medieval"), QStringLiteral("medieval-world")},
        {QStringLiteral("medieval world"), QStringLiteral("medieval-world")},
        {QStringLiteral("urban"), QStringLiteral("urban")},
        {QStringLiteral("city"), QStringLiteral("urban")},
        {QStringLiteral("small town"), QStringLiteral("small-town")},
        {QStringLiteral("small-town"), QStringLiteral("small-town")},
        {QStringLiteral("high seas"), QStringLiteral("high-seas")},
        {QStringLiteral("the sea"), QStringLiteral("high-seas")},
        {QStringLiteral("ocean"), QStringLiteral("high-seas")},
    };
    return t;
}

const QHash<QString, QString> &periodTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("ancient"), QStringLiteral("ancient")},
        {QStringLiteral("antiquity"), QStringLiteral("ancient")},
        {QStringLiteral("medieval"), QStringLiteral("medieval")},
        {QStringLiteral("middle ages"), QStringLiteral("medieval")},
        {QStringLiteral("renaissance"), QStringLiteral("early-modern")},
        {QStringLiteral("early modern"), QStringLiteral("early-modern")},
        {QStringLiteral("victorian"), QStringLiteral("victorian")},
        {QStringLiteral("world war i"), QStringLiteral("world-war-i")},
        {QStringLiteral("wwi"), QStringLiteral("world-war-i")},
        {QStringLiteral("world war 1"), QStringLiteral("world-war-i")},
        {QStringLiteral("first world war"), QStringLiteral("world-war-i")},
        {QStringLiteral("world war ii"), QStringLiteral("world-war-ii")},
        {QStringLiteral("wwii"), QStringLiteral("world-war-ii")},
        {QStringLiteral("world war 2"), QStringLiteral("world-war-ii")},
        {QStringLiteral("second world war"), QStringLiteral("world-war-ii")},
        {QStringLiteral("cold war"), QStringLiteral("cold-war")},
        {QStringLiteral("contemporary"), QStringLiteral("contemporary")},
        {QStringLiteral("modern"), QStringLiteral("contemporary")},
    };
    return t;
}

// Publisher table folds imprints under the parent publishing house.
const QHash<QString, QString> &publisherTable()
{
    static const QHash<QString, QString> t = {
        {QStringLiteral("penguin random house"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("penguin"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("penguin books"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("penguin classics"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("vintage"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("vintage books"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("knopf"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("alfred a. knopf"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("random house"), QStringLiteral("penguin-random-house")},
        {QStringLiteral("macmillan"), QStringLiteral("macmillan")},
        {QStringLiteral("tor"), QStringLiteral("macmillan")},
        {QStringLiteral("tor books"), QStringLiteral("macmillan")},
        {QStringLiteral("st. martin's press"), QStringLiteral("macmillan")},
        {QStringLiteral("farrar, straus and giroux"), QStringLiteral("macmillan")},
        {QStringLiteral("harpercollins"), QStringLiteral("harpercollins")},
        {QStringLiteral("harper"), QStringLiteral("harpercollins")},
        {QStringLiteral("william morrow"), QStringLiteral("harpercollins")},
        {QStringLiteral("hachette"), QStringLiteral("hachette")},
        {QStringLiteral("little, brown"), QStringLiteral("hachette")},
        {QStringLiteral("orbit"), QStringLiteral("hachette")},
    };
    return t;
}

const QHash<QString, QString> &tableForAxis(const QString &axis)
{
    static const QHash<QString, QString> empty;
    const QString a = axis.trimmed().toLower();
    if (a == QLatin1String("genre"))     return genreTable();
    if (a == QLatin1String("audience"))  return audienceTable();
    if (a == QLatin1String("theme"))     return themeTable();
    if (a == QLatin1String("setting"))   return settingTable();
    if (a == QLatin1String("period"))    return periodTable();
    if (a == QLatin1String("publisher")) return publisherTable();
    return empty;
}

BiblioFacet facet(const char *key, const char *label)
{
    return BiblioFacet{QString::fromLatin1(key), QString::fromLatin1(label)};
}

} // namespace

QVector<BiblioFilterGroup> BiblioTaxonomy::filterGroups()
{
    QVector<BiblioFilterGroup> groups;

    groups.push_back({QStringLiteral("length"), QStringLiteral("Length"),
                      {facet("short", "Short"), facet("standard", "Standard"),
                       facet("long", "Long"), facet("epic", "Epic")}});

    groups.push_back({QStringLiteral("era"), QStringLiteral("Publication Era"),
                      {facet("before-1900", "Before 1900"), facet("1900-1949", "1900-1949"),
                       facet("1950-1979", "1950-1979"), facet("1980-1999", "1980-1999"),
                       facet("2000-2009", "2000-2009"), facet("2010-2019", "2010-2019"),
                       facet("2020-present", "2020-Present")}});

    groups.push_back({QStringLiteral("audience"), QStringLiteral("Audience"),
                      {facet("children", "Children"), facet("middle-grade", "Middle Grade"),
                       facet("young-adult", "Young Adult"), facet("adult", "Adult")}});

    groups.push_back({QStringLiteral("language"), QStringLiteral("Original Language"),
                      {facet("english", "English"), facet("translated", "Translated")}});

    groups.push_back({QStringLiteral("genre"), QStringLiteral("Genre"),
                      {facet("science-fiction", "Science Fiction"), facet("fantasy", "Fantasy"),
                       facet("mystery", "Mystery"), facet("thriller", "Thriller"),
                       facet("romance", "Romance"), facet("horror", "Horror"),
                       facet("historical-fiction", "Historical Fiction"),
                       facet("literary-fiction", "Literary Fiction"),
                       facet("nonfiction", "Nonfiction"), facet("biography", "Biography")}});

    groups.push_back({QStringLiteral("theme"), QStringLiteral("Themes & Subjects"),
                      {facet("coming-of-age", "Coming of Age"), facet("survival", "Survival"),
                       facet("revenge", "Revenge"), facet("war", "War"),
                       facet("friendship", "Friendship"), facet("identity", "Identity"),
                       facet("love", "Love"), facet("power", "Power"),
                       facet("family", "Family"), facet("grief", "Grief & Loss")}});

    groups.push_back({QStringLiteral("setting"), QStringLiteral("Setting & Place"),
                      {facet("space", "Space"), facet("dystopia", "Dystopia"),
                       facet("post-apocalyptic", "Post-Apocalyptic"),
                       facet("medieval-world", "Medieval World"), facet("urban", "Urban"),
                       facet("small-town", "Small Town"), facet("high-seas", "High Seas")}});

    groups.push_back({QStringLiteral("period"), QStringLiteral("Historical Period"),
                      {facet("ancient", "Ancient"), facet("medieval", "Medieval"),
                       facet("early-modern", "Early Modern"), facet("victorian", "Victorian"),
                       facet("world-war-i", "World War I"), facet("world-war-ii", "World War II"),
                       facet("cold-war", "Cold War"), facet("contemporary", "Contemporary")}});

    // Publisher values are data-derived (curatedPublishers() applies the coverage
    // floor to the active snapshot), so the axis is advertised with no static
    // values here.
    groups.push_back({QStringLiteral("publisher"), QStringLiteral("Publisher"), {}});

    return groups;
}

QString BiblioTaxonomy::normalize(const QString &axis, const QString &raw)
{
    const QString key = foldRaw(raw);
    if (key.isEmpty())
        return QString();
    return tableForAxis(axis).value(key); // "" when the tag is unknown
}

QString BiblioTaxonomy::lengthKey(int pages)
{
    if (pages <= 0)   return QString();           // no reliable pagination
    if (pages < 200)  return QStringLiteral("short");
    if (pages < 500)  return QStringLiteral("standard");
    if (pages < 800)  return QStringLiteral("long");
    return QStringLiteral("epic");
}

QString BiblioTaxonomy::eraKey(int year)
{
    if (year <= 0)    return QString();           // unknown / unreliable year
    if (year < 1900)  return QStringLiteral("before-1900");
    if (year < 1950)  return QStringLiteral("1900-1949");
    if (year < 1980)  return QStringLiteral("1950-1979");
    if (year < 2000)  return QStringLiteral("1980-1999");
    if (year < 2010)  return QStringLiteral("2000-2009");
    if (year < 2020)  return QStringLiteral("2010-2019");
    return QStringLiteral("2020-present");
}

QString BiblioTaxonomy::languageKey(const QString &originalLanguage, bool englishEditionAvailable)
{
    const QString lang = originalLanguage.simplified().toLower();
    const bool originalIsEnglish = lang == QLatin1String("english")
                                   || lang == QLatin1String("en")
                                   || lang == QLatin1String("eng");
    if (originalIsEnglish)
        return QStringLiteral("english");
    if (englishEditionAvailable)
        return QStringLiteral("translated");
    return QString(); // originally another language, no English edition => out of catalogue
}

QStringList BiblioTaxonomy::curatedPublishers(const QList<BiblioWork> &works)
{
    QHash<QString, int> counts;
    for (const BiblioWork &w : works) {
        const QString key = normalize(QStringLiteral("publisher"), w.publisher);
        if (!key.isEmpty())
            counts[key] += 1;
    }
    QStringList out;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() >= kPublisherCoverageFloor)
            out << it.key();
    }
    out.sort();
    return out;
}
