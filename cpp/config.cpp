#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

QString protocolName(Protocol value) {
  return value == Protocol::Nfs ? "nfs" : "smb";
}
QString automountName(AutomountMode value) {
  if (value == AutomountMode::Boot)
    return "boot";
  if (value == AutomountMode::Access)
    return "access";
  return "disabled";
}
static Protocol protocolFrom(const QString &value) {
  return value.compare("smb", Qt::CaseInsensitive) == 0 ? Protocol::Smb
                                                        : Protocol::Nfs;
}
static AutomountMode automountFrom(const QString &value) {
  if (value == "boot")
    return AutomountMode::Boot;
  if (value == "access")
    return AutomountMode::Access;
  return AutomountMode::Disabled;
}

QJsonObject toJson(const AppConfig &config) {
  QJsonArray servers, shares;
  for (const auto &s : config.servers)
    servers.append(QJsonObject{{"id", s.id},
                               {"name", s.name},
                               {"hostname", s.hostname},
                               {"fallback_ip", s.fallbackIp},
                               {"protocol", protocolName(s.protocol)},
                               {"nfs_version", s.nfsVersion},
                               {"smb_username", s.smbUsername},
                               {"smb_domain", s.smbDomain},
                               {"smb_version", s.smbVersion},
                               {"mount_options", s.mountOptions}});
  for (const auto &s : config.shares)
    shares.append(QJsonObject{{"id", s.id},
                              {"server_id", s.serverId},
                              {"name", s.name},
                              {"remote_path", s.remotePath},
                              {"local_path", s.localPath},
                              {"mount_options", s.mountOptions},
                              {"automount", automountName(s.automount)},
                              {"enabled", s.enabled}});
  return QJsonObject{{"schema_version", config.schemaVersion},
                     {"mount_root", config.mountRoot},
                     {"servers", servers},
                     {"shares", shares}};
}

AppConfig fromJson(const QJsonObject &o) {
  AppConfig c;
  c.schemaVersion = o.value("schema_version").toInt(1);
  c.mountRoot = o.value("mount_root").toString();
  for (const auto &v : o.value("servers").toArray()) {
    auto x = v.toObject();
    c.servers.append(
        {x["id"].toString(), x["name"].toString(), x["hostname"].toString(),
         x["fallback_ip"].toString(), protocolFrom(x["protocol"].toString()),
         x["nfs_version"].toString("4"), x["smb_username"].toString(),
         x["smb_domain"].toString(), x["smb_version"].toString("default"),
         x["mount_options"].toString()});
  }
  for (const auto &v : o.value("shares").toArray()) {
    auto x = v.toObject();
    c.shares.append({x["id"].toString(), x["server_id"].toString(),
                     x["name"].toString(), x["remote_path"].toString(),
                     x["local_path"].toString(), x["mount_options"].toString(),
                     automountFrom(x["automount"].toString()),
                     x["enabled"].toBool(true)});
  }
  return c;
}

AppConfig defaultConfig() {
  AppConfig c;
  c.mountRoot = QDir::homePath() + "/Mount";
  return c;
}
bool serverHasEnabledShares(const AppConfig &config, const QString &serverId) {
  for (const auto &share : config.shares)
    if (share.enabled && share.serverId == serverId)
      return true;
  return false;
}

ConfigStore::ConfigStore(QString path) : m_path(std::move(path)) {
  if (m_path.isEmpty())
    m_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
             "/tether/config.json";
}
AppConfig ConfigStore::load() const {
  QFile file(m_path);
  if (!file.exists())
    return defaultConfig();
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(file.errorString().toStdString());
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error(error.errorString().toStdString());
  return fromJson(doc.object());
}
void ConfigStore::save(const AppConfig &config) const {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error(file.errorString().toStdString());
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  file.write(QJsonDocument(toJson(config)).toJson(QJsonDocument::Indented));
  if (!file.commit())
    throw std::runtime_error(file.errorString().toStdString());
  QFile::setPermissions(m_path,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
