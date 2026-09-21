#pragma once
#include "config.h"
#include "systemdclient.h"
#include <QDialog>
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
  void applyServer();
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
  void rebuildServers();
  void rebuildShares();
  QString lookupPassword(const QString &) const;
  bool storePassword(const QString &, const QString &) const;
  void runDiscovery(const Server &, const QString &);
  AppConfig m_config;
  QListWidget *m_servers;
  QTableWidget *m_shares;
  QLineEdit *m_root, *m_name, *m_host, *m_fallback, *m_nfs, *m_user, *m_domain,
      *m_smbver, *m_options, *m_password;
  QComboBox *m_protocol;
  HelperClient m_helper;
};
