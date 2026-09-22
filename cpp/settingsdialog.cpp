#include "settingsdialog.h"
#include "backends.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <memory>

SettingsDialog::SettingsDialog(const AppConfig &c, QWidget *p)
    : QDialog(p), m_config(c), m_helper(this) {
  setWindowTitle("omamounter Settings");
  resize(900, 600);
  auto *outer = new QVBoxLayout(this);
  auto *tabs = new QTabWidget;
  outer->addWidget(tabs);
  auto *g = new QWidget;
  auto *gf = new QFormLayout(g);
  m_root = new QLineEdit(c.mountRoot);
  gf->addRow("Default mount root", m_root);
  auto *review = new QPushButton("Advanced: preview system configuration");
  connect(review, &QPushButton::clicked, this, &SettingsDialog::preview);
  gf->addRow("System integration", review);
  tabs->addTab(g, "General");
  auto *sPage = new QWidget;
  auto *sl = new QHBoxLayout(sPage);
  m_servers = new QListWidget;
  connect(m_servers, &QListWidget::currentRowChanged, this,
          &SettingsDialog::loadServer);
  sl->addWidget(m_servers, 1);
  auto *formWidget = new QWidget;
  auto *f = new QFormLayout(formWidget);
  m_name = new QLineEdit;
  m_host = new QLineEdit;
  m_fallback = new QLineEdit;
  m_protocol = new QComboBox;
  m_protocol->addItems({"NFS", "SMB"});
  m_nfs = new QLineEdit;
  m_user = new QLineEdit;
  m_domain = new QLineEdit;
  m_smbver = new QLineEdit;
  m_options = new QLineEdit;
  m_password = new QLineEdit;
  m_password->setEchoMode(QLineEdit::Password);
  for (auto row :
       QList<QPair<QString, QWidget *>>{{"Name", m_name},
                                        {"Hostname / IP", m_host},
                                        {"Fallback IP", m_fallback},
                                        {"Protocol", m_protocol},
                                        {"NFS version", m_nfs},
                                        {"SMB username", m_user},
                                        {"SMB domain", m_domain},
                                        {"SMB version", m_smbver},
                                        {"Mount options", m_options},
                                        {"SMB password", m_password}})
    f->addRow(row.first, row.second);
  auto *sb = new QHBoxLayout;
  for (auto pair : QList<QPair<QString, void (SettingsDialog::*)()>>{
           {"Add", &SettingsDialog::addServer},
           {"Remove", &SettingsDialog::removeServer},
           {"Test", &SettingsDialog::testConnection},
           {"Discover", &SettingsDialog::discover}}) {
    auto *b = new QPushButton(pair.first);
    connect(b, &QPushButton::clicked, this, pair.second);
    sb->addWidget(b);
  }
  f->addRow(sb);
  sl->addWidget(formWidget, 2);
  tabs->addTab(sPage, "Servers");
  auto *hPage = new QWidget;
  auto *hl = new QVBoxLayout(hPage);
  m_shares = new QTableWidget(0, 6);
  m_shares->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_shares->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_shares->setHorizontalHeaderLabels(
      {"Server", "Name", "Remote path", "Local path", "Automount", "Enabled"});
  hl->addWidget(m_shares);
  auto *hb = new QHBoxLayout;
  auto *add = new QPushButton("Add manually");
  auto *remove = new QPushButton("Remove selected");
  connect(add, &QPushButton::clicked, this, &SettingsDialog::addShare);
  connect(remove, &QPushButton::clicked, this, &SettingsDialog::removeShare);
  hb->addWidget(add);
  hb->addWidget(remove);
  hb->addStretch();
  hl->addLayout(hb);
  tabs->addTab(hPage, "Shares");
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Save)->setText("Save & Apply");
  auto *about = buttons->addButton("About", QDialogButtonBox::HelpRole);
  about->setObjectName("aboutButton");
  connect(about, &QPushButton::clicked, this, [this] {
    auto *popup = new QMessageBox(this);
    popup->setObjectName("aboutDialog");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setWindowTitle("About omamounter");
    popup->setTextFormat(Qt::RichText);
    popup->setTextInteractionFlags(Qt::TextBrowserInteraction);
    popup->setText(
        "<h3>omamounter</h3>"
        "<p>A lightweight desktop manager for NFS and SMB network shares "
        "on Omarchy Linux.</p>"
        "<p>Created by <b>Seth_Reee</b><br>"
        "<a href=\"https://github.com/seth-reee\">GitHub: seth-reee</a><br>"
        "<a href=\"https://github.com/seth-reee/omamounter\">Project repository</a></p>"
        "<p>Licensed under the MIT License.</p>");
    // Use the application's icon automatically once one is supplied.
    if (!windowIcon().isNull())
      popup->setIconPixmap(windowIcon().pixmap(64, 64));
    popup->setStandardButtons(QMessageBox::Ok);
    popup->open();
  });
  connect(buttons, &QDialogButtonBox::accepted, this,
          &SettingsDialog::saveAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  outer->addWidget(buttons);
  connect(&m_helper, &HelperClient::finished, this, [this](const QString &m) {
    if (m_acceptAfterApply) {
      m_acceptAfterApply = false;
      accept();
      return;
    }
    QMessageBox::information(this, "Applied", m);
  });
  connect(&m_helper, &HelperClient::failed, this, [this](const QString &m) {
    m_acceptAfterApply = false;
    QMessageBox::warning(this, "Apply failed", m);
  });
  rebuildServers();
  rebuildShares();
}
void SettingsDialog::rebuildServers() {
  QSignalBlocker blocker(m_servers);
  m_loadedServer = -1;
  int row = m_servers->currentRow();
  m_servers->clear();
  for (const auto &s : m_config.servers)
    m_servers->addItem(s.name + " (" + protocolName(s.protocol).toUpper() +
                       ")");
  if (!m_config.servers.isEmpty())
    m_servers->setCurrentRow(qBound(0, row, m_config.servers.size() - 1));
  loadServer(m_servers->currentRow());
}
void SettingsDialog::loadServer(int row) {
  if (row < 0 || row >= m_config.servers.size())
    return;
  if (m_loadedServer >= 0 && m_loadedServer != row && !applyServer()) {
    QSignalBlocker blocker(m_servers);
    m_servers->setCurrentRow(m_loadedServer);
    return;
  }
  m_loadedServer = row;
  const auto &s = m_config.servers[row];
  m_name->setText(s.name);
  m_host->setText(s.hostname);
  m_fallback->setText(s.fallbackIp);
  m_protocol->setCurrentText(protocolName(s.protocol).toUpper());
  m_nfs->setText(s.nfsVersion);
  m_user->setText(s.smbUsername);
  m_domain->setText(s.smbDomain);
  m_smbver->setText(s.smbVersion);
  m_options->setText(s.mountOptions);
  m_password->clear();
  m_password->setPlaceholderText("Leave blank to keep saved password");
  if (m_passwords.contains(s.id))
    m_password->setText(m_passwords.value(s.id));
  m_password->setModified(false);
}
bool SettingsDialog::applyServer() {
  int row = m_loadedServer;
  if (row < 0)
    return true;
  try {
    auto s = m_config.servers[row];
    s.name = m_name->text().trimmed();
    s.hostname = validateAddress(m_host->text());
    s.fallbackIp = m_fallback->text().trimmed();
    if (!s.fallbackIp.isEmpty())
      validateAddress(s.fallbackIp);
    s.protocol =
        m_protocol->currentText() == "SMB" ? Protocol::Smb : Protocol::Nfs;
    s.nfsVersion = m_nfs->text().trimmed();
    s.smbUsername = m_user->text().trimmed();
    s.smbDomain = m_domain->text().trimmed();
    s.smbVersion = m_smbver->text().trimmed();
    s.mountOptions = validateMountOptions(m_options->text());
    if (s.protocol == Protocol::Smb && m_password->isModified()) {
      m_passwords[s.id] = m_password->text();
      m_passwordEdits.insert(s.id);
      m_password->setModified(false);
    }
    m_config.servers[row] = s;
    return true;
  } catch (const std::exception &e) {
    QMessageBox::warning(this, "Invalid server", e.what());
    return false;
  }
}
void SettingsDialog::addServer() {
  if (!collectSettings())
    return;
  m_config.servers.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                           "New server", "", "", Protocol::Nfs});
  rebuildServers();
  m_servers->setCurrentRow(m_config.servers.size() - 1);
  m_host->setPlaceholderText("Hostname or IP address");
  m_host->setFocus();
}
void SettingsDialog::beginAddServer() {
  findChild<QTabWidget *>()->setCurrentIndex(1);
  addServer();
}
void SettingsDialog::removeServer() {
  if (!collectSettings())
    return;
  int row = m_servers->currentRow();
  if (row < 0)
    return;
  auto id = m_config.servers[row].id;
  m_config.servers.removeAt(row);
  m_config.shares.removeIf([&](const Share &s) { return s.serverId == id; });
  rebuildServers();
  rebuildShares();
}
void SettingsDialog::rebuildShares() {
  m_shares->setRowCount(0);
  for (const auto &h : m_config.shares) {
    int r = m_shares->rowCount();
    m_shares->insertRow(r);
    auto it = std::find_if(m_config.servers.begin(), m_config.servers.end(),
                           [&](const Server &s) { return s.id == h.serverId; });
    QStringList v{it == m_config.servers.end() ? "Missing" : it->name,
                  h.name,
                  h.remotePath,
                  h.localPath,
                  automountName(h.automount),
                  h.enabled ? "yes" : "no"};
    for (int i = 0; i < v.size(); ++i)
      m_shares->setItem(r, i, new QTableWidgetItem(v[i]));
    m_shares->item(r, 0)->setFlags(m_shares->item(r, 0)->flags() &
                                   ~Qt::ItemIsEditable);
    auto *mode = new QComboBox;
    mode->addItem("Manual", "disabled");
    mode->addItem("At boot", "boot");
    mode->addItem("On first access", "access");
    mode->setCurrentIndex(mode->findData(automountName(h.automount)));
    m_shares->setCellWidget(r, 4, mode);
    auto *enabled = new QCheckBox;
    enabled->setChecked(h.enabled);
    m_shares->setCellWidget(r, 5, enabled);
  }
}
void SettingsDialog::addShare() {
  if (!collectSettings())
    return;
  if (m_config.servers.isEmpty())
    return;
  bool ok = false;
  auto name = QInputDialog::getText(this, "Add share", "Share name",
                                    QLineEdit::Normal, "", &ok);
  if (!ok || name.isEmpty())
    return;
  int row = qMax(0, m_servers->currentRow());
  auto &s = m_config.servers[row];
  auto remote = QInputDialog::getText(
      this, "Add share", "Remote path / SMB share", QLineEdit::Normal,
      s.protocol == Protocol::Nfs ? "/" : name, &ok);
  if (ok)
    m_config.shares.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                            s.id, name, remote, m_root->text() + "/" + name, "",
                            AutomountMode::Disabled, true});
  rebuildShares();
}
void SettingsDialog::removeShare() {
  auto rows = m_shares->selectionModel()->selectedRows();
  if (rows.isEmpty())
    return;
  // Discard selected rows before validating; invalid entries must also be
  // removable. Descending order preserves the remaining row-to-share mapping.
  std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) {
    return a.row() > b.row();
  });
  for (const auto &row : rows) {
    m_config.shares.removeAt(row.row());
    m_shares->removeRow(row.row());
  }
}
QString SettingsDialog::lookupPassword(const QString &id) {
  QProcess p;
  p.start("/usr/bin/secret-tool",
          {"lookup", "application", "omamounter", "server", id});
  if (!p.waitForFinished(3000) || p.exitCode() != 0)
    return {};
  auto bytes = p.readAllStandardOutput();
  if (bytes.endsWith('\n'))
    bytes.chop(1);
  return QString::fromUtf8(bytes);
}
bool SettingsDialog::storePassword(const QString &id, const QString &password) {
  QProcess p;
  p.start("/usr/bin/secret-tool", {"store", "--label=omamounter SMB password",
                                   "application", "omamounter", "server", id});
  if (!p.waitForStarted(2000))
    return false;
  p.write(password.toUtf8());
  p.closeWriteChannel();
  return p.waitForFinished(5000) && p.exitCode() == 0;
}
void SettingsDialog::preview() {
  if (!collectSettings())
    return;
  QDialog d(this);
  d.setWindowTitle("System configuration preview");
  d.resize(760, 520);
  auto *l = new QVBoxLayout(&d);
  auto *text = new QPlainTextEdit(previewUnits(m_config));
  text->setReadOnly(true);
  l->addWidget(text);
  auto *b =
      new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
  connect(b->button(QDialogButtonBox::Apply), &QPushButton::clicked, &d,
          [this] { applySystemd(); });
  connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  l->addWidget(b);
  d.exec();
}
void SettingsDialog::applySystemd() {
  if (!collectSettings())
    return;
  withPasswords([this] { m_helper.apply(m_config, m_passwords); }, true);
}
void SettingsDialog::saveAndAccept() {
  if (!collectSettings())
    return;
  withPasswords(
      [this] {
        // Save the desired state before invoking system integration, so a
        // failed activation can be retried without losing share IDs and edited
        // settings.
        try {
          ConfigStore().save(m_config);
        } catch (const std::exception &error) {
          QMessageBox::warning(this, "Save failed", error.what());
          return;
        }
        m_acceptAfterApply = true;
        m_helper.apply(m_config, m_passwords);
      },
      true);
}

void SettingsDialog::withPasswords(std::function<void()> next, bool persist) {
  if (m_secretBusy)
    return;
  QStringList ids;
  for (const auto &server : m_config.servers)
    if (server.protocol == Protocol::Smb &&
        (!persist || serverHasEnabledShares(m_config, server.id)))
      ids << server.id;
  if (ids.isEmpty()) {
    next();
    return;
  }
  m_secretBusy = true;
  setEnabled(false);
  using Result = QPair<QHash<QString, QString>, QString>;
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, next, persist, ids] {
            const auto result = watcher->result();
            watcher->deleteLater();
            m_secretBusy = false;
            setEnabled(true);
            if (!result.second.isEmpty()) {
              QMessageBox::warning(this, "Password storage", result.second);
              return;
            }
            m_passwords = result.first;
            if (persist)
              for (const auto &id : ids)
                m_passwordEdits.remove(id);
            next();
          });
  watcher->setFuture(QtConcurrent::run([ids, cached = m_passwords,
                                        edits = m_passwordEdits,
                                        persist]() mutable -> Result {
    for (const auto &id : ids) {
      if (!cached.contains(id))
        cached[id] = lookupPassword(id);
      if (persist && edits.contains(id) && !storePassword(id, cached.value(id)))
        return {cached, "Secret Service could not store the SMB password. "
                        "Unlock your keyring and try again."};
    }
    return {cached, {}};
  }));
}
bool SettingsDialog::collectSettings() {
  if (!applyServer())
    return false;
  auto next = m_config;
  next.mountRoot = m_root->text().trimmed();
  try {
    validateAbsolutePath(next.mountRoot);
    for (int r = 0; r < m_shares->rowCount() && r < m_config.shares.size();
         ++r) {
      auto &h = next.shares[r];
      h.name = m_shares->item(r, 1)->text();
      h.remotePath = m_shares->item(r, 2)->text();
      h.localPath = validateAbsolutePath(m_shares->item(r, 3)->text());
      auto mode = qobject_cast<QComboBox *>(m_shares->cellWidget(r, 4))
                      ->currentData()
                      .toString();
      if (mode != "boot" && mode != "access" && mode != "disabled")
        throw std::invalid_argument(
            "Mount mode must be disabled, boot, or access.");
      h.automount = mode == "boot"     ? AutomountMode::Boot
                    : mode == "access" ? AutomountMode::Access
                                       : AutomountMode::Disabled;
      h.enabled =
          qobject_cast<QCheckBox *>(m_shares->cellWidget(r, 5))->isChecked();
    }
    m_config = next;
    return true;
  } catch (const std::exception &e) {
    QMessageBox::warning(this, "Invalid settings", e.what());
    return false;
  }
}

void SettingsDialog::testConnection() {
  if (!applyServer())
    return;
  int row = m_servers->currentRow();
  if (row < 0)
    return;
  Server server = m_config.servers[row];
  QHostInfo::lookupHost(
      server.hostname, this, [this, server](const QHostInfo &info) {
        QString address =
            info.addresses().isEmpty() ? server.fallbackIp : server.hostname;
        if (address.isEmpty()) {
          QMessageBox::warning(
              this, "Connection failed",
              "Hostname did not resolve and no fallback IP is configured.");
          return;
        }
        auto *socket = new QTcpSocket(this);
        auto done = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::connected, this,
                [this, socket, done, address] {
                  if (*done)
                    return;
                  *done = true;
                  QMessageBox::information(this, "Connection",
                                           "Connected to " + address + ".");
                  socket->deleteLater();
                });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this, socket, done](QAbstractSocket::SocketError) {
                  if (*done)
                    return;
                  *done = true;
                  QMessageBox::warning(this, "Connection failed",
                                       socket->errorString());
                  socket->deleteLater();
                });
        QTimer::singleShot(4000, this, [this, socket, done] {
          if (!*done) {
            *done = true;
            socket->abort();
            QMessageBox::warning(this, "Connection failed",
                                 "Connection timed out.");
            socket->deleteLater();
          }
        });
        socket->connectToHost(address,
                              server.protocol == Protocol::Nfs ? 2049 : 445);
      });
}

void SettingsDialog::discover() {
  if (!collectSettings())
    return;
  int row = m_servers->currentRow();
  if (row < 0)
    return;
  Server server = m_config.servers[row];
  QHostInfo::lookupHost(
      server.hostname, this, [this, server](const QHostInfo &info) {
        QString address =
            info.addresses().isEmpty() ? server.fallbackIp : server.hostname;
        if (address.isEmpty()) {
          QMessageBox::warning(
              this, "Discovery failed",
              "Hostname did not resolve and no fallback IP is configured.");
          return;
        }
        runDiscovery(server, address);
      });
}

void SettingsDialog::runDiscovery(const Server &server,
                                  const QString &address) {
  if (server.protocol == Protocol::Smb && !m_passwords.contains(server.id)) {
    withPasswords([this, server, address] { runDiscovery(server, address); },
                  false);
    return;
  }
  auto *process = new QProcess(this);
  QStringList arguments;
  QTemporaryFile *credentials = nullptr;
  if (server.protocol == Protocol::Nfs)
    arguments = {"--exports", address};
  else {
    auto password = m_passwords.value(server.id);
    if (password.isEmpty()) {
      QMessageBox::warning(this, "Discovery failed",
                           "Save an SMB password first.");
      process->deleteLater();
      return;
    }
    credentials = new QTemporaryFile(
        QDir::tempPath() + "/omamounter-XXXXXX.cred", process);
    credentials->setPermissions(QFileDevice::ReadOwner |
                                QFileDevice::WriteOwner);
    if (!credentials->open()) {
      QMessageBox::warning(this, "Discovery failed",
                           "Could not create private credentials file.");
      process->deleteLater();
      return;
    }
    credentials->write(
        ("username=" + server.smbUsername + "\npassword=" + password + "\n" +
         (server.smbDomain.isEmpty() ? QString()
                                     : "domain=" + server.smbDomain + "\n"))
            .toUtf8());
    credentials->flush();
    arguments = {"-L", address, "-g", "-A", credentials->fileName()};
    if (server.smbVersion != "default")
      arguments << "-m" << server.smbVersion;
  }
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this, process, server](int code, QProcess::ExitStatus) {
            if (code != 0) {
              QMessageBox::warning(
                  this, "Discovery failed",
                  QString::fromUtf8(process->readAllStandardError()) +
                      "\nYou can still add shares manually.");
              process->deleteLater();
              return;
            }
            auto found = server.protocol == Protocol::Nfs
                             ? parseNfsExports(process->readAllStandardOutput())
                             : parseSmbShares(process->readAllStandardOutput());
            if (found.isEmpty()) {
              QMessageBox::information(
                  this, "Discovery",
                  "No shares were enumerated. NFSv4 discovery is not always "
                  "available; manual entry remains supported.");
              process->deleteLater();
              return;
            }
            QDialog dialog(this);
            dialog.setWindowTitle("Select discovered shares");
            auto *layout = new QVBoxLayout(&dialog);
            auto *list = new QListWidget;
            for (const auto &remote : found) {
              auto *item = new QListWidgetItem(remote, list);
              item->setCheckState(Qt::Checked);
            }
            layout->addWidget(list);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                                 QDialogButtonBox::Cancel);
            connect(buttons, &QDialogButtonBox::accepted, &dialog,
                    &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog,
                    &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() == QDialog::Accepted)
              for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->checkState() == Qt::Checked) {
                  auto remote = list->item(i)->text(),
                       name = server.protocol == Protocol::Nfs
                                  ? QFileInfo(remote).fileName()
                                  : remote;
                  bool exists =
                      std::any_of(m_config.shares.begin(),
                                  m_config.shares.end(), [&](const Share &s) {
                                    return s.serverId == server.id &&
                                           s.remotePath == remote;
                                  });
                  if (!exists)
                    m_config.shares.append(
                        {QUuid::createUuid().toString(QUuid::WithoutBraces),
                         server.id, name, remote, m_root->text() + "/" + name,
                         "", AutomountMode::Disabled, true});
                }
            rebuildShares();
            process->deleteLater();
          });
  process->start(server.protocol == Protocol::Nfs ? "/usr/bin/showmount"
                                                  : "/usr/bin/smbclient",
                 arguments);
}
