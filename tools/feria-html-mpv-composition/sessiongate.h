// Throwaway, in-memory command identity. Not canonical player state.
#pragma once
#include <QString>
#include <QUuid>
class SessionGate {
    QString m_document = fresh(), m_session;
    static QString fresh() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
public:
    QString document() const { return m_document; }
    QString session() const { return m_session; }
    bool accepts(const QString &document, const QString &session) const {
        return document == m_document && !m_session.isEmpty() && session == m_session;
    }
    static bool fixtureAllowed(const QString &id) { return id == "h264" || id == "ffv1" || id == "missing"; }
    bool replace(const QString &document, const QString &expectedSession, const QString &fixture) {
        if (document != m_document || expectedSession != m_session || !fixtureAllowed(fixture)) return false;
        m_session = fresh(); return true;
    }
    bool close(const QString &document, const QString &session) {
        if (!accepts(document, session)) return false;
        m_session.clear(); return true;
    }
    void navigate() { m_document = fresh(); m_session.clear(); }
};
