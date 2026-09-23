#include "minercontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QUrl>

namespace
{
QString helperPath;

MiningConfig configuration(const QString& mode = {})
{
    MiningConfig config;
    config.executable = helperPath;
    config.poolUrl = "stratum+tcp://pool.example:3333" + mode;
    config.wallet = "wallet";
    config.worker = "worker";
    config.password = "private-password";
    return config;
}

class NodeServer : public QTcpServer
{
public:
    QList<QJsonObject> requests;
    QList<QByteArray> authorizations;
    QList<QByteArray> targets;

    explicit NodeServer(const QString& mode = {})
    {
        connect(this, &QTcpServer::newConnection, this, [this, mode] {
            while (hasPendingConnections())
            {
                auto* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, mode] {
                    const auto bytes = socket->property("buffer").toByteArray() + socket->readAll();
                    socket->setProperty("buffer", bytes);
                    const auto end = bytes.indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const auto headers = QString::fromLatin1(bytes.left(end));
                    const auto length = QRegularExpression("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption).match(headers).captured(1).toInt();
                    if (bytes.size() < end + 4 + length)
                        return;
                    const auto request = QJsonDocument::fromJson(bytes.mid(end + 4, length)).object();
                    requests.append(request);
                    targets.append(bytes.left(bytes.indexOf("\r\n")).split(' ').value(1));
                    authorizations.append(QRegularExpression("Authorization: ([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption).match(headers).captured(1).toLatin1());
                    if (mode == "timeout")
                        return;
                    const bool info = request.value("method") == QJsonValue("getblockchaininfo");
                    QJsonObject result = info ? QJsonObject{{"chain", mode == "network" ? "test" : "main"},
                        {"blocks", 1000000}, {"headers", mode == "sync" ? 1000001 : 1000000}} :
                        QJsonObject{{"pprpcheader", QString(64, '1')}, {"pprpcepoch", 769},
                            {"height", 1000001}, {"bits", "1e00ffff"}, {"target", QString(64, 'f')}};
                    if (mode == "incomplete" && !info)
                        result.remove("pprpcheader");
                    QJsonObject response{{"id", mode == "wrong-id" ? QJsonValue(-1) : request.value("id")},
                        {"result", result}, {"error", QJsonValue::Null}};
                    if (!info && (mode == "reward" || mode == "masternode-sync" || mode == "peers"))
                        response["error"] = QJsonObject{{"code", mode == "reward" ? -5 : mode == "peers" ? -9 : -10},
                            {"message", "Remote error must not echo the RPC password"}};
                    QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    if (mode == "malformed") body = "not json";
                    if (mode == "oversized") body = QByteArray(16 * 1024 * 1024 + 1, 'x');
                    const QByteArray status = mode == "auth" ? "401 Unauthorized" :
                        mode == "forbidden" ? "403 Forbidden" :
                        mode == "redirect" ? "302 Found" :
                        response.value("error").isObject() ? "500 Internal Server Error" : "200 OK";
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nConnection: close\r\n"
                        "Location: " + endpoint().toLatin1() + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }

    QString endpoint() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
};

MiningConfig soloConfiguration(const QString& endpoint)
{
    auto config = configuration();
    config.solo = true;
    config.nodeUrl = endpoint;
    config.rpcUser = "miner.name";
    config.rpcPassword = "rpc.secret+/@:%";
    config.rewardAddress = "aTransparentRewardAddress";
    return config;
}
}

class ControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void soloArgumentsAndValidation()
    {
        auto config = soloConfiguration("getwork://127.0.0.1:8888");
        QVERIFY(MinerController::validate(config).isEmpty());
        auto args = MinerController::arguments(config, 3456, "api-secret");
        QCOMPARE(args.value(args.indexOf("-P") + 1),
            QString("getwork://miner%2Ename:rpc%2Esecret%2B%2F%40%3A%25@127.0.0.1:8888"));
        QCOMPARE(args.value(args.indexOf("--reward-address") + 1), config.rewardAddress);
        QVERIFY(!args.join(' ').contains(config.wallet + '.' + config.worker));
        for (const auto& endpoint : {"https://127.0.0.1:8888", "stratum+tcp://127.0.0.1:8888",
                 "http://miner:password@localhost:8888", "http://localhost:8888/%0a", "http://[::1]:8888"})
        {
            config.nodeUrl = endpoint;
            QVERIFY(!MinerController::validate(config).isEmpty());
        }
        config = soloConfiguration("http://localhost:8888");
        for (const auto& reward : {"", "--pool", "address with spaces", "address\n"})
        {
            config.rewardAddress = reward;
            QVERIFY(!MinerController::validate(config).isEmpty());
        }
        config = soloConfiguration("http://localhost:8888");
        config.rpcPassword.clear();
        QVERIFY(!MinerController::validate(config).isEmpty());
        config = soloConfiguration("http://localhost:8888");
        config.rpcUser = "user:name";
        QVERIFY(!MinerController::validate(config).isEmpty());
    }

    void checksNodeWithoutStartingMiner_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("message");
        QTest::newRow("ready") << "" << "Node ready";
        QTest::newRow("blockchain-sync") << "sync" << "syncing";
        QTest::newRow("masternode-sync") << "masternode-sync" << "syncing";
        QTest::newRow("no-peers") << "peers" << "no network peers";
        QTest::newRow("wrong-network") << "network" << "Network mismatch";
        QTest::newRow("bad-login") << "auth" << "RPC login failed";
        QTest::newRow("not-allowed") << "forbidden" << "rpcallowip";
        QTest::newRow("offline") << "offline" << "Cannot reach the node";
        QTest::newRow("invalid-or-spark-address") << "reward" << "transparent Mainnet";
        QTest::newRow("not-firo") << "incomplete" << "FiroPoW mining work";
        QTest::newRow("malformed") << "malformed" << "valid Firo RPC response";
        QTest::newRow("wrong-id") << "wrong-id" << "valid Firo RPC response";
        QTest::newRow("no-credential-redirect") << "redirect" << "redirected";
        QTest::newRow("oversized") << "oversized" << "too large";
        QTest::newRow("timeout") << "timeout" << "timed out";
    }

    void checksNodeWithoutStartingMiner()
    {
        QFETCH(QString, mode);
        QFETCH(QString, message);
        NodeServer node(mode);
        QVERIFY(node.isListening());
        auto config = soloConfiguration(node.endpoint());
        if (mode == "offline")
            node.close();
        config.executable.clear(); // A node check does not need an installed miner or GPU.
        MinerController controller;
        QSignalSpy checked(&controller, &MinerController::nodeChecked);
        QSignalSpy stats(&controller, &MinerController::statistics);
        QVERIFY(controller.testNode(config));
        QVERIFY(!controller.testNode(config));
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 12000);
        QCOMPARE(checked.first().first().toBool(), mode.isEmpty());
        QVERIFY2(checked.first().at(1).toString().contains(message), qPrintable(checked.first().at(1).toString()));
        QVERIFY(!checked.first().at(1).toString().contains(config.rpcPassword));
        QVERIFY(!controller.isRunning());
        QVERIFY(stats.isEmpty());
        for (const auto& auth : node.authorizations)
            QCOMPARE(auth, "Basic " + (config.rpcUser + ':' + config.rpcPassword).toUtf8().toBase64());
        if (mode.isEmpty())
        {
            QCOMPARE(node.requests.size(), 2);
            QCOMPARE(node.requests[0].value("method"), QJsonValue("getblockchaininfo"));
            QCOMPARE(node.requests[1].value("method"), QJsonValue("getblocktemplate"));
            QCOMPARE(node.requests[1].value("params").toArray(), (QJsonArray{QJsonObject{}, config.rewardAddress}));
        }
        if (mode == "redirect")
            QCOMPARE(node.requests.size(), 1);
    }

    void soloStartChecksNodeAndCanCancel()
    {
        NodeServer node;
        QVERIFY(node.isListening());
        MinerController controller;
        QSignalSpy checked(&controller, &MinerController::nodeChecked);
        QSignalSpy stats(&controller, &MinerController::statistics);
        QSignalSpy finished(&controller, &MinerController::finished);
        QSignalSpy logs(&controller, &MinerController::logLine);
        const auto config = soloConfiguration(node.endpoint());
        QVERIFY(controller.start(config));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty(), 7000);
        QCOMPARE(checked.count(), 1);
        QVERIFY(checked.first().first().toBool());
        QCOMPARE(node.requests.size(), 2);
        for (const auto& line : logs)
            QVERIFY(!line.first().toString().contains(config.rpcPassword));
        controller.stop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);

        NodeServer waiting("timeout");
        QVERIFY(controller.start(soloConfiguration(waiting.endpoint())));
        controller.stop();
        QCOMPARE(finished.count(), 2);
        QVERIFY(!controller.isRunning());
        QCOMPARE(checked.count(), 2);
        QVERIFY(!checked.last().first().toBool());

        NodeServer badReward("reward");
        stats.clear();
        QVERIFY(controller.start(soloConfiguration(badReward.endpoint())));
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 3, 3000);
        QVERIFY(!controller.isRunning());
        QVERIFY(stats.isEmpty());
    }

    void soloEndpointPathMatchesTheNodeCheck()
    {
        NodeServer node;
        QVERIFY(node.isListening());
        auto config = soloConfiguration(node.endpoint() + "/wallet/a%3Fb%23c%2Fd+e@f");
        MinerController controller;
        QSignalSpy checked(&controller, &MinerController::nodeChecked);
        QVERIFY(controller.testNode(config));
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, 3000);
        QVERIFY(checked.first().first().toBool());
        const auto args = MinerController::arguments(config, 3456, "api-secret");
        const QUrl minerUrl(args.value(args.indexOf("-P") + 1));
        // PoolURI decodes once, then the miner sends Path() verbatim in HTTP.
        const auto target = QByteArray::fromPercentEncoding(minerUrl.path(QUrl::FullyEncoded).toUtf8().replace('+', ' '));
        QCOMPARE(node.targets.size(), 2);
        QCOMPARE(target, node.targets.first());
    }

    void passwordRedactionDoesNotHideStartupFailure()
    {
        NodeServer node;
        QVERIFY(node.isListening());
        auto config = soloConfiguration(node.endpoint() + "/bind-failure");
        config.rpcPassword = "port";
        MinerController controller;
        QSignalSpy failed(&controller, &MinerController::failure);
        QSignalSpy finished(&controller, &MinerController::finished);
        QVERIFY(controller.start(config));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("statistics port"));
    }

    void encodesCredentialsAndDeviceArguments()
    {
        auto config = configuration();
        config.wallet = "account.name+tag@example";
        config.worker = "rig.one:gpu";
        config.password = "p@ss:/+% `word";
        config.backend = "cuda";
        config.devices = "0, 2 3";
        QVERIFY2(MinerController::validate(config).isEmpty(), qPrintable(MinerController::validate(config)));
        const auto args = MinerController::arguments(config, 3456, "api-secret");
        QCOMPARE(args.value(args.indexOf("-P") + 1),
            QString("stratum+tcp://account%2Ename%2Btag%40example.rig%2Eone%3Agpu:"
                    "p%40ss%3A%2F%2B%25%20%60word@pool.example:3333"));
        QCOMPARE(args.value(args.indexOf("--api-bind") + 1), QString("127.0.0.1:-3456"));
        QCOMPARE(args.value(args.indexOf("--api-password") + 1), QString("api-secret"));
        QCOMPARE(args.value(args.indexOf("--HWMON") + 1), QString("2"));
        QCOMPARE(args.mid(args.indexOf("--cu-devices")), (QStringList{"--cu-devices", "0", "2", "3"}));
        QVERIFY(args.contains("--cuda"));
        config.backend = "opencl";
        const auto opencl = MinerController::arguments(config, 3456, "api-secret");
        QVERIFY(opencl.contains("--opencl"));
        QVERIFY(opencl.contains("--cl-devices"));
        config.backend = "auto";
        config.devices.clear();
        config.worker.clear();
        const auto automatic = MinerController::arguments(config, 3456, "api-secret");
        QVERIFY(!automatic.contains("--cuda"));
        QVERIFY(!automatic.contains("--opencl"));
        QVERIFY(!automatic.value(automatic.indexOf("-P") + 1).contains(".rig"));
        config = configuration();
        config.poolUrl = "stratum+tcp://intended.example:3333/worker@different.example:4444/a+b";
        QVERIFY(MinerController::validate(config).isEmpty());
        const auto withPath = MinerController::arguments(config, 3456, "api-secret");
        QCOMPARE(withPath.value(withPath.indexOf("-P") + 1),
            QString("stratum+tcp://wallet.worker:private-password@intended.example:3333/"
                    "worker%40different.example:4444/a%2Bb"));
    }

    void rejectsInvalidConfiguration()
    {
        auto config = configuration();
        QVERIFY(MinerController::validate(config).isEmpty());
        const QStringList invalidUrls{"stratum+tcp://host", "stratum+tcp://host:0",
            "stratum+tcp://host:65536", "file://host:3333", "simulation://host:3333",
            "http://host:3333", "getwork://host:3333",
            "stratum+tcp://account@host:3333", "stratum+tcp://@host:3333",
            "stratum+tcp://host:3333/?query", "stratum+tcp://host:3333/#fragment",
            "stratum+tcp://[::1]:3333", "stratum+tcp://exit:3333"};
        for (const auto& url : invalidUrls)
        {
            config.poolUrl = url;
            QVERIFY2(!MinerController::validate(config).isEmpty(), qPrintable(url));
        }
        config = configuration();
        config.executable = helperPath + ".missing";
        QVERIFY(!MinerController::validate(config).isEmpty());
        config = configuration();
        config.wallet.clear();
        QVERIFY(!MinerController::validate(config).isEmpty());
        config = configuration();
        config.password = "password\nargument";
        QVERIFY(!MinerController::validate(config).isEmpty());
        config = configuration();
        config.worker = QString(1100, 'x');
        QVERIFY(!MinerController::validate(config).isEmpty());
        config = configuration();
        config.backend = "other";
        QVERIFY(!MinerController::validate(config).isEmpty());
        config.backend = "auto";
        config.devices = "0";
        QVERIFY(!MinerController::validate(config).isEmpty());
        config.backend = "cuda";
        for (const auto& ids : {"-1", "0 --pool", ",,,", "4294967296"})
        {
            config.devices = ids;
            QVERIFY(!MinerController::validate(config).isEmpty());
        }
    }

    void readsAuthenticatedStatistics_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("state");
        QTest::newRow("normal") << "" << "Mining";
        QTest::newRow("fragmented") << "/fragmented" << "Mining";
        QTest::newRow("paused") << "/paused" << "Paused";
        QTest::newRow("disconnected") << "/disconnected" << "Reconnecting";
    }

    void readsAuthenticatedStatistics()
    {
        QFETCH(QString, mode);
        QFETCH(QString, state);
        MinerController controller;
        QSignalSpy stats(&controller, &MinerController::statistics);
        QSignalSpy states(&controller, &MinerController::stateChanged);
        QSignalSpy logs(&controller, &MinerController::logLine);
        QSignalSpy failures(&controller, &MinerController::failure);
        QSignalSpy finished(&controller, &MinerController::finished);
        QVERIFY(controller.start(configuration(mode)));
        QVERIFY(!controller.start(configuration(mode)));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty(), 7000);
        QCOMPARE(states.last().first().toString(), state);
        const auto result = stats.first().first().toJsonObject();
        QCOMPARE(result.value("mining").toObject().value("shares").toArray().first().toInt(), 17);
        QCOMPARE(result.value("host").toObject().value("runtime").toInt(), 123);
        QVERIFY(controller.isRunning());
        QVERIFY(failures.isEmpty());
        bool redacted = false;
        for (const auto& log : logs)
        {
            const auto line = log.first().toString();
            QVERIFY(!line.contains("private-password"));
            redacted |= line.contains("[credentials]@");
        }
        QVERIFY(redacted);
        QElapsedTimer shutdown;
        shutdown.start();
        controller.stop();
        controller.stop();
        QCOMPARE(states.last().first().toString(), QString("Stopping"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
        QVERIFY2(shutdown.elapsed() < 2500, "Stopping reached the forced-kill deadline");
        QVERIFY(logs.contains({QString("Graceful shutdown complete")}));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(states.last().first().toString(), QString("Stopped"));
        QVERIFY(failures.isEmpty());
    }

    void launchesExternalMiner_data()
    {
        QTest::addColumn<bool>("legacy");
        QTest::newRow("current") << false;
#ifdef Q_OS_WIN
        QTest::newRow("legacy") << true;
#endif
    }

    void launchesExternalMiner()
    {
        QFETCH(bool, legacy);
        QTemporaryDir directory(QDir::tempPath() + "/External miner XXXXXX");
        QVERIFY(directory.isValid());
        auto config = configuration(legacy ? "/external-legacy" : "/external");
        config.executable = directory.filePath(QFileInfo(helperPath).fileName());
        QVERIFY(QFile::copy(helperPath, config.executable));
        MinerController controller;
        QSignalSpy stats(&controller, &MinerController::statistics);
        QSignalSpy failures(&controller, &MinerController::failure);
        QSignalSpy logs(&controller, &MinerController::logLine);
        QVERIFY(controller.start(config));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty(), 7000);
        QVERIFY(failures.isEmpty());
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
        QCOMPARE(logs.contains({QString("Graceful shutdown complete")}), !legacy);
        QVERIFY(failures.isEmpty());
    }

    void rejectsBadApi_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("message");
        QTest::newRow("malformed") << "/malformed" << "invalid response";
        QTest::newRow("oversized") << "/oversized" << "too large";
        QTest::newRow("wrong id") << "/wrong-id" << "invalid response";
        QTest::newRow("auth error") << "/auth-error" << "invalid response";
        QTest::newRow("incomplete") << "/incomplete" << "incomplete statistics";
        QTest::newRow("timeout") << "/timeout" << "did not respond";
    }

    void rejectsBadApi()
    {
        QFETCH(QString, mode);
        QFETCH(QString, message);
        MinerController controller;
        QSignalSpy stats(&controller, &MinerController::statistics);
        QSignalSpy logs(&controller, &MinerController::logLine);
        QVERIFY(controller.start(configuration(mode)));
        auto reported = [&] {
            for (const auto& log : logs)
                if (log.first().toString().contains(message))
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reported(), 9000);
        QVERIFY(stats.isEmpty());
        QVERIFY(controller.isRunning());
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
    }

    void reportsUnexpectedExitAndCanRestart()
    {
        MinerController controller;
        QSignalSpy failed(&controller, &MinerController::failure);
        QSignalSpy finished(&controller, &MinerController::finished);
        QSignalSpy stats(&controller, &MinerController::statistics);
        QVERIFY(controller.start(configuration("/exit")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(failed.count(), 1);
        QVERIFY(!controller.isRunning());
        QVERIFY(controller.start(configuration()));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty(), 7000);
        controller.stop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(failed.count(), 1);
    }

    void stopsDuringLaunch()
    {
        MinerController controller;
        QSignalSpy logs(&controller, &MinerController::logLine);
        QSignalSpy finished(&controller, &MinerController::finished);
        QVERIFY(controller.start(configuration()));
        controller.stop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2500);
#ifdef Q_OS_WIN
        // The named event stays signaled even if Stop precedes child startup.
        QVERIFY(logs.contains({QString("Graceful shutdown complete")}));
#endif
        QVERIFY(!controller.isRunning());
    }

    void stopsWhenMonitoringFails_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("message");
        QTest::newRow("port collision") << "/bind-failure" << "statistics port";
        QTest::newRow("lost API") << "/lost-api" << "statistics connection";
    }

    void stopsWhenMonitoringFails()
    {
        QFETCH(QString, mode);
        QFETCH(QString, message);
        MinerController controller;
        QSignalSpy failed(&controller, &MinerController::failure);
        QSignalSpy finished(&controller, &MinerController::finished);
        QSignalSpy logs(&controller, &MinerController::logLine);
        QVERIFY(controller.start(configuration(mode)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 22000);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains(message));
        QVERIFY(logs.contains({QString("Graceful shutdown complete")}));
        QVERIFY(!controller.isRunning());
    }

    void killsAnUnresponsiveMiner()
    {
        MinerController controller;
        QSignalSpy stats(&controller, &MinerController::statistics);
        QSignalSpy logs(&controller, &MinerController::logLine);
        QVERIFY(controller.start(configuration("/ignore-stop")));
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty(), 7000);
        controller.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
        QVERIFY(!logs.contains({QString("Graceful shutdown complete")}));
    }
};

int main(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    helperPath = QString::fromLocal8Bit(argv[1]);
    // The first argument is the fixture executable, not a QtTest selector.
    --argc;
    for (int i = 1; i <= argc; ++i)
        argv[i] = argv[i + 1];
    QCoreApplication app(argc, argv);
    ControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_controller.moc"
