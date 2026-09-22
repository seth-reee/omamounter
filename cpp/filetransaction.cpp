#include "filetransaction.h"
#include <QFileInfo>
#include <QSaveFile>
void FileTransaction::capture(const QString &path) {
  if (snapshots.contains(path))
    return;
  QFileInfo info(path);
  if (info.isSymLink() || (info.exists() && !info.isFile()))
    throw std::runtime_error(
        "Refusing a symlink or non-file transaction target");
  QFile file(path);
  if (info.exists() && !file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Could not back up configuration");
  snapshots.insert(path, {info.exists(),
                          info.exists() ? file.readAll() : QByteArray(),
                          info.permissions()});
}
void FileTransaction::rollback() {
  QStringList errors;
  for (auto it = snapshots.begin(); it != snapshots.end(); ++it) {
    if (!it->exists) {
      if (QFile::exists(it.key()) && !QFile::remove(it.key()))
        errors << it.key();
      continue;
    }
    QSaveFile file(it.key());
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(it->permissions) ||
        file.write(it->bytes) != it->bytes.size() || !file.commit())
      errors << it.key();
  }
  if (!errors.isEmpty())
    throw std::runtime_error(
        ("Recovery failed for: " + errors.join(", ")).toStdString());
}
