#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

enum class Protocol { Nfs, Smb };
enum class AutomountMode { Disabled, Boot, Access };

struct Server {
  QString id, name, hostname, fallbackIp;
  Protocol protocol = Protocol::Nfs;
  QString nfsVersion = "4", smbUsername, smbDomain, smbVersion = "default",
          mountOptions;
};

struct Share {
  QString id, serverId, name, remotePath, localPath, mountOptions;
  AutomountMode automount = AutomountMode::Disabled;
  bool enabled = true;
};

struct AppConfig {
  int schemaVersion = 1;
  QString mountRoot;
  QVector<Server> servers;
  QVector<Share> shares;
};

QString protocolName(Protocol value);
QString automountName(AutomountMode value);
QJsonObject toJson(const AppConfig &config);
AppConfig fromJson(const QJsonObject &object);
AppConfig importedDefaults();

class ConfigStore {
public:
  explicit ConfigStore(QString path = {});
  QString path() const { return m_path; }
  AppConfig load() const;
  void save(const AppConfig &config) const;

private:
  QString m_path;
};
