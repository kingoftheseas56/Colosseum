#pragma once
#include "account/credentials/AccountSecretBackend.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

class HomeServerStore {
public:
  explicit HomeServerStore(std::shared_ptr<AccountSecretBackend> secrets =
                               createAccountSecretBackend());
  bool activate(QString profileId, QString profileRoot);
  void deactivate();
  bool available() const;
  QString error() const;
  QJsonArray connections() const;
  QJsonObject connection(QString id) const;
  QString token(QString id) const;
  QString add(QJsonObject metadata, QString token);
  bool setEnabled(QString id, bool enabled);
  bool remove(QString id);
  QJsonArray items(QString connectionId) const;
  bool replaceItems(QString id, QJsonArray items);
  // Missing libraries means an older, items-only snapshot, not zero libraries.
  QJsonObject catalog(QString connectionId) const;
  bool replaceCatalog(QString id, QJsonArray libraries, QJsonArray items);

private:
  QString target(const QString &id) const;
  QString indexPath(const QString &id) const;
  bool saveConnections(const QJsonArray &rows);
  std::shared_ptr<AccountSecretBackend> m_secrets;
  QString m_profileId, m_directory, m_secretPrefix;
  mutable QString m_error;
  QJsonArray m_connections;
  bool m_active = false;
};
