#include "config.h"
#include "mainwindow.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("Tether");
  app.setApplicationVersion(TETHER_VERSION);
  app.setDesktopFileName("tether");
  app.setWindowIcon(QIcon(":/tether.png"));
  ThemeManager theme(&app);
  try {
    ConfigStore store;
    MainWindow window(store.load(), store);
    window.show();
    return app.exec();
  } catch (const std::exception &e) {
    QMessageBox::critical(nullptr, "Tether", e.what());
    return 1;
  }
}
