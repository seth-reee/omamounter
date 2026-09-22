#include "backends.h"
#include "config.h"
#include "filetransaction.h"
#include "mountidentity.h"
#include "settingsdialog.h"
#include "systemdclient.h"
#include <QCheckBox>
#include <QComboBox>
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
    SettingsDialog dialog(importedDefaults());
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
    auto c = importedDefaults();
    QCOMPARE(c.servers.size(), 1);
    QCOMPARE(c.shares.size(), 9);
    QCOMPARE(c.servers[0].hostname, QString("sakuya.weeb"));
    QCOMPARE(c.shares[0].remotePath, QString("/volume1/Usenet"));
  }
  void nfsCommand() {
    auto c = importedDefaults();
    QCOMPARE(nfsMountCommand(c.servers[0], c.shares[1], "sakuya.weeb"),
             QStringList({"mount", "-t", "nfs", "sakuya.weeb:/volume1/Anime",
                          "/home/seth/Mount/Anime", "-o", "nfsvers=4"}));
  }
  void parsers() {
    QCOMPARE(parseNfsExports("Export list for host:\n/volume1/Anime *\n"),
             QStringList{"/volume1/Anime"});
    QCOMPARE(parseSmbShares("Disk|Media|Films\nIPC|IPC$|\n"),
             QStringList{"Media"});
  }
  void escaping() {
    QCOMPARE(systemdEscapePath("/mnt/TV-Shows"), QString("mnt-TV\\x2dShows"));
  }
  void configRoundTrip() {
    auto c = importedDefaults();
    auto r = fromJson(toJson(c));
    QCOMPARE(r.shares.size(), 9);
    QCOMPARE(r.servers[0].fallbackIp, QString("192.168.100.11"));
  }
};
QTEST_MAIN(Tests)
#include "tests.moc"
