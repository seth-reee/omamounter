#include "theme.h"
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QRegularExpression>

ThemeManager::ThemeManager(QApplication *app)
    : QObject(app), m_app(app),
      m_path(QDir::homePath() +
             "/.local/state/omarchy/current/theme/colors.toml") {
  m_watcher.addPath(QFileInfo(m_path).absolutePath());
  if (QFile::exists(m_path))
    m_watcher.addPath(m_path);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
          &ThemeManager::apply);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
          &ThemeManager::apply);
  apply();
}
void ThemeManager::apply() {
  QMap<QString, QString> c{{"background", "#1e1e2e"},
                           {"foreground", "#cdd6f4"},
                           {"accent", "#89b4fa"},
                           {"selection", "#45475a"},
                           {"muted", "#585b70"}};
  QFile f(m_path);
  if (f.open(QIODevice::ReadOnly)) {
    QRegularExpression re("^([a-z_]+)\\s*=\\s*\"(#[0-9A-Fa-f]{6,8})\"");
    for (const auto &line : QString::fromUtf8(f.readAll()).split('\n')) {
      auto m = re.match(line.trimmed());
      if (m.hasMatch())
        c[m.captured(1)] = m.captured(2);
    }
  }
  QPalette p;
  p.setColor(QPalette::Window, QColor(c["background"]));
  p.setColor(QPalette::WindowText, QColor(c["foreground"]));
  p.setColor(QPalette::Base, QColor(c["background"]));
  p.setColor(QPalette::Text, QColor(c["foreground"]));
  p.setColor(QPalette::Link, QColor(c["foreground"]));
  p.setColor(QPalette::LinkVisited, QColor(c["foreground"]));
  p.setColor(QPalette::Button, QColor(c["muted"]));
  p.setColor(QPalette::ButtonText, QColor(c["foreground"]));
  p.setColor(QPalette::Highlight, QColor(c["accent"]));
  p.setColor(QPalette::HighlightedText, QColor(c["background"]));
  m_app->setPalette(p);
  m_app->setStyleSheet(
      QString("QHeaderView::section{background:%1;color:%2} "
              "QLabel#title{color:%3;font-size:24px;font-weight:600}")
          .arg(c["muted"], c["foreground"], c["accent"]));
  if (QFile::exists(m_path) && !m_watcher.files().contains(m_path))
    m_watcher.addPath(m_path);
}
