#include "config.h"
#include "filetransaction.h"
#include "mountidentity.h"
#include <QLockFile>

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
#ifdef TETHER_HELPER_TEST
QString systemdDir, stateDir, manifestPath;
QJsonObject testRequest;
QByteArray testMountinfo;
QStringList testCommands;
QString testFailure;
#else
const QString systemdDir = "/etc/systemd/system";
const QString stateDir = "/etc/tether";
const QString manifestPath = stateDir + "/managed.json";
#endif

struct Generated {
  QString source, type;
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
#ifdef TETHER_HELPER_TEST
  return server.hostname;
#else
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
#endif
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
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(permissions) ||
      file.write(data) != data.size() || !file.commit())
    fail("Could not write " + path);
  if (!QFile::setPermissions(path, permissions))
    fail("Could not secure " + path);
}
void systemctl(const QStringList &arguments, bool tolerateFailure = false) {
#ifdef TETHER_HELPER_TEST
  const auto command = arguments.join(' ');
  testCommands << command;
  if (command == testFailure) {
    testFailure.clear();
    if (!tolerateFailure) fail("Injected systemctl failure");
  }
#else
  int code = QProcess::execute("/usr/bin/systemctl", arguments);
  if (code != 0 && !tolerateFailure)
    fail("systemctl " + arguments.join(' ') + " failed");
#endif
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
  result.source = what;
  result.type = type;
  result.shareId = share.id;
  result.localPath = share.localPath;
  auto escaped = escapePath(share.localPath);
  result.mountUnit = escaped + ".mount";
  result.files[result.mountUnit] =
      QString("[Unit]\nDescription=Tether %1 share "
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
            "[Unit]\nDescription=Automount Tether share "
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
#ifdef TETHER_HELPER_TEST
  return testRequest;
#else
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(stdinData(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    fail("Invalid JSON request");
  return doc.object();
#endif
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

void verifyMount(const QJsonObject &record) {
#ifdef TETHER_HELPER_TEST
  if (!mountIdentityMatches(testMountinfo, record["local_path"].toString(),
                            record["source"].toString(), record["type"].toString(),
                            !record["automount_unit"].toString().isEmpty()))
#else
  QFile mounts("/proc/self/mountinfo");
  if (!mounts.open(QIODevice::ReadOnly) ||
      !mountIdentityMatches(mounts.readAll(), record["local_path"].toString(),
                            record["source"].toString(),
                            record["type"].toString(),
                            !record["automount_unit"].toString().isEmpty()))
#endif
    fail("Mount identity could not be verified; refusing to change this share. "
         "Reapply legacy configurations first.");
}

void apply() {
  auto request = parseInput(),
       configObject = request.value("config").toObject(),
       credentials = request.value("credentials").toObject();
  AppConfig config = fromJson(configObject);
  QHash<QString, Server> servers;
  for (const auto &s : config.servers) {
    if (!validId(s.id) || !validHost(s.hostname))
      fail("Invalid server identity");
    if (!QStringList{"3", "4", "4.0", "4.1", "4.2"}.contains(s.nfsVersion))
      fail("Unsupported NFS version");
    if (!QStringList{"default", "2.0", "2.1", "3", "3.0", "3.02", "3.1.1",
                     "SMB2", "SMB3"}
             .contains(s.smbVersion))
      fail("Unsupported SMB version");
    if (s.protocol == Protocol::Smb && serverHasEnabledShares(config, s.id)) {
      auto c = credentials.value(s.id).toObject();
      const auto user = c["username"].toString(),
                 password = c["password"].toString(),
                 domain = c["domain"].toString();
      if (user != s.smbUsername || user.isEmpty() || password.isEmpty() ||
          (user + password + domain)
              .contains(QRegularExpression("[\\n\\r\\x00]")))
        fail("Invalid or missing SMB credentials");
    }
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
  QSet<QString> newUnits;
  for (const auto &g : generated)
    for (auto it = g.files.begin(); it != g.files.end(); ++it) {
      if (newUnits.contains(it.key()))
        fail("Two shares use the same mount destination");
      newUnits.insert(it.key());
    }
  const auto oldManifest = loadManaged();
  // Check every target before any unit removal, directory creation, or write.
  for (const auto &g : generated) {
    QString path = QDir::cleanPath(g.localPath);
    if (path == "/" || path == "/etc" || path == "/usr" || path == "/var" ||
        path == "/home")
      fail("Refusing a system directory as a mount destination");
    while (path != "/") {
      if (QFileInfo(path).isSymLink())
        fail("Mount destinations must not traverse symlinks");
      path = QFileInfo(path).absolutePath();
    }
    bool ownedAutomount = false;
    for (const auto &old : oldManifest)
      if (old.toObject()["local_path"].toString() == g.localPath &&
          !old.toObject()["automount_unit"].toString().isEmpty())
        ownedAutomount = true;
    verifyMount(
        QJsonObject{{"local_path", g.localPath},
                    {"source", g.source},
                    {"type", g.type},
                    {"automount_unit", ownedAutomount ? "managed" : ""}});
  }
  QSet<QString> ownedUnits;
  for (const auto &v : oldManifest) {
    ownedUnits.insert(v.toObject()["mount_unit"].toString());
    ownedUnits.insert(v.toObject()["automount_unit"].toString());
  }
  for (const auto &name : newUnits) {
    if (!ownedUnits.contains(name) &&
        (QFileInfo::exists(systemdDir + "/" + name) ||
         QFileInfo::exists("/usr/lib/systemd/system/" + name) ||
         QFileInfo::exists("/run/systemd/system/" + name)))
      fail("Refusing to replace an unrelated systemd unit: " + name);
  }
  FileTransaction transaction;
  transaction.capture(manifestPath);
  for (const auto &name : ownedUnits)
    if (!name.isEmpty())
      transaction.capture(systemdDir + "/" + name);
  for (const auto &name : newUnits)
    transaction.capture(systemdDir + "/" + name);
  for (const auto &server : config.servers)
    if (server.protocol == Protocol::Smb && serverHasEnabledShares(config, server.id))
      transaction.capture(stateDir + "/credentials/" + server.id + ".cred");
  try {
    for (const auto &v : oldManifest) {
      auto o = v.toObject();
      for (const auto &name :
           {o["automount_unit"].toString(), o["mount_unit"].toString()})
        if (!name.isEmpty() && !newUnits.contains(name)) {
          verifyMount(o);
          // Stop before removal; enablement is reconciled after files commit.
          systemctl({"stop", name});
          if (QFile::exists(systemdDir + "/" + name) &&
              !QFile::remove(systemdDir + "/" + name))
            fail("Could not remove retired unit");
        }
    }
    QDir().mkpath(systemdDir);
    QDir().mkpath(stateDir + "/credentials");
    QFile::setPermissions(stateDir, QFileDevice::ReadOwner |
                                        QFileDevice::WriteOwner |
                                        QFileDevice::ExeOwner);
    QFile::setPermissions(stateDir + "/credentials",
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    for (const auto &g : generated) {
      QDir().mkpath(g.localPath);
      for (auto it = g.files.begin(); it != g.files.end(); ++it)
        writeFile(systemdDir + "/" + it.key(), it.value(),
                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                      QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    for (const auto &s : config.servers)
      if (s.protocol == Protocol::Smb && serverHasEnabledShares(config, s.id)) {
        auto c = credentials.value(s.id).toObject();
        auto user = c["username"].toString(),
             password = c["password"].toString(),
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
    QJsonArray manifest;
    for (const auto &g : generated)
      manifest.append(QJsonObject{{"share_id", g.shareId},
                                  {"local_path", g.localPath},
                                  {"source", g.source},
                                  {"type", g.type},
                                  {"mount_unit", g.mountUnit},
                                  {"automount_unit", g.automountUnit}});
    writeFile(manifestPath,
              QJsonDocument(QJsonObject{{"shares", manifest}})
                  .toJson(QJsonDocument::Indented),
              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // Persist ownership before activation: an offline server must not leave
    // successfully installed units absent from the control manifest.
    systemctl({"daemon-reload"});
  } catch (const std::exception &error) {
    const QString cause = QString::fromUtf8(error.what());
    transaction.rollback();
    systemctl({"daemon-reload"});
    fail(cause + "\nConfiguration files restored. Previously stopped shares "
                 "may need mounting again.");
  }
  for (const auto &name : ownedUnits)
    if (!name.isEmpty() && !newUnits.contains(name))
      systemctl({"disable", name}, true);
  QStringList failures;
  for (const auto &g : generated) {
    try {
      systemctl({"disable", g.mountUnit});
      if (!g.automountUnit.isEmpty())
        systemctl({"disable", g.automountUnit});
      if (!g.enableUnit.isEmpty())
        systemctl({"enable", "--now", g.enableUnit});
    } catch (const std::exception &error) {
      failures << QString::fromUtf8(error.what());
    }
  }
  if (!failures.isEmpty())
    fail("Configuration installed; some shares could not activate. Retry after "
         "checking the server.\n" +
         failures.join('\n'));
}

void control() {
  auto request = parseInput();
  auto action = request["action"].toString();
  if (action != "mount" && action != "unmount")
    fail("Invalid action");
  QHash<QString, QJsonObject> managed;
  for (const auto &v : loadManaged()) {
    auto o = v.toObject();
    managed[o["share_id"].toString()] = o;
  }
  auto ids = request["share_ids"].toArray();
  if (ids.isEmpty())
    fail("No shares requested");
  for (const auto &v : ids) {
    auto id = v.toString();
    if (!managed.contains(id))
      fail("Share is not in the root-owned manifest");
    verifyMount(managed[id]);
    if (action == "unmount" &&
        !managed[id]["automount_unit"].toString().isEmpty())
      systemctl({"stop", managed[id]["automount_unit"].toString()});
    verifyMount(managed[id]);
    systemctl({action == "mount" ? "start" : "stop",
               managed[id]["mount_unit"].toString()});
  }
}
} // namespace

#ifndef TETHER_HELPER_TEST
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (geteuid() != 0) {
    fprintf(stderr, "Tether helper must run as root\n");
    return 1;
  }
  QLockFile lock("/run/tether.lock");
  if (!lock.tryLock(0)) {
    fprintf(stderr, "Another Tether operation is running\n");
    return 1;
  }
  try {
#ifdef TETHER_APPLY_HELPER
    apply();
#else
    control();
#endif
    fprintf(stdout, "{\"ok\":true}\n");
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "Tether helper: %s\n", e.what());
    return 1;
  }
}
#endif
