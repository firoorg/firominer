#include "minercontroller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

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
}

class ControllerTest : public QObject
{
    Q_OBJECT

private slots:
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
