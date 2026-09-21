#include "backends.h"
#include "config.h"
#include "systemdclient.h"
#include <QtTest>

class Tests : public QObject {
  Q_OBJECT
private slots:
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
