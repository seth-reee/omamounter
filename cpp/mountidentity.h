#pragma once
#include <QString>
#include <QStringList>
QString mountStatus(const QByteArray &mountinfo, const QString &target,
                    const QStringList &sources, const QString &type);

// Every filesystem at the target must match; stacked mounts fail closed.
bool mountIdentityMatches(const QByteArray &mountinfo, const QString &target,
                          const QString &source, const QString &type,
                          bool allowAutomount);
