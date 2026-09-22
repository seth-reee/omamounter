#include "mountidentity.h"
#include <QStringList>

static QString decode(QString value) {
  // Decode backslash last, so literal escape-looking names are not decoded
  // twice.
  return value.replace("\\040", " ")
      .replace("\\011", "\t")
      .replace("\\012", "\n")
      .replace("\\134", "\\");
}

QString mountStatus(const QByteArray &data, const QString &target,
                    const QStringList &sources, const QString &type) {
  bool mounted = false, waiting = false;
  for (const auto &line : QString::fromUtf8(data).split('\n')) {
    if (line.isEmpty())
      continue;
    const auto fields = line.split(' ');
    const int separator = fields.indexOf("-");
    if (separator < 6 || fields.size() <= separator + 2)
      return "Unknown";
    if (decode(fields[4]) != target)
      continue;
    const auto fs = fields[separator + 1],
               source = decode(fields[separator + 2]);
    if (fs == "autofs" && source == "systemd-1") {
      waiting = true;
      continue;
    }
    if (!(fs == type || (type == "nfs" && fs == "nfs4")) ||
        !sources.contains(source))
      return "Conflicting filesystem";
    mounted = true;
  }
  return mounted ? "Mounted" : waiting ? "Waiting for access" : "Unmounted";
}
bool mountIdentityMatches(const QByteArray &data, const QString &target,
                          const QString &source, const QString &type,
                          bool allowAutomount) {
  if (target.isEmpty() || source.isEmpty() || type.isEmpty())
    return false;
  for (const auto &line : QString::fromUtf8(data).split('\n')) {
    if (line.isEmpty())
      continue;
    const auto fields = line.split(' ');
    const int separator = fields.indexOf("-");
    if (fields.size() < 7 || separator < 6 || fields.size() <= separator + 2)
      return false;
    if (decode(fields[4]) != target)
      continue;
    const QString fs = fields[separator + 1];
    const QString origin = decode(fields[separator + 2]);
    if (allowAutomount && fs == "autofs" && origin == "systemd-1")
      continue;
    const bool compatible = fs == type || (type == "nfs" && fs == "nfs4");
    if (!compatible || origin != source)
      return false;
  }
  return true;
}
