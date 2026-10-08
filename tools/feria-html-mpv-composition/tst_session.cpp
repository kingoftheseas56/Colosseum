// Isolated experiment. No Colosseum production state is created.
#include <QtTest/QtTest>
#include "sessiongate.h"
class SessionTests : public QObject {
 Q_OBJECT
private slots:
 void identity() {
   SessionGate gate;
   const QString first = gate.document();
   QVERIFY(!first.isEmpty());
   QVERIFY(gate.replace(first, "", "h264"));
   const QString old = gate.session();
   QVERIFY(gate.accepts(first, old));
   QVERIFY(gate.replace(first, old, "ffv1"));
   QVERIFY(!gate.accepts(first, old));
   QVERIFY(!gate.replace(first, old, "h264"));
   const QString second = gate.session();
   QVERIFY(gate.close(first, second));
   QVERIFY(!gate.accepts(first, second));
   QVERIFY(gate.replace(first, "", "h264"));
   const QString prior = gate.session();
   gate.navigate();
   QVERIFY(first != gate.document());
   QVERIFY(!gate.accepts(first, prior));
   QVERIFY(!gate.replace(first, "", "h264"));
 }
 void finiteFixtures() {
   SessionGate gate;
   for (const auto &id : {"https://example.org/x", "../h264-aac.mp4", "file:///etc/passwd", "", "H264"})
     QVERIFY(!gate.replace(gate.document(), "", id));
   QVERIFY(gate.session().isEmpty());
   QVERIFY(gate.replace(gate.document(), "", "missing"));
 }
};
QTEST_GUILESS_MAIN(SessionTests)
#include "tst_session.moc"
