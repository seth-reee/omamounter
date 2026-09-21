#pragma once
#include <QApplication>
#include <QFileSystemWatcher>
#include <QObject>

class ThemeManager : public QObject {
  Q_OBJECT
public:
  explicit ThemeManager(QApplication *app);
  void apply();

private:
  QApplication *m_app;
  QFileSystemWatcher m_watcher;
  QString m_path;
};
