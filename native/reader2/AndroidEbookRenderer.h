#pragma once

#include <QJniObject>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QWindow>

class Reader2Bridge;

class AndroidEbookRenderer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* foreignWindow READ foreignWindow NOTIFY foreignWindowChanged)
    Q_PROPERTY(bool glueUp READ glueUp NOTIFY glueUpChanged)
public:
    explicit AndroidEbookRenderer(Reader2Bridge* bridge, QObject* parent = nullptr);
    ~AndroidEbookRenderer() override;
    QWindow* foreignWindow() const { return m_window; }
    bool glueUp() const { return m_ready; }
    Q_INVOKABLE void create();
    Q_INVOKABLE void close();
    Q_INVOKABLE void open(const QString& path, const QString& cfi, int generation);
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void goTo(const QVariant& target);
    Q_INVOKABLE void setAppearance(const QVariant& value);
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void addHighlight(const QVariant& value);
    Q_INVOKABLE void removeHighlight(const QString& id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setReadAlongStyle(const QVariant& value);
    Q_INVOKABLE void paintReadAlong(const QVariant& value);
    Q_INVOKABLE void clearReadAlong();
    Q_INVOKABLE void ensureReadAlongVisible(const QVariant& value);
    Q_INVOKABLE void navigateReadAlong(const QVariant& value);
    Q_INVOKABLE void focusPaper();
    Q_INVOKABLE void configureOverlay(QWindow* window);
    void receive(quint64 instance, const QString& event, const QString& json);
signals:
    void foreignWindowChanged();
    void glueUpChanged();
    void eventRaised(const QString& name, const QString& json);
private:
    void command(const QString& name, const QVariantList& args = {});
    void setReady(bool ready);
    QPointer<Reader2Bridge> m_bridge;
    QPointer<QWindow> m_window;
    QJniObject m_host;
    quint64 m_instance = 0;
    int m_generation = 0;
    bool m_ready = false;
    bool m_shellReady = false;
    bool m_creating = false;
};
