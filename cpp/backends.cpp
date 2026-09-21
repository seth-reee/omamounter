#include "backends.h"
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QRegularExpression>
#include <stdexcept>

static void fail(const QString &message) {
  throw std::invalid_argument(message.toStdString());
}
QString validateAddress(const QString &v) {
  auto s = v.trimmed();
  static QRegularExpression h("^(?=.{1,253}$)[A-Za-z0-9][A-Za-z0-9.:-]*$");
  if (s.isEmpty() || !h.match(s).hasMatch())
    fail("Invalid server address");
  return s;
}
QString validateAbsolutePath(const QString &v) {
  if (!v.startsWith('/') || v.contains(QChar('\0')) ||
      v.split('/').contains(".."))
    fail("Invalid absolute path");
  return QDir::cleanPath(v);
}
QString validateMountOptions(const QString &v) {
  auto s = v.trimmed();
  if (s.contains(QRegularExpression("[\\s\\x00]")) ||
      s.toLower().contains("password="))
    fail("Invalid mount options");
  return s;
}
QString resolveAddress(const Server &server, bool *fallback) {
  validateAddress(server.hostname);
  QHostAddress address;
  if (address.setAddress(server.hostname)) {
    if (fallback)
      *fallback = false;
    return server.hostname;
  }
  if (fallback)
    *fallback = false;
  return server.hostname;
}
QStringList nfsMountCommand(const Server &s, const Share &h, const QString &a) {
  auto options = validateMountOptions(
      h.mountOptions.isEmpty() ? s.mountOptions : h.mountOptions);
  if (options.isEmpty())
    options = "nfsvers=" + s.nfsVersion;
  return {"mount",
          "-t",
          "nfs",
          validateAddress(a) + ":" + validateAbsolutePath(h.remotePath),
          validateAbsolutePath(h.localPath),
          "-o",
          options};
}
QStringList smbMountCommand(const Server &s, const Share &h, const QString &a,
                            const QString &credential) {
  if (h.remotePath.isEmpty() || h.remotePath.contains('/') ||
      h.remotePath.contains('\\'))
    fail("Invalid SMB share");
  QStringList options{"credentials=" + credential};
  if (s.smbVersion != "default")
    options << "vers=" + QString(s.smbVersion).remove("SMB");
  auto extra = validateMountOptions(h.mountOptions.isEmpty() ? s.mountOptions
                                                             : h.mountOptions);
  if (!extra.isEmpty())
    options << extra;
  return {"mount",
          "-t",
          "cifs",
          "//" + validateAddress(a) + "/" + h.remotePath,
          validateAbsolutePath(h.localPath),
          "-o",
          options.join(',')};
}
QStringList parseNfsExports(const QByteArray &o) {
  QStringList result;
  for (auto line : QString::fromUtf8(o).split('\n')) {
    line = line.trimmed();
    if (line.startsWith('/'))
      result << line.section(QRegularExpression("\\s+"), 0, 0);
  }
  result.removeDuplicates();
  return result;
}
QStringList parseSmbShares(const QByteArray &o) {
  QStringList result;
  for (const auto &line : QString::fromUtf8(o).split('\n')) {
    auto f = line.split('|');
    if (f.size() > 1 && f[0].compare("Disk", Qt::CaseInsensitive) == 0)
      result << f[1];
  }
  result.removeDuplicates();
  return result;
}
QSet<QString> mountedPaths() {
  QSet<QString> result;
  QFile f("/proc/self/mountinfo");
  if (f.open(QIODevice::ReadOnly))
    for (auto line : QString::fromUtf8(f.readAll()).split('\n')) {
      auto p = line.split(' ');
      if (p.size() > 4)
        result.insert(QString(p[4]).replace("\\040", " "));
    }
  return result;
}
