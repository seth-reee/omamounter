// Compile the actual helper implementation with process, DNS and mount-table
// boundaries replaced. This target is never installed or run as root.
#define OMAMOUNTER_HELPER_TEST
#include "helper.cpp"
#include <QtTest>
#include <QTemporaryDir>

class HelperIntegration : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> temp;
  AppConfig config;
  QString unit() const { return escapePath(config.shares[0].localPath) + ".mount"; }
  void requestApply() { testRequest = {{"config", toJson(config)}}; apply(); }
  QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
private slots:
  void init() {
    temp = std::make_unique<QTemporaryDir>(); QVERIFY(temp->isValid());
    systemdDir = temp->path() + "/units";
    stateDir = temp->path() + "/state";
    manifestPath = stateDir + "/managed.json";
    testMountinfo.clear(); testCommands.clear(); testFailure.clear();
    config = importedDefaults(); config.shares.resize(1);
    config.shares[0].localPath = temp->path() + "/mount";
  }
  void applyControlRemove() {
    requestApply(); QVERIFY(QFile::exists(systemdDir + "/" + unit()));
    QCOMPARE(loadManaged().size(), 1);
    testRequest = {{"action", "mount"}, {"share_ids", QJsonArray{config.shares[0].id}}};
    control(); QVERIFY(testCommands.contains("start " + unit()));
    testMountinfo = ("31 24 0:30 / " + config.shares[0].localPath + " rw - nfs4 sakuya.weeb:/volume1/Usenet rw\n").toUtf8();
    testRequest["action"] = "unmount"; control();
    QVERIFY(testCommands.contains("stop " + unit()));
    testMountinfo.clear(); const auto oldUnit = unit(); config.shares.clear();
    requestApply(); QVERIFY(loadManaged().isEmpty());
    QVERIFY(!QFile::exists(systemdDir + "/" + oldUnit));
  }
  void modeTransitions() {
    config.shares[0].automount = AutomountMode::Boot; requestApply();
    QVERIFY(testCommands.contains("enable --now " + unit()));
    config.shares[0].automount = AutomountMode::Access; requestApply();
    const auto automount = escapePath(config.shares[0].localPath) + ".automount";
    QVERIFY(testCommands.contains("enable --now " + automount));
    config.shares[0].automount = AutomountMode::Disabled; testCommands.clear(); requestApply();
    QVERIFY(testCommands.contains("stop " + automount));
    QVERIFY(testCommands.contains("disable " + unit()));
    QVERIFY(!QFile::exists(systemdDir + "/" + automount));
  }
  void reloadFailureRestoresFiles() {
    requestApply(); const auto beforeUnit = read(systemdDir + "/" + unit());
    const auto beforeManifest = read(manifestPath);
    config.shares[0].remotePath = "/changed"; testFailure = "daemon-reload";
    QVERIFY_EXCEPTION_THROWN(requestApply(), std::runtime_error);
    QCOMPARE(read(systemdDir + "/" + unit()), beforeUnit);
    QCOMPARE(read(manifestPath), beforeManifest);
  }
  void activationFailureKeepsOwnership() {
    config.shares[0].automount = AutomountMode::Boot;
    testFailure = "enable --now " + unit();
    QVERIFY_EXCEPTION_THROWN(requestApply(), std::runtime_error);
    QCOMPARE(loadManaged().size(), 1);
    QVERIFY(QFile::exists(systemdDir + "/" + unit()));
  }
  void rejectsUnrelatedMount() {
    requestApply(); testCommands.clear();
    testMountinfo = ("31 24 0:30 / " + config.shares[0].localPath + " rw - ext4 /dev/fake rw\n").toUtf8();
    testRequest = {{"action", "unmount"}, {"share_ids", QJsonArray{config.shares[0].id}}};
    QVERIFY_EXCEPTION_THROWN(control(), std::runtime_error);
    QVERIFY(testCommands.isEmpty());
  }
  void credentialsPrivateAndNotInUnits() {
    config.servers[0].protocol = Protocol::Smb;
    config.servers[0].smbUsername = "tester"; config.shares[0].remotePath = "Media";
    testRequest = {{"config", toJson(config)}, {"credentials", QJsonObject{{config.servers[0].id,
      QJsonObject{{"username", "tester"}, {"password", "test-only-secret"}}}}}};
    apply();
    const auto path = stateDir + "/credentials/" + config.servers[0].id + ".cred";
    QVERIFY(read(path).contains("test-only-secret"));
    QVERIFY(!read(systemdDir + "/" + unit()).contains("test-only-secret"));
    QVERIFY(!(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
  }
};
QTEST_GUILESS_MAIN(HelperIntegration)
#include "helper_integration.moc"
