#pragma once
#include <QFile>
#include <QMap>
#include <stdexcept>

// Snapshot all affected files before mutation. Caller explicitly handles
// rollback errors.
class FileTransaction {
  struct Snapshot {
    bool exists;
    QByteArray bytes;
    QFileDevice::Permissions permissions;
  };
  QMap<QString, Snapshot> snapshots;

public:
  void capture(const QString &path);
  void rollback();
};
