#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <netdb.h>
#include <unistd.h>

#include <stdexcept>

namespace {
const QString systemdDir = "/etc/systemd/system";
const QString stateDir = "/etc/omamounter";
const QString manifestPath = stateDir + "/managed.json";

struct Generated {
  QString shareId, mountUnit, automountUnit, localPath, enableUnit;
  QHash<QString, QByteArray> files;
};

[[noreturn]] void fail(const QString &message) {
  throw std::runtime_error(message.toStdString());
}
bool validId(const QString &v) {
  return !v.isEmpty() && v.size() <= 128 &&
         !v.contains(QRegularExpression("[^A-Za-z0-9_.-]"));
}
bool validHost(const QString &v) {
  return !v.isEmpty() && v.size() <= 253 &&
         !v.contains(QRegularExpression("[^A-Za-z0-9.:-]"));
}
bool validPath(const QString &v) {
  return v.startsWith('/') && !v.contains(QChar('\0')) && !v.contains('\n') &&
         !v.contains('\r') && !v.split('/').contains("..");
}
bool validOptions(const QString &v) {
  return v.size() <= 1024 && !v.contains(QRegularExpression("[\\s\\x00]")) &&
         !v.toLower().contains("password=") &&
         !(v.contains(",,") || v.startsWith(',') || v.endsWith(','));
}

QString chosenAddress(const Server &server) {
  QByteArray hostname = server.hostname.toUtf8();
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo *result = nullptr;
  if (getaddrinfo(hostname.constData(), nullptr, &hints, &result) == 0) {
    freeaddrinfo(result);
    return server.hostname;
  }
  if (!server.fallbackIp.isEmpty() && validHost(server.fallbackIp))
    return server.fallbackIp;
  fail("Hostname did not resolve and no fallback IP is configured");
}

QString escapePath(const QString &value) {
  if (!validPath(value))
    fail("Invalid local mount path");
  QByteArray raw = QDir::cleanPath(value).mid(1).toUtf8(), out;
  if (raw.isEmpty())
    return "-";
  for (uchar b : raw) {
    if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
        (b >= '0' && b <= '9') || b == '_' || b == '.')
      out += char(b);
    else if (b == '/')
      out += '-';
    else
      out += QString("\\x%1").arg(b, 2, 16, QChar('0')).toLatin1();
  }
  return QString::fromLatin1(out);
}

void writeFile(const QString &path, const QByteArray &data,
               QFileDevice::Permissions permissions) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
      !file.commit())
    fail("Could not write " + path);
  if (!QFile::setPermissions(path, permissions))
    fail("Could not secure " + path);
}
void systemctl(const QStringList &arguments, bool tolerateFailure = false) {
  int code = QProcess::execute("/usr/bin/systemctl", arguments);
  if (code != 0 && !tolerateFailure)
    fail("systemctl " + arguments.join(' ') + " failed");
}

Generated generate(const Server &server, const Share &share) {
  if (!validId(server.id) || !validId(share.id) ||
      !validHost(server.hostname) || !validPath(share.localPath) ||
      !validOptions(server.mountOptions) || !validOptions(share.mountOptions) ||
      share.name.contains(QRegularExpression("[\\n\\r\\x00]")))
    fail("Invalid configuration for share " + share.name);
  if (!server.fallbackIp.isEmpty() && !validHost(server.fallbackIp))
    fail("Invalid fallback address");
  QString what, type,
      options = share.mountOptions.isEmpty() ? server.mountOptions
                                             : share.mountOptions;
  const QString address = chosenAddress(server);
  if (server.protocol == Protocol::Nfs) {
    if (!validPath(share.remotePath))
      fail("Invalid NFS export path");
    what = address + ":" + share.remotePath;
    type = "nfs";
    if (options.isEmpty())
      options = "nfsvers=" + server.nfsVersion;
  } else {
    if (share.remotePath.isEmpty() ||
        share.remotePath.contains(QRegularExpression("[\\\\/\\n\\r\\x00]")) ||
        server.smbUsername.isEmpty())
      fail("Invalid SMB configuration");
    what = "//" + address + "/" + share.remotePath;
    type = "cifs";
    QStringList values{"credentials=" + stateDir + "/credentials/" + server.id +
                       ".cred"};
    if (server.smbVersion != "default")
      values << "vers=" + QString(server.smbVersion).remove("SMB");
    if (!options.isEmpty())
      values << options;
    options = values.join(',');
  }
  Generated result;
  result.shareId = share.id;
  result.localPath = share.localPath;
  auto escaped = escapePath(share.localPath);
  result.mountUnit = escaped + ".mount";
  result.files[result.mountUnit] =
      QString("[Unit]\nDescription=omamounter %1 share "
              "%2\nWants=network-online.target\nAfter=network-online."
              "target\n\n[Mount]\nWhat=%3\nWhere=%4\nType=%5\nOptions=%6\n\n["
              "Install]\nWantedBy=multi-user.target\n")
          .arg(protocolName(server.protocol).toUpper(), share.name, what,
               share.localPath, type, options)
          .toUtf8();
  if (share.automount == AutomountMode::Access) {
    result.automountUnit = escaped + ".automount";
    result.enableUnit = result.automountUnit;
    result.files[result.automountUnit] =
        QString(
            "[Unit]\nDescription=Automount omamounter share "
            "%1\nWants=network-online.target\nAfter=network-online.target\n\n["
            "Automount]\nWhere=%2\n\n[Install]\nWantedBy=multi-user.target\n")
            .arg(share.name, share.localPath)
            .toUtf8();
  } else if (share.automount == AutomountMode::Boot)
    result.enableUnit = result.mountUnit;
  return result;
}

QByteArray stdinData() {
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly))
    fail("Could not read request");
  return input.readAll();
}
QJsonObject parseInput() {
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(stdinData(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    fail("Invalid JSON request");
  return doc.object();
}

QJsonArray loadManaged() {
  QFile f(manifestPath);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(f.readAll())
      .object()
      .value("shares")
      .toArray();
}

void apply() {
  auto request = parseInput(),
       configObject = request.value("config").toObject(),
       credentials = request.value("credentials").toObject();
  AppConfig config = fromJson(configObject);
  QHash<QString, Server> servers;
  for (const auto &s : config.servers) {
    if (servers.contains(s.id))
      fail("Duplicate server ID");
    servers[s.id] = s;
  }
  QVector<Generated> generated;
  QSet<QString> shareIds;
  for (const auto &share : config.shares)
    if (share.enabled) {
      if (shareIds.contains(share.id))
        fail("Duplicate share ID");
      shareIds.insert(share.id);
      if (!servers.contains(share.serverId))
        fail("Share references missing server");
      generated << generate(servers[share.serverId], share);
    }
  if (generated.isEmpty())
    fail("No enabled shares configured");
  QSet<QString> newUnits;
  for (const auto &g : generated)
    for (auto it = g.files.begin(); it != g.files.end(); ++it)
      newUnits.insert(it.key());
  for (const auto &v : loadManaged()) {
    auto o = v.toObject();
    for (const auto &name :
         {o["mount_unit"].toString(), o["automount_unit"].toString()})
      if (!name.isEmpty() && !newUnits.contains(name)) {
        systemctl({"disable", "--now", name}, true);
        QFile::remove(systemdDir + "/" + name);
      }
  }
  QDir().mkpath(systemdDir);
  QDir().mkpath(stateDir + "/credentials");
  QFile::setPermissions(stateDir, QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);
  QFile::setPermissions(stateDir + "/credentials", QFileDevice::ReadOwner |
                                                       QFileDevice::WriteOwner |
                                                       QFileDevice::ExeOwner);
  for (const auto &g : generated) {
    QDir().mkpath(g.localPath);
    for (auto it = g.files.begin(); it != g.files.end(); ++it)
      writeFile(systemdDir + "/" + it.key(), it.value(),
                QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                    QFileDevice::ReadGroup | QFileDevice::ReadOther);
  }
  for (const auto &s : config.servers)
    if (s.protocol == Protocol::Smb) {
      auto c = credentials.value(s.id).toObject();
      auto user = c["username"].toString(), password = c["password"].toString(),
           domain = c["domain"].toString();
      if (user != s.smbUsername || user.isEmpty() || password.isEmpty() ||
          (user + password + domain)
              .contains(QRegularExpression("[\\n\\r\\x00]")))
        fail("Invalid or missing SMB credentials");
      QByteArray content = "username=" + user.toUtf8() +
                           "\npassword=" + password.toUtf8() + "\n";
      if (!domain.isEmpty())
        content += "domain=" + domain.toUtf8() + "\n";
      writeFile(stateDir + "/credentials/" + s.id + ".cred", content,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
  systemctl({"daemon-reload"});
  for (const auto &g : generated)
    if (!g.enableUnit.isEmpty())
      systemctl({"enable", "--now", g.enableUnit});
  QJsonArray manifest;
  for (const auto &g : generated)
    manifest.append(QJsonObject{{"share_id", g.shareId},
                                {"mount_unit", g.mountUnit},
                                {"automount_unit", g.automountUnit}});
  writeFile(manifestPath,
            QJsonDocument(QJsonObject{{"shares", manifest}})
                .toJson(QJsonDocument::Indented),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void control() {
  auto request = parseInput();
  auto action = request["action"].toString();
  if (action != "mount" && action != "unmount")
    fail("Invalid action");
  QHash<QString, QString> managed;
  for (const auto &v : loadManaged()) {
    auto o = v.toObject();
    managed[o["share_id"].toString()] = o["mount_unit"].toString();
  }
  auto ids = request["share_ids"].toArray();
  if (ids.isEmpty())
    fail("No shares requested");
  for (const auto &v : ids) {
    auto id = v.toString();
    if (!managed.contains(id))
      fail("Share is not in the root-owned manifest");
    systemctl({action == "mount" ? "start" : "stop", managed[id]});
  }
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (geteuid() != 0) {
    fprintf(stderr, "omamounter helper must run as root\n");
    return 1;
  }
  try {
#ifdef OMAMOUNTER_APPLY_HELPER
    apply();
#else
    control();
#endif
    fprintf(stdout, "{\"ok\":true}\n");
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "omamounter helper: %s\n", e.what());
    return 1;
  }
}
