#include "mainwindow.h"
#include "backends.h"
#include "settingsdialog.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(AppConfig c, ConfigStore s)
    : m_config(std::move(c)), m_store(std::move(s)), m_helper(this) {
  setWindowTitle("omamounter");
  resize(1000, 540);
  auto *w = new QWidget;
  auto *l = new QVBoxLayout(w);
  auto *t = new QLabel("omamounter");
  t->setObjectName("title");
  l->addWidget(t);
  l->addWidget(new QLabel("Network share manager"));
  m_table = new QTableWidget(0, 7);
  m_table->setHorizontalHeaderLabels({"", "Server", "Share", "Protocol",
                                      "Remote path", "Mount destination",
                                      "Status"});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_table);
  m_summary = new QLabel;
  l->addWidget(m_summary);
  auto *a = new QHBoxLayout;
  auto add = [&](QString n, auto f) {
    auto *b = new QPushButton(n);
    connect(b, &QPushButton::clicked, this, f);
    a->addWidget(b);
  };
  add("Mount All", [this] { control("mount", true); });
  add("Mount Selected", [this] { control("mount", false); });
  add("Unmount Selected", [this] { control("unmount", false); });
  add("Unmount All", [this] { control("unmount", true); });
  a->addStretch();
  add("Refresh", [this] { refresh(); });
  add("Settings", [this] { settings(); });
  l->addLayout(a);
  setCentralWidget(w);
  connect(&m_helper, &HelperClient::finished, this,
          [this](const QString &) { refresh(); });
  connect(&m_helper, &HelperClient::failed, this, [this](const QString &e) {
    QMessageBox::warning(this, "Operation failed", e);
  });
  refresh();
}
void MainWindow::refresh() {
  auto active = mountedPaths();
  m_table->setRowCount(0);
  int mounted = 0;
  for (const auto &h : m_config.shares) {
    auto it = std::find_if(m_config.servers.begin(), m_config.servers.end(),
                           [&](const Server &s) { return s.id == h.serverId; });
    if (it == m_config.servers.end())
      continue;
    int r = m_table->rowCount();
    m_table->insertRow(r);
    auto *c = new QCheckBox;
    c->setChecked(h.enabled);
    c->setProperty("shareId", h.id);
    m_table->setCellWidget(r, 0, c);
    QStringList v{it->name,
                  h.name,
                  protocolName(it->protocol).toUpper(),
                  h.remotePath,
                  h.localPath,
                  active.contains(h.localPath) ? "Mounted" : "Unmounted"};
    if (active.contains(h.localPath))
      mounted++;
    for (int i = 0; i < v.size(); ++i)
      m_table->setItem(r, i + 1, new QTableWidgetItem(v[i]));
  }
  m_summary->setText(QString("%1 configured shares · %2 mounted")
                         .arg(m_config.shares.size())
                         .arg(mounted));
}
void MainWindow::settings() {
  SettingsDialog d(m_config, this);
  if (d.exec() == QDialog::Accepted) {
    m_config = d.config();
    try {
      m_store.save(m_config);
    } catch (const std::exception &e) {
      QMessageBox::critical(this, "Save failed", e.what());
    }
    refresh();
  }
}
void MainWindow::control(const QString &a, bool all) {
  QStringList ids;
  for (int r = 0; r < m_table->rowCount(); ++r) {
    auto *c = qobject_cast<QCheckBox *>(m_table->cellWidget(r, 0));
    if (c && (all || c->isChecked()))
      ids << c->property("shareId").toString();
  }
  if (ids.isEmpty()) {
    QMessageBox::information(this, "No shares", "Select at least one share.");
    return;
  }
  m_helper.control(a, ids);
}
