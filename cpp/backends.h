#pragma once
#include "config.h"
#include <QStringList>

QString validateAddress(const QString &value);
QString validateAbsolutePath(const QString &value);
QString validateMountOptions(const QString &value);
QString resolveAddress(const Server &server, bool *fallback = nullptr);
QStringList nfsMountCommand(const Server &server, const Share &share,
                            const QString &address);
QStringList smbMountCommand(const Server &server, const Share &share,
                            const QString &address,
                            const QString &credentialPath);
QStringList parseNfsExports(const QByteArray &output);
QStringList parseSmbShares(const QByteArray &output);
QSet<QString> mountedPaths();
