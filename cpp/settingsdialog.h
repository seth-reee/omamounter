#pragma once
#include "config.h"
#include "systemdclient.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QLineEdit;
class QListWidget;
class QTableWidget;

class SettingsDialog : public QDialog {
  Q_OBJECT
public:
  SettingsDialog(const AppConfig &, QWidget *parent = nullptr);
  AppConfig config() const { return m_config; }
private slots:
  void loadServer(int);
  bool applyServer();
  void addServer();
  void removeServer();
  void addShare();
  void removeShare();
  void applySystemd();
  void preview();
  void saveAndAccept();
  void testConnection();
  void discover();

private:
  Q_INVOKABLE bool collectSettings();
  void rebuildServers();
  void rebuildShares();
  static QString lookupPassword(const QString &);
  static bool storePassword(const QString &, const QString &);
  void withPasswords(std::function<void()> next, bool persist);
  QHash<QString, QString> m_passwords;
  QSet<QString> m_passwordEdits;
  bool m_secretBusy = false;
  bool m_acceptAfterApply = false;
  void runDiscovery(const Server &, const QString &);
  AppConfig m_config;
  int m_loadedServer = -1;
  QListWidget *m_servers;
  QTableWidget *m_shares;
  QLineEdit *m_root, *m_name, *m_host, *m_fallback, *m_nfs, *m_user, *m_domain,
      *m_smbver, *m_options, *m_password;
  QComboBox *m_protocol;
  HelperClient m_helper;
};
