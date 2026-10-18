#include "SessionStore.h"
#include <QCoreApplication>
#include <iostream>
#include <cstdlib>
void check(bool ok, const char *message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SessionStore store;
    const QVariantMap music{{"appType","vinyl"},{"contentKind","music"},{"title","Vinyl"},
                            {"target",QVariantMap{{"id","vinyl-session"}}}};
    const auto id=store.openOrSwitch(music);
    check(store.openOrSwitch(music)==id && store.list().size()==1,"one canonical music tile");
    auto group=store.groups().first().toMap();
    check(group.value("title")=="Vinyl","music tile title");
    check(group.value("icon")=="../assets/icons/music.svg","music tile icon");
    store.saveState(id,{{"page","song"}});
    check(store.get(id).value("savedState").toMap().size()==1,"route only state");
    for (const auto &name : {"theatre","biblio","tankoban"}) {
        store.openOrSwitch({{"appType",name},{"contentKind","fixture"},{"target",QVariantMap{{"id",name}}}});
        const auto other=store.groups().last().toMap();
        const QString appName = QString::fromLatin1(name);
        const QString title = appName == "theatre" ? "Theatre" : appName == "biblio" ? "Biblio" : "Tankoban";
        const QString icon = appName == "theatre" ? "play" : appName == "biblio" ? "books" : "manga";
        check(other.value("title") == title && other.value("icon") == "../assets/icons/" + icon + ".svg",
              "existing world title and icon unchanged");
    }
    store.close(id);
    check(store.get(id).isEmpty(),"closed tile clears saved route");
    check(store.list().size()==3,"other world records retained");
    std::cout << "PASS Vinyl SessionStore identity, icon, route-only state, close and other worlds (9 checks)\n";
}
