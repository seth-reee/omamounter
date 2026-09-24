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
  QMap<QString, QString> c{{"background", "#121212"},
                           {"lighter_background", "#1e1e1e"},
                           {"dark_background", "#121212"},
                           {"foreground", "#bebebe"},
                           {"light_foreground", "#8a8a8d"},
                           {"dark_foreground", "#555555"},
                           {"accent", "#e68e0d"},
                           {"selection", "#333333"},
                           {"muted", "#333333"}};
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
  const QColor bg(c["background"]), panel(c["lighter_background"]),
      fg(c["foreground"]), accent(c["accent"]), selection(c["selection"]),
      muted(c["muted"]), placeholder(c["light_foreground"]);
  p.setColor(QPalette::Window, bg);
  p.setColor(QPalette::WindowText, fg);
  p.setColor(QPalette::Base, panel);
  p.setColor(QPalette::AlternateBase, bg);
  p.setColor(QPalette::Text, QColor(c["foreground"]));
  p.setColor(QPalette::Button, panel);
  p.setColor(QPalette::ButtonText, fg);
  p.setColor(QPalette::Highlight, selection);
  p.setColor(QPalette::HighlightedText, fg);
  p.setColor(QPalette::PlaceholderText, placeholder);
  p.setColor(QPalette::ToolTipBase, panel);
  p.setColor(QPalette::ToolTipText, fg);
  p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c["dark_foreground"]));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(c["dark_foreground"]));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c["dark_foreground"]));
  m_app->setPalette(p);
  m_app->setStyleSheet(
      QString("QWidget { font-size: 13px; } "
              "QPushButton, QLineEdit, QComboBox { padding: 7px 10px; border: 1px solid %1; border-radius: 6px; } "
              "QPushButton:hover { border-color: %2; } QPushButton:disabled { color: %3; } "
              "QTableWidget { border: 1px solid %1; border-radius: 6px; gridline-color: %1; } "
              "QHeaderView::section { background: %4; padding: 8px; border: none; border-bottom: 1px solid %1; } "
              "QTableWidget::item { padding: 5px; } QTableWidget::item:selected { background: %5; }")
          .arg(muted.name(), accent.name(), QColor(c["dark_foreground"]).name(),
               panel.name(), selection.name()));
  if (QFile::exists(m_path) && !m_watcher.files().contains(m_path))
    m_watcher.addPath(m_path);
}
