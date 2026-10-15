#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include "FeriaAccountStore.h"
#include "FeriaBrowserPolicy.h"
#ifdef FERIA_WRAPPER_RUNTIME
#include <QtWebEngineQuick/QtWebEngineQuick>
#endif

class FeriaResumeTestSetup : public QObject {
    Q_OBJECT
    QTemporaryDir m_temp;
    FeriaAccountStore m_store;
    FeriaBrowserPolicy m_policy;
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        if (!m_temp.isValid()) qFatal("Cannot create isolated Feria test storage");
        m_policy.setStorageRoot(m_temp.filePath("browser"));
        FeriaBrowserPolicy::registerTypes();
        engine->rootContext()->setContextProperty("FeriaBrowserPolicy", &m_policy);
        m_store.setStoragePath(m_temp.filePath("account.json"));
        engine->rootContext()->setContextProperty("feriaTestStore", &m_store);
        engine->rootContext()->setContextProperty("feriaTestRoot", m_temp.path());
        engine->rootContext()->setContextProperty("feriaBrowserTestBaseUrl",
            qEnvironmentVariable("FERIA_BROWSER_TEST_BASE_URL"));
        engine->rootContext()->setContextProperty("feriaBrowserTestOutput",
            qEnvironmentVariable("FERIA_BROWSER_TEST_OUTPUT"));
    }
};
int main(int argc, char **argv) {
#ifdef FERIA_WRAPPER_RUNTIME
    QtWebEngineQuick::initialize();
#endif
    FeriaResumeTestSetup setup;
    return quick_test_main_with_setup(argc, argv, "feria_resume", nullptr, &setup);
}
#include "feria_resume_qml_test.moc"
