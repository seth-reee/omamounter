#pragma once
#include "config.h"
#include "systemdclient.h"
#include <QMainWindow>
class QLabel;
class QTableWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppConfig config, ConfigStore store);
private slots:
  void refresh();
  void settings();
  void control(const QString &, bool);

private:
  AppConfig m_config;
  ConfigStore m_store;
  QTableWidget *m_table;
  QLabel *m_summary;
  HelperClient m_helper;
};
