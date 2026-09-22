#include "backends.h"
#include "config.h"
#include "fixtures.h"
#include "mainwindow.h"
#include "filetransaction.h"
#include "mountidentity.h"
#include "settingsdialog.h"
#include "systemdclient.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest>

class Tests : public QObject {
  Q_OBJECT
private slots:
  void failedWriteRecovery() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto original = dir.filePath("unit.mount"),
               created = dir.filePath("new.mount");
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original");
    file.close();
    FileTransaction transaction;
    transaction.capture(original);
    transaction.capture(created);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("changed");
    file.close();
    QFile added(created);
    QVERIFY(added.open(QIODevice::WriteOnly));
    added.write("new");
    added.close();
    transaction.rollback();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("original"));
    QVERIFY(!QFile::exists(created));
  }
  void statusDistinguishesAutomounts() {
    const QByteArray waiting =
        "31 24 0:30 / /mnt/media rw - autofs systemd-1 rw\n";
    QCOMPARE(mountStatus(waiting, "/mnt/media", {"nas:/media"}, "nfs"),
             QString("Waiting for access"));
    QCOMPARE(mountStatus(
                 waiting + "32 31 0:31 / /mnt/media rw - nfs4 nas:/media rw\n",
                 "/mnt/media", {"nas:/media"}, "nfs"),
             QString("Mounted"));
    QCOMPARE(mountStatus("32 24 0:31 / /mnt/media rw - ext4 /dev/sdb rw\n",
                         "/mnt/media", {"nas:/media"}, "nfs"),
             QString("Conflicting filesystem"));
    QCOMPARE(mountStatus({}, "/mnt/media", {"nas:/media"}, "nfs"),
             QString("Unmounted"));
  }
  void settingsControlsPersist() {
    SettingsDialog dialog(sampleConfig());
    auto *table = dialog.findChild<QTableWidget *>();
    QVERIFY(table);
    auto *mode = qobject_cast<QComboBox *>(table->cellWidget(0, 4));
    auto *enabled = qobject_cast<QCheckBox *>(table->cellWidget(0, 5));
    QVERIFY(mode);
    QVERIFY(enabled);
    QCOMPARE(mode->currentText(), QString("Manual"));
    mode->setCurrentIndex(mode->findData("access"));
    enabled->setChecked(false);
    table->item(0, 3)->setText("/mnt/test-share");
    bool collected = false;
    QVERIFY(QMetaObject::invokeMethod(&dialog, "collectSettings",
                                      Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, collected)));
    QVERIFY(collected);
    QCOMPARE(dialog.config().shares[0].automount, AutomountMode::Access);
    QVERIFY(!dialog.config().shares[0].enabled);
    QCOMPARE(dialog.config().shares[0].localPath, QString("/mnt/test-share"));
  }
  void mountIdentity() {
    const QByteArray mounted =
        "31 24 0:30 / /mnt/media rw - nfs4 nas:/media rw\n";
    QVERIFY(mountIdentityMatches(mounted, "/mnt/media", "nas:/media", "nfs",
                                 false));
    QVERIFY(!mountIdentityMatches(mounted, "/mnt/media", "other:/media", "nfs",
                                  false));
    QVERIFY(!mountIdentityMatches(mounted, "/mnt/media", "nas:/media", "cifs",
                                  false));
    QVERIFY(!mountIdentityMatches(
        mounted + "32 24 0:31 / /mnt/media rw - ext4 /dev/sdb rw\n",
        "/mnt/media", "nas:/media", "nfs", false));
    QVERIFY(!mountIdentityMatches("malformed\n", "/mnt/media", "nas:/media",
                                  "nfs", false));
    QVERIFY(!mountIdentityMatches(mounted, "", "", "", false));
  }
  void automountIdentity() {
    const QByteArray mounted =
        "31 24 0:30 / /mnt/media rw - autofs systemd-1 rw\n";
    QVERIFY(
        mountIdentityMatches(mounted, "/mnt/media", "nas:/media", "nfs", true));
    QVERIFY(!mountIdentityMatches(mounted, "/mnt/media", "nas:/media", "nfs",
                                  false));
    QVERIFY(mountIdentityMatches(
        "31 24 0:30 / /mnt/My\\040Share rw - cifs //nas/My\\040Share rw\n",
        "/mnt/My Share", "//nas/My Share", "cifs", false));
  }
  void defaults() {
    QTemporaryDir dir;
    ConfigStore store(dir.filePath("config.json"));
    auto c = store.load();
    QVERIFY(c.servers.isEmpty());
    QVERIFY(c.shares.isEmpty());
    QCOMPARE(c.mountRoot, QDir::homePath() + "/Mount");
    QVERIFY(!QFile::exists(store.path()));
    store.save(sampleConfig());
    QCOMPARE(toJson(store.load()), toJson(sampleConfig()));
    store.save(defaultConfig());
    QVERIFY(store.load().servers.isEmpty());
  }
  void firstRunPrompt() {
    QTemporaryDir dir;
    MainWindow window(defaultConfig(), ConfigStore(dir.filePath("config.json")));
    QVERIFY(!window.findChild<QWidget *>("welcome")->isHidden());
    QVERIFY(window.findChild<QPushButton *>("addServer"));
    SettingsDialog dialog(defaultConfig());
    dialog.beginAddServer();
    QCOMPARE(dialog.findChild<QTabWidget *>()->currentIndex(), 1);
    QCOMPARE(dialog.config().servers.size(), 1);
    QVERIFY(dialog.config().servers[0].hostname.isEmpty());
    QVERIFY(dialog.config().shares.isEmpty());
    dialog.reject();
    QVERIFY(!QFile::exists(dir.filePath("config.json")));
    MainWindow configured(sampleConfig(), ConfigStore(dir.filePath("config.json")));
    QVERIFY(configured.findChild<QWidget *>("welcome")->isHidden());
  }
  void nfsCommand() {
    auto c = sampleConfig();
    QCOMPARE(nfsMountCommand(c.servers[0], c.shares[0], "nas.example"),
             QStringList({"mount", "-t", "nfs", "nas.example:/exports/media",
                          "/mnt/test/Media", "-o", "nfsvers=4"}));
  }
  void parsers() {
    QCOMPARE(parseNfsExports("Export list for host:\n/exports/media *\n"),
             QStringList{"/exports/media"});
    QCOMPARE(parseSmbShares("Disk|Media|Films\nIPC|IPC$|\n"),
             QStringList{"Media"});
  }
  void escaping() {
    QCOMPARE(systemdEscapePath("/mnt/TV-Shows"), QString("mnt-TV\\x2dShows"));
  }
  void configRoundTrip() {
    auto c = sampleConfig();
    auto r = fromJson(toJson(c));
    QCOMPARE(r.shares.size(), 1);
    QCOMPARE(r.servers[0].fallbackIp, QString("192.0.2.10"));
  }
};
QTEST_MAIN(Tests)
#include "tests.moc"
