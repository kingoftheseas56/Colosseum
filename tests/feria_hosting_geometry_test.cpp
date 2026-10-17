#include "feria/FeriaBrowserPolicy.h"
#include <QGuiApplication>
#include <QMargins>
#include <QScreen>
#include <QWindow>
#include <QtTest>

class FeriaHostingGeometryTest : public QObject {
    Q_OBJECT
    static QRect clamp(FeriaBrowserPolicy &policy, const QRect &requested,
                       const QRect &workArea, const QMargins &frame) {
        QRect out;
        QMetaObject::invokeMethod(&policy, "clampWindowGeometry", Q_RETURN_ARG(QRect, out),
            Q_ARG(QRect, requested), Q_ARG(QRect, workArea), Q_ARG(QMargins, frame));
        return out;
    }
private slots:
    void apiContract() {
        FeriaBrowserPolicy policy;
        for (const char *signature : {"windowWorkArea(QObject*)", "boundedWindowGeometry(QRect,QObject*)",
                                     "clampWindowGeometry(QRect,QRect,QMargins)"})
            QVERIFY2(policy.metaObject()->indexOfMethod(signature) >= 0, signature);
    }
    void negativeMonitorOriginAndFrameStayInside() {
        FeriaBrowserPolicy policy;
        const QRect area(-1920, -200, 1920, 1040);
        const QMargins frame(8, 30, 8, 8);
        const QRect result = clamp(policy, QRect(9000, -9000, 9000, 9000), area, frame);
        QVERIFY(!result.isEmpty()); QVERIFY(area.contains(result.marginsAdded(frame)));
        QCOMPARE(result.marginsAdded(frame), area);
        const QRect fitting(-1800, -100, 800, 600);
        QCOMPARE(clamp(policy, fitting, area, frame), fitting);
    }
    void defaultsMinimaAndTinyWorkArea() {
        FeriaBrowserPolicy policy;
        const QRect ordinary(50, 100, 1280, 720);
        const QMargins frame(5, 25, 5, 5);
        const QRect defaults = clamp(policy, QRect(50, 100, 0, 0), ordinary, frame);
        QVERIFY(defaults.width() >= 320); QVERIFY(defaults.height() >= 240);
        QVERIFY(ordinary.contains(defaults.marginsAdded(frame)));
        const QRect tiny(-80, -60, 90, 70);
        const QRect result = clamp(policy, QRect(1000, 1000, 1, 1), tiny, frame);
        QVERIFY(!result.isEmpty()); QVERIFY(tiny.contains(result.marginsAdded(frame)));
        QCOMPARE(result.size(), QSize(80, 40)); // Minima shrink only to actual available client size.
        QVERIFY(clamp(policy, {}, tiny, QMargins(60, 60, 60, 60)).isEmpty());
        QVERIFY(clamp(policy, {}, {}, {}).isEmpty());
    }
    void actualWindowUsesItsScreenAvailableGeometry() {
        FeriaBrowserPolicy policy;
        QWindow window;
        window.create();
        QVERIFY(window.screen());
        QRect area;
        QVERIFY(QMetaObject::invokeMethod(&policy, "windowWorkArea", Q_RETURN_ARG(QRect, area),
            Q_ARG(QObject*, static_cast<QObject *>(&window))));
        QCOMPARE(area, window.screen()->availableGeometry());
        QRect result;
        const QRect requested(area.x() - 10000, area.y() - 10000, 100000, 100000);
        QVERIFY(QMetaObject::invokeMethod(&policy, "boundedWindowGeometry", Q_RETURN_ARG(QRect, result),
            Q_ARG(QRect, requested), Q_ARG(QObject*, static_cast<QObject *>(&window))));
        QVERIFY(!result.isEmpty()); QVERIFY(area.contains(result.marginsAdded(window.frameMargins())));
    }
};
QTEST_MAIN(FeriaHostingGeometryTest)
#include "feria_hosting_geometry_test.moc"
