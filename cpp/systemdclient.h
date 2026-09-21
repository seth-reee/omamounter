#pragma once
#include "config.h"
#include <QObject>
#include <QProcess>

QString systemdEscapePath(const QString &path);
QString previewUnits(const AppConfig &config);

class HelperClient : public QObject {
  Q_OBJECT
public:
  explicit HelperClient(QObject *parent = nullptr);
  void apply(const AppConfig &config, const QHash<QString, QString> &passwords);
  void control(const QString &action, const QStringList &shareIds);
signals:
  void finished(const QString &message);
  void failed(const QString &message);

private:
  void start(const QString &program, const QJsonObject &request);
  QProcess *m_process = nullptr;
};
